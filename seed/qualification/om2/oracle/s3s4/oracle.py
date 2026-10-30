#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Independent oracle for seed/qualification/om2/sec/s3s4 (#748, ledger L836, lane QualPhaseB.FixRotUnroll).
# It models the documented semantics (contract omegese-om2.md sections 3-4, OMEGESE.md 9.3/9.4) of each
# fixture directly in Python integers; it never runs or reads output of any Omega compiler.
# Writes exp/<name>.out (exact expected stdout) and exp/exits.txt (<name> <exit status>), and checks that
# every rotation operand has w distinct rotations (so n+1, w-n and ROR-for-ROL mutants change a line).
# Usage: python3 oracle.py   (from any directory; writes next to this file)
import os

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, "exp")
os.makedirs(EXP, exist_ok=True)
M64 = (1 << 64) - 1
EXITS = {}


class Trap(Exception):
    def __init__(self, code):
        self.code = code


def rotr(x, n, w):
    if n >= w:
        raise Trap(7)  # contract section 3 line 79
    m = (1 << w) - 1
    return ((x >> n) | (x << (w - n))) & m if n else x


def rotl(x, n, w):
    if n >= w:
        raise Trap(7)
    return rotr(x, (w - n) % w, w)


def sadd(a, b, bits):  # signed add, trap 1 outside the type (OMEGESE 9.3)
    r = a + b
    if not -(1 << (bits - 1)) <= r < (1 << (bits - 1)):
        raise Trap(1)
    return r


def smul(a, b, bits):
    r = a * b
    if not -(1 << (bits - 1)) <= r < (1 << (bits - 1)):
        raise Trap(1)
    return r


def sdiv(a, b, bits):  # round toward zero; /0 trap 2; min/-1 trap 1
    if b == 0:
        raise Trap(2)
    q = abs(a) // abs(b)
    q = q if (a < 0) == (b < 0) else -q
    if not -(1 << (bits - 1)) <= q < (1 << (bits - 1)):
        raise Trap(1)
    return q


def udiv(a, b):
    if b == 0:
        raise Trap(2)
    return a // b


def urem(a, b):
    if b == 0:
        raise Trap(2)
    return a % b


def conv(v, lo, hi):  # conversion range check, trap 5
    if not lo <= v <= hi:
        raise Trap(5)
    return v


def shl64(x, n):
    if not 0 <= n < 64:
        raise Trap(7)
    return (x << n) & M64


def program(name, body):
    """Run body(out) where out is a list of printed values; record stdout and exit status."""
    out = []
    try:
        ex = body(out)
        ex = 0 if ex is None else ex % 256
    except Trap as t:
        ex = 100 + t.code
    with open(os.path.join(EXP, name + ".out"), "w") as f:
        f.write("".join("%d\n" % v for v in out))
    EXITS[name] = ex


XS = {8: (177, 3), 16: (45508, 5), 32: (4041331395, 13), 64: (9305357566071262703, 29)}
for w, (x, mid) in XS.items():
    assert len({rotr(x, n, w) for n in range(w)}) == w, (w, x)


def rot_values(out):
    for w, (x, mid) in XS.items():
        ns = [0, 1, mid, w - 1]
        for _path in ("const", "runtime"):
            out += [rotr(x, n, w) for n in ns]
            out += [rotl(x, n, w) for n in ns]
        for n in range(w):
            out += [rotr(x, n, w), rotl(x, n, w)]


program("rot-values", rot_values)

ROT_TRAP = {  # selector: (op, w, n)
    "A": (rotr, 8, 8), "B": (rotl, 16, 16), "C": (rotr, 32, 32), "D": (rotl, 64, 64),
    "E": (rotl, 8, 8), "F": (rotr, 16, 16), "G": (rotl, 32, 32), "H": (rotr, 64, 64),
    "I": (rotr, 8, 9), "J": (rotl, 16, 65535), "K": (rotr, 32, 4294967295),
    "L": (rotl, 64, M64), "M": (rotr, 32, 33), "N": (rotl, 64, 65), "O": (rotl, 8, 255),
    "P": (rotr, 32, 31),
}
for sel, (op, w, n) in ROT_TRAP.items():
    program("rot-trap-" + sel, lambda out, op=op, w=w, n=n: out.append(op(XS[w][0], n, w)))


