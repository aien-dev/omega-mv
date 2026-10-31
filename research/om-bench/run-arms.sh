#!/bin/sh
# om-bench (#653) scored run: generate, judge, gate.om cross-judge (Omega arms), publishable summary.
# Usage: run-arms.sh RUNROOT SPLIT ARM [ARM...]     e.g. run-arms.sh ~/handoffs/OmegaEval/runs/v1 heldA a fpy fc b
# Writes RUNROOT/ARM-SPLIT/{cand/, gen.log, verdicts-SPLIT.jsonl, gate-SPLIT.cases, gate.log, crosscheck.txt,
# summary-SPLIT.json, status.txt}. gen skips a task that already has gen.json, so a run interrupted by a host
# fault resumes without regenerating; such a resume is recorded in status.txt by the operator, never silent.
# Run on the Spark, pinned with taskset by the caller; not in CI.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BENCH=$HERE/bench
REPO=${OMB_REPO:-$(cd "$HERE/../.." && pwd)}
OMC=$REPO/seed/out/omc
SHA=2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f
ROOT=$1; SPLIT=$2; shift 2
mkdir -p "$ROOT"
GATE=$ROOT/gate
if [ ! -x "$GATE" ]; then
  "$OMC" < "$REPO/seed/qualification/om2/gate.om" > "$GATE" && chmod +x "$GATE" || { echo "gate build failed"; exit 1; }
fi
for ARM in "$@"; do
  R=$ROOT/$ARM-$SPLIT
  mkdir -p "$R"
  S=$R/status.txt
  echo "start $(date -u +%FT%TZ) arm=$ARM split=$SPLIT omc=$SHA" >> "$S"
  python3 "$BENCH/ombench.py" gen --arm "$ARM" --split "$SPLIT" --run "$R"
  echo "gen rc=$? $(date -u +%FT%TZ)" >> "$S"
  python3 "$BENCH/ombench.py" judge --run "$R" --split "$SPLIT" --arm "$ARM" > "$R/judge.txt" 2>&1
  echo "judge rc=$? $(tail -1 "$R/judge.txt")" >> "$S"
  case $ARM in
    a|b|aeq)
      python3 "$BENCH/ombench.py" gatelist --run "$R" --split "$SPLIT" > /dev/null
      rm -rf "$R/gate-scratch"
      (cd "$REPO" && "$GATE" "$OMC" "$SHA" "$R/gate-$SPLIT.cases" "$R/gate-scratch" > "$R/gate.log" 2>&1)
      echo "gate rc=$? $(grep '^OM2GATE ' "$R/gate.log" | tail -1)" >> "$S"
      rm -rf "$R/gate-scratch"
      python3 "$BENCH/ombench.py" crosscheck --run "$R" --split "$SPLIT" --gate-log "$R/gate.log" > "$R/crosscheck.txt" 2>&1
      echo "crosscheck rc=$? $(head -1 "$R/crosscheck.txt")" >> "$S"
      ;;
  esac
  python3 "$BENCH/ombench.py" summary --run "$R" --split "$SPLIT" --arm "$ARM" > /dev/null
  echo "summary rc=$? end $(date -u +%FT%TZ)" >> "$S"
  cat "$S"
done
