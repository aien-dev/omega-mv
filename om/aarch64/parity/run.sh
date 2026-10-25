#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Parity of om/aarch64/aarch64.om against the C it ports (src/aarch64_encoder.c, src/aarch64_decoder.c),
# by RECORDED C output: corpus.py regenerates the corpus, the .om harness runs every stream, and each
# stream's output must hash to parity/expected.txt, the sha256 of the C oracle's output for the same
# stream (recorded when the port was made; the C oracle driver itself is not in the tree, so this script
# does not run any C). Then 23 small mutants of aarch64.om must each change their stream's hash, and
# six inputs outside the C parameter types or the fixed arrays must trap with the named code.
# With `as` and `objcopy` on PATH, up to 200 words per form (4,801) are also assembled from their fields by GNU as and
# compared. With OMEGA_BRIDGE set, the 28 exported functions are cut into function-mode units
# (unit.py), bound by the pinned omega-bridge generator and compiled as C (bindability).
#   OMEGA_OMC     seed-born omc, sha256 pinned below (exit 3 NOT_RUN without it)
#   OMEGA_BRIDGE  optional: an aien-dev/omega-bridge checkout whose src/native.om matches the pin
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
NATIVE_PIN=2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940
P=$(cd "$(dirname "$0")" && pwd)
OM=$P/../aarch64.om
[ -n "${OMEGA_OMC:-}" ] || { echo "AARCH64-PARITY NOT_RUN: set OMEGA_OMC"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "AARCH64-PARITY NOT_RUN: omc is not the pinned compiler"; exit 3; }
command -v python3 > /dev/null || { echo "AARCH64-PARITY NOT_RUN: python3 missing"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 "$P/corpus.py" "$T/corp" > "$T/counts"
"$OMEGA_OMC" < "$OM" > "$T/am"; chmod +x "$T/am"
grep -v '^#' "$P/expected.txt" | while read -r s n want; do
  "$T/am" < "$T/corp/$s.txt" > "$T/$s.out"
  got=$(sha256sum < "$T/$s.out" | cut -c1-64)
  lines=$(wc -l < "$T/$s.out")
  inputs=$(grep "^$s " "$T/counts" | cut -d' ' -f2)
  if [ "$got" = "$want" ] && [ "$lines" = "$n" ] && [ "$inputs" = "$n" ]; then
    echo "PASS $s: $n records, output identical to the recorded C oracle output"
  else
    echo "FAIL $s: $lines records of $inputs inputs, sha256 $got (want $n, $want)"; echo x >> "$T/failed"
  fi
done
[ ! -e "$T/failed" ] || { echo "AARCH64-PARITY FAIL"; exit 1; }
[ "$(grep -vc '^#' "$P/expected.txt")" = "$(wc -l < "$T/counts")" ] || { echo "AARCH64-PARITY FAIL: stream lists differ"; exit 1; }
cat "$T"/e*.out | awk '/^E /{w++; m[$NF]++} /^A64_LEGACY/{r++}
  END {printf "encoder: %d words (round trip: %d re-encode identical, %d lossy decode with every carried field equal, %d wrong), %d refusals\n", w, m["="], m["~"], m["!"], r; if (m["!"] > 0) exit 1}' \
  || { echo "AARCH64-PARITY FAIL: round trip"; exit 1; }

# Mutants: name | stream | exact text in aarch64.om (\n for a newline) | replacement
cat > "$T/mutants" <<'EOF'
add_opcode|e01_add_reg|ret a64rrr 184549376 sf rd rn rm|ret a64rrr 167772160 sf rd rn rm
rd_range|e01_add_reg|nu8 rn\nnu8 rm\nif gt rd 31|nu8 rn\nnu8 rm\nif gt rd 32
movreg_field|e07_mov_reg|if gt rm 31\nret -4|if gt rm 31\nret -3
movz_shift48|e08_movz|mx = 48|mx = 32
hw_position|e14_movk|w = or w shl and div sh 16 3 21|w = or w shl and div sh 16 3 22
b_range|e10_b|if or lt imm -33554432 ge imm 33554432|if or lt imm -33554432 gt imm 33554432
bcond_cond15|e11_b_cond|if or lt cond 0 gt cond 15|if or lt cond 0 gt cond 14
cbx_sf|e12_cbz|w = or base shl sf 31\nw = or w shl and bitcast ulong imm 524287 5|w = or base shl sf 30\nw = or w shl and bitcast ulong imm 524287 5
adr_immlo|e15_adr|w = or 268435456 shl and x 3 29|w = or 268435456 shl and x 3 28
scaled_max|e16_ldr_uoff|if or ne rem uo sz 0 gt div uo sz 4095|if or ne rem uo sz 0 gt div uo sz 4096
ldrb_max|e18_ldrb_uoff|if gt uo 4095\nret -4|if gt uo 4096\nret -4
post_mask|e20_ldr_post|w = or base shl and bitcast ulong imm 511 12|w = or base shl and bitcast ulong imm 255 12
subs_base|e24_subs_imm|base = 4043309056|base = 3942645760
put_full|buf|if gt add p 4 get st 1|if ge add p 4 get st 1
put_byte0|buf|put b p uchar and x 255|put b p uchar and shr x 8 255
dec_mul_o0|dec|if eq and insn 2145418240 453016576|if eq and insn 2145451008 453016576
dec_ret_rn|dec|put out 3 30|put out 3 0
dec_post_sf|dec|if eq m 3091203072\ndregs out 15 insn 30 2|if eq m 3091203072\ndregs out 15 insn 31 2
dec_bcond_imm|dec|put out 7 and shr insn 5 524287\nret 0\nend\nm = and insn 2130706432|put out 7 and shr insn 4 524287\nret 0\nend\nm = and insn 2130706432
val_hex8|val|n = apphex t n w 8|n = apphex t n w 7
val_trunc|val|m = sub ml 1|m = ml
val_last|val|if and eq i sub c 1 ne get d 0 8|if and eq i sub c 2 ne get d 0 8
val_digit|val|put t add n sub sub k 1 i uchar add 48 rem q 10|put t add n sub sub k 1 i uchar add 49 rem q 10
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
[ ! -e "$T/failed" ] || { echo "AARCH64-PARITY FAIL"; exit 1; }

# Named limits: record | expected exit (100 + trap code)
cat > "$T/traps" <<'EOF'
E 1 0 100 0 0|105|register 256 is outside uint8_t
E 1 2 0 0 0|105|sf 2 is outside bool
E a 80000000 0 0 0|105|imm26 2^31 is outside int32_t
D 100000000|105|word 2^32 is outside uint32_t
V 4 101 c0035fd6|103|ml 257 is more than the 256-byte msg array
P 2000 ffd 1 9 0 0 0 0|103|a write past the 4096-byte buffer (pos 4093, max_len 8192)
EOF
while IFS='|' read -r rec want why; do
  rc=0; printf '%s\n' "$rec" | "$T/am" > /dev/null 2>&1 || rc=$?
  if [ "$rc" = "$want" ]; then echo "limit trapped (exit $rc): $why"; else echo "limit FAIL (exit $rc, want $want): $why"; echo x >> "$T/failed"; fi
done < "$T/traps"
[ ! -e "$T/failed" ] || { echo "AARCH64-PARITY FAIL"; exit 1; }

if command -v as > /dev/null && command -v objcopy > /dev/null && [ "$(uname -m)" = aarch64 ]; then
  mkdir "$T/gas"
  python3 "$P/gas.py" "$T/corp" "$T" "$T/gas" || { echo "AARCH64-PARITY FAIL: GNU as cross-check"; exit 1; }
else
  echo "gas cross-check NOT_RUN: no AArch64 as/objcopy"
fi

if [ -z "${OMEGA_BRIDGE:-}" ]; then
  echo "AARCH64-PARITY PASS (bindability NOT_RUN: set OMEGA_BRIDGE)"; exit 0
fi
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "AARCH64-PARITY PASS (bindability NOT_RUN: native.om is not the pinned generator)"; exit 0; }
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
: > "$T/units"
for u in a64addreg a64subreg a64mulreg a64andreg a64orrreg a64eorreg a64movreg a64movz a64ret a64b a64bcond \
         a64cbz a64cbnz a64movk a64adr a64ldruoff a64struoff a64ldrbuoff a64strbuoff a64ldrpost a64strpost \
         a64ldrxpost a64strxpost a64subsimm a64subsreg a64put a64decode a64validate; do
  python3 "$P/unit.py" "$OM" "$u" > "$T/u.om"
  "$OMEGA_OMC" < "$T/u.om" > "$T/u.bin"
  { cat "$T/u.om"; printf '\0'; printf '%08d' "$(wc -c < "$T/u.bin")"; cat "$T/u.bin"; } >> "$T/units"
done
"$T/native" < "$T/units" > "$T/units.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -c "$T/units.c" -o "$T/units.o"
echo "bindability: 28 function-mode units accepted by native.om and compiled ($(grep -c ') uint32_t omega_[a-z0-9]*(uint64_t \*result' "$T/units.c") exported functions)"
echo "AARCH64-PARITY PASS"
