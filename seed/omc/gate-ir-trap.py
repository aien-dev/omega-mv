#!/usr/bin/env python3
"""L524 reference-only harness. Never used to build/run a shipped Omega program.
om4 builds the compiler; C interpreter/backend, checker2 and Wolfram are oracles.
Deleting the trap emission must fail both byte equality and native behavior.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / 'build/seed/ir-trap'
B.mkdir(parents=True, exist_ok=True)
checks = failures = 0

def run(args, data=None):
    return subprocess.run([str(x) for x in args], input=data, cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

def check(ok, name):
    global checks, failures
    checks += 1
    failures += not ok
    print(('PASS ' if ok else 'FAIL ') + name, flush=True)

def compile_elf(compiler, source, target):
    p = run([compiler], source)
    if p.returncode or len(p.stdout) < 64 or p.stdout[:4] != b'\x7fELF' or p.stdout[18:20] != b'\xb7\x00':
        raise RuntimeError(f'{target}: compiler RC={p.returncode}; not nonempty AArch64 ELF: {p.stderr!r}')
    target.write_bytes(p.stdout)
    target.chmod(0o755)
    return p.stdout

source = Path(os.environ.get('OMC_SOURCE', ROOT / 'seed/omc/omc.om')).read_bytes()
compiler = ROOT / 'seed/rung4/om4.bin'
stages = []
for i in range(1, 4):
    target = B / f'stage{i}'
    stages.append(compile_elf(compiler, source, target))
    compiler = target
check(stages[0] == stages[1] == stages[2], 'om4/omc parents and stage2/stage3 fixpoint')
print('COMPILER_SHA256=' + hashlib.sha256(stages[2]).hexdigest())
if '--build-only' in sys.argv:
    raise SystemExit(0 if not failures else 1)

# This derived test-only runner adds interpreter/native differential checking to
# the immutable seed/check/runner.c; never a compiler in the seed ancestry.
p = run(['cc', '-O1', '-Isrc', '-Isrc/compiler', '-o', B/'oracle-runner',
         'seed/check/ir-trap-runner.c',
         *sorted(x for x in Path('src/compiler').glob('*.c') if x.name != 'oscc_main.c'), 'src/sha256.c'])
if p.returncode:
    raise RuntimeError(p.stderr.decode())

calc = json.loads((ROOT / 'seed/omc/tests/ir-trap-wolfram.json').read_text())
inputs = ''.join(str(row['input']) + '\n' for row in calc['trapFixture']).encode()
(B/'inputs').write_bytes(inputs)
valid = []
for code in range(1, 15):
    valid.append((f'trap{code:02}', f'fn f a u32 to u32\nif gt a 10\ntrap {code}\nend\nret a\nend\n', code))
# return types exercise the unreachable return terminator required by the IR.
valid += [
    ('signed', 'fn f a u32 to i64\nif gt a 10\ntrap 3\nend\nret i64 a\nend\n', 3),
    ('nested', 'fn f a u32 to u32\nif gt a 10\nif gt a 0\ntrap 3\nelse\nret 9\nend\nend\nret a\nend\n', 3),
    ('bothtrap', 'fn f a u32 to u32\nif gt a 10\ntrap 3\nelse\nret a\nend\nend\n', 3),
]
for name, text, code in valid:
    f = B / (name + '.om')
    f.write_text(text)
    oracle = B/(name + '.oracle')
    p = run(['build/omegese/omegese', 'emit', f, B/(name+'.json')])
    if p.returncode:
        raise RuntimeError(f'oracle frontend refused {name}: {p.stdout!r} {p.stderr!r}')
    p = run(['build/omega_emit/omega_emit', 'emit', B/(name+'.json'), oracle])
    if p.returncode:
        raise RuntimeError(f'oracle emitter refused {name}: {p.stdout!r}')
    p = run([compiler], b'ir\n' + text.encode())
    got = B/(name+'.got')
    got.write_bytes(p.stdout)
    check(p.returncode == 0 and p.stdout == oracle.read_bytes(), name+' exact C-oracle IR')
    q = run(['build/checker2/checker2', 'check', got])
    check(q.returncode == 0 and b'checker2=ACCEPT ' in q.stdout, name+' checker2 ACCEPT')
    q = run([B/'oracle-runner', got, 'f', B/'inputs'])
    expected = []
    for row in calc['trapFixture']:
        if row['trap']:
            expected.append(f'TRAP {code}\t-')
        else:
            v = row['return']
            expected.append(f'{v}\t0x{v:016x}')
    actual = q.stdout.decode().splitlines()[1:]
    check(q.returncode == 0 and actual == expected,
          name+f' Wolfram boundary outputs + interpreter/native agreement ({len(expected)} runs)')
    if q.returncode or actual != expected:
        print('  ACTUAL', actual, 'EXPECTED', expected, 'ERR', q.stderr.decode().strip())

# Void and bool terminators, no-input and boolean-input cases.
for name, text, inp, expected in [
    ('void', 'fn f to void\ntrap 3\nend\n', b'\n', ['TRAP 3\t-']),
    ('bool', 'fn f a bool to bool\nif a\ntrap 3\nend\nret a\nend\n', b'0\n1\n', ['0\t0x0000000000000000', 'TRAP 3\t-']),
]:
    f = B/(name+'.om'); f.write_text(text)
    p = run(['build/omegese/omegese', 'emit', f, B/(name+'.json')])
    if p.returncode: raise RuntimeError(f'oracle refused {name}: {p.stdout!r}')
    p = run(['build/omega_emit/omega_emit', 'emit', B/(name+'.json'), B/(name+'.oracle')])
    if p.returncode: raise RuntimeError(f'oracle refused {name}: {p.stdout!r}')
    p = run([compiler], b'ir\n'+text.encode())
    got = B/(name+'.got'); got.write_bytes(p.stdout)
    check(p.returncode == 0 and p.stdout == (B/(name+'.oracle')).read_bytes(), name+' exact C-oracle IR')
    (B/(name+'.inputs')).write_bytes(inp)
    q = run([B/'oracle-runner', got, 'f', B/(name+'.inputs')])
    check(q.returncode == 0 and q.stdout.decode().splitlines()[1:] == expected, name+' interpreter/native expected behavior')

# Refusal codes follow docs/one_layer/OMEGESE.md 9.8 and seed/README.md ("trap N": error 4 when the line ends,
# 16 when the token is not a number, 8 out of range): `trap a` and `trap uint 3` are error 16, `trap` alone is 4.
bad = [
    ('zero', 'trap 0', 8), ('too_large', 'trap 15', 8), ('negative', 'trap -1', 8),
    ('missing', 'trap', 4), ('named', 'trap a', 16), ('extra', 'trap 3 4', 4),
    ('extra_word', 'trap 3 ret a', 4), ('typed', 'trap uint 3', 16),
    ('after', 'trap 3\nret a', 17),
    ('unsupported_break', 'break', 9), ('unsupported_own', 'own', 9),
]
for name, stmt, code in bad:
    text = f'ir\nfn f a u32 to u32\n{stmt}\nend\n'.encode()
    p = run([compiler], text)
    check(p.returncode == code and not p.stdout and f'error {code}'.encode() in p.stderr,
          name+f' refused error {code}; no output')

print(f'CHECKS={checks} FAILURES={failures} VALID_PROGRAMS={len(valid)+2} REFUSALS={len(bad)}')
print('RC=' + str(int(bool(failures))))
raise SystemExit(int(bool(failures)))
