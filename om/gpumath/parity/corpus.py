#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Deterministic parity corpus for om/gpumath/gpumath.om (aien-dev/omega-mv#726).

usage: corpus.py OUTDIR [--c-cases FILE]

Writes one record file per stream into OUTDIR (record formats: the harness section of gpumath.om).
Every random value comes from splitmix64 with a fixed seed per stream, so the files are the same
on every run and every Python 3. --c-cases also writes the typed omega_gpu_fmt calls a C oracle
needs for the fmt stream (one `case` per M record, in order).
"""
import math
import os
import struct
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

    def unit(self):
        return (self.u64() >> 11) / float(1 << 53)

    def pick(self, seq):
        return seq[self.below(len(seq))]


def dbits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def bitsd(u):
    return struct.unpack("<d", struct.pack("<Q", u))[0]


def fbits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def h(v):
    return format(v, "x")


def hb(bs):
    return bs.hex() if bs else "-"


D_SPECIAL = [
    0, 1 << 63, 1, (1 << 63) | 1, 0x000FFFFFFFFFFFFF, 0x800FFFFFFFFFFFFF, 0x0010000000000000,
    0x8010000000000000, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF, 0x7FF0000000000000, 0xFFF0000000000000,
    0x7FF8000000000000, 0xFFF8000000000000, 0x7FF8000000000001, 0x7FF0000000000001, 0xFFF4000000000123,
    0x7FFFFFFFFFFFFFFF, 0x3FF0000000000000, 0xBFF0000000000000, 0x4000000000000000, 0x3FE0000000000000,
    0x3FF6A09E667F3BCD, 0x3FF6A09E667F3BCC, 0x3FF6A09E667F3BCE, 0x0000000000000002, 0x0008000000000000,
]
F_SPECIAL = [
    0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000, 0x7F7FFFFF, 0xFF7FFFFF,
    0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FC00001, 0x7F800001, 0xFFA00123, 0x7FFFFFFF,
    0x3F800000, 0xBF800000, 0x1F800000, 0x1F7FFFFF, 0x5F800000, 0x5F7FFFFF, 0x20000000,
]


def rand_double(r):
    """A double from a mix of classes: raw bits, specials, subnormals, normals of every exponent."""
    k = r.below(8)
    if k == 0:
        return r.u64()
    if k == 1:
        return r.pick(D_SPECIAL)
    if k == 2:
        return (r.below(2) << 63) | (r.u64() & 0x000FFFFFFFFFFFFF)
    return (r.below(2) << 63) | ((1 + r.below(2046)) << 52) | (r.u64() & 0x000FFFFFFFFFFFFF)


def rand_f32(r):
    k = r.below(8)
    if k == 0:
        return r.pick(F_SPECIAL)
    if k == 1:
        return (r.below(2) << 31) | (r.u64() & 0x7FFFFF)
    if k == 2:
        return (r.below(2) << 31) | (0x7F800000) | (r.u64() & 0x7FFFFF)
    return r.u64() & 0xFFFFFFFF


def norm_with_exp(r, e):
    """A random double with unbiased exponent e (clamped to the normal range), random sign."""
    e = max(-1022, min(1023, e))
    return (r.below(2) << 63) | ((e + 1023) << 52) | (r.u64() & 0x000FFFFFFFFFFFFF)


def short_mant(r, e, bits):
    """A double 1.f * 2^e whose significand has at most `bits` significant bits."""
    m = (1 << (bits - 1)) | r.below(1 << (bits - 1))
    return (r.below(2) << 63) | ((e + 1023) << 52) | ((m << (53 - bits)) & 0x000FFFFFFFFFFFFF)


def gen_fma(out):
    r = Rng(0x726F4D41)
    n = 0
    lines = []
    def rec(a, b, c):
        lines.append("Z %s %s %s\n" % (h(a), h(b), h(c)))
    # every pair of specials with every special addend
    for a in D_SPECIAL:
        for b in D_SPECIAL:
            for c in D_SPECIAL[::3]:
                rec(a, b, c)
    for _ in range(100000):  # class mix
        rec(rand_double(r), rand_double(r), rand_double(r))
    for _ in range(60000):  # near cancellation: c close to -(a * b)
        ea = r.below(400) - 200
        eb = r.below(400) - 200
        a = norm_with_exp(r, ea)
        b = norm_with_exp(r, eb)
        p = bitsd(a) * bitsd(b)
        c = dbits(-p) ^ r.below(16)
        if r.below(4) == 0:
            c = (c & ~0xFFFF) | r.below(1 << 16)
        rec(a, b, c & M64)
    for _ in range(50000):  # exact short products plus addends at and around half an ulp (ties)
        ea = r.below(200) - 100
        eb = r.below(200) - 100
        a = short_mant(r, ea, 1 + r.below(27))
        b = short_mant(r, eb, 1 + r.below(27))
        p = abs(bitsd(a) * bitsd(b))
        e = math.frexp(p)[1] - 53
        f = r.pick([0.5, 0.25, 0.75, 1.5, 1.0, 2.5, 0.5 + 2.0 ** -40, 0.5 - 2.0 ** -40, 3.0])
        c = math.ldexp(f, e) * (1 if r.below(2) else -1)
        rec(a, b, dbits(c))
    for _ in range(50000):  # products near and below the subnormal range, addends tiny
        ea = r.below(1100) - 1100
        eb = -1022 - 60 - ea + r.below(120)
        a = norm_with_exp(r, ea) if r.below(5) else (r.u64() & 0x800FFFFFFFFFFFFF)
        b = norm_with_exp(r, eb)
        c = r.pick([0, 1 << 63, r.u64() & 0x800FFFFFFFFFFFFF, norm_with_exp(r, -1022 + r.below(8))])
        rec(a, b, c)
    for _ in range(20000):  # products near overflow
        ea = 500 + r.below(524)
        eb = 1023 - ea + r.below(3) - 1
        rec(norm_with_exp(r, ea), norm_with_exp(r, eb), r.pick([0, rand_double(r), 0xFFEFFFFFFFFFFFFF, 0x7FEFFFFFFFFFFFFF]))
    for _ in range(20000):  # the hostmath shapes: -n * piece + a, s * t2 + 1/k
        nd = float(r.below(1 << 21)) * (1 if r.below(2) else -1)
        piece = r.pick([0x3FF921FB54400000, 0x3DD0B4611A600000, 0x3BA3198A2E000000, 0x397B839A252049C1,
                        0x3FE62E42FEE00000, 0x3DEA39EF35793C76])
        a = dbits(nd * 1.5707963267948966 + (r.unit() - 0.5))
        rec(dbits(-nd), piece, a)
    for _ in range(20000):  # exact 54-bit products (a tie) plus an addend far below: only the sticky bit decides
        while True:
            ma = (1 << 26) | r.below(1 << 26) | 1
            mb = (1 << 26) | r.below(1 << 26) | 1
            if ma * mb >= 1 << 53:
                break
        ea = r.below(600) - 300
        eb = r.below(600) - 300
        a = math.ldexp(ma, ea) * (1 if r.below(2) else -1)
        b = math.ldexp(mb, eb) * (1 if r.below(2) else -1)
        low = ea + eb  # exponent of the product's lowest (tie) bit
        c = math.ldexp(1.0 + r.unit(), max(-1074, low - 2 - r.below(900))) * (1 if r.below(2) else -1)
        rec(dbits(a), dbits(b), r.pick([dbits(c), dbits(c), dbits(c), 0, 1 << 63]))
    with open(os.path.join(out, "fma.txt"), "w") as f:
        f.writelines(lines)
    return len(lines)


def gen_log(out):
    r = Rng(0x6C6F67)
    L = []
    for v in D_SPECIAL:
        L.append(v)
    for e in range(-1074, 1024):
        L.append(dbits(math.ldexp(1.0, e)))
    for _ in range(100000):
        L.append(r.u64())
    for _ in range(100000):
        if r.below(20) == 0:
            L.append(1 + (r.u64() & 0x000FFFFFFFFFFFFE))
        else:
            L.append(((1 + r.below(2046)) << 52) | (r.u64() & 0x000FFFFFFFFFFFFF))
    for _ in range(20000):
        L.append(dbits(1.0 + (r.unit() - 0.5) * 2.0 ** -(r.below(50))))
    for d in range(-64, 65):
        L.append(0x3FF6A09E667F3BCD + d)
    for e in range(-1070, 1024, 3):  # x = 2^e (1 + tiny): t is small, the fused s * 2t + e * ln2lo matters
        for _ in range(6):
            L.append(dbits(math.ldexp(1.0 + r.unit() * 2.0 ** -(1 + r.below(45)), e)))
    with open(os.path.join(out, "log.txt"), "w") as f:
        f.writelines("A %s\n" % h(v) for v in L)
    return len(L)


def gen_exp(out):
    r = Rng(0x657870)
    L = list(D_SPECIAL)
    for _ in range(100000):
        L.append(r.u64())
    for _ in range(100000):
        L.append(dbits(-750.0 + 1462.0 * r.unit()))
    for edge in (709.78, -745.2):
        for d in range(-200, 201):
            L.append(dbits(edge) + d)
    ln2 = math.log(2.0)
    for k in list(range(-1080, -1015)) + list(range(1018, 1025)) + list(range(-5, 6)):
        for _ in range(60):
            L.append(dbits(k * ln2 + (r.unit() - 0.5) * 1.4))
    for _ in range(5000):
        L.append(dbits((r.unit() - 0.5) * 2.0 ** -(r.below(60))))
    for k in range(-1075, 1025):  # y within a few ulps of k * ln2: r is small, the fused reduction matters
        y = dbits(k * ln2)
        for d in (-3, -1, 0, 1, 2):
            L.append((y + d) & M64)
    with open(os.path.join(out, "exp.txt"), "w") as f:
        f.writelines("B %s\n" % h(v) for v in L)
    return len(L)


def gen_pow(out):
    r = Rng(0x706F77)
    P = []
    thetas = [10000.0, 500000.0, 1000000.0, 5000000.0, 10000000.0, 1000.0, 50000.0]
    for th in thetas:
        for d in (64, 80, 96, 128, 256):
            for i in range(d // 2):
                P.append((dbits(th), dbits(-(2.0 * i) / d)))
                P.append((dbits(th), dbits((2.0 * i) / d)))
    for _ in range(40000):
        P.append((dbits(math.exp((r.unit() - 0.5) * 40.0)), dbits((r.unit() - 0.5) * 20.0)))
    for _ in range(30000):
        P.append((r.u64(), r.u64()))
    for _ in range(25000):
        P.append((rand_double(r), rand_double(r)))
    for a in D_SPECIAL:
        for b in D_SPECIAL:
            P.append((a, b))
    with open(os.path.join(out, "pow.txt"), "w") as f:
        f.writelines("C %s %s\n" % (h(a), h(b)) for a, b in P)
    return len(P)


def gen_sincos(out):
    r = Rng(0x73696E)
    L = list(D_SPECIAL)
    for _ in range(100000):
        L.append(r.u64())
    lim = 2.0 ** 20 * math.pi / 2
    for _ in range(100000):
        L.append(dbits((2.0 * r.unit() - 1.0) * lim))
    for _ in range(100000):  # RoPE angles: pos * theta^(-2i/d), pos < 65536
        th = r.pick([10000.0, 500000.0, 1000000.0, 10000000.0])
        d = r.pick([64, 128, 256])
        i = r.below(d // 2)
        inv = th ** (-(2.0 * i) / d)
        L.append(dbits(float(r.below(65536)) * inv))
    for _ in range(30000):  # near multiples of pi/2: the reduction cancels
        n = r.below(1 << 21) - (1 << 20)
        L.append((dbits(n * (math.pi / 2)) + r.below(9) - 4) & M64)
    for _ in range(10000):
        L.append(dbits((r.unit() - 0.5) * 2.0 ** -(r.below(70))))
    with open(os.path.join(out, "sincos.txt"), "w") as f:
        f.writelines("D %s\n" % h(v) for v in L)
    return len(L)


def gen_pow2f(out):
    r = Rng(0x703266)
    L = list(range(-1000, 1001)) + [-(1 << 31), (1 << 31) - 1, -(1 << 31) + 1, 1 << 30, -(1 << 30)]
    for _ in range(10000):
        L.append(r.below(1 << 32) - (1 << 31))
    with open(os.path.join(out, "pow2f.txt"), "w") as f:
        f.writelines("E %s\n" % h(v & M64) for v in L)
    return len(L)


def gen_e4m3(out):
    with open(os.path.join(out, "e4m3.txt"), "w") as f:
        f.writelines("F %s\n" % h(b) for b in range(256))
    return 256


def le16(vals):
    return b"".join(struct.pack("<H", v) for v in vals)


def le32(vals):
    return b"".join(struct.pack("<I", v) for v in vals)


def gen_scales(out):
    r = Rng(0x73636C)
    lines = []
    for nf in (0, 1):
        for v in range(65536):
            lines.append("G %d %s\n" % (nf, hb(le16([v]))))
    bad = [0x7F80, 0xFF80, 0x7FC0, 0x7F81, 0x6000, 0x6400, 0x7F7F]  # NaN/inf, and folds past 3e38
    for i in range(60):
        n = r.pick([0, 1, 2, 4095, 4096]) if i < 10 else 1 + r.below(4096)
        vals = [r.below(0x5000) | (r.below(2) << 15) for _ in range(n)]
        if n and i % 2:
            vals[r.below(n)] = r.pick(bad)
        lines.append("G %d %s\n" % (i % 3 == 0, hb(le16(vals))))
    with open(os.path.join(out, "scales.txt"), "w") as f:
        f.writelines(lines)
    return len(lines)


def gen_xf32(out):
    r = Rng(0x786633)
    vals = list(F_SPECIAL) + [rand_f32(r) for _ in range(100000)]
    lines = ["H -\n"]
    for o in range(0, len(vals), 4096):
        lines.append("H %s\n" % hb(le32(vals[o:o + 4096])))
    lines.append("H %s\n" % hb(le32(vals[:777])))
    with open(os.path.join(out, "xf32.txt"), "w") as f:
        f.writelines(lines)
    return len(vals) + 777


def gen_xbf16(out):
    lines = ["I -\n"]
    for o in range(0, 65536, 4096):
        lines.append("I %s\n" % hb(le16(range(o, o + 4096))))
    lines.append("I %s\n" % hb(le16(range(0x7F00, 0x7F00 + 333))))
    with open(os.path.join(out, "xbf16.txt"), "w") as f:
        f.writelines(lines)
    return 65536 + 333


def gen_scan(out):
    r = Rng(0x73636E)
    lines = []
    sizes = [0, 1, 2, 4095, 4096, 4097, 8191, 8192, 8193, 12288, 20000]
    for i in range(400):
        n = sizes[i] if i < len(sizes) else r.below(20001)
        dens = r.pick([0, 0, 1, 5, 100, 2000, 65536])
        w = bytearray(r.below(256) for _ in range(n))
        for j in range(n):
            if w[j] & 0x7F == 0x7F and not (dens and r.below(65536) < dens):
                w[j] ^= 1
            elif dens and r.below(65536) < dens:
                w[j] = r.pick([0x7F, 0xFF])
        lines.append("N %s\n" % hb(bytes(w)))
    with open(os.path.join(out, "scan.txt"), "w") as f:
        f.writelines(lines)
    return len(lines)


def gen_gemv(out):
    r = Rng(0x67656D)
    lines = []
    rows = 0
    shapes = [(128, 128), (128, 256), (256, 128), (256, 384), (128, 512), (384, 256), (128, 2048),
              (256, 1024), (128, 128), (256, 256), (128, 384), (128, 1024)]
    for i, (n, k) in enumerate(shapes):
        nan_ok = i % 3 == 0
        w = bytearray(r.below(256) for _ in range(n * k))
        if not nan_ok:
            for j in range(len(w)):
                if w[j] & 0x7F == 0x7F:
                    w[j] ^= 1
        sc = [r.below(0x8000) | (r.below(2) << 15) if i % 4 == 1 else (0x3B00 + r.below(0x700)) for _ in range((n // 128) * (k // 128))]
        if i % 4 == 2:
            x = [rand_f32(r) for _ in range(k)]
        else:
            x = [fbits((r.unit() - 0.5) * 8.0) for _ in range(k)]
        lines.append("Q %s %s %s %s %s\n" % (h(n), h(k), hb(bytes(w)), hb(le16(sc)), hb(le32(x))))
        rows += n
    # first NaN of every row is an e4m3 NaN code times a NaN x with a payload: the operand order decides
    n, k = 128, 256
    w = bytearray(r.below(256) for _ in range(n * k))
    for j in range(len(w)):
        if w[j] & 0x7F == 0x7F:
            w[j] ^= 1
    c0 = 3 + r.below(100)
    for row in range(n):
        w[row * k + c0] = r.pick([0x7F, 0xFF])
    x = [fbits((r.unit() - 0.5) * 8.0) for _ in range(k)]
    x[c0] = 0x7FC00000 | (1 + r.below(0x3FFFFF)) | (r.below(2) << 31)
    x[c0 + 7] = 0x7F800001 + r.below(0x3FFFFF)
    sc = [0x3B00 + r.below(0x700) for _ in range((n // 128) * (k // 128))]
    lines.append("Q %s %s %s %s %s\n" % (h(n), h(k), hb(bytes(w)), hb(le16(sc)), hb(le32(x))))
    rows += n
    with open(os.path.join(out, "gemv.txt"), "w") as f:
        f.writelines(lines)
    return rows


# ---- omega_gpu_fmt -------------------------------------------------------------------------------

def fmt_kinds(fb):
    """The va_arg kinds omega_gpu_fmt consumes for format bytes fb (a model of the C parser)."""
    out = []
    p = 0
    n = len(fb)
    at = lambda i: fb[i] if i < n else 0
    while at(p):
        if at(p) != 37:
            p += 1
            continue
        q = p + 1
        z = 0
        l = 0
        if at(q) == 122:
            z = 1
            q += 1
        else:
            while at(q) == 108 and l < 2:
                l += 1
                q += 1
        c = at(q)
        k = 0
        if c == 115:
            k = 9
        elif c == 99:
            k = 1
        elif c == 100:
            k = 4 if z else (3 if l == 2 else (2 if l == 1 else 1))
        elif c in (117, 120):
            k = 8 if z else (7 if l == 2 else (6 if l == 1 else 5))
        if k:
            out.append((k, c))
        p = q if c == 0 else q + 1
    return out


C_TYPES = {1: "int", 2: "long", 3: "long long", 4: "ptrdiff_t", 5: "unsigned", 6: "unsigned long",
           7: "unsigned long long", 8: "size_t"}

STRINGS = [b"", b"hello", b"x", b"stage: device open", b"%d%s%%", bytes(range(1, 256)), b"a" * 300,
           b"\xff\xfe\x80 bytes", b"(null)", b"tab\tnl\n"]


def cstr(bs):
    return '"' + "".join("\\%03o" % c for c in bs) + '"'


def fmt_value(r, k, c):
    """(word for the .om, C argument expression) for kind k."""
    if k == 9:
        if r.below(6) == 0:
            return None, "(const char *)0"
        s = r.pick(STRINGS)
        return s, cstr(s)
    if k == 1:
        v = r.pick([0, 1, -1, 2147483647, -2147483648, 42, -42, 65, 255, 256, 300, -129, 10, 99999])
        if r.below(3) == 0:
            v = r.below(1 << 32) - (1 << 31)
        word = v & 0xFFFFFFFF
        if r.below(2):
            word |= r.u64() & 0xFFFFFFFF00000000  # upper half garbage: the C reads an int
        return word, "(int)%dLL" % v if v != -2147483648 else "(int)(-2147483647 - 1)"
    if k in (2, 3, 4):
        v = r.pick([0, 1, -1, (1 << 63) - 1, -(1 << 63), 1 << 40, -(1 << 40), 123456789012])
        if r.below(3) == 0:
            v = r.u64() - (1 << 63)
        e = "%dLL" % v if v != -(1 << 63) else "(-9223372036854775807LL - 1)"
        return v & M64, "(%s)%s" % (C_TYPES[k], e)
    if k == 5:
        v = r.pick([0, 1, 0xFFFFFFFF, 0x80000000, 255, 4096, 0xDEADBEEF])
        if r.below(3) == 0:
            v = r.below(1 << 32)
        word = v
        if r.below(2):
            word |= r.u64() & 0xFFFFFFFF00000000
        return word, "(unsigned)%dU" % v
    v = r.pick([0, 1, M64, 1 << 63, 0xFFFFFFFF, 1 << 32, 0xDEADBEEFCAFEF00D])
    if r.below(3) == 0:
        v = r.u64()
    return v, "(%s)%dULL" % (C_TYPES[k], v)


def fmt_record(r, fb, cap, lines, cases):
    kinds = fmt_kinds(fb)
    words = []
    cargs = []
    pool = bytearray()
    for k, c in kinds:
        w, ce = fmt_value(r, k, c)
        if k == 9:
            if w is None:
                words.append(M64)
            else:
                if len(pool) + len(w) + 1 > 4000:
                    w, ce = b"x", cstr(b"x")
                words.append(len(pool))
                pool += w + b"\0"
        else:
            words.append(w)
        cargs.append(ce)
    if not pool:
        pool = bytearray(b"\0")
    ks = "".join(str(k) for k, _ in kinds) or "-"
    parts = ["M", h(cap), hb(fb), h(len(words))] + [h(w) for w in words] + [hb(bytes(pool))]
    lines.append(" ".join(parts) + "\n")
    cases.append("case %d: kinds = \"%s\"; n = omega_gpu_fmt(out, %d, %s%s); break;\n"
                 % (len(cases), ks, cap, cstr(fb), "".join(", " + a for a in cargs)))


def gen_fmt(out, cfile):
    r = Rng(0x666D74)
    lines = []
    cases = []
    alpha = b"%lzdusxcA"
    import itertools
    for L in (1, 2, 3, 4):
        for t in itertools.product(alpha, repeat=L):
            fb = bytes(t)
            if len(fmt_kinds(fb)) > 16:
                continue
            fmt_record(r, fb, 1024, lines, cases)
            fmt_record(r, fb, r.below(len(fb) + 30), lines, cases)
    mods = [b"", b"l", b"ll", b"lll", b"z", b"zl", b"lz", b"zz"]
    convs = b"duxsc%qXi.5-"
    for m in mods:
        for c in convs:
            for _ in range(20):
                fb = b"<" + b"%" + m + bytes([c]) + b">"
                fmt_record(r, fb, r.pick([1024, 1024, 0, 1, 2, 3, 5, 8]), lines, cases)
        fmt_record(r, b"tail %" + m, 1024, lines, cases)
    pieces = [b"%d", b"%ld", b"%lld", b"%zd", b"%u", b"%lu", b"%llu", b"%zu", b"%x", b"%lx", b"%llx", b"%zx",
              b"%s", b"%c", b"%%", b"%q", b"%5d", b"%", b"%l", b"%z", b"stage ", b": ", b"=", b" rc ", b"\xff"]
    for _ in range(12000):
        fb = bytearray()
        for _ in range(r.below(14)):
            fb += r.pick(pieces)
        fb = bytes(fb)
        if len(fmt_kinds(fb)) > 16 or len(fb) > 1000:
            continue
        cap = r.pick([1024, 1024, 1024, r.below(80), r.below(8), 1, 0])
        fmt_record(r, fb, cap, lines, cases)
    with open(os.path.join(out, "fmt.txt"), "w") as f:
        f.writelines(lines)
    if cfile:
        with open(cfile, "w") as f:
            f.write("/* generated by om/gpumath/parity/corpus.py: one typed omega_gpu_fmt call per M record */\n")
            f.write("static int fmt_case(long idx, char *out, const char **kindsp) {\n  int n = -1; const char *kinds = 0;\n  switch (idx) {\n")
            f.writelines("  " + c for c in cases)
            f.write("  default: break;\n  }\n  *kindsp = kinds;\n  return n;\n}\n")
    return len(lines)


def main():
    if len(sys.argv) not in (2, 4) or (len(sys.argv) == 4 and sys.argv[2] != "--c-cases"):
        sys.exit(__doc__)
    out = sys.argv[1]
    cfile = sys.argv[3] if len(sys.argv) == 4 else None
    os.makedirs(out, exist_ok=True)
    counts = [
        ("fma", gen_fma(out)), ("log", gen_log(out)), ("exp", gen_exp(out)), ("pow", gen_pow(out)),
        ("sincos", gen_sincos(out)), ("pow2f", gen_pow2f(out)), ("e4m3", gen_e4m3(out)),
        ("scales", gen_scales(out)), ("xf32", gen_xf32(out)), ("xbf16", gen_xbf16(out)),
        ("scan", gen_scan(out)), ("gemv", gen_gemv(out)), ("fmt", gen_fmt(out, cfile)),
    ]
    for name, n in counts:
        print("%s %d" % (name, n))


if __name__ == "__main__":
    main()
