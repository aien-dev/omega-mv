#!/usr/bin/env python3
"""Verification-only host oracle. No kernel/module/device operations."""
from pathlib import Path
import hashlib, json, os, re, subprocess
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'build/kernel-profile'
FIXTURES = ROOT / 'seed/omc/tests'
HERE.mkdir(parents=True, exist_ok=True)
for name in ('span.om', 'divide.om', 'scrub.om', 'oracle.S', 'leaf_oracle.c', 'Makefile'):
    (HERE / name).write_bytes((FIXTURES / f'kernel_{name}').read_bytes())
SEED = ROOT / 'seed/out/omc'
PROTO = HERE / 'omc-kleaf'
SRC = ROOT / 'seed/omc/omc.om'
BRIDGE = Path(os.environ.get('OMEGA_KERNEL_BRIDGE', str(ROOT.parent / 'omega-kernel-bridge-profile')))
ENVELOPE = BRIDGE / 'src/kernel_leaf.om'
PACK = HERE / 'kernel-envelope'
results = []
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def command(args, *, data=None, expected=0):
    p = subprocess.run([str(x) for x in args], input=data, cwd=HERE, capture_output=True)
    results.append({'command': [str(x) for x in args], 'exit': p.returncode,
                    'stderr': p.stderr.decode(errors='replace')})
    if p.returncode != expected:
        raise RuntimeError(f'{args}: expected {expected}, observed {p.returncode}: {p.stderr!r}')
    return p.stdout

def source_commit(repo):
    commit = command(['git', '-C', repo, 'rev-parse', 'HEAD']).decode().strip()
    dirty = command(['git', '-C', repo, 'status', '--porcelain', '--untracked-files=normal'])
    if dirty:
        raise RuntimeError(f'commit source changes before qualification: {repo}')
    return commit

compiler_commit = source_commit(ROOT)
bridge_commit = source_commit(BRIDGE)

def compile_unit(source, path):
    path.write_bytes(command([PROTO], data=source))

before = sha(SEED)
PROTO.write_bytes(command([SEED], data=SRC.read_bytes()))
PROTO.chmod(0o755)
rebuilt = command([PROTO], data=SRC.read_bytes())
assert rebuilt == PROTO.read_bytes(), 'prototype self-rebuild not identical'
PACK.write_bytes(command([SEED], data=ENVELOPE.read_bytes()))
PACK.chmod(0o755)
for name in ('span', 'divide', 'scrub'):
    compile_unit((HERE / f'{name}.om').read_bytes(), HERE / f'{name}.bin')
(HERE / 'omega_leaf.S').write_bytes(command([PACK], data=(HERE / 'span.bin').read_bytes()))
for data in (b'', b'\0' * 20, b'\0' * 4097,
             (HERE / 'span.bin').read_bytes() + b'\0'):
    assert command([PACK], data=data, expected=110) == b''
results.append({'envelope_refusal': 'empty/bad-prefix/oversized/unaligned', 'result': 'PASS'})
normal = (HERE / 'span.om').read_bytes().split(b'\n', 1)[1]
assert command([SEED], data=normal) == command([PROTO], data=normal), 'ordinary mode changed'
# Deliberately unsupported sources exercise refusal before output.
negative = {
 'loop': 'kleaf\nfn f a ulong to ulong\nwhile eq a 0\nend\nret a\nend\n',
 'helper': 'kleaf\nfn g a ulong to ulong\nret a\nend\nfn f a ulong to ulong\nret g a\nend\n',
 'recursive': 'kleaf\nfn f a ulong to ulong\nret f a\nend\n',
 'array': 'kleaf\nfn f a view ulong 1 to ulong\nret get a 0\nend\n',
 'float': 'kleaf\nfn f a f64 to ulong\nret ulong a\nend\n',
 'host': 'kleaf\nfn f to ulong\nret ulong getpid\nend\n',
 'main': 'kleaf\nfn main to ulong\nret 0\nend\n',
 'text': 'kleaf\ntext txt "no"\nfn f to ulong\nret 0\nend\n',
 'om2': 'kleaf\nom2\nfn f to ulong\nret 0\nend\n',
 'void': 'kleaf\nfn f to void\nret\nend\n',
 'byte-budget': 'kleaf\nfn f a ulong to ulong\n' + 'if eq a 0\nret 0\nend\n' * 300 + 'ret a\nend\n',
}
for name, src in negative.items():
    p = subprocess.run([str(PROTO)], input=src.encode(), cwd=HERE, capture_output=True)
    assert p.returncode != 0 and not p.stdout, f'{name} failed refusal: {p.returncode}'
    assert b'error 9' in p.stderr, (name, p.stderr)
    results.append({'negative': name, 'exit': p.returncode, 'stderr': p.stderr.decode()})
