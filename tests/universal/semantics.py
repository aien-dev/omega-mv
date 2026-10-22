#!/usr/bin/env python3
"""Development-oracle conformance; not a language adapter or proof checker."""
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def run(args, data=None):
    p = subprocess.run(args, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if p.returncode:
        raise AssertionError(f'command failed rc={p.returncode}: {args!r}\n{p.stderr.decode(errors="replace")}')
    return p.stdout

def main():
    # Invalidate transient evidence before every possible refusal. Published
    # append-only evidence is copied elsewhere, never reused as this output.
    out = Path(os.environ.get('UNIVERSAL_OUT', str(ROOT / 'build/universal-semantics'))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'receipt.json').unlink(missing_ok=True)
    contract = ROOT / 'spec/universal/semantics.md'
    profiles = ROOT / 'spec/universal/profiles.json'
    assert contract.is_file() and profiles.is_file(), 'MISSING_SEMANTIC_CONTRACT: #625 has no versioned observation/fidelity contract'
    manifest = json.loads(profiles.read_text())
    assert manifest['schema'] == 'omega.universal.semantics.v1'
    assert manifest['contract_sha256'] == hashlib.sha256(contract.read_bytes()).hexdigest(), 'STALE_CONTRACT_DIGEST'
    assert manifest['fidelity'] == ['EXACT_SOURCE_REPLAY', 'PROVEN_EQUIVALENCE', 'PROVEN_REFINEMENT', 'BOUNDED_VALIDATION', 'DIFFERENTIAL_COMPATIBILITY', 'RUNTIME_INTEROPERABILITY', 'EXPLICIT_APPROXIMATION', 'UNSUPPORTED', 'UNKNOWN']
    assert manifest['non_success'] == ['FAIL', 'UNKNOWN', 'TIMEOUT', 'UNSUPPORTED', 'UB', 'MISSING_RUNTIME', 'NOT_RUN']
    assert manifest['adapters_implemented_by_this_change'] == []
    fixtures = ROOT / 'tests/universal'
    cc = os.environ.get('CC', 'cc')
    rustc = os.environ.get('RUSTC', 'rustc')
    versions = {cc: run([cc, '--version']).decode(), rustc: run([rustc, '--version','--verbose']).decode(), 'python': sys.version, 'host': platform.uname()._asdict()}
    run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(fixtures/'arithmetic.c'), '-o', str(out/'c-oracle')])
    run([rustc, '--edition=2021', '-C', 'overflow-checks=yes', str(fixtures/'arithmetic.rs'), '-o', str(out/'rust-oracle')])
    m = (1 << 64) - 1
    boundaries = [0, 1, 2, 255, 256, 65535, 65536, (1<<32)-1, 1<<32, (1<<63)-1, 1<<63, m-1, m]
    pairs = [(a,b) for a in boundaries for b in boundaries if b != 0]
    rng = random.Random(625)
    pairs += [(rng.getrandbits(64), rng.randrange(1, 1<<64)) for _ in range(256)]
    payload = ''.join(f'{a} {b}\n' for a,b in pairs).encode()
    expected = ''.join(f'{(a+b)&m} {(a-b)&m} {(a*b)&m} {a//b}\n' for a,b in pairs).encode()
    observations = {}
    for name, cmd in [('c', [str(out/'c-oracle')]), ('rust', [str(out/'rust-oracle')]), ('python_explicit_u64', [sys.executable, str(fixtures/'arithmetic.py'), 'u64']), ('osc_interp_native', [str(out/'osc-oracle'), str(fixtures/'arithmetic.osc')])]:
        actual = run(cmd,payload)
        assert actual == expected, f'ARITHMETIC_MISMATCH:{name}'
        observations[name] = {'rows': len(actual.splitlines()), 'sha256': hashlib.sha256(actual).hexdigest()}
    big = run([sys.executable, str(fixtures/'arithmetic.py'), 'bigint'], payload)
    assert len(big.splitlines()) == len(pairs)
    differing = sum(a != b for a,b in zip(big.splitlines(), expected.splitlines()))
    assert differing > 0, 'BIGINT_SILENTLY_NARROWED'
    for pair in [(m,1),(0,1),(m,2)]:
        i = pairs.index(pair)
        assert big.splitlines()[i] != expected.splitlines()[i], f'COUNTEREXAMPLE_LOST:{pair}'
    mutants=[]
    for name, old, new, compiler in [
        ('c-xor-not-add', 'a + b', 'a ^ b', 'c'),
        ('rust-saturate-not-wrap', 'a.wrapping_add(b)', 'a.saturating_add(b)', 'rust')]:
        src=fixtures/('arithmetic.c' if compiler=='c' else 'arithmetic.rs')
        text=src.read_text();assert text.count(old)==1
        mutant=out/(name+src.suffix);mutant.write_text(text.replace(old,new))
        binary=out/name
        run([cc,'-std=c11','-O2',str(mutant),'-o',str(binary)] if compiler=='c' else [rustc,'--crate-name','semantic_mutant','--edition=2021',str(mutant),'-o',str(binary)])
        actual=run([str(binary)],payload)
        assert len(actual.splitlines())==len(pairs), 'mutant failed without semantic observation'
        kills=sum(a!=b for a,b in zip(actual.splitlines(),expected.splitlines()))
        assert kills>0, f'MUTANT_SURVIVED:{name}'
        mutants.append({'name':name,'mismatched_rows':kills,'compiled':True})
    tracked = [contract,profiles,Path(__file__).resolve()] + sorted(fixtures.glob('arithmetic.*')) + [fixtures/'osc_probe.c', fixtures/'test_evidence_lifecycle.py', ROOT/'mk/universal.mk']
    report={'schema':'omega.universal.semantics.conformance.v1','classification':'DIFFERENTIAL_COMPATIBILITY','result':'PASS','proof':False,'conversion_implemented':False,'source_commit':run(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip(),'rows_per_oracle':len(pairs),'boundary_rows':len(pairs)-256,'random_rows':256,'random_seed':625,'bigint_counterexample_rows':differing,'observations':observations,'mutants':mutants,'versions':versions,'inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked},'artifacts':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [out/'c-oracle',out/'rust-oracle',out/'osc-oracle']},'not_run':['x86_64','RV64GC_QEMU','GPU','checker2_translation_proof','language_import_export']}
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS rows_per_oracle={len(pairs)} bigint_counterexamples={differing} compiled_mutants={len(mutants)} evidence={out}/receipt.json')

if __name__ == '__main__':
    try:
        main()
    except (AssertionError, OSError, KeyError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