def unroll_sum(out):
    h = 7
    for i in range(1):
        h = (h * 1000003 + (i + 11)) & M64
    out.append(h)
    s = 0
    for i in range(4):
        s = (s * 131 + (i + 3)) & M64
    out.append(s)
    a = [0] * 65
    t = c = 0
    for i in range(64):
        t = (t * 6364136223846793005 + ((i + 1442695040888963407) & M64)) & M64
        c += 1
        a[i] = (i * 37 + 5) & 255
    out += [t, c]
    q = 0
    for k in range(65):
        q = (q * 257 + a[k]) & M64
    out.append(q)


program("unroll-sum", unroll_sum)


def unroll_nest(out):
    m = [0] * 64
    f = 14695981039346656037
    for i in range(8):
        for j in range(8):
            f = ((f ^ (i * 8 + j)) * 1099511628211) & M64
            m[i * 8 + j] = i * 1000 + j
    out.append(f)
    g = 0
    for k in range(64):
        g = (g * 3 + m[k]) & M64
    out.append(g)


program("unroll-nest", unroll_nest)


def unroll_nest_64x2(out):  # erratum E7: 64 x 2 nested unroll, 128 copies
    f, c = 1, 0
    for i in range(64):
        for j in range(2):
            f = (f * 31 + i * 2 + j) & M64
            c += 1
    out += [f, c]


program("unroll-nest-64x2", unroll_nest_64x2)


def nested_exit(stop):  # erratum E7: break/continue of a rolled loop inside an unrolled body
    def body(out):
        s = 0
        for i in range(4):
            for j in range(3):
                if j == 1:
                    if stop:
                        break
                    continue
                s += 1
        return s
    return body


program("unroll-break-nested", nested_exit(True))
program("unroll-continue-nested", nested_exit(False))


def write_i_rolled(out):  # for i ulong 4: body i = add i 1; pu i; then the for increments i
    i = 0
    while i < 4:
        i += 1
        out.append(i)
        i += 1


program("unroll-write-i-rolled", write_i_rolled)


def brk(out):
    s = 0
    for i in range(4):
        if i == 2:
            break
        s += 1
    return s


def cont(out):
    s = 0
    for i in range(4):
        if i == 2:
            continue
        s += 1
    return s


program("unroll-break-rolled", brk)
program("unroll-continue-rolled", cont)


def grow(a):
    x, s, t, u = a, a + 1, a + 2, a + 3
    for i in range(64):
        for j in range(64):
            x = (x * 3 + (s + (i + j))) & M64
            s ^= x >> 7
            t = (t + s * x) & M64
            u ^= (t << 3) & M64
            x = (x + u * 5) & M64
            s = (s + (x ^ t)) & M64
            t ^= s >> 11
            u = (u + t * 9) & M64
            x ^= (u + s) & M64
            s = (s * (x + 1)) & M64
            t = (t + (u ^ 13)) & M64
            u ^= (s + t) & M64
    return x ^ s ^ (t ^ u)


program("unroll-growth-rolled", lambda out: grow(5) & 255)

I32 = 32


def mk(out, v, t):
    out.append(t)
    return v


def f_inl(a, b):
    x = sadd(a, 1, I32)
    return sdiv(x, b, I32)


def g_inl(a):
    return 0 if a < 0 else smul(a, 2, I32)


INLINE = {
    "A": lambda out: out.append(f_inl(mk(out, 10, 1), mk(out, 3, 2))),
    "B": lambda out: out.append(f_inl(sdiv(1, 0, I32), sadd(2147483647, 1, I32))),
    "C": lambda out: out.append(f_inl(sadd(2147483647, 1, I32), sdiv(1, 0, I32))),
    "D": lambda out: out.append(f_inl(2147483647, 0)),
    "E": lambda out: out.append(f_inl(5, 0)),
    "F": lambda out: out.append(f_inl(mk(out, 1, 1), sdiv(1, 0, I32))),
    "G": lambda out: out.append(f_inl(sdiv(1, 0, I32), mk(out, 1, 2))),
    "H": lambda out: out.extend([g_inl(-5), g_inl(7)]),
    "I": lambda out: out.append(g_inl(mk(out, 7, 3))),
}
for sel, fn in INLINE.items():
    program("inline-order-" + sel, fn)


def chain(n, a):
    p = [3, 5, 7, 11, 13, 17]

    def f(k, a):
        if k == n:
            return (a * p[k - 1] + k) & M64
        return (f(k + 1, (a * p[k - 1]) & M64) + k) & M64

    return f(1, a)


program("inline-depth4", lambda out: out.extend([chain(4, 2), chain(4, 1000)]))
program("inline-depth6", lambda out: out.extend([chain(6, 2), chain(6, 1000)]))
program("inline-new", lambda out: out.append(3))


