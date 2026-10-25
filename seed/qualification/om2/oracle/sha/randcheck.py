#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# ORACLE (outside the delivered closure), OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2.
# Second, independent oracle for sha_qual rand output: regenerates each message from its printed
# (seed, index, length) with the same splitmix64 byte generator (fixtures/PROVENANCE.txt) and compares
# the OM-2 digest printed by the driver with Python hashlib (OpenSSL).
# Usage: randcheck.py < rand.out    exit 0 iff every RAND line matches and at least one was read.
import hashlib
import sys

M = (1 << 64) - 1


def rnd(seed, n):
    s = seed & M
    out = bytearray()
    while len(out) < n:
        s = (s + 0x9E3779B97F4A7C15) & M
        z = s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M
        z ^= z >> 31
        out += z.to_bytes(8, "little")
    return bytes(out[:n])


ok = bad = 0
for line in sys.stdin:
    f = line.split()
    if not f or f[0] != "RAND":
        continue
    seed, i, n, dig = int(f[1]), int(f[2]), int(f[3]), f[4]
    want = hashlib.sha256(rnd(seed ^ (i << 20) ^ 0x5EED, n)).hexdigest()
    if want == dig:
        ok += 1
    else:
        bad += 1
        print(f"FAIL rand {seed} {i} len={n} got={dig} hashlib={want}")
print(f"{'PASS' if bad == 0 and ok > 0 else 'FAIL'} randcheck hashlib matched={ok} mismatched={bad}")
sys.exit(0 if bad == 0 and ok > 0 else 1)
