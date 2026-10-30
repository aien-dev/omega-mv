#!/usr/bin/env python3
"""Reference-only compiled mutation gate for L524. Compile errors never count as kills."""
from pathlib import Path
import hashlib
import os
import subprocess

root = Path(__file__).resolve().parents[2]
os.chdir(root)
out = root/'build/seed/ir-trap-mutants'
out.mkdir(parents=True, exist_ok=True)
src = (root/'seed/omc/omc.om').read_text()
mutants = [
    ('wrong-code', 'ir_op 12 code 0 0 0', 'ir_op 12 1 0 0 0', 'FAIL trap03 Wolfram boundary outputs'),
    ('void-tag', 'if eq get ir_ret 0 76\nemitb 0', 'if eq get ir_ret 0 76\nemitb 76', 'FAIL void exact C-oracle IR'),
    ('unchecked-code', 'if or lt code 1 gt code 14\nfail 8', 'if false\nfail 8', 'FAIL too_large refused error 8'),
    ('eol-code', 'if eq k 0\nfail eolc', 'if eq k 0\nfail 16', 'FAIL missing refused error 4'),
    ('name-code', 'end\nend\nend\nend\nfail 16\nret 0', 'end\nend\nend\nend\nfail 4\nret 0', 'FAIL named refused error 16'),
]
for name, old, new, marker in mutants:
    assert src.count(old) == 1, name
    path = out/(name+'.om')
    path.write_text(src.replace(old, new))
    env = dict(os.environ, OMC_SOURCE=str(path))
    p = subprocess.run(['python3','seed/omc/gate-ir-trap.py','--build-only'],env=env,capture_output=True,timeout=30)
    (out/(name+'-build.log')).write_bytes(p.stdout+p.stderr)
    # Outside the kill classification: a refusal, crash or empty shell is never a kill.
    assert p.returncode == 0, f'{name}: compilation/fixpoint failure is NOT caught'
    elf = root/'build/seed/ir-trap/stage3'
    data = elf.read_bytes()
    assert len(data) >= 64 and data[:4] == b'\x7fELF' and data[18:20] == b'\xb7\x00'
    p = subprocess.run(['python3','seed/omc/gate-ir-trap.py'],env=env,capture_output=True,timeout=60)
    (out/(name+'-test.log')).write_bytes(p.stdout+p.stderr)
    assert p.returncode == 1 and marker.encode() in p.stdout and b'CHECKS=67 ' in p.stdout, (name,p.stdout,p.stderr)
    print(f'CAUGHT {name}: compiled ELF sha256={hashlib.sha256(data).hexdigest()}; {marker}',flush=True)
print(f'COMPILED_MUTANTS={len(mutants)} CAUGHT={len(mutants)} RC=0')
