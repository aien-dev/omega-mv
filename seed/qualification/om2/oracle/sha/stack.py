#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# ORACLE (outside the delivered closure), OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2.
# Static code-size, rotate-instruction and stack-bound report for one omc function-mode artifact,
# from GNU objdump 2.42 text (-D -b binary -m aarch64) plus the raw words.
# Stack method: functions are the export entry targets (the leading B words) and every BL target.
# Each function's CFG is walked from its entry (fallthrough, B, both edges of B.cond/CBZ/CBNZ/TBZ/
# TBNZ; RET and BR end a path) while tracking the SP depth below the function's entry SP from
# `sub sp, sp, #imm` / `add sp, sp, #imm` (the only SP writes the artifact contains; anything else
# aborts). Depths must agree at joins and be 0 at RET. bound(f) = max over reached instructions of
# depth, and of depth + bound(callee) at each BL (the call graph must be acyclic).
# Usage: stack.py ARTIFACT
import re
import struct
import subprocess
import sys

art = open(sys.argv[1], "rb").read()
words = struct.unpack(f"<{len(art) // 4}I", art)
dis = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "aarch64", sys.argv[1]], check=True,
                     capture_output=True, text=True).stdout
ins = {}
for line in dis.splitlines():
    m = re.match(r"^\s*([0-9a-f]+):\t[0-9a-f]{8} \t(\S+)\s*(.*)$", line)
    if m:
        ins[int(m.group(1), 16)] = (m.group(2), m.group(3))

# rotates: by encoding (independent of objdump's alias choice) and by objdump mnemonic
extr = sum(1 for w in words if w & 0x7FA00000 == 0x13800000)      # EXTR (ROR immediate alias when Rn==Rm)
ror_imm = sum(1 for w in words if w & 0x7FA00000 == 0x13800000 and (w >> 5) & 31 == (w >> 16) & 31)
rorv = sum(1 for w in words if w & 0x7FE0FC00 == 0x1AC02C00)       # RORV (DP 2-source, opcode 001011)
obj_ror = sum(1 for (mn, _) in ins.values() if mn == "ror")
obj_extr = sum(1 for (mn, _) in ins.values() if mn == "extr")
obj_rorv = sum(1 for (mn, _) in ins.values() if mn == "rorv")
print(f"SIZE bytes={len(art)} words={len(words)}")
print(f"ROTATE encoding: EXTR={extr} (of which ROR-immediate alias Rn==Rm: {ror_imm}) RORV={rorv}; "
      f"objdump mnemonics: ror={obj_ror} extr={obj_extr} rorv={obj_rorv}")

entries = []
for k in range(32):
    w = words[k]
    if w & 0xFC000000 != 0x14000000:
        break
    imm = w & 0x03FFFFFF
    imm = imm - (1 << 26) if imm & (1 << 25) else imm
    entries.append(4 * k + 4 * imm)


def tgt(ops):
    t = ops.split("//")[0].replace(",", " ").split()[-1]
    return int(t, 16)


memo = {}


def bound(f, stack=()):
    if f in memo:
        return memo[f]
    if f in stack:
        sys.exit(f"call cycle at {f:#x}")
    depth = {f: 0}
    work = [f]
    best = 0
    calls = []
    while work:
        pc = work.pop()
        d = depth[pc]
        mn, ops = ins[pc]
        nd = d
        m = re.match(r"^(sub|add)$", mn) and re.match(r"^sp, sp, #(0x[0-9a-f]+|\d+)$", ops)
        if m:
            v = int(m.group(1), 0)
            nd = d + v if mn == "sub" else d - v
        elif re.search(r"(^|, )sp(,|$)", ops.split("[")[0]) and not ops.startswith("x") and not ops.startswith("w"):
            sys.exit(f"unmodelled SP write at {pc:#x}: {mn} {ops}")
        if re.search(r"\[sp[^\]]*\]!|\[sp\], #", ops):
            sys.exit(f"SP writeback at {pc:#x}")
        best = max(best, nd)
        succ = []
        if mn == "ret":
            if nd != 0:
                sys.exit(f"RET at {pc:#x} with depth {nd}")
        elif mn == "br":
            pass
        elif mn == "b":
            succ = [tgt(ops)]
        elif mn.startswith("b.") or mn in ("cbz", "cbnz", "tbz", "tbnz"):
            succ = [tgt(ops), pc + 4]
        elif mn == "bl":
            calls.append((pc, nd, tgt(ops)))
            succ = [pc + 4]
        else:
            succ = [pc + 4]
        for s in succ:
            if s in depth:
                if depth[s] != nd:
                    sys.exit(f"depth conflict at {s:#x}: {depth[s]} vs {nd}")
            else:
                depth[s] = nd
                work.append(s)
    for pc, d, c in calls:
        best = max(best, d + bound(c, stack + (f,)))
    memo[f] = best
    return best


names = ["sha256_init", "sha256_update", "sha256_final", "sha256_hash"]
for k, e in enumerate(entries):
    print(f"STACK export {k} ({names[k] if k < len(names) else '?'}) entry={e:#x} bound={bound(e)} bytes")
for f in sorted(memo):
    print(f"FRAME fn@{f:#x} bound-incl-callees={memo[f]}")
