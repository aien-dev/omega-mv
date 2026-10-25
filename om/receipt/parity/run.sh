#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Parity of om/receipt/receipt.om against the C it ports (src/omega_receipt.c + omega_receipt.h), by
# RECORDED oracle output: this script does not build or run any C. corpus.py regenerates the corpus,
# the .om harness runs every stream, and each stream's output must have the line count and sha256 in
# parity/expected.txt, which is the C oracle driver's output recorded when the port was made (the
# driver is not in the tree; README.md says how to rerun it). Then small mutants of receipt.om must
# each run to completion and change their stream's hash (the corpus discriminates). With OMEGA_BRIDGE
# set, the five entry points are also cut into function-mode units (unit.py), bound by the pinned
# omega-bridge generator and compiled as C (bindability).
#   OMEGA_OMC     seed-born omc, sha256 pinned below (exit 3 NOT_RUN without it)
#   OMEGA_BRIDGE  optional: an aien-dev/omega-bridge checkout whose src/native.om matches the pin
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
NATIVE_PIN=2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940
P=$(cd "$(dirname "$0")" && pwd)
OM=$P/../receipt.om
[ -n "${OMEGA_OMC:-}" ] || { echo "RECEIPT-PARITY NOT_RUN: set OMEGA_OMC"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "RECEIPT-PARITY NOT_RUN: omc is not the pinned compiler"; exit 3; }
command -v python3 > /dev/null || { echo "RECEIPT-PARITY NOT_RUN: python3 missing"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 "$P/corpus.py" "$T/corp" > "$T/counts"
"$OMEGA_OMC" < "$OM" > "$T/rc"; chmod +x "$T/rc"
grep -v '^#' "$P/expected.txt" | while read -r s n want; do
  rc=0; "$T/rc" < "$T/corp/$s.bin" > "$T/$s.out" || rc=$?
  got=$(sha256sum < "$T/$s.out" | cut -c1-64)
  lines=$(wc -l < "$T/$s.out")
  if [ "$rc" = 0 ] && [ "$got" = "$want" ] && [ "$lines" = "$n" ]; then
    echo "PASS $s: $n lines identical to the recorded C oracle output ($(grep "^$s " "$T/counts" | cut -d' ' -f2) records)"
  else
    echo "FAIL $s: exit $rc, $lines lines, sha256 $got (want $n, $want)"; echo x >> "$T/failed"
  fi
done
[ ! -e "$T/failed" ] || { echo "RECEIPT-PARITY FAIL"; exit 1; }

# Mutants: name | stream | exact text in receipt.om (\n for a newline) | replacement.
# Killed = the mutant harness exits 0 with the expected line count and a different sha256.
cat > "$T/mutants" <<'EOF'
json_dup_key|json|if dupkey js n f k|if eq k 0
json_depth|json|if gt d 6|if gt d 7
json_surrogate_utf8|json|if and ge cp 2048 not and ge cp 55296 le cp 57343|if ge cp 2048
json_pair_offset|json|ret fail wk 7 add p 4|ret fail wk 7 p
json_leading_zero|json|if gt sub q d0 1|if gt sub q d0 2
deny_unknown|shapes|if eq k 999\nret failk wk 22 0|if false\nret failk wk 22 0
schema|shapes|if not nameq js wk get wk 64 59|if false
env_class|shapes|if ne dcmp js get wk 77 get wk 68 0|if false
pass_needs_assertion|shapes|if and eq m 0 eq get wk 103 0|if false
prod_test_only|shapes|if eq get a 8 1\nret fail wk 60 0|if false\nret fail wk 60 0
canon_sort|shapes|put wk 7 0\nmsort js wk m|put wk 7 0
id_compare|shapes|if not same|if false
escape_f|shapes|if eq e 102\nret 12|if eq e 102\nret 13
set_dedup|fields|ret ne dcmp js x y 0|ret true
mutation_order|fields|if gt so sev get wk 105|if false
u64_overflow|fields|if gt x div sub 18446744073709551615 dg 10|if false
upper_hex|rust|ret sub c 55|ret 16
b3_root|rust|put w 74 or get w 74 8|put w 74 or get w 74 0
canon_window|big|if and ge t lo lt t add lo 1048576|if and ge t lo lt t add lo 1048575
tier_test_only|tier|if eq need 0\nret 1|if eq need 0\nret 0
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
  rc=0; "$T/m" < "$T/corp/$s.bin" > "$T/m.out" 2> /dev/null || rc=$?
  got=$(sha256sum < "$T/m.out" | cut -c1-64)
  lines=$(wc -l < "$T/m.out")
  want=$(grep "^$s " "$P/expected.txt" | cut -d' ' -f3)
  n=$(grep "^$s " "$P/expected.txt" | cut -d' ' -f2)
  if [ "$rc" != 0 ] || [ "$lines" != "$n" ] || [ "$got" = "$want" ]; then
    echo "mutant $name NOT killed on $s (exit $rc, $lines lines, sha256 $got)"; echo x >> "$T/failed"; continue
  fi
  dl=$(paste -d'\n' "$T/m.out" "$T/$s.out" | awk 'NR % 2 == 1 { a = $0; next } a != $0' | wc -l)
  echo "mutant $name killed on $s (exit 0, $dl of $n lines differ)"
done < "$T/mutants"
[ ! -e "$T/failed" ] || { echo "RECEIPT-PARITY FAIL"; exit 1; }

if [ -z "${OMEGA_BRIDGE:-}" ]; then
  echo "RECEIPT-PARITY PASS (bindability NOT_RUN: set OMEGA_BRIDGE)"; exit 0
fi
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "RECEIPT-PARITY PASS (bindability NOT_RUN: native.om is not the pinned generator)"; exit 0; }
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
: > "$T/units"
for u in rcparse rccanon rcverify rctier rcsat; do
  python3 "$P/unit.py" "$OM" "$u" > "$T/u.om"
  "$OMEGA_OMC" < "$T/u.om" > "$T/u.bin"
  { cat "$T/u.om"; printf '\0'; printf '%08d' "$(wc -c < "$T/u.bin")"; cat "$T/u.bin"; } >> "$T/units"
done
"$T/native" < "$T/units" > "$T/units.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -c "$T/units.c" -o "$T/units.o"
echo "bindability: 5 function-mode units accepted by native.om and compiled ($(grep -c ') uint32_t omega_[a-z0-9]*(uint64_t \*result' "$T/units.c") exported functions)"
echo "RECEIPT-PARITY PASS"
