#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Parity of om/evidence/evidence.om against the C it ports (src/omega_vector.c, src/omega_evidence.c),
# by recorded oracle output: corpus.py regenerates the corpus, the .om harness runs every stream in its
# own empty directory, and each stream's output, plus the directory tree the path and digest streams
# leave behind (pathtree, digtree), must hash to parity/expected.txt. Those hashes are the C oracle's
# output, recorded when the port was made; the oracle driver itself is not in the tree. Then sixteen
# small mutants of evidence.om must each change their stream's hash (the corpus discriminates). With
# OMEGA_BRIDGE set, the 13 exported functions are also cut into function-mode units (unit.py), bound by
# the pinned omega-bridge generator and compiled as C (bindability).
#   OMEGA_OMC     seed-born omc, sha256 pinned below (exit 3 NOT_RUN without it)
#   OMEGA_BRIDGE  optional: an aien-dev/omega-bridge checkout whose src/native.om matches the pin
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
NATIVE_PIN=2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940
P=$(cd "$(dirname "$0")" && pwd)
OM=$P/../evidence.om
[ -n "${OMEGA_OMC:-}" ] || { echo "EVIDENCE-PARITY NOT_RUN: set OMEGA_OMC"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "EVIDENCE-PARITY NOT_RUN: omc is not the pinned compiler"; exit 3; }
command -v python3 > /dev/null || { echo "EVIDENCE-PARITY NOT_RUN: python3 missing"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 "$P/corpus.py" "$T/corp" > "$T/counts"

# run BIN STREAM OUT: one stream in a fresh directory; pathtree/digtree list what path/digest left
run() {
  case $2 in pathtree) rs=path ;; digtree) rs=digest ;; *) rs=$2 ;; esac
  rm -rf "$T/w"; mkdir "$T/w"
  rc=0; (cd "$T/w" && umask 022 && "$1" $rs < "$T/corp/$rs.txt" > "$T/s.out" 2> /dev/null) || rc=$?
  case $2 in
    pathtree|digtree) (cd "$T/w" && { find . -printf '%p %y %m %s\n'; find . -type f -exec sha256sum {} +; } | LC_ALL=C sort) > "$3" ;;
    *) cp "$T/s.out" "$3" ;;
  esac
  return $rc
}

"$OMEGA_OMC" < "$OM" > "$T/ev"; chmod +x "$T/ev"
grep -v '^#' "$P/expected.txt" | while read -r s n want; do
  rc=0; run "$T/ev" "$s" "$T/$s.out" || rc=$?
  got=$(sha256sum < "$T/$s.out" | cut -c1-64)
  lines=$(wc -l < "$T/$s.out")
  if [ "$rc" = 0 ] && [ "$got" = "$want" ] && [ "$lines" = "$n" ]; then
    echo "PASS $s: $n lines, output identical to the C oracle"
  else
    echo "FAIL $s: exit $rc, $lines lines, sha256 $got (want $n, $want)"; echo x >> "$T/failed"
  fi
done
[ ! -e "$T/failed" ] || { echo "EVIDENCE-PARITY FAIL"; exit 1; }
sed 's/^/corpus records: /' "$T/counts"

# Mutants: name | stream | exact text in evidence.om (\n for a newline) | replacement
cat > "$T/mutants" <<'EOF'
overflow_byte|vspec|put p 22 1|put p 22 0
name_64|vspec|put onm 63 0|put onm 63 get nm 63
mismatch_idx|vadd|put idx 0 i|put idx 0 add i 1
gen_b|vgen|y = add 2000 mul i 2|y = add 2000 i
trim_cr|line|if and ne c 10 and ne c 13 ne c 32|if and ne c 10 ne c 32
len_rule|line|if or eq l 0 ge l n|if or eq l 0 gt l n
commit_len|line|if ne slen256 view t 40|if ne slen256 view t 41
floor_div|runid|q = sub q 1|q = sub q 0
leap_month|runid|if le m 2|if lt m 2
hex_upper|hw|ret uchar add v 87|ret uchar add v 55
path_limit|path|if ge tot 1024\nret -1|if gt tot 1024\nret -1
mkdir_mode|pathtree|r = makedir p 0 i 493|r = makedir p 0 i 448
rel_dot|digest|put rel sl 46|put rel sl 95
exists_rc|digest|if eq fd -17\nret 1|if eq fd -17\nret 0
sha_pad|digest|if ge r 56|if ge r 55
file_mode|digtree|fd = fopen out 0 l 524481 292|fd = fopen out 0 l 524481 420
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
  rc=0; run "$T/m" "$s" "$T/m.out" || rc=$?
  got=$(sha256sum < "$T/m.out" | cut -c1-64)
  want=$(grep "^$s " "$P/expected.txt" | cut -d' ' -f3)
  if [ "$got" = "$want" ]; then echo "mutant $name NOT killed on $s"; echo x >> "$T/failed"; continue; fi
  d=$(diff "$T/m.out" "$T/$s.out" | grep -c '^>' || true)
  echo "mutant $name killed on $s ($d lines differ, exit $rc)"
done < "$T/mutants"
[ ! -e "$T/failed" ] || { echo "EVIDENCE-PARITY FAIL"; exit 1; }

if [ -z "${OMEGA_BRIDGE:-}" ]; then
  echo "EVIDENCE-PARITY PASS (bindability NOT_RUN: set OMEGA_BRIDGE)"; exit 0
fi
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "EVIDENCE-PARITY PASS (bindability NOT_RUN: native.om is not the pinned generator)"; exit 0; }
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
: > "$T/units"
for u in vecspec vecadd vecver vecgen lnacc cmtacc runid evpath digrel hwhex shainit shaupd shafin; do
  python3 "$P/unit.py" "$OM" "$u" > "$T/u.om"
  "$OMEGA_OMC" < "$T/u.om" > "$T/u.bin"
  { cat "$T/u.om"; printf '\0'; printf '%08d' "$(wc -c < "$T/u.bin")"; cat "$T/u.bin"; } >> "$T/units"
done
"$T/native" < "$T/units" > "$T/units.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -c "$T/units.c" -o "$T/units.o"
echo "bindability: 13 function-mode units accepted by native.om and compiled ($(grep -c ') uint32_t omega_[a-z0-9]*(uint64_t \*result' "$T/units.c") exported functions)"
echo "EVIDENCE-PARITY PASS"
