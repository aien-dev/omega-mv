#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# seed/qualification/om2/oracle/leak/xcheck.py: independent ORACLE (outside the delivered closure) for
# seed/qualification/om2/leak/leakscan.om. It disassembles the same function-mode artifact with GNU
# objdump (-D -b binary -m aarch64) and compares, word offset by word offset:
#   1. the set of conditional branches (b.<cond>, cbz, cbnz, tbz, tbnz) and their targets,
#   2. the set of loads/stores whose address involves a register other than sp/x29
#      (a base other than sp/x29, or any register index), from objdump's operand text,
#   3. the words objdump cannot decode (.inst / undefined) against leakscan's UNKNOWN lines.
# It uses only objdump's text, never leakscan's decoder. Usage:
#   xcheck.py ARTIFACT REPORT            exit 0 when all three sets agree, 1 otherwise
#   xcheck.py --subset ARTIFACT REPORT   decoder differential (fuzz.py): a word leakscan lists as
#                                        UNKNOWN may be anything to objdump (leakscan decodes a
#                                        subset); every other disagreement still fails
import re
import subprocess
import sys


def objdump_listing(path):
    out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "aarch64", path],
                         check=True, capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\t([0-9a-f]{8}) \t(.*)$", line)
        if not m:
            m2 = re.match(r"^\s*([0-9a-f]+):\t([0-9a-f]{8}) \t?$", line)
            if m2:
                rows[int(m2.group(1), 16) // 4] = ("", "")
            continue
        text = m.group(3).split("//")[0].strip()
        parts = text.split(None, 1)
        rows[int(m.group(1), 16) // 4] = (parts[0], parts[1] if len(parts) > 1 else "")
    return rows


COND = re.compile(r"^(b\.[a-z]{2}|cbz|cbnz|tbz|tbnz)$")
MEM = re.compile(r"^(ld|st)[a-z0-9]*$")


def oracle_sets(rows):
    br, mem, unk, spmem = {}, set(), set(), 0
    for w, (mn, ops) in rows.items():
        if mn in (".inst", "") or "undefined" in ops:
            unk.add(w)
            continue
        if COND.match(mn):
            addr = int(ops.split(",")[-1].strip(), 16)
            if addr >= 1 << 63:
                addr -= 1 << 64
            br[w] = addr // 4
            continue
        if MEM.match(mn) and "[" in ops:
            inside = ops[ops.index("[") + 1:ops.index("]")]
            regs = [r.strip() for r in inside.split(",")]
            base = regs[0]
            index = [r for r in regs[1:] if re.match(r"^[xw][0-9]+$", r)]
            if (base not in ("sp", "x29")) or index:
                mem.add(w)
            else:
                spmem += 1
        elif MEM.match(mn):
            # a literal load ldr xN, <address>: no register in the address
            pass
    return br, mem, unk, spmem


def tool_sets(report):
    br, mem, unk = {}, set(), set()
    with open(report) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "BRANCH":
                tgt = int(t[3][3:])
                if tgt >= 1 << 63:
                    tgt -= 1 << 64
                br[int(t[1][1:])] = tgt
            elif t[0] == "MEM":
                mem.add(int(t[1][1:]))
            elif t[0] == "UNKNOWN":
                unk.add(int(t[1][1:]))
    return br, mem, unk


def main():
    args = sys.argv[1:]
    subset = bool(args) and args[0] == "--subset"
    if subset:
        args = args[1:]
    if len(args) != 2:
        print("usage: xcheck.py [--subset] ARTIFACT REPORT", file=sys.stderr)
        return 90
    ver = subprocess.run(["objdump", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    rows = objdump_listing(args[0])
    obr, omem, ounk, spmem = oracle_sets(rows)
    tbr, tmem, tunk = tool_sets(args[1])
    ok = True
    tolerated = 0

    def differ(w, text):
        nonlocal ok, tolerated
        if subset and w in tunk:
            tolerated += 1
            return
        ok = False
        print(text)

    print(f"XCHECK oracle={ver!r} words={len(rows)}{' mode=subset' if subset else ''}")
    print(f"XCHECK branches objdump={len(obr)} leakscan={len(tbr)}")
    for w in sorted(set(obr) | set(tbr)):
        if obr.get(w) != tbr.get(w):
            differ(w, f"  branch @{w}: objdump target={obr.get(w)} leakscan target={tbr.get(w)} {rows.get(w)}")
    print(f"XCHECK mem-non-sp objdump={len(omem)} leakscan={len(tmem)} (sp/x29-only accesses not listed: {spmem})")
    for w in sorted(omem ^ tmem):
        differ(w, f"  mem @{w}: objdump={'yes' if w in omem else 'no'} leakscan={'yes' if w in tmem else 'no'} {rows.get(w)}")
    print(f"XCHECK undecodable objdump={len(ounk)} leakscan-unknown={len(tunk)}")
    for w in sorted(ounk ^ tunk):
        differ(w, f"  unknown @{w}: objdump={'yes' if w in ounk else 'no'} leakscan={'yes' if w in tunk else 'no'} {rows.get(w)}")
    if subset:
        print(f"XCHECK tolerated={tolerated} (words leakscan lists as UNKNOWN that objdump decodes)")
    print("XCHECK AGREE" if ok else "XCHECK DIFFER")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
