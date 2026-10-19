#!/usr/bin/env bash
# run.sh -- #276 entity pilot driver (ledger L058). Spec: evidence/ENTITY-276/FREEZE.md.
# Runs the pilot as two processes on one journal, a second identical run for determinism,
# the no-retained-knowledge restart control, the idle control, and every mutant. Each
# mutant must go red (exit 1, bad_rows > 0). Exit 0 only when all of that holds.
set -u
BIN=${ENTITY276_BIN:?set ENTITY276_BIN}
OUT=${PILOT_OUT:?set PILOT_OUT}
mkdir -p "$OUT"
fails=0
note() { echo "$*" | tee -a "$OUT/run-verdict.txt"; }
: > "$OUT/run-verdict.txt"
field() { sed -n "s/.*\"$2\":\([0-9]*\).*/\1/p" "$1"; }

# expect NAME WANT_EXIT -- CMD...
expect() {
  local name=$1 want=$2; shift 3
  "$@" > "$OUT/$name.log" 2>&1; local rc=$?
  if [ "$rc" = "$want" ]; then note "OK   $name exit $rc"; else note "FAIL $name exit $rc (want $want)"; fails=$((fails+1)); fi
}

for r in 1 2; do
  D="$OUT/run$r"; mkdir -p "$D"; rm -f "$D"/journal*
  expect "run$r-pilot" 0 -- env PILOT_OUT="$D" "$BIN" pilot "$D/journal.cx"
  cp "$D/journal.cx" "$D/journal-after-pilot.cx"
  expect "run$r-restart" 0 -- env PILOT_OUT="$D" "$BIN" restart "$D/journal.cx" "$D/identity-pilot.json"
done
for s in pilot restart; do
  if cmp -s "$OUT/run1/summary-$s.json" "$OUT/run2/summary-$s.json"; then note "OK   determinism summary-$s identical"
  else note "FAIL determinism summary-$s differs"; fails=$((fails+1)); fi
done

# No-retained-knowledge control: the same restart, template records ignored.
D="$OUT/cold"; mkdir -p "$D"; cp "$OUT/run1/journal-after-pilot.cx" "$D/journal.cx"
cp "$OUT/run1/identity-pilot.json" "$D/"
expect cold-restart 0 -- env PILOT_OUT="$D" "$BIN" restart-cold "$D/journal.cx" "$D/identity-pilot.json"

# Unchanged-state control on its own.
D="$OUT/idle"; mkdir -p "$D"; rm -f "$D"/journal*
expect control-idle 0 -- env PILOT_OUT="$D" "$BIN" control-idle "$D/journal.cx"

# Mutants: each must exit 1 with bad_rows > 0.
red() {
  local name=$1 sum=$2
  local b; b=$(field "$sum" bad_rows)
  if [ -n "$b" ] && [ "$b" -gt 0 ]; then note "OK   $name red with bad_rows $b"; else note "FAIL $name bad_rows '${b}'"; fails=$((fails+1)); fi
}
for m in SHADOW VIEWWRITE IDLEPOLL; do
  D="$OUT/mutant-$m"; mkdir -p "$D"; rm -f "$D"/journal*
  expect "mutant-$m" 1 -- env PILOT_OUT="$D" "$BIN" mutant "$m" "$D/journal.cx"
  red "mutant-$m" "$D/summary-mutant-$m.json"
done
# SHADOW also leaves no trace after restart: the restart cannot reproduce process 1's theta.
D="$OUT/mutant-SHADOW"
expect mutant-SHADOW-restart 1 -- env PILOT_OUT="$D" "$BIN" restart "$D/journal.cx" "$D/identity-mutant-SHADOW.json"
red mutant-SHADOW-restart "$D/summary-restart.json"

# TAMPER: in-memory cx_tamper must break cx_verify_chain; a flipped journal word must make
# the restart refuse the journal, start from empty knowledge, and still answer correctly.
D="$OUT/mutant-TAMPER"; mkdir -p "$D"; rm -f "$D"/journal*
cp "$OUT/run1/journal-after-pilot.cx" "$D/journal.cx"; cp "$OUT/run1/identity-pilot.json" "$D/"
expect mutant-TAMPER-file 0 -- "$BIN" tamper-file "$D/journal.cx"
expect mutant-TAMPER-restart 1 -- env PILOT_OUT="$D" "$BIN" restart "$D/journal.cx" "$D/identity-pilot.json"
red mutant-TAMPER-restart "$D/summary-restart.json"
w=$(field "$D/summary-restart.json" wrong_answers); rr=$(field "$D/summary-restart.json" restore_refused)
if [ "$w" = 0 ] && [ "$rr" = 1 ]; then note "OK   mutant-TAMPER refused journal, 0 wrong answers"
else note "FAIL mutant-TAMPER wrong_answers '$w' restore_refused '$rr'"; fails=$((fails+1)); fi

note "RESULT fails=$fails"
[ "$fails" = 0 ]
