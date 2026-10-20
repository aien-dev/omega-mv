#!/bin/bash
# L312 host receipts on the 30B (qwen3-30b-a3b-instruct-2507-fp8): schedule proof, movement engine alone,
# both engines together on the host, today's path agreement. CPU + NVMe only (no GPU lock needed).
# Binary: sc feat/typewriter-engine crates/aien-engine (cargo build --release -p aien-engine).
set -u
MODEL=${MODEL:-$HOME/models/qwen3-30b-a3b-instruct-2507-fp8}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-30b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
OUT=${OUT:-$(dirname "$0")/30b-host-$(date -u +%Y%m%dT%H%MZ)}
PROMPT=${PROMPT:-"Explain in three sentences how a four-stroke engine keeps its pistons in time."}
GEN=${GEN:-40}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
log "model $MODEL bin $BIN out $OUT load $(cut -d' ' -f1-3 /proc/loadavg) memavail_kb $(grep MemAvailable /proc/meminfo | awk '{print $2}')"
[ -f "$MANIFEST" ] || { log "manifest build"; $BIN manifest "$MODEL" "$MANIFEST" 2>&1 | tee -a "$OUT/run.log"; }
log "trace (host reference through the ring, routes + argmax)"
$BIN trace "$MODEL" "$MANIFEST" "$OUT/trace.json" --prompt "$PROMPT" --gen "$GEN" --slots 512 2>&1 | tee -a "$OUT/run.log" || exit 1
POS=$(jq '.argmax | length' "$OUT/trace.json")
log "plan from trace (head = union of routed experts per layer over $POS positions)"
$BIN plan trace "$OUT/trace.json" "$MODEL" "$OUT/plan-trace.json" --buffers 2 2>&1 | tee -a "$OUT/run.log" || exit 1
log "schedule + host check"
$BIN schedule "$OUT/plan-trace.json" "$MANIFEST" "$POS" "$OUT/schedule-trace.json" 2>&1 | tee -a "$OUT/run.log" || exit 1
log "movement engine alone, fixed head 8 experts x 48 layers, buffers 2, beat 0/2000/4000 us"
$BIN plan fixed "$MODEL" "$OUT/plan-fixed8.json" --layers 48 --experts 8 --buffers 2 2>&1 | tee -a "$OUT/run.log"
$BIN schedule "$OUT/plan-fixed8.json" "$MANIFEST" 40 "$OUT/schedule-fixed8.json" 2>&1 | tee -a "$OUT/run.log" || exit 1
for us in 0 2000 4000; do
  $BIN move "$OUT/schedule-fixed8.json" "$MANIFEST" "$OUT/move-b2-beat${us}us" --workers 8 --beat-us $us > "$OUT/move-b2-beat${us}us.log" 2>&1 && log "move beat ${us}us: $(jq -c '{beats,misfires,late_beats,gap_us:.gap_us.median,fill_us:.fill_us.median,beat_us:.beat_us.median,gbps:.payload_gbps_during_fill}' "$OUT/move-b2-beat${us}us/summary.json")"
done
$BIN plan fixed "$MODEL" "$OUT/plan-fixed8-b3.json" --layers 48 --experts 8 --buffers 3 >/dev/null 2>&1
$BIN schedule "$OUT/plan-fixed8-b3.json" "$MANIFEST" 40 "$OUT/schedule-fixed8-b3.json" > /dev/null 2>&1 && \
$BIN move "$OUT/schedule-fixed8-b3.json" "$MANIFEST" "$OUT/move-b3-beat2000us" --workers 8 --beat-us 2000 > "$OUT/move-b3-beat2000us.log" 2>&1 && log "move b3 beat 2000us: $(jq -c '{beats,late_beats,gap_us:.gap_us.median,fill_us:.fill_us.median,beat_us:.beat_us.median}' "$OUT/move-b3-beat2000us/summary.json")"
log "both engines on the host (identical tokens gate vs the trace)"
$BIN run "$OUT/schedule-trace.json" "$MANIFEST" "$OUT/trace.json" "$MODEL" "$OUT/loop-host" --workers 8 > "$OUT/loop-host.log" 2>&1; rc=$?
log "loop rc $rc: $(tail -1 "$OUT/loop-host.log")"
log "today's path (18091) on the same prompt"
$BIN today http://127.0.0.1:18091/v1/chat/completions "$MODEL" "$OUT/trace.json" "$OUT/today.json" > "$OUT/today.log" 2>&1 && log "today: $(jq -c '{same_prefix_ids,of,today_ms}' "$OUT/today.json")"
log "done"
