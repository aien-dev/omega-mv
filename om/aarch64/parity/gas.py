#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""gas.py CORPDIR OUTDIR WORKDIR: cross-check words that aarch64.om wrote against GNU as.

For each encoder stream eNN_*, up to 200 records whose .om output is a word (evenly spaced over the
stream) are written back as assembly text from their fields, assembled with `as`, and the .text words
must equal the .om words. A 31 register is written as xzr/wzr where the form reads ZR and sp where it
reads SP. Exit 0 and one summary line when every word is identical, 1 otherwise. Needs `as` and
`objcopy` for AArch64 on PATH.
"""
import os
import subprocess
import sys

CC = "eq ne cs cc mi pl vs vc hi ls ge lt gt le al nv".split()


def sgn(v):
    return v - (1 << 64) if v >> 63 else v


def zr(r, sf):
    if r == 31:
        return "xzr" if sf else "wzr"
    return ("x%d" if sf else "w%d") % r


def sp(r, sf=1):
    if r == 31:
        return "sp" if sf else "wsp"
    return ("x%d" if sf else "w%d") % r


def rel(n):
    return ". + %d" % n if n >= 0 else ". - %d" % -n


def text(form, f):
    a, b, c, d = f
    if 1 <= form <= 6 or form == 25:
        op = ["", "add", "sub", "mul", "and", "orr", "eor"][form] if form != 25 else "subs"
        return "%s %s, %s, %s" % (op, zr(b, a), zr(c, a), zr(d, a))
    if form == 7:
        return "orr %s, %s, %s" % (zr(b, a), zr(31, a), zr(c, a))
    if form in (8, 14):
        return "%s %s, #%d, lsl #%d" % ("movz" if form == 8 else "movk", zr(b, a), c, d)
    if form == 9:
        return "ret"
    if form == 10:
        return "b %s" % rel(4 * sgn(a))
    if form == 11:
        return "b.%s %s" % (CC[a], rel(4 * sgn(b)))
    if form in (12, 13):
        return "%s %s, %s" % ("cbz" if form == 12 else "cbnz", zr(b, a), rel(4 * sgn(c)))
    if form == 15:
        return "adr %s, %s" % (zr(a, 1), rel(sgn(b)))
    if form in (16, 17):
        return "%s %s, [%s, #%d]" % ("ldr" if form == 16 else "str", zr(b, a), sp(c), d)
    if form in (18, 19):
        return "%s %s, [%s, #%d]" % ("ldrb" if form == 18 else "strb", zr(a, 0), sp(b), c)
    if 20 <= form <= 23:
        op = "ldr" if form in (20, 22) else "str"
        return "%s %s, [%s], #%d" % (op, zr(a, form >= 22), sp(b), sgn(c))
    if form == 24:
        return "subs %s, %s, #%d" % (zr(b, a), sp(c, a), d)
    raise AssertionError(form)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    corp, outd, work = sys.argv[1:]
    lines, want = [], []
    forms = set()
    for name in sorted(os.listdir(corp)):
        if not (name.startswith("e") and name.endswith(".txt")):
            continue
        s = name[:-4]
        recs = open(os.path.join(corp, name)).read().split("\n")
        outs = open(os.path.join(outd, s + ".out")).read().split("\n")
        ok = [i for i, o in enumerate(outs) if o.startswith("E ")]
        step = max(1, len(ok) // 200)
        for i in ok[::step][:200]:
            w = recs[i].split()
            form, f = int(w[1], 16), [int(x, 16) for x in w[2:6]]
            lines.append(text(form, f))
            want.append(int(outs[i].split()[1], 16))
            forms.add(form)
    src = os.path.join(work, "gas.s")
    obj = os.path.join(work, "gas.o")
    binf = os.path.join(work, "gas.bin")
    open(src, "w").write("\n".join(lines) + "\n")
    subprocess.run(["as", src, "-o", obj], check=True, stderr=subprocess.DEVNULL)
    subprocess.run(["objcopy", "-O", "binary", "--only-section=.text", obj, binf], check=True)
    data = open(binf, "rb").read()
    got = [int.from_bytes(data[4 * i:4 * i + 4], "little") for i in range(len(data) // 4)]
    bad = [i for i in range(len(want)) if i >= len(got) or got[i] != want[i]]
    if len(got) != len(want) or bad:
        for i in bad[:20]:
            print("gas MISMATCH %s: as %s, om %08x" % (lines[i], "%08x" % got[i] if i < len(got) else "-", want[i]))
        print("gas FAIL: %d of %d differ" % (len(bad), len(want)))
        sys.exit(1)
    print("gas: %d words over %d forms identical to GNU as" % (len(want), len(forms)))


if __name__ == "__main__":
    main()
