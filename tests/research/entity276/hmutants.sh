#!/usr/bin/env bash
# hmutants.sh -- the horizon-mode checks must fail on purpose (evidence/ENTITY-276/h40):
# a clean H 3 run exits 0 with 0 bad rows in every arm; each mutant exits 1 with bad rows
# > 0 in the arm whose check it breaks. Needs a pilot journal and identity from run.sh.
set -u
BIN=${ENTITY276_BIN:?}; OUT=${PILOT_OUT:?}; J=${1:?pilot journal}; X=${2:?identity json}
mkdir -p "$OUT"; : > "$OUT/hmutants-verdict.txt"
fails=0
note() { echo "$*" | tee -a "$OUT/hmutants-verdict.txt"; }
bad() { sed -n 's/.*"bad_rows":\([0-9]*\).*/\1/p' "$1/$2/summary-horizon-$2.json"; }
D="$OUT/clean"; mkdir -p "$D"
env PILOT_OUT="$D" "$BIN" horizon 3 "$J" "$X" --interleave --lazy-rebuild --forget --release > "$D/out.txt" 2>&1; rc=$?
b=0; for a in warm cold lazy forget; do b=$((b + $(bad "$D" $a))); done
if [ $rc = 0 ] && [ $b = 0 ]; then note "OK   clean exit 0, bad rows 0"; else note "FAIL clean exit $rc bad rows $b"; fails=$((fails+1)); fi
for m in FORGETSTORE:forget LAZYSKIP:lazy COLDKEEP:cold DOUBLERELEASE:warm; do
  name=${m%%:*}; arm=${m#*:}; D="$OUT/$name"; mkdir -p "$D"
  env ENTITY276_HMUTANT=$name PILOT_OUT="$D" "$BIN" horizon 3 "$J" "$X" --interleave --lazy-rebuild --forget --release > "$D/out.txt" 2>&1; rc=$?
  b=$(bad "$D" $arm)
  if [ $rc = 1 ] && [ -n "$b" ] && [ "$b" -gt 0 ]; then note "OK   $name red: exit 1, $arm bad rows $b"
  else note "FAIL $name exit $rc, $arm bad rows '$b'"; fails=$((fails+1)); fi
done
note "RESULT fails=$fails"
[ $fails = 0 ]