command(['as', '-o', 'oracle.o', 'oracle.S'])
command(['ld', '-e', '_start', '-o', 'oracle', 'oracle.o'])
command([HERE / 'oracle'])
# Behavioral mutant: reversed stale-generation guard must trip oracle, not pass.
mutant = (HERE / 'span.om').read_bytes().replace(b'if ne seen expect', b'if eq seen expect')
compile_unit(mutant, HERE / 'mutant.bin')
(HERE / 'mutant-leaf.S').write_bytes(command([PACK], data=(HERE / 'mutant.bin').read_bytes()))
mutant_asm = (HERE / 'oracle.S').read_text().replace('"omega_leaf.S"', '"mutant-leaf.S"')
(HERE / 'mutant.S').write_text(mutant_asm)
command(['as', '-o', 'mutant.o', 'mutant.S'])
command(['ld', '-e', '_start', '-o', 'mutant', 'mutant.o'])
command([HERE / 'mutant'], expected=1)
# Wolfram FromDigits["d2800006",16] -> 3531603974.
# Removing x6 scrubbing must be observable even when admission still succeeds.
clearing = (HERE / 'omega_leaf.S').read_text()
assert clearing.count('.inst 3531603974\n') == 1
(HERE / 'bad-clearing-leaf.S').write_text(clearing.replace('.inst 3531603974\n', '.inst 3573751839\n'))
(HERE / 'bad-clearing.S').write_text((HERE / 'oracle.S').read_text().replace('"omega_leaf.S"', '"bad-clearing-leaf.S"'))
command(['as', '-o', 'bad-clearing.o', 'bad-clearing.S'])
command(['ld', '-e', '_start', '-o', 'bad-clearing', 'bad-clearing.o'])
command([HERE / 'bad-clearing'], expected=8)
# Inspect complete emitted leafs, not the test-only syscall in _start.
for name in ('span', 'divide', 'scrub'):
    dis = command(['objdump', '-D', '-b', 'binary', '-m', 'aarch64', HERE / f'{name}.bin']).decode()
    (HERE / f'{name}.dis').write_text(dis)
    insns = []
    offsets = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(.+)$', line)
        if m:
            offsets.append(int(m.group(1), 16))
            insns.append(m.group(2))
    assert len(insns) >= 5 and insns[:4] == ['nop'] * 4, insns[:4]
    assert insns.count('ret') == 1 and insns[-1] == 'ret', insns
    used = {int(r) for insn in insns for r in re.findall(r'\b[xw](\d+)\b', insn)
            if 2 <= int(r) <= 17}
    cleared = set()
    for insn in reversed(insns[:-1]):
        m = re.fullmatch(r'mov\s+x(\d+),\s*#0x0\s*//.*', insn)
        if not m:
            m = re.fullmatch(r'mov\s+x(\d+),\s*#0x0', insn)
        if not m: break
        cleared.add(int(m.group(1)))
    assert used <= cleared, (name, used, cleared)
    for insn in insns:
        assert not re.search(r'\b(?:[xw](?:18|19|2[0-9]|30)|sp|[bhsdqv][0-9]+)\b', insn), insn
        op = insn.split()[0]
        assert op not in ('bl', 'blr', 'br', 'svc', 'hvc', 'smc', 'mrs', 'msr'), insn
        assert not op.startswith(('ld', 'st')), insn
    by_offset = dict(zip(offsets, range(len(insns))))
    visited, active = set(), set()
    def walk(i):
        assert i not in active, (name, 'runtime cycle', offsets[i])
        if i in visited: return
        active.add(i)
        insn, op = insns[i], insns[i].split()[0]
        successors = []
        if op != 'ret':
            branch = op in ('b', 'b.al', 'cbz', 'cbnz', 'tbz', 'tbnz') or op.startswith('b.')
            if branch:
                target = int(re.findall(r'0x[0-9a-f]+', insn)[-1], 16)
                assert target in by_offset, (name, 'external branch', insn)
                successors.append(by_offset[target])
            if op not in ('b', 'b.al'):
                assert i + 1 < len(insns), (name, 'falls out', insn)
                successors.append(i + 1)
        for j in successors: walk(j)
        active.remove(i)
        visited.add(i)
    walk(by_offset[8])
    results.append({'closed_acyclic_cfg': name, 'entry_offset': 8,
                    'reachable_instructions': len(visited), 'result': 'PASS'})
    results.append({'instruction_inspection': name, 'count': len(insns), 'result': 'PASS'})
    results.append({'used_gpr_clearing': name, 'used': sorted(used),
                    'cleared': sorted(cleared), 'result': 'PASS'})
