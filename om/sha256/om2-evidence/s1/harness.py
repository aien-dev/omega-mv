#!/usr/bin/env python3
# TEMPORARY harness (L838, oracle side, outside the delivered closure): feeds every row of the L836
# fixture fixtures/sha256-vectors.txt to a program on stdin and compares its hex output with the
# fixture digest. Generators as in fixtures/PROVENANCE.txt section 2.
import sys, subprocess
M = (1 << 64) - 1
def gen(spec):
    if spec == '-': return b''
    k, *a = spec.split(':')
    if k == 'file': return open('fx/msg/' + a[0].rsplit('/', 1)[1], 'rb').read()
    if k == 'seq': return bytes(i % 251 for i in range(int(a[0])))
    if k == 'rep': return bytes([int(a[0])]) * int(a[1])
    if k == 'rnd':
        s, n, out = int(a[0]), int(a[1]), bytearray()
        while len(out) < n:
            s = (s + 0x9E3779B97F4A7C15) & M; z = s
            z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M
            z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M
            z ^= z >> 31; out += z.to_bytes(8, 'little')
        return bytes(out[:n])
    raise ValueError(spec)
ok = bad = 0
for line in open('fx/sha256-vectors.txt'):
    if not line.strip() or line.startswith('#'): continue
    name, spec, n, dig = line.split()
    msg = gen(spec); assert len(msg) == int(n), name
    r = subprocess.run([sys.argv[1]], input=msg, capture_output=True)
    got = r.stdout.decode().strip()
    if r.returncode == 0 and got == dig: ok += 1
    else: bad += 1; print('FAIL', name, r.returncode, got)
print(f'vectors pass={ok} fail={bad}')
sys.exit(1 if bad else 0)
