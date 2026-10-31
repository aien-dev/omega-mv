#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Writes the vectors for the P-256 p2_* section of om/tls/ec.om (p256_driver.om format) to stdout.
# Test oracle only: Python integers and textbook affine arithmetic, no code shared with ec.om.
# Usage: gen_vectors.py [N_RANDOM] [SEED]
import random
import sys

P = 2**256 - 2**224 + 2**192 + 2**96 - 1
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def h(v):
    return format(v, "064x")


def add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    (x1, y1), (x2, y2) = p1, p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        l = (3 * x1 * x1 - 3) * pow(2 * y1, -1, P) % P
    else:
        l = (y2 - y1) * pow(x2 - x1, -1, P) % P
    x3 = (l * l - x1 - x2) % P
    return (x3, (l * (x1 - x3) - y1) % P)


def mul(k, pt):
    r = None
    for bit in bin(k)[2:] if k else "":
        r = add(r, r)
        if bit == "1":
            r = add(r, pt)
    return r


def pt_line(r):
    return (h(0), h(0), 0) if r is None else (h(r[0]), h(r[1]), 1)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    edge = [0, 1, 2, 3, P - 1, P - 2, P - 3, 2**32 - 1, 2**32, 2**224, 2**192, 2**96, 2**96 - 1,
            2**255, P - 2**224, P - 2**96, (P - 1) // 2, 2**256 - 2**224 - 1]
    edge = [e % P for e in edge]
    # words that exercise every Solinas column at its extremes: all-ones / all-zero 32-bit words
    for m in range(16):
        e = sum((0xFFFFFFFF if (m >> (i % 4)) & 1 else 0) << (32 * i) for i in range(8))
        edge.append(e % P)
    vals = edge + [rnd.randrange(P) for _ in range(n)]
    out = []
    for a in edge:
        for b in edge:
            out.append(f"M {h(a)} {h(b)} {h(a * b % P)}")
            out.append(f"A {h(a)} {h(b)} {h((a + b) % P)}")
            out.append(f"U {h(a)} {h(b)} {h((a - b) % P)}")
    # multiplication accepts any inputs below 2^256 (used only with reduced inputs by the library)
    for a in [2**256 - 1, 2**256 - 2, P, P + 1] + vals[:20]:
        for b in [2**256 - 1, P, P + 5, 1, 0]:
            out.append(f"M {h(a)} {h(b)} {h(a * b % P)}")
    for _ in range(n):
        a, b = rnd.randrange(P), rnd.randrange(P)
        out.append(f"M {h(a)} {h(b)} {h(a * b % P)}")
        out.append(f"A {h(a)} {h(b)} {h((a + b) % P)}")
        out.append(f"U {h(a)} {h(b)} {h((a - b) % P)}")
    for a in vals:
        out.append(f"S {h(a)} {h(a * a % P)}")
        out.append(f"I {h(a)} {h(pow(a, P - 2, P))}")
    # scalar multiplication: edge scalars on G and on random points, random scalars
    pts = [G] + [mul(rnd.randrange(1, N), G) for _ in range(3)]
    scal = [0, 1, 2, 3, 15, 16, 17, N - 1, N - 2, N, N + 1, (N - 1) // 2, 2**255, 2**256 - 1,
            0x10, 0xF0, 0x8000000000000000000000000000000000000000000000000000000000000000]
    for q in pts:
        for k in scal:
            rx, ry, f = pt_line(mul(k, q))
            out.append(f"K {h(k)} {h(q[0])} {h(q[1])} {rx} {ry} {f}")
    for _ in range(max(n // 10, 4)):
        q = pts[rnd.randrange(len(pts))]
        k = rnd.randrange(2**256)
        rx, ry, f = pt_line(mul(k, q))
        out.append(f"K {h(k)} {h(q[0])} {h(q[1])} {rx} {ry} {f}")
    # double-scalar: random, infinity (u G + v Q with Q = G and u + v = n), doubling (u = v, Q = G)
    for _ in range(max(n // 10, 4)):
        q = pts[rnd.randrange(len(pts))]
        u, v = rnd.randrange(N), rnd.randrange(N)
        rx, ry, f = pt_line(add(mul(u, G), mul(v, q)))
        out.append(f"D {h(u)} {h(v)} {h(q[0])} {h(q[1])} {rx} {ry} {f}")
    for u, v in [(5, N - 5), (7, 7), (0, 0), (0, 1), (1, 0), (N - 1, N - 1)]:
        rx, ry, f = pt_line(add(mul(u, G), mul(v, G)))
        out.append(f"D {h(u)} {h(v)} {h(G[0])} {h(G[1])} {rx} {ry} {f}")
    print("\n".join(out))


main()