# Build only translation units against the exact installed kernel configuration.
# There is no modules/modpost/install/load target anywhere in this experiment.
log = command(['make', '-C', '/usr/src/linux-headers-7.0.0-1019-nvidia',
               f'M={HERE}', 'V=1', 'leaf_oracle.o', 'omega_leaf.o'])
(HERE / 'kbuild-hardened.log').write_bytes(log)
def metadata(path):
    sections = command(['readelf', '-WS', path]).decode()
    symbols = command(['readelf', '-Ws', path]).decode()
    relocs = command(['readelf', '-Wr', path]).decode()
    notes = command(['readelf', '-n', path]).decode()
    text = command(['objcopy', '-O', 'binary', '-j', '.text', path, '/dev/stdout'])
    assert re.search(r'__patchable_function_entries\s+PROGBITS\s+\S+\s+\S+\s+000008\s+00\s+WAL', sections)
    symbol = 'omega_kernel_unit' if path.name != 'leaf_oracle.o' else 'om_leaf_oracle'
    assert re.search(rf'\b0000000000000008\s+\d+\s+FUNC\s+GLOBAL\s+DEFAULT\s+\d+\s+{symbol}\b', symbols)
    patch = relocs.split("Relocation section '.rela__patchable_function_entries'", 1)[1].split('\n\n', 1)[0]
    assert re.search(r'R_AARCH64_ABS64\s+\S+\s+\.text \+ 0\b', patch), patch
    assert 'AArch64 feature: PAC' in notes and 'BTI' not in notes, notes
    assert text[:16] == (HERE / 'span.bin').read_bytes()[:16]
    return {'sections': sections, 'symbols': symbols, 'relocations': relocs, 'notes': notes}
for name in ('leaf_oracle.o', 'omega_leaf.o'):
    info = metadata(HERE / name)
    (HERE / f'{name}.metadata.json').write_text(json.dumps(info, indent=2) + '\n')
assert command(['objcopy', '-O', 'binary', '-j', '.text', HERE / 'omega_leaf.o', '/dev/stdout']) == (HERE / 'span.bin').read_bytes()
mapping = command(['readelf', '-Ws', HERE / 'omega_leaf.o']).decode()
assert not re.search(r'LOCAL\s+DEFAULT\s+1\s+\$d\b', mapping), mapping
assert re.search(r'LOCAL\s+DEFAULT\s+1\s+\$x\b', mapping), mapping
results.append({'exact_kernel_object_contract': '4,2 ftrace, pre-entry relocation, used-gpr, PAC leaf, fixed SCS x18, AX instruction mapping', 'result':'PASS'})
# Negative metadata control: tracing must point to pre-entry, not callable entry.
bad = (HERE / 'omega_leaf.S').read_text().replace('.xword .Lkleaf_start', '.xword omega_kernel_unit')
(HERE / 'bad-metadata.S').write_text(bad)
command(['as', '-o', 'bad-metadata.o', 'bad-metadata.S'])
try:
    metadata(HERE / 'bad-metadata.o')
except AssertionError:
    results.append({'metadata_negative': 'entry rather than pre-entry relocation', 'result': 'PASS'})
else:
    raise AssertionError('bad tracing relocation was accepted')
assert sha(SEED) == before, 'seed binary changed'
report = {'scope':'UNPRIVILEGED_HOST_ONLY, no kernel execution or module load',
          'source_commit': compiler_commit, 'source_tree_clean': True,
          'seed_sha256':before, 'prototype_source_sha256':sha(SRC),
          'prototype_binary_sha256':sha(PROTO),
          'bridge_source_commit': bridge_commit, 'bridge_tree_clean': True,
          'bridge_source_sha256': sha(ENVELOPE),
          'experiment_source_sha256': sha(Path(__file__)),
          'kernel_config_sha256': sha(Path('/usr/src/linux-headers-7.0.0-1019-nvidia/.config')),
          'kernel_symvers_sha256': sha(Path('/usr/src/linux-headers-7.0.0-1019-nvidia/Module.symvers')),
          'tools': {name: command([name, '--version']).decode().strip()
                    for name in ('gcc', 'as', 'ld', 'make', 'python3')},
          'files':{p.name:sha(p) for p in HERE.iterdir() if p.is_file() and not p.name.startswith('result')},
          'results':results}
payload = json.dumps(report, indent=2) + '\n'
digest = hashlib.sha256(payload.encode()).hexdigest()
receipt = HERE / f'kernel-leaf-host.{digest}.json'
with receipt.open('x') as out:
    out.write(payload)
print(f'Immutable host receipt: {receipt}')
print('HOST_ONLY PASS: normal-mode identity, self-rebuild, six argument ABI, checked failure recovery, fixed/callee GPRs/SP, every allocated volatile pool register cleared, refusal controls, behavioral and metadata mutants, complete leaf inspection, exact kbuild object ABI. NO MODULE LOAD.')
