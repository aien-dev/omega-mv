#!/usr/bin/env python3
"""Read-only verification: wrong host-word mutants must fail real Omega fixtures.

Python orchestrates compiler invocations only; it does not implement a host word.
Run with nice -n 19 ionice -c3. OMC selects the seed compiler used to compile
candidate sources, not the candidate compiler being qualified.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SEED = Path(os.environ.get("OMC", ROOT / "seed/out/omc")).resolve()
SOURCE = ROOT / "seed/omc/omc.om"


def compile_unit(compiler, source, target):
    result = subprocess.run([str(compiler)], input=source, capture_output=True, timeout=30)
    if result.returncode != 0 or result.stderr:
        raise RuntimeError(f"compile refused rc={result.returncode}: {result.stderr.decode(errors='replace')}")
    target.write_bytes(result.stdout)
    target.chmod(0o700)


def exercise(source, fixture, directory):
    compiler = directory / "omc"
    program = directory / "fixture"
    compile_unit(SEED, source, compiler)
    compile_unit(compiler, (ROOT / "seed/host/tests" / fixture).read_bytes(), program)
    return subprocess.run([str(program)], cwd=directory, capture_output=True, timeout=10)


def main():
    original = SOURCE.read_bytes()
    mutations = [
        ("parent-pid-wrong-syscall", "netpeer.om", b"if eq c 154\nret 173\n", b"if eq c 154\nret 110\n"),
        ("ignored-option-refusal", "netdata.om", b"matconst 0 sub ulong 0 92\n", b"matconst 0 0\n"),
    ]
    with tempfile.TemporaryDirectory(prefix="net-mutants-") as scratch:
        for name, fixture, needle, replacement in mutations:
            if original.count(needle) != 1:
                raise RuntimeError(f"mutation anchor not unique: {name}")
            positive = Path(scratch) / (name + "-positive")
            positive.mkdir()
            result = exercise(original, fixture, positive)
            if result.returncode != 0 or not result.stdout.startswith(b"PASS "):
                raise RuntimeError(f"positive fixture failed: {name}: rc={result.returncode}")
            negative = Path(scratch) / (name + "-mutant")
            negative.mkdir()
            result = exercise(original.replace(needle, replacement), fixture, negative)
            if result.returncode != 1:
                raise RuntimeError(f"mutant not caught by assertion: {name}: rc={result.returncode}")
            print(f"PASS caught {name} via real {fixture} assertion", flush=True)
    print("PASS native networking wrong-word mutants")


if __name__ == "__main__":
    main()
