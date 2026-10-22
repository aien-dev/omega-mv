#!/usr/bin/env python3
"""Assemble tests/compiler/slices/cubin_elf.osc: run lines from the Wolfram decode, then a body.
usage: build_osc.py stub|real OUT [ORACLE_CUBIN]
ORACLE_CUBIN defaults to ~/workspace/primes-race/oracle/ops.cubin and must match the recorded sha256.
The real reader body is tests/compiler/slices/cubin_elf.osc below its run lines (cubin_reader_body.osc here)."""
import hashlib, os
import json, pathlib, sys

here = pathlib.Path(__file__).parent
mode, out = sys.argv[1], pathlib.Path(sys.argv[2])
fx = json.loads((here / "fixtures.json").read_text())["fixtures"]
w = json.loads((here / "wolfram_v2.json").read_text())
oracle_path = sys.argv[3] if len(sys.argv) > 3 else os.path.expanduser("~/workspace/primes-race/oracle/ops.cubin")
oracle = pathlib.Path(oracle_path).read_bytes()
assert hashlib.sha256(oracle).hexdigest() == w["oracle"]["sha256"], "oracle image differs from the decoded one"

M = 2**64 - 1
L = [
    "// slice-entry: u16_at u32_at u64_at hdr_ok arch_of layout_ok text_count",
    "// Run lines for the Omega cubin reader below. Expected values were decoded independently in Wolfram",
    "// (evidence/L401-cubin-reader/), never by the script that wrote the images. 18446744073709551615 (all ones)",
    "// is the refusal value of arch_of and text_count. The last four lines carry a real sm_121 image",
    "// (nvcc -arch=sm_121 -cubin of the prime-race ops.cu encoding oracle, 16136 bytes, sha256 2712695...3e50).",
]
sm = w["small_le_reads_of_0102030405060708"]
E = "0102030405060708"
L += [
    f"// slice-run: u16_at bytes:{E} 6 -> {sm['u16@6']}",
    f"// slice-run: u16_at bytes:{E} 7 -> trap=BOUNDS",
    "// slice-run: u16_at bytes:NULL 0 -> trap=BOUNDS",
    f"// slice-run: u32_at bytes:{E} 0 -> {sm['u32@0']}",
    f"// slice-run: u32_at bytes:{E} 1 -> {sm['u32@1']}",
    f"// slice-run: u32_at bytes:{E} 4 -> {sm['u32@4']}",
    f"// slice-run: u32_at bytes:{E} 5 -> trap=BOUNDS",
    f"// slice-run: u32_at bytes:{E} 18446744073709551615 -> trap=BOUNDS",
    "// slice-run: u32_at bytes: 0 -> trap=BOUNDS",
    f"// slice-run: u64_at bytes:{E} 0 -> {sm['u64@0']}",
    f"// slice-run: u64_at bytes:{E} 1 -> trap=BOUNDS",
]
def lines_for(hexs, vals):
    _, _, h, a, lo, t = vals
    arg = "bytes:" + hexs
    return [f"// slice-run: hdr_ok {arg} -> {h}", f"// slice-run: arch_of {arg} -> {a}",
            f"// slice-run: layout_ok {arg} -> {lo}", f"// slice-run: text_count {arg} -> {t}"]
for name in fx:
    L += lines_for(fx[name], w["fixtures"][name])
od = w["oracle"]["decode"]
L += lines_for(oracle.hex(), [None, None, od["hdr_ok"], od["arch_of"], od["layout_ok"], od["text_count"]])
L += ["// slice-run: hdr_ok bytes:NULL -> 0", "// slice-run: text_count bytes:NULL -> 18446744073709551615"]

if mode == "stub":
    body = "\n".join(
        ["// RED stub: every entry returns 7777777, so each run line must fail on its value"]
        + [f"fn {f}(b: bytes, o: u64) -> u64 {{\n    return 7777777;\n}}" for f in ("u16_at", "u32_at", "u64_at")]
        + [f"fn {f}(b: bytes) -> u64 {{\n    return 7777777;\n}}" for f in ("hdr_ok", "arch_of", "layout_ok", "text_count")]) + "\n"
else:
    body = (here / "cubin_reader_body.osc").read_text()
out.write_text("\n".join(L) + "\n" + body)
runs = [l for l in L if l.startswith("// slice-run: ")]
print(f"{mode}: {len(runs)} run lines, {sum(len(l) - 14 >= 400 for l in runs)} at or over 400 chars, longest {max(map(len, runs))}")
