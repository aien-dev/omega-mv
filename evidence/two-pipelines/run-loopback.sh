#!/bin/bash
# L258 step 2: the 35B cut in two stages, two processes on the Spark over loopback TCP. Stage 0 fires layers 0..K
# (embedding + its layers), stage 1 fires K+1..39 (+ lm_head) and sends the token back. Both run the motor (experts
# from the heartbeat ring along each stage's own schedule; the boundary is the schedule's `stage` field).
# Cells: Spark alone (one process, same code, same pass order) and the split, with 1 and 4 conversations in flight;
# then the wrong-boundary mutants (stage 1 told it starts at K instead of K+1): with the check on it must refuse at
# hello, with the check off the token gate must go red. Gate = tokens + routes identical to L306's goldens.
set -u
MODEL=${MODEL:-$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-35b-manifest.json}
BIN=${BIN:-$HOME/workspace/sc-two-pipelines/target/release/engine}
G=${G:-$HOME/workspace/sc-two-pipelines/evidence/q36-host-20261020}
GOLDENS=${GOLDENS:-$G/gen-p1.json,$G/gen-p2.json,$G/gen-p3.json,$G/gen-p4.json}
K=${K:-19}
STEPS=${STEPS:-32}
MSTEPS=${MSTEPS:-4}
SOURCE=${SOURCE:-ring}
CELLS=${CELLS:-"alone-1 split-1 alone-4 split-4 mutant-checked mutant-unchecked"}
NEED_KB=${NEED_KB:-62914560}   # 60 GiB free before a 35B host cell (L306 has first claim)
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/loopback-$(date -u +%Y%m%dT%H%MZ)}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
mem_wait() {
  while spark-window status 2>/dev/null | grep -qE 'window .*owner=L306 .*state=(open|held)' || [ "$(awk '/MemAvailable/{print $2}' /proc/meminfo)" -lt "$NEED_KB" ]; do sleep 60; done
  echo "memavail_kb $(awk '/MemAvailable/{print $2}' /proc/meminfo) load $(cut -d' ' -f1-3 /proc/loadavg)"
}
common="--model $MODEL --manifest $MANIFEST --goldens $GOLDENS --source $SOURCE"
log "bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) model $MODEL K $K steps $STEPS source $SOURCE goldens $GOLDENS"
cell() {  # name convs steps first1 extra1
  local name=$1 convs=$2 steps=$3 first1=$4 extra=${5:-}
  local d=$OUT/$name
  mkdir -p "$d"
  log "$name: $(mem_wait)"
  if [ "${name%%-*}" = alone ]; then
    quiet-measure $BIN stage35 $common --index 0 --of 1 --first 0 --last 39 --convs $convs --steps $steps --out $d/s0 > $d/s0.log 2>&1
    log "$name rc $?: $(tail -1 $d/s0.log | cut -c1-200)"
    $BIN pipe-report $d/report $steps $d/s0 --goldens $GOLDENS > $d/report.log 2>&1; log "$name report rc $?: $(tail -1 $d/report.log)"
    return
  fi
  local p0=$((47300 + RANDOM % 500)) p1=$((47900 + RANDOM % 500))
  quiet-measure bash -c "
    $BIN stage35 $common --index 1 --of 2 --first $first1 --last 39 --convs $convs --steps $steps --prev 127.0.0.1:$p1 --next 127.0.0.1:$p0 $extra --out $d/s1 > $d/s1.log 2>&1 &
    $BIN stage35 $common --index 0 --of 2 --first 0 --last $K --convs $convs --steps $steps --prev 127.0.0.1:$p0 --next 127.0.0.1:$p1 --out $d/s0 > $d/s0.log 2>&1
    echo s0 rc \$?; wait; echo s1 rc \$?" > $d/cell.log 2>&1
  log "$name: $(tr '\n' ' ' < $d/cell.log) | s0: $(tail -1 $d/s0.log | cut -c1-200) | s1: $(tail -1 $d/s1.log | cut -c1-200)"
  $BIN pipe-report $d/report $steps $d/s0 $d/s1 --goldens $GOLDENS > $d/report.log 2>&1; log "$name report rc $?: $(tail -1 $d/report.log)"
}
for c in $CELLS; do
  case $c in
    alone-1) cell alone-1 1 $STEPS 0 ;;
    alone-4) cell alone-4 4 $STEPS 0 ;;
    split-1) cell split-1 1 $STEPS $((K + 1)) ;;
    split-4) cell split-4 4 $STEPS $((K + 1)) ;;
    mutant-checked) cell mutant-checked 1 $MSTEPS $K ;;
    mutant-unchecked) cell mutant-unchecked 1 $MSTEPS $K --no-boundary-check ;;
  esac
done
log done
