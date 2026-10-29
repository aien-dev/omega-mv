#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# ORACLE (outside the delivered closure), OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2.
# Re-derives every row of fixtures/sha256-vectors.txt today: regenerates the message from its spec
# (generators of fixtures/PROVENANCE.txt), hashes it with Python hashlib and with /usr/bin/sha256sum
# (fed on stdin), and requires both to equal the fixture's digest column and the length column.
# Usage (repository root): vecoracle.py seed/qualification/om2/fixtures/sha256-vectors.txt
import hashlib
import subprocess
import sys

M = (1 << 64) - 1


def rnd(seed, n):
    s, out = seed & M, bytearray()
    while len(out) < n:
        s = (s + 0x9E3779B97F4A7C15) & M
        z = ((s ^ (s >> 30)) * 0xBF58476D1CE4E5B9) & M
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M
        out += (z ^ (z >> 31)).to_bytes(8, "little")
    return bytes(out[:n])


def gen(spec):
    if spec == "-":
        return b""
    kind, rest = spec.split(":", 1)
    if kind == "file":
        return open(rest, "rb").read()
    if kind == "seq":
        return bytes(i % 251 for i in range(int(rest)))
    if kind == "rep":
        b, n = rest.split(":")
        return bytes([int(b)]) * int(n)
    if kind == "rnd":
        s, n = rest.split(":")
        return rnd(int(s), int(n))
    raise SystemExit(f"bad spec {spec}")


ok = bad = 0
for line in open(sys.argv[1]):
    name, spec, n, dig = line.split()
    m = gen(spec)
    h = hashlib.sha256(m).hexdigest()
    s = subprocess.run(["/usr/bin/sha256sum", "-b"], input=m, capture_output=True, check=True).stdout.split()[0].decode()
    if len(m) == int(n) and h == dig and s == dig:
        ok += 1
    else:
        bad += 1
        print(f"FAIL {name} len={len(m)}/{n} hashlib={h} sha256sum={s} fixture={dig}")
print(f"{'PASS' if bad == 0 else 'FAIL'} vecoracle rows={ok + bad} agree={ok} disagree={bad}")
sys.exit(1 if bad else 0)