def tri(n):
    return 0 if n == 0 else n + tri(n - 1)


program("inline-rec", lambda out: out.append(tri(10)))


def fold_guard(out):
    out.append(sum(udiv(100, i) for i in range(4) if i != 0))
    out.append(sum(urem(101, i) for i in range(4) if i != 0))
    out.append(sum(sdiv(1000000, i, 64) for i in range(4) if i != 0))
    q = lambda a, b: 0 if b == 0 else udiv(a, b)
    out += [q(100, 0), q(100, 7)]


program("fold-guard", fold_guard)


def ft_a(out):
    out.append(999)
    for i in range(4):
        out.append(udiv(100, i))


def ft_b(out):
    for i in range(4):
        out.append(sadd(2147483646, i, I32))


def ft_c(out):
    for i in range(4):
        out.append(conv(254 + i, 0, 255))


def ft_d(out):
    for i in range(4):
        out.append(shl64(1, 62 + i))


def ft_e(out):
    for i in range(4):
        out.append(smul(1073741824, sadd(i, 1, I32), I32))


def ft_f(out):
    for i in range(4):
        out.append(sadd(5, sdiv(-2147483648, sadd(i, -1, I32), I32), I32))


def ft_g(out):
    out.append(999)
    for i in range(4):
        out.append(urem(7, i))


def ft_h(out):
    out.append(conv(255, 0, 255))
    out.append(conv(256, 0, 255))


def ft_i(out):
    out.append(udiv(100, 4))
    out.append(udiv(100, 0))


for sel, fn in zip("ABCDEFGHI", [ft_a, ft_b, ft_c, ft_d, ft_e, ft_f, ft_g, ft_h, ft_i]):
    program("fold-trap-" + sel, fn)


def ce_sized(out):
    a = [i * 10 + 1 for i in range(4)]
    s = 0
    for i in range(4):
        s = s * 100 + a[i]
    t = 0
    for i in range(4):
        t = t * 100 + a[3 - i]
    out += [s, t]


program("ce-sized", ce_sized)


def vget(v, i):  # unsized view read: index checked at run time, trap 3
    if not 0 <= i < len(v):
        raise Trap(3)
    return v[i]


B = [10 ** k for k in range(8)]


def sumv(out, v):
    s = 0
    for i in range(5):
        s = (s + vget(v, i)) & M64
        out.append(s)
    return s


CE_VIEW = {
    "A": lambda out: out.append(sumv(out, B[0:4])),
    "B": lambda out: out.append(sumv(out, B[0:4])),
    "C": lambda out: out.append(sumv(out, B[0:4])),
    "D": lambda out: out.append(vget(B[0:4], 6)),
    "E": lambda out: out.append(sumv(out, B[0:5])),
    "F": lambda out: out.append(sumv(out, B[3:7])),
}
for sel, fn in CE_VIEW.items():
    program("ce-view-" + sel, fn)

# ---- Mutant predictions (kill specs, README section M). Each model is the fixture run by a compiler
# with one deliberate fault; exp/mut/<name>.out is its predicted stdout and exp/mutants.txt lists
# "<mutant> <exit> <first differing stdout byte, 0-based, vs the correct expectation>".
os.makedirs(os.path.join(EXP, "mut"), exist_ok=True)
MUT = []


def mutant(name, base, body):
    program("mut/" + name, body)
    with open(os.path.join(EXP, "mut", name + ".out"), "rb") as f:
        m = f.read()
    with open(os.path.join(EXP, base + ".out"), "rb") as f:
        o = f.read()
    d = next((i for i in range(min(len(m), len(o))) if m[i] != o[i]), None)
    if d is None:
        d = min(len(m), len(o)) if len(m) != len(o) else -1
    MUT.append("%s %d %d" % (name, EXITS.pop("mut/" + name), d))


def unroll_sum_k(dk):  # (a) every unrolled loop emits K+dk copies
    def body(out):
        h = 7
        for i in range(1 + dk):
            h = (h * 1000003 + (i + 11)) & M64
        out.append(h)
        s = 0
        for i in range(4 + dk):
            s = (s * 131 + ((i + 3) & M64)) & M64
        out.append(s)
        a = [0] * 65
        t = c = 0
        for i in range(64 + dk):
            t = (t * 6364136223846793005 + ((i + 1442695040888963407) & M64)) & M64
            c += 1
            a[i] = (i * 37 + 5) & 255
        out += [t, c]
        q = 0
        for k in range(65):
            q = (q * 257 + a[k]) & M64
        out.append(q)
    return body


