#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# seed/qualification/om2/oracle/leak/fuzz.py: decoder differential for leakscan.om (ORACLE, outside the
# delivered closure). It builds a word file from the 32-bit instruction constants written in omc.om
# (every number 2^24 .. 2^32-1 outside note lines that GNU objdump decodes as an instruction), each
# with random bits flipped in its register/immediate fields (or, for 1 in 6, in all 32 bits), drops
# the words that make objdump 2.42 abort (an assertion in aarch64-dis.c on some SVE encodings), and
# writes the result. xcheck.py --subset then compares leakscan's listing with objdump's.
# Usage: fuzz.py OMC_OM SEED COUNT OUT.bin
import random
import re
import struct
import subprocess
import sys
import tempfile


def objdump_ok(words, tmp):
    with open(tmp, "wb") as f:
        f.write(b"".join(struct.pack("<I", w) for w in words))
    return subprocess.run(["objdump", "-D", "-b", "binary", "-m", "aarch64", tmp],
                          capture_output=True).returncode == 0


def clean(words, tmp):
    if objdump_ok(words, tmp):
        return words
    if len(words) == 1:
        return []
    h = len(words) // 2
    return clean(words[:h], tmp) + clean(words[h:], tmp)


def bases(omc_om, tmp):
    nums = set()
    for line in open(omc_om):
        if line.startswith("note"):
            continue
        for m in re.finditer(r"\b(\d{8,10})\b", line):
            v = int(m.group(1))
            if 1 << 24 <= v < 1 << 32:
                nums.add(v)
    nums = sorted(nums)
    with open(tmp, "wb") as f:
        f.write(b"".join(struct.pack("<I", w) for w in nums))
    out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "aarch64", tmp],
                         check=True, capture_output=True, text=True).stdout
    keep = []
    for line in out.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\t([0-9a-f]{8}) \t(\S+)", line)
        if m and m.group(2) != ".inst" and "undefined" not in line:
            keep.append(int(m.group(1), 16))
    return keep


def main():
    if len(sys.argv) != 5:
        print("usage: fuzz.py OMC_OM SEED COUNT OUT.bin", file=sys.stderr)
        return 90
    random.seed(int(sys.argv[2]))
    with tempfile.TemporaryDirectory() as d:
        tmp = d + "/t.bin"
        base = bases(sys.argv[1], tmp)
        masks = [0x00ffffff, 0x03ffffff, 0x40ffffff, 0x0000ffff, 0x001fffff, 0xffffffff]
        words = [random.choice(base) ^ (random.getrandbits(32) & random.choice(masks))
                 for _ in range(int(sys.argv[3]))]
        out = []
        for i in range(0, len(words), 2000):
            out += clean(words[i:i + 2000], tmp)
    if out and out[0] == 0x464C457F:
        out[0] = 0
    with open(sys.argv[4], "wb") as f:
        f.write(b"".join(struct.pack("<I", w) for w in out))
    print(f"FUZZ bases={len(base)} words={len(out)} seed={sys.argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
