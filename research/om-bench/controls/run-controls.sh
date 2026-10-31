#!/bin/sh
# om-bench (#653) negative and positive controls for the driver and for gate.om.
# Each control asserts its diagnostic text, not only an exit status.
# Usage: run-controls.sh OUTDIR   (OUTDIR must not exist)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BENCH=$HERE/../bench
REPO=${OMB_REPO:-$(cd "$HERE/../../.." && pwd)}
OMC=$REPO/seed/out/omc
SHA=2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f
OUT=$1
[ -e "$OUT" ] && { echo "OUTDIR exists"; exit 91; }
mkdir -p "$OUT"
FAILS=0
ok()  { echo "CONTROL PASS $1"; }
bad() { echo "CONTROL FAIL $1"; FAILS=$((FAILS+1)); }

# Task tree: one split "ctl", each task a copy of a public dev task.
T=$OUT/tasks/ctl
mkdir -p "$T"
mk() { cp -r "$BENCH/pubtasks/dev/$2" "$T/$1"; }
for n in ref mut empty trap slow e13 authz nocode; do mk ctl-fibo-$n fibo.v1; done
for n in ref mut; do mk ctl-brk-$n brackets.v1; done

R=$OUT/run-om
place() {  # place TID SRCFILE LANG
  d=$R/cand/$1; mkdir -p "$d"
  case $3 in om) f=prog.om;; py) f=prog.py;; c) f=prog.c;; esac
  cp "$2" "$d/$f"
  s=$(sha256sum "$2" | cut -d' ' -f1)
  printf '{"tid":"%s","code_sha256":"%s","attempts":[]}\n' "$1" "$s" > "$d/gen.json"
}
for n in ref mut empty trap slow e13 authz; do place ctl-fibo-$n "$HERE/fibo-$n.om" om; done
for n in ref mut; do place ctl-brk-$n "$HERE/brk-$n.om" om; done
mkdir -p "$R/cand/ctl-fibo-nocode"
printf '{"tid":"ctl-fibo-nocode","code_sha256":null,"attempts":[]}\n' > "$R/cand/ctl-fibo-nocode/gen.json"

