#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Parity of om/gpumath/gpumath.om against the C it ports (src/omega_gpu_hostmath.c, src/omega_gpu_fp8_pack.c,
# src/omega_gpu_fmt.c), by recorded oracle output: corpus.py regenerates the corpus, the .om harness runs every
# stream, and each stream's output must hash to parity/expected.txt (the C oracle's output, recorded when the
# port was made; the oracle driver itself is not in the tree). Then thirteen small mutants of gpumath.om
# must each change their stream's hash (the corpus discriminates). With OMEGA_BRIDGE set, the 15 exported
# functions are also cut into function-mode units (unit.py), bound by the pinned omega-bridge generator and
# compiled as C (bindability).
#   OMEGA_OMC     seed-born omc, sha256 pinned below (exit 3 NOT_RUN without it)
#   OMEGA_BRIDGE  optional: an aien-dev/omega-bridge checkout whose src/native.om matches the pin
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
NATIVE_PIN=2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940
P=$(cd "$(dirname "$0")" && pwd)
OM=$P/../gpumath.om
[ -n "${OMEGA_OMC:-}" ] || { echo "GPUMATH-PARITY NOT_RUN: set OMEGA_OMC"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "GPUMATH-PARITY NOT_RUN: omc is not the pinned compiler"; exit 3; }
command -v python3 > /dev/null || { echo "GPUMATH-PARITY NOT_RUN: python3 missing"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 "$P/corpus.py" "$T/corp" > "$T/counts"
"$OMEGA_OMC" < "$OM" > "$T/gm"; chmod +x "$T/gm"
fail=0
grep -v '^#' "$P/expected.txt" | while read -r s n want; do
  "$T/gm" < "$T/corp/$s.txt" > "$T/$s.out"
  got=$(sha256sum < "$T/$s.out" | cut -c1-64)
  lines=$(wc -l < "$T/$s.out")
  if [ "$got" = "$want" ] && [ "$lines" = "$n" ]; then
    echo "PASS $s: $n records, output identical to the C oracle ($(grep "^$s " "$T/counts" | cut -d' ' -f2) inputs)"
  else
    echo "FAIL $s: $lines records, sha256 $got (want $n, $want)"; echo x >> "$T/failed"
  fi
done
[ ! -e "$T/failed" ] || { echo "GPUMATH-PARITY FAIL"; exit 1; }

# Mutants: name | stream | exact text in gpumath.om (\n for a newline) | replacement
cat > "$T/mutants" <<'EOF'
tie_up|fma|if or low128 hi lo sub us 1 eq and q 1 1|if true
sticky_off|fma|put t add k 1 or nlo st|put t add k 1 nlo
nan_order|fma|if issnan64 a\nret or a 2251799813685248\nend\nif issnan64 b|if issnan64 b\nret or b 2251799813685248\nend\nif issnan64 a
zero_sign|fma|if eq or get t 4 get t 5 0\nret 0|if eq or get t 4 get t 5 0\nret shl sg 63
log_unfused|log|lo = fma64 s bitcast ulong add t t lo|lo = bitcast ulong add mul bitcast f64 s add t t bitcast f64 lo
sincos_unfused|sincos|r = fma64 nn 4609753056924401664 ab|r = bitcast ulong sub a mul f64 n bitcast f64 ulong 4609753056924401664
pow_order|pow|ret hmexp bitcast ulong mul bitcast f64 hmlog bb e|ret hmexp bitcast ulong mul e bitcast f64 hmlog bb
pow2f_bias|pow2f|ret shl ulong add e 127 23|ret shl ulong add e 126 23
e4m3_sub|e4m3|ret ulong bitcast uint mul mul sg mf bitcast f32 p2f -9|ret ulong bitcast uint mul mul sg mf bitcast f32 p2f -10
scales_fold|scales|fold = bitcast f32 p2f 56|fold = bitcast f32 p2f 55
scan_first|scan|put st 1 add base i|put st 1 i
gemv_order|gemv|p = mul mul f64 bitcast f32 ldx x c rf s|p = mul mul rf f64 bitcast f32 ldx x c s
fmt_char|fmt|fput out sn uchar and w 255|fput out sn 63
EOF
while IFS='|' read -r name s old new; do
  python3 - "$OM" "$T/m.om" "$old" "$new" <<'PY'
import sys
src, dst, old, new = sys.argv[1], sys.argv[2], sys.argv[3].replace("\\n", "\n"), sys.argv[4].replace("\\n", "\n")
s = open(src).read()
assert s.count(old + "\n") == 1, "mutant text not found exactly once: " + old
open(dst, "w").write(s.replace(old + "\n", new + "\n"))
PY
  "$OMEGA_OMC" < "$T/m.om" > "$T/m"; chmod +x "$T/m"
  rc=0; "$T/m" < "$T/corp/$s.txt" > "$T/m.out" 2> /dev/null || rc=$?
  got=$(sha256sum < "$T/m.out" | cut -c1-64)
  want=$(grep "^$s " "$P/expected.txt" | cut -d' ' -f3)
  if [ "$got" = "$want" ]; then echo "mutant $name NOT killed on $s"; echo x >> "$T/failed"; continue; fi
  d=$(paste -d'|' "$T/m.out" "$T/$s.out" | awk -F'|' '$1 != $2' | wc -l)
  echo "mutant $name killed on $s ($d records differ, exit $rc)"
done < "$T/mutants"
[ ! -e "$T/failed" ] || { echo "GPUMATH-PARITY FAIL"; exit 1; }

if [ -z "${OMEGA_BRIDGE:-}" ]; then
  echo "GPUMATH-PARITY PASS (bindability NOT_RUN: set OMEGA_BRIDGE)"; exit 0
fi
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "GPUMATH-PARITY PASS (bindability NOT_RUN: native.om is not the pinned generator)"; exit 0; }
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
: > "$T/units"
for u in hmlog hmexp hmpow hmsincos hmpow2f e4m3ref e4m3kbits e4m3isnan f8scannan f8packscales f8packxf32 \
         f8packxbf16 f8gemvblock gpufmt gpufmtkinds; do
  python3 "$P/unit.py" "$OM" "$u" > "$T/u.om"
  "$OMEGA_OMC" < "$T/u.om" > "$T/u.bin"
  { cat "$T/u.om"; printf '\0'; printf '%08d' "$(wc -c < "$T/u.bin")"; cat "$T/u.bin"; } >> "$T/units"
done
"$T/native" < "$T/units" > "$T/units.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -c "$T/units.c" -o "$T/units.o"
echo "bindability: 15 function-mode units accepted by native.om and compiled ($(grep -c ') uint32_t omega_[a-z0-9]*(uint64_t \*result' "$T/units.c") exported functions)"
echo "GPUMATH-PARITY PASS"