mutant("unroll-sum-Kminus1", "unroll-sum", unroll_sum_k(-1))
mutant("unroll-sum-Kplus1", "unroll-sum", unroll_sum_k(1))


def rot_mut(kind, paths):  # (b) faulty rotate lowering on the given paths ("const", "runtime")
    def bad_r(x, n, w):
        if kind == "nplus1":
            return rotr(x, (n + 1) % w, w)
        if kind == "wminusn":
            return rotr(x, (w - n) % w, w)
        return rotr(x, n, w)

    def bad_l(x, n, w):
        if kind == "nplus1":
            return rotl(x, (n + 1) % w, w)
        if kind == "rorforrol":
            return rotr(x, n, w)
        return rotl(x, n, w)

    def body(out):
        for w, (x, mid) in XS.items():
            ns = [0, 1, mid, w - 1]
            for path in ("const", "runtime"):
                r = bad_r if path in paths else rotr
                l = bad_l if path in paths else rotl
                out += [r(x, n, w) for n in ns]
                out += [l(x, n, w) for n in ns]
            r = bad_r if "runtime" in paths else rotr
            l = bad_l if "runtime" in paths else rotl
            for n in range(w):
                out += [r(x, n, w), l(x, n, w)]
    return body


for kind in ("nplus1", "wminusn", "rorforrol"):
    for tag, paths in (("both", ("const", "runtime")), ("const", ("const",)), ("runtime", ("runtime",))):
        mutant("rot-values-%s-%s" % (kind, tag), "rot-values", rot_mut(kind, paths))

# rotation range check dropped: RORV and the immediate use n mod w (no trap 7)
for sel, (op, w, n) in ROT_TRAP.items():
    if sel != "P":
        mutant("rot-trap-%s-nocheck" % sel, "rot-trap-" + sel,
               lambda out, op=op, w=w, n=n: out.append(op(XS[w][0], n % w, w)))


def sumv_nc(out, off):
    s = 0
    for i in range(5):
        s = (s + B[off + i]) & M64
        out.append(s)
    return s


mutant("ce-view-A-nocheck", "ce-view-A", lambda out: out.append(sumv_nc(out, 0)))
mutant("ce-view-C-nocheck", "ce-view-C", lambda out: out.append(sumv_nc(out, 0)))
mutant("ce-view-D-nocheck", "ce-view-D", lambda out: out.append(B[6]))
mutant("ce-view-F-nocheck", "ce-view-F", lambda out: out.append(sumv_nc(out, 3)))


# (d) a trapping all-constant operation folded with AArch64 results instead of the trap:
# UDIV/SDIV by 0 give 0 (so rem gives the dividend), signed results wrap, a conversion truncates,
# a shift amount is taken mod 64. ps prints 7777 for a negative int.
def wrap32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >> 31 else v


def ps(out, v):
    out.append(7777 if v < 0 else v)


FOLD_MUT = {
    "A": lambda out: (out.append(999), [out.append(100 // i if i else 0) for i in range(4)]),
    "B": lambda out: [ps(out, wrap32(2147483646 + i)) for i in range(4)],
    "C": lambda out: [out.append((254 + i) & 255) for i in range(4)],
    "D": lambda out: [out.append((1 << ((62 + i) % 64)) & M64) for i in range(4)],
    "E": lambda out: [ps(out, wrap32(1073741824 * (i + 1))) for i in range(4)],
    "F": lambda out: ft_f_mut(out),
    "G": lambda out: (out.append(999), [out.append(7 % i if i else 7) for i in range(4)]),
    "H": lambda out: (out.append(255), out.append(256 & 255)),
    "I": lambda out: (out.append(25), out.append(0)),
}


def ft_f_mut(out):  # i = 0: min / -1 wraps to min; i = 1: div by 0 folded to 0
    for i in range(4):
        d = i - 1
        if d == 0:
            q = 0
        elif d == -1:
            q = wrap32(2147483648)
        else:
            q = sdiv(-2147483648, d, I32)
        ps(out, wrap32(5 + q))


for sel, fn in FOLD_MUT.items():
    mutant("fold-trap-%s-fold" % sel, "fold-trap-" + sel, lambda out, fn=fn: (fn(out), None)[1])

with open(os.path.join(EXP, "exits.txt"), "w") as f:
    for k in sorted(EXITS):
        f.write("%s %d\n" % (k, EXITS[k]))
with open(os.path.join(EXP, "mutants.txt"), "w") as f:
    f.write("".join(m + "\n" for m in MUT))
print("oracle: %d programs, %d mutant models written to %s" % (len(EXITS), len(MUT), EXP))
