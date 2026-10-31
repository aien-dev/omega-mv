#!/usr/bin/env python3
# Verification harness only (not a build input). Computes the full-domain digest layout of
# tests/test_omega_transc.c over the raw opencl-host --sweep stream:
#   B_c,b = sha256(65536 bytes = 2^14 LE fp32 outputs), C_c = sha256(B_c,0..B_c,1023),
#   D = sha256(C_0..C_255).
# usage: chunkdig.py CHUNKFILE   pass stdin through to stdout, append "start hexC" to CHUNKFILE
#        chunkdig.py --final CHUNKFILE   print D over the 256 chunk digests in start order
import hashlib, sys

if sys.argv[1] == "--final":
    fields = [l.split() for l in open(sys.argv[2])]
    if any(len(f) != 2 or len(f[1]) != 64 for f in fields):
        print("INCOMPLETE short_or_bad_chunks"); sys.exit(1)
    rows = sorted((int(s), h) for s, h in fields)
    starts = [s for s, _ in rows]
    if starts != [c << 24 for c in range(256)]:
        print("INCOMPLETE chunks=%d" % len(rows)); sys.exit(1)
    print(hashlib.sha256(b"".join(bytes.fromhex(h) for _, h in rows)).hexdigest()); sys.exit(0)

start, out, inp = int(sys.argv[2]), sys.stdout.buffer, sys.stdin.buffer
blocks, n = [], 0
while True:
    b = inp.read(65536)
    if not b: break
    out.write(b)
    if len(b) != 65536:
        while len(b) < 65536:
            more = inp.read(65536 - len(b))
            if not more: break
            out.write(more); b += more
    n += len(b)
    blocks.append(hashlib.sha256(b).digest())
out.flush()
with open(sys.argv[1], "a") as f:
    if n == 1 << 26 and len(blocks) == 1024:
        f.write("%d %s\n" % (start, hashlib.sha256(b"".join(blocks)).hexdigest()))
    else:
        f.write("%d SHORT bytes=%d\n" % (start, n))
