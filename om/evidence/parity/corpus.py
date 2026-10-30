#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""corpus.py DIR: write the om/evidence parity corpus, one file DIR/<stream>.txt per stream.

Deterministic (splitmix64, fixed seeds). One record per line; numbers are lowercase hex (signed
values as 64-bit two's complement), byte strings one hex token ("-" when empty). Prints
"<stream> <records>" for every stream on stdout.
"""
import os
import sys

M64 = (1 << 64) - 1


class Rng:
    def __init__(self, seed):
        self.s = seed & M64

    def u64(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(self, n):
        return self.u64() % n

    def bytes(self, n, lo=0, hi=255):
        return bytes(lo + self.below(hi - lo + 1) for _ in range(n))

    def pick(self, xs):
        return xs[self.below(len(xs))]


def hx(b):
    return b.hex() if b else "-"


def num(v):
    return "%x" % (v & M64)


def vspec(r):
    out = []
    for i in range(300):
        has, n = 1, r.u64() & 0xFFFFFFFF
        name = r.bytes(r.below(101), 1, 255)
        if i < 10:
            has = 0
        elif i < 20:
            n = 0
        elif i < 40:
            n = [1, 2, 4096, 0xFFFFFFFF, 0x80000000][i % 5]
        if 40 <= i < 70:  # NUL inside the name: strncpy stops there
            name = name[:r.below(len(name) + 1)] + b"\0" + r.bytes(r.below(10), 0, 255)
        if 70 <= i < 90:
            name = r.bytes(60 + (i - 70) % 8, 1, 255)  # around the 63-byte cut
        out.append("%d %s %s" % (has, hx(name), num(n)))
    return out


def le32(xs):
    return b"".join(x.to_bytes(4, "little") for x in xs)


def vadd(r):
    out = []
    for i in range(500):
        n = [0, 1, 2, 4096, 4095][i] if i < 5 else r.below(4097)
        a = [r.u64() & 0xFFFFFFFF for _ in range(n)]
        b = [r.u64() & 0xFFFFFFFF for _ in range(n)]
        if i % 7 == 0:  # high-bit wrap cases
            a = [0xFFFFFFFF - (x & 15) for x in a]
        c = [(x + y) & 0xFFFFFFFF for x, y in zip(a, b)]
        mode = i % 5
        if n and mode == 1:
            c[r.below(n)] ^= 1 << r.below(32)
        elif n and mode == 2:
            c = [0xDEADBEEF] * n
        elif n and mode == 3:
            c[n - 1] = (c[n - 1] + 1) & 0xFFFFFFFF
            if n > 1 and r.below(2):
                c[r.below(n - 1)] ^= 0x80000000  # two mismatches: the first wins
        out.append("%s %s %s %s" % (num(n), hx(le32(a)), hx(le32(b)), hx(le32(c))))
    return out


def vgen(r):
    return [num(n) for n in [0, 1, 2, 3, 4096] + [r.below(4097) for _ in range(35)]]


def shaline(r):
    h = bytes(r.pick(b"0123456789abcdef") for _ in range(40))  # 40 hex characters
    k = r.below(16)
    if k == 0:
        return b""
    if k == 1:
        return b"\n"
    if k == 2:
        return b"  \r\n \n"
    if k == 3:
        return h[:39] + b"\n"
    if k == 4:
        return h + b"0\n"
    if k == 5:
        return h + b"\r\n"
    if k == 6:
        return h + b" \r \n"
    if k == 7:
        return h + b"\nsecond line\n"
    if k == 8:
        return h[:20] + b"\0" + h[21:] + b"\n"
    if k == 9:
        return r.bytes(300 + r.below(100), 33, 126)  # longer than fgets' 255
    if k == 10:
        return h[:12] + b"\n"
    if k == 11:
        return r.bytes(r.below(80), 0, 255)
    if k == 12:
        return h  # no newline
    if k == 13:
        return b" " * r.below(300)
    return h + b"\n"


def line(r):
    out = []
    for i in range(300):
        k = i % 6
        raw = shaline(r)
        if k == 2 and i % 4 == 0:
            raw = r.bytes(r.below(300), 32, 126) + b"\n"
        n = r.pick([0, 1, 2, 40, 41, 64, 255, 256, 300, r.below(300)])
        out.append("%d %s %s" % (k, hx(raw), num(n)))
    for i in range(24):  # physics.lock whose trimmed length is n, n - 1 or n - 2
        ln = 1 + r.below(200)
        raw = r.bytes(ln, 33, 126) + r.pick([b"\n", b"\r\n", b" \n", b""])
        out.append("2 %s %s" % (hx(raw), num(ln + i % 3)))
    return out


def runid(r):
    lo, hi = -62135596800, 253402300799  # years 1 .. 9999
    edge = [0, -1, 1, 951782400, 1792900000, 2147483647, 2147483648, -2147483648, lo, hi,
            -62135596801 + 1, 4107542399, 4107542400, 978307200, 946684799]
    out = []
    for i in range(200):
        s = edge[i] if i < len(edge) else lo + r.below(hi - lo + 1)
        k = 1 if i % 11 == 5 else 0
        out.append("%s %d %s" % (num(s), k, hx(shaline(r))))
    return out


def hw(r):
    out = []
    for i in range(100):
        has = 0 if i < 5 else 1
        cc = r.pick([0, 0xC9C0, 0xCDC0, 0xFFFFFFFF, r.u64() & 0xFFFFFFFF])
        sm = r.pick([0, 0x1201, 0xFFFFFFFF, r.u64() & 0xFFFFFFFF])
        u = r.bytes(16) if i % 9 else bytes(16)
        out.append("%d %s %s %s" % (has, num(cc), num(sm), hx(u)))
    return out


def relpath(r, n):
    s = bytearray()
    while len(s) < n:
        c = r.pick(b"abcxyz019_-/abcdef/")
        s.append(c)
    if n > 6 and r.below(3) == 0:
        s[-5:] = b".json"
    return bytes(s)


CAND = len("build/qual-runs/") + len("20261026T005320Z-acb3f15cd0ab") + 1  # 46


def path(r):
    out = []
    for i in range(300):
        has = 0 if i < 5 else 1
        if i < 40:
            rl = 1024 - CAND - 2 + (i % 4)  # around the 1024-byte candidate limit
        else:
            rl = r.below(1100) if i % 3 else r.below(60)
        rel = relpath(r, rl)
        c = CAND + rl
        n = r.pick([0, 1, c, c + 1, c - 1, 2048, 1024, r.below(1200)])
        out.append("%d %s %s" % (has, hx(rel), num(n)))
    return out


def digest(r):
    out = []
    prev = []
    for i in range(200):
        hs = he = hb = 1
        if i < 3:
            hs = 0
        elif i < 6:
            he = 0
        elif i < 9:
            hb = 0
        if i < 30:
            sl = 768 - 66 - 2 - (i % 4) + 2  # rel = stem.HEX.ext around 768
            el = 2
        else:
            sl = r.below(120) if i % 4 else r.below(800)
            el = r.below(21)
        stem = relpath(r, sl)
        ext = relpath(r, el).replace(b"/", b"x")
        bl = r.pick([0, 1, 55, 56, 63, 64, 65, 119, 120, 4095, 4096, 4097, 8192, r.below(20001)])
        buf = r.bytes(bl)
        if hb == 0:
            buf = b"" if i == 6 else r.bytes(5)  # NULL buffer with len 0 (ok) or len 5 (refused)
        if i >= 40 and prev and r.below(6) == 0:  # exactly the same content again: rc 1
            stem, ext, buf = r.pick(prev)
        elif hs and he and hb:
            prev.append((stem, ext, buf))
        n = r.pick([0, 2048, 1024, 900, r.below(1100), CAND + len(stem) + len(ext) + 66])
        out.append("%d %s %d %s %d %s %s" % (hs, hx(stem), he, hx(ext), hb, hx(buf), num(n)))
    return out


STREAMS = [("vspec", vspec), ("vadd", vadd), ("vgen", vgen), ("line", line), ("runid", runid),
           ("hw", hw), ("path", path), ("digest", digest)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    for seed, (name, fn) in enumerate(STREAMS):
        recs = fn(Rng(0x0E51D0 + seed))
        with open(os.path.join(d, name + ".txt"), "w") as f:
            f.write("".join(x + "\n" for x in recs))
        print(name, len(recs))


if __name__ == "__main__":
    main()
