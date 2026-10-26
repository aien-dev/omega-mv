#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# ORACLE-side harness transform (outside the delivered closure), L836 phaseB/sha-f5d14cb2.
# leakscan seeds its analysis only at word 0 (or the direct B there). The S4 multi-export artifact
# starts with E entry words `b <export k>` (NATIVE-ABI.md "Multi-entry code"; the binding enters
# export k at byte 4k). For export k this writes a copy whose word 0 is the B that word k encodes,
# re-based to byte 0 (imm26 + k), so the scanner starts exactly where the binding enters export k.
# Every other byte is unchanged. Usage: entry.py ARTIFACT K OUT
import struct
import sys

art = bytearray(open(sys.argv[1], "rb").read())
k = int(sys.argv[2])
w = struct.unpack_from("<I", art, 4 * k)[0]
if w & 0xFC000000 != 0x14000000:
    sys.exit(f"word {k} is not B: {w:#010x}")
imm = w & 0x03FFFFFF
imm = imm - (1 << 26) if imm & (1 << 25) else imm
tgt = 4 * k + 4 * imm
new = 0x14000000 | ((tgt // 4) & 0x03FFFFFF)
struct.pack_into("<I", art, 0, new)
open(sys.argv[3], "wb").write(art)
print(f"export {k}: word {k} = {w:#010x} -> target byte {tgt:#x}; word 0 = {new:#010x}")
