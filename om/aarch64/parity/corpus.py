#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""corpus.py DIR: write the deterministic parity corpus of om/aarch64/aarch64.om into DIR, one file per
stream (STREAM.txt), and print "STREAM RECORDS" per stream on stdout.

Record formats (all numbers lowercase hex; a signed field is its 64-bit two's complement):
  E form f1 f2 f3 f4       one aarch64_emit_<form> call; form 1..25 in the order of aarch64_encoder.h,
                           fields in the C parameter order after max_len, unused fields 0
  P maxlen pos n {form f1 f2 f3 f4}*n   a sequence of emitter calls into one 4096-byte buffer
  D word                   aarch64_decode_instruction
  V len ml bytes           aarch64_validate_code_buffer(bytes, len, msg, ml); bytes one hex token, "-" empty

Every field value is inside its C parameter type (bool, uint8_t, uint16_t, int16_t, int32_t), so the C
receives it unchanged; values outside the architectural ranges inside those types are swept on purpose
(the C refuses them). Generators are splitmix64 with fixed seeds; nothing here reads the C or the .om.
"""
import os
import sys

M64 = (1 << 64) - 1

# form: (C name, field kinds); kinds: b bool, r uint8_t register, u8 uint8_t, u16 uint16_t,
# s16 int16_t, s32 int32_t
FORMS = [
    None,
    ("add_reg", "b r r r"), ("sub_reg", "b r r r"), ("mul_reg", "b r r r"), ("and_reg", "b r r r"),
    ("orr_reg", "b r r r"), ("eor_reg", "b r r r"), ("mov_reg", "b r r"), ("movz", "b r u16 u8"),
    ("ret", ""), ("b", "s32"), ("b_cond", "s32 s32"), ("cbz", "b r s32"), ("cbnz", "b r s32"),
    ("movk", "b r u16 u8"), ("adr", "r s32"), ("ldr_uoff", "b r r u16"), ("str_uoff", "b r r u16"),
    ("ldrb_uoff", "r r u16"), ("strb_uoff", "r r u16"), ("ldr_post", "r r s16"), ("str_post", "r r s16"),
    ("ldr_x_post", "r r s16"), ("str_x_post", "r r s16"), ("subs_imm", "b r r u16"), ("subs_reg", "b r r r"),
]


class Rng:
    def __init__(self, seed):
        self.s = seed & M64

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def range(self, lo, hi):
        """uniform in [lo, hi]"""
        return lo + self.below(hi - lo + 1)


TYPE = {"b": (0, 1), "r": (0, 255), "u8": (0, 255), "u16": (0, 65535), "s16": (-32768, 32767),
        "s32": (-(1 << 31), (1 << 31) - 1)}


def h(v):
    return "%x" % (v & M64)


def rec_e(form, fields):
    kinds = FORMS[form][1].split()
    assert len(fields) == len(kinds), (form, fields)
    for k, v in zip(kinds, fields):
        lo, hi = TYPE[k]
        assert lo <= v <= hi, (form, fields)
    f = list(fields) + [0] * (4 - len(fields))
    return "E %x %s" % (form, " ".join(h(v) for v in f))


# in-range field values per form, for the P stream and the fillers of the sweeps
def valid_fields(rng, form):
    name, kinds = FORMS[form]
    sf = rng.below(2)
    r = lambda: rng.below(32)
    if kinds == "b r r r":
        return [sf, r(), r(), r()]
    if form == 7:
        return [sf, r(), r()]
    if form in (8, 14):
        return [sf, r(), rng.below(65536), 16 * rng.below(4 if sf else 2)]
    if form == 9:
        return []
    if form == 10:
        return [rng.range(-(1 << 25), (1 << 25) - 1)]
    if form == 11:
        return [rng.below(16), rng.range(-(1 << 18), (1 << 18) - 1)]
    if form in (12, 13):
        return [sf, r(), rng.range(-(1 << 18), (1 << 18) - 1)]
    if form == 15:
        return [r(), rng.range(-(1 << 20), (1 << 20) - 1)]
    if form in (16, 17):
        return [sf, r(), r(), rng.below(4096) * (8 if sf else 4)]
    if form in (18, 19):
        return [r(), r(), rng.below(4096)]
    if form in (20, 21, 22, 23):
        return [r(), r(), rng.range(-256, 255)]
    if form == 24:
        return [sf, r(), r(), rng.below(4096)]
    raise AssertionError(form)


def full_random(rng, form):
    """every field uniform over its whole C type"""
    out = []
    for k in FORMS[form][1].split():
        lo, hi = TYPE[k]
        out.append(rng.range(lo, hi))
    return out


def edge_set(bits):
    """signed boundaries of a bits-wide immediate and of int32"""
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    s = set()
    for c in (lo, hi, 0, -1, 1):
        for d in range(-3, 4):
            s.add(c + d)
    s.update([-(1 << 31), (1 << 31) - 1, -(1 << 31) + 1, (1 << 31) - 2, 1 << bits, -(1 << bits)])
    return sorted(v for v in s if -(1 << 31) <= v < (1 << 31))


def gen_rrr(form, rng):
    out = []
    for sf in range(2):
        for rd in range(32):
            for rn in range(32):
                for rm in range(32):
                    out.append([sf, rd, rn, rm])
    for pos in (1, 2, 3):
        for v in range(32, 256):
            f = valid_fields(rng, form)
            f[pos] = v
            out.append(f)
    for _ in range(200):
        out.append(full_random(rng, form))
    return out


def gen_movreg(rng):
    out = [[sf, rd, rm] for sf in range(2) for rd in range(32) for rm in range(32)]
    for pos in (1, 2):
        for v in range(32, 256):
            f = valid_fields(rng, 7)
            f[pos] = v
            out.append(f)
    for _ in range(200):
        out.append(full_random(rng, 7))
    return out


def gen_wide(form, rng):
    out = []
    for sf in range(2):
        for rd in range(256):
            for sh in range(256):
                out.append([sf, rd, rng.below(65536), sh])
    for imm in range(65536):
        out.append([1, rng.below(32), imm, 16 * (imm % 4)])
    for _ in range(10000):
        out.append(full_random(rng, form))
    return out


def gen_imm_only(form, bits, rng):
    """b: every edge, three exhaustive windows, 10,000 in range, 10,000 over int32"""
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    vals = set(edge_set(bits))
    for c in (lo, 0, hi):
        vals.update(range(c - 4096, c + 4097))
    out = [[v] for v in sorted(vals)]
    for _ in range(10000):
        out.append([rng.range(lo, hi)])
    for _ in range(10000):
        out.append(full_random(rng, form))
    return out


def gen_bcond(rng):
    conds = list(range(-3, 20)) + [255, 256, -(1 << 31), (1 << 31) - 1]
    out = [[c, v] for c in conds for v in edge_set(19)]
    for c in range(16):
        for v in range(-512, 513):
            out.append([c, v])
    for _ in range(10000):
        out.append(valid_fields(rng, 11))
    for _ in range(10000):
        out.append([rng.range(-2, 17), rng.range(-(1 << 31), (1 << 31) - 1)])
    return out


def gen_cb(form, rng):
    out = [[sf, rt, v] for sf in range(2) for rt in range(256) for v in edge_set(19)]
    for _ in range(10000):
        out.append(valid_fields(rng, form))
    for _ in range(10000):
        out.append(full_random(rng, form))
    return out


def gen_adr(rng):
    out = [[rd, v] for rd in range(256) for v in edge_set(21)]
    for v in range(-4096, 4097):
        out.append([rng.below(32), v])
    for _ in range(10000):
        out.append(valid_fields(rng, 15))
    for _ in range(10000):
        out.append(full_random(rng, 15))
    return out


def gen_ls(form, rng):
    out = [[sf, rng.below(32), rng.below(32), u] for sf in range(2) for u in range(65536)]
    for rt in range(256):
        for rn in range(256):
            sf = rng.below(2)
            out.append([sf, rt, rn, rng.below(4096) * (8 if sf else 4)])
    return out


def gen_lsb(form, rng):
    out = [[rng.below(32), rng.below(32), u] for u in range(65536)]
    for rt in range(256):
        for rn in range(256):
            u = rng.below(4096) if rng.below(4) else rng.below(65536)
            out.append([rt, rn, u])
    return out


def gen_post(form, rng):
    out = [[rng.below(32), rng.below(32), v] for v in range(-32768, 32768)]
    for rt in range(256):
        for rn in range(256):
            v = rng.range(-256, 255) if rng.below(4) else rng.range(-32768, 32767)
            out.append([rt, rn, v])
    return out


def gen_subsimm(rng):
    out = [[sf, rng.below(32), rng.below(32), u] for sf in range(2) for u in range(65536)]
    for rd in range(256):
        for rn in range(256):
            out.append([rng.below(2), rd, rn, rng.below(4096)])
    return out


def enc_stream(form):
    rng = Rng(0xA64E0000 + form)
    if FORMS[form][1] == "b r r r":
        rows = gen_rrr(form, rng)
    elif form == 7:
        rows = gen_movreg(rng)
    elif form in (8, 14):
        rows = gen_wide(form, rng)
    elif form == 9:
        rows = [[]]
    elif form == 10:
        rows = gen_imm_only(form, 26, rng)
    elif form == 11:
        rows = gen_bcond(rng)
    elif form in (12, 13):
        rows = gen_cb(form, rng)
    elif form == 15:
        rows = gen_adr(rng)
    elif form in (16, 17):
        rows = gen_ls(form, rng)
    elif form in (18, 19):
        rows = gen_lsb(form, rng)
    elif form in (20, 21, 22, 23):
        rows = gen_post(form, rng)
    else:
        rows = gen_subsimm(rng)
    if form != 9:
        # 10,000 more calls with every field in range (words for the decode and round-trip checks)
        rows += [valid_fields(rng, form) for _ in range(10000)]
    return [rec_e(form, r) for r in rows]


def buf_stream():
    rng = Rng(0xA64B0F)
    out = []

    def calls(n):
        parts = []
        for _ in range(n):
            form = rng.range(1, 25)
            f = valid_fields(rng, form)
            parts.append(rec_e(form, f)[2:])
        return parts

    for maxlen in range(0, 41):
        for pos in range(0, maxlen + 9):
            n = rng.range(1, 12)
            out.append("P %x %x %x %s" % (maxlen, pos, n, " ".join(calls(n))))
    for maxlen, pos, n in ((4096, 4092, 1), (4096, 4088, 2), (4096, 4093, 1), (4096, 4096, 1), (4095, 4092, 1),
                           (4096, 0, 1024), (4096, 2048, 512), (M64, 0, 64), (M64, 4092, 1), (1 << 63, 4000, 24),
                           (4097, 4092, 1), (5000, 4090, 1)):
        out.append("P %x %x %x %s" % (maxlen, pos, n, " ".join(calls(n))))
    for _ in range(2000):
        n = rng.range(1, 40)
        pos = rng.range(0, 4096 - 4 * n)
        maxlen = rng.range(0, 4096)
        out.append("P %x %x %x %s" % (maxlen, pos, n, " ".join(calls(n))))
    return out


# the decoder's checks, only to place corpus words in or near its classes (not an oracle)
PATTERNS = [(0xFC000000, 0x14000000), (0xFF000010, 0x54000000), (0x7F000000, 0x34000000),
            (0x7F000000, 0x35000000), (0x7F800000, 0x52800000), (0x7FE07C00, 0x1B007C00),
            (0x7F200000, 0x0A000000), (0x7F200000, 0x2A000000), (0x7F200000, 0x4A000000),
            (0x7F200000, 0x0B000000), (0x7F200000, 0x4B000000), (0x7F800000, 0x72800000),
            (0x9F000000, 0x10000000), (0xBF400000, 0xB9400000), (0xBF400000, 0xB9000000),
            (0xBFE00400, 0xB8400400), (0xBFE00400, 0xB8000400), (0xFFC00000, 0x39400000),
            (0xFFC00000, 0x39000000), (0x7F800000, 0x71000000), (0x7F200000, 0x6B000000)]
RET = 0xD65F03C0


def classed(w):
    return w == RET or any((w & m) == p for m, p in PATTERNS)


def dec_stream():
    rng = Rng(0xA64D0C)
    words = []
    for _ in range(200000):
        words.append(rng.next() & 0xFFFFFFFF)
    # every value of the bits the decoder tests (mask 0xFFE07C10, 17 bits), twice, other bits random
    disc = [b for b in range(32) if (0xFFE07C10 >> b) & 1]
    assert len(disc) == 17
    for v in range(1 << 17):
        base = 0
        for i, b in enumerate(disc):
            if (v >> i) & 1:
                base |= 1 << b
        for _ in range(2):
            words.append(base | (rng.next() & ~0xFFE07C10 & 0xFFFFFFFF))
    words.append(RET)
    for b in range(32):
        words.append(RET ^ (1 << b))
    for m, p in PATTERNS:
        words.append(p)
        words.append(p | (~m & 0xFFFFFFFF))
        for b in range(32):
            words.append(p ^ (1 << b))
    words += [0, 0xFFFFFFFF]
    return ["D %x" % w for w in words]


def val_stream():
    rng = Rng(0xA64FA1)
    out = []

    def good():
        while True:
            m, p = PATTERNS[rng.below(len(PATTERNS))]
            w = p | (rng.next() & ~m & 0xFFFFFFFF)
            if classed(w):
                return w

    def bad():
        while True:
            w = rng.next() & 0xFFFFFFFF
            if not classed(w):
                return w

    def code(ws):
        return "".join("%02x" % ((w >> (8 * i)) & 255) for w in ws for i in range(4)) or "-"

    def rec(n, ml, ws):
        out.append("V %x %x %s" % (n, ml, code(ws)))

    # ml is at most 256, the msg array of a64validate (above it the port traps 3; with ml = SIZE_MAX the
    # glibc snprintf of the C drops the last byte, an address-dependent wrap, so it is no contract)
    mls = list(range(0, 101)) + [128, 255, 256]
    for ml in mls:
        rec(0, ml, [])
        rec(6, ml, [RET, RET])
        rec(0xFFFFFFFFFFFFFFFF, ml, [])
        rec(8, ml, [RET, 0])
        rec(8, ml, [0xF2FFFFE1 | 0, RET])
        rec(8, ml, [good(), 0x8B030041])
    for n in (1, 2, 3, 5, 7, 4095, 4097, 1 << 63, (1 << 63) + 2, M64 - 2, 12345678901234567):
        rec(n, 256, [])
    # every op as the terminal word (the "terminates with op %d" message), and RET
    for m, p in PATTERNS:
        rec(4, 256, [p])
        rec(12, 256, [RET, good(), p | (~m & 0xFFFFFFFF)])
    rec(4, 256, [RET])
    # huge lengths stopped by a bad first word (the C reads only that word)
    for n in (1 << 62, M64 - 3, 4100):
        rec(n, 256, [bad()])
    for _ in range(1500):
        k = rng.range(1, 1024 if rng.below(8) == 0 else 64)
        ws = [good() for _ in range(k - 1)] + [RET]
        kind = rng.below(4)
        if kind == 1:
            ws[rng.below(k)] = bad()
        elif kind == 2:
            ws[-1] = good()
        elif kind == 3 and k > 1:
            ws[rng.below(k - 1)] = RET
        n = 4 * k
        if rng.below(10) == 0:
            n -= rng.range(1, 3)
        ml = rng.below(101) if rng.below(3) == 0 else 256
        rec(n, ml, ws)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    streams = [("e%02d_%s" % (f, FORMS[f][0]), lambda f=f: enc_stream(f)) for f in range(1, 26)]
    streams += [("buf", buf_stream), ("dec", dec_stream), ("val", val_stream)]
    for name, fn in streams:
        rows = fn()
        with open(os.path.join(d, name + ".txt"), "w") as fh:
            fh.write("\n".join(rows) + "\n")
        print(name, len(rows))


if __name__ == "__main__":
    main()