export OMB_TASKROOT=$OUT/tasks
python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/judge-om.txt" 2>&1
echo "judge rc=$?" >> "$OUT/judge-om.txt"
expect() {  # expect RUN TID CLASS
  got=$(python3 -c "import json,sys
for l in open(sys.argv[1]):
  r=json.loads(l)
  if r['tid']==sys.argv[2]: print(r['class'])" "$1/verdicts-ctl.jsonl" "$2")
  [ "$got" = "$3" ] && ok "$2 -> $3" || bad "$2 expected $3 got '$got'"
}
expect "$R" ctl-fibo-ref pass
expect "$R" ctl-brk-ref pass
expect "$R" ctl-fibo-mut wrong_output
expect "$R" ctl-brk-mut wrong_output
expect "$R" ctl-fibo-empty runtime_mechanical
expect "$R" ctl-fibo-trap runtime_mechanical
expect "$R" ctl-fibo-slow runtime_mechanical
expect "$R" ctl-fibo-e13 compile_refused
expect "$R" ctl-fibo-authz authz_violation
expect "$R" ctl-fibo-nocode no_code

# gate.om judges the same Omega candidates; crosscheck must agree case by case.
"$OMC" < "$REPO/seed/qualification/om2/gate.om" > "$OUT/gate" && chmod +x "$OUT/gate" || bad "gate build"
python3 "$BENCH/ombench.py" gatelist --run "$R" --split ctl > /dev/null
(cd "$REPO" && "$OUT/gate" "$OMC" "$SHA" "$R/gate-ctl.cases" "$OUT/gate-scratch" > "$OUT/gate-ctl.log" 2>&1)
echo "gate rc=$?" >> "$OUT/gate-ctl.log"
grep -q '^OM2GATE ' "$OUT/gate-ctl.log" && ok "gate final line present" || bad "gate final line missing"
python3 "$BENCH/ombench.py" crosscheck --run "$R" --split ctl --gate-log "$OUT/gate-ctl.log" > "$OUT/crosscheck.txt" 2>&1
rc=$?
grep -q 'crosscheck agree=[1-9][0-9]* disagree=0' "$OUT/crosscheck.txt" && [ $rc -eq 0 ] \
  && ok "gate.om and comparator agree" || bad "crosscheck rc=$rc $(head -3 "$OUT/crosscheck.txt")"

# gate.om identity guard: a wrong compiler hash must make the gate INVALID with every case NOT_RUN.
(cd "$REPO" && "$OUT/gate" "$OMC" 0000000000000000000000000000000000000000000000000000000000000000 \
  "$R/gate-ctl.cases" "$OUT/gate-badsha" > "$OUT/gate-badsha.log" 2>&1)
rc=$?
grep -q '^OM2GATE INVALID' "$OUT/gate-badsha.log" && [ $rc -eq 3 ] && ok "wrong omc sha -> INVALID" \
  || bad "wrong omc sha rc=$rc"

# Denominator guard: a task with no candidate record is NOT_RUN and the judge exits 2, never a pass rate.
R2=$OUT/run-missing; mkdir -p "$R2/cand"
cp -r "$R/cand/ctl-fibo-ref" "$R2/cand/"
python3 "$BENCH/ombench.py" judge --run "$R2" --split ctl --arm a > "$OUT/judge-missing.txt" 2>&1
rc=$?
grep -q '^NOT_RUN 9 of 10' "$OUT/judge-missing.txt" && [ $rc -eq 2 ] && ! grep -q '^judged' "$OUT/judge-missing.txt" \
  && ok "missing candidates -> NOT_RUN 9 of 10, rc=2" || bad "denominator guard rc=$rc"

# Corpus guards: a task without cases or without a hidden output file is a CORPUS FAULT.
mkdir -p "$OUT/tasks-bad/ctl"; cp -r "$T/ctl-fibo-ref" "$OUT/tasks-bad/ctl/zz-nocases"
printf '{"cases":[]}\n' > "$OUT/tasks-bad/ctl/zz-nocases/meta.json"
OMB_TASKROOT=$OUT/tasks-bad python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/fault1.txt" 2>&1
rc=$?
grep -q 'CORPUS FAULT \[zz-nocases\] no cases' "$OUT/fault1.txt" && [ $rc -eq 1 ] && ok "empty task -> CORPUS FAULT" \
  || bad "empty task guard rc=$rc"
rm -r "$OUT/tasks-bad/ctl/zz-nocases"; cp -r "$T/ctl-fibo-ref" "$OUT/tasks-bad/ctl/zz-noout"
rm "$OUT/tasks-bad/ctl/zz-noout/05.out"
OMB_TASKROOT=$OUT/tasks-bad python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/fault2.txt" 2>&1
rc=$?
grep -q 'CORPUS FAULT \[zz-noout\] case 5 missing .out' "$OUT/fault2.txt" && [ $rc -eq 1 ] \
  && ok "missing expected output -> CORPUS FAULT" || bad "missing output guard rc=$rc"

# Python and C arms: correct references pass, a planted wrong program fails, a forbidden import is flagged.
T3=$OUT/tasks3/ctl; mkdir -p "$T3"
for n in ref wrong authz; do cp -r "$BENCH/pubtasks/dev/fibo.v1" "$T3/ctl-fibo-$n"; done
for L in py c; do
  R3=$OUT/run-$L; arm=f$L; R=$R3
  for n in ref wrong authz; do place ctl-fibo-$n "$HERE/fibo-$n.$L" $L; done
  OMB_TASKROOT=$OUT/tasks3 python3 "$BENCH/ombench.py" judge --run "$R3" --split ctl --arm $arm > "$OUT/judge-$L.txt" 2>&1
  expect "$R3" ctl-fibo-ref pass
  expect "$R3" ctl-fibo-wrong wrong_output
  expect "$R3" ctl-fibo-authz authz_violation
done

echo "CONTROLS FAILS=$FAILS"
[ $FAILS -eq 0 ]
