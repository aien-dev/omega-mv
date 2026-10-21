#!/bin/bash
# L312 the smallest full loop on the 35B host (Drake: the motor is the 35B): L306's golden (host oracle, prompt + 32 greedy ids
# + routes) -> my trace (step 0 = prefill, union head) -> plan (head per layer = union of routed experts over the steps) ->
# schedule + host proof -> run35: L306's exact 35B forward with every routed expert served from the ring the movement
# engines fill along the schedule. Gate: tokens and routes identical to the golden. Then movement alone on the same
# schedule (stand-in piston) for the bytes-per-beat receipt. Host only (CPU + NVMe); the chip piston is run-chip-piston.sh.
set -u
MODEL=${MODEL:-$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-35b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
GOLDEN=${GOLDEN:-$HOME/workspace/sc-engine/evidence/q36-host-20261020/gen-p1.json}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/35b-host-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}            # e.g. taskset -c 5-9,15-19
ENGINES=${ENGINES:-2}   # 1: NVMe -> cylinder; 2: NVMe engine -> staging -> RAM engine
BUFFERS=${BUFFERS:-2}
WORKERS=${WORKERS:-8}
NEED_KB=${NEED_KB:-62914560}   # 60 GiB free before a 35B host cell (L306 has first claim)
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
l306_wait() {
  while spark-window status 2>/dev/null | grep -qE 'window .*owner=L306 .*state=(open|held)' || [ "$(awk '/MemAvailable/{print $2}' /proc/meminfo)" -lt "$NEED_KB" ]; do sleep 60; done
  echo "memavail_kb $(awk '/MemAvailable/{print $2}' /proc/meminfo)"
}
log "model $MODEL bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) golden $GOLDEN out $OUT engines $ENGINES buffers $BUFFERS workers $WORKERS load $(cut -d' ' -f1-3 /proc/loadavg)"
cp "$GOLDEN" "$OUT/golden.json"
$BIN trace35 "$OUT/golden.json" "$MODEL" "$OUT/trace.json" 2>&1 | tee -a "$OUT/run.log" || { log "trace35 FAILED"; exit 1; }
$BIN plan trace "$OUT/trace.json" "$MODEL" "$OUT/plan.json" --buffers $BUFFERS 2>&1 | tail -1 | cut -c1-200 | tee -a "$OUT/run.log"
STEPS=$(jq '.routes|length' "$OUT/trace.json")
$BIN schedule "$OUT/plan.json" "$MANIFEST" "$STEPS" "$OUT/schedule.json" 2>&1 | tail -1 | cut -c1-220 | tee -a "$OUT/run.log" || { log "schedule FAILED"; exit 1; }
Q="$(quiet_wait) $(l306_wait)"
$RUN $BIN run35 "$OUT/schedule.json" "$MANIFEST" "$OUT/trace.json" "$MODEL" "$OUT/loop" --workers $WORKERS --engines $ENGINES > "$OUT/loop.log" 2>&1; rc=$?
log "run35 engines $ENGINES buffers $BUFFERS ($Q) rc $rc: $(jq -c '{identical_tokens, same, of, steps, routes_identical_steps, eager_expert_reads_at_open, misfires, failure}' $OUT/loop/gate.json 2>/dev/null) $(jq -c '{beats, gap:.gap_us.median, fill:.fill_us.median, fire:.fire_us.median, beat:.beat_us.median, spread:.beat_us.spread, bytes:.bytes_per_beat.median, gbps:.payload_gbps_during_fill, open_s:.open_seconds}' $OUT/loop/summary.json 2>/dev/null) $(tail -1 $OUT/loop.log | cut -c1-200)"
Q="$(quiet_wait)"
$RUN $BIN move "$OUT/schedule.json" "$MANIFEST" "$OUT/move" --workers $WORKERS --fire-us 2000 --engines $ENGINES --coupling brigade > "$OUT/move.log" 2>&1; rc=$?
log "move engines $ENGINES ($Q) rc $rc: $(jq -c '{beats, late_beats, gap:.gap_us.median, fill:.fill_us.median, beat:.beat_us.median, spread:.beat_us.spread, bytes_min:.bytes_per_beat.min, bytes_max:.bytes_per_beat.max, gbps:.payload_gbps_during_fill}' $OUT/move/summary.json 2>/dev/null) $(tail -1 $OUT/move.log | cut -c1-160)"
log "done"
