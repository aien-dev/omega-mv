#!/bin/bash
# L312 H6 on the 30B host: N heads on one stroke. 32 short traces (teacher-forced host pass through the ring), one
# union plan + schedule covering all 32, then strokes at N = 1,2,4,8,16,32 (every head gated against its own trace),
# and the offline union-vs-N receipt. CPU + NVMe only.
set -u
MODEL=${MODEL:-$HOME/models/qwen3-30b-a3b-instruct-2507-fp8}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-30b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/30b-stroke-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}   # e.g. quiet-measure: holds the quiet lock exclusive per measured cell only
GEN=${GEN:-2}
POS=${POS:-8}
mkdir -p "$OUT/traces"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
log "model $MODEL bin $BIN out $OUT load $(cut -d' ' -f1-3 /proc/loadavg) memavail_kb $(grep MemAvailable /proc/meminfo | awk '{print $2}')"
i=0; LIST=""
while IFS= read -r prompt; do
  i=$((i+1)); f="$OUT/traces/t$(printf %02d $i).json"
  [ -s "$f" ] || $BIN trace "$MODEL" "$MANIFEST" "$f" --prompt "$prompt" --gen "$GEN" --slots 512 > "$f.log" 2>&1 || { log "trace $i FAILED: $(tail -1 $f.log)"; exit 1; }
  LIST="$LIST${LIST:+,}$f"
done < "$HERE/prompts-32.txt"
log "traces done: $i, positions per trace: $(for f in $OUT/traces/t*.json; do jq '.argmax|length' $f; done | sort -n | tr '\n' ' ')"
$BIN union "$OUT/union.json" --positions $POS $(echo $LIST | tr ',' ' ') > "$OUT/union.log" 2>&1 && log "union: $(jq -c '[.rows[] | {n, mean:.measured_union.mean, indep:.independent_draw_expectation}]' $OUT/union.json)"
$BIN plan union "$MODEL" "$OUT/plan-union.json" --buffers 2 $(echo $LIST | tr ',' ' ') 2>&1 | tee -a "$OUT/run.log"
$BIN schedule "$OUT/plan-union.json" "$MANIFEST" $POS "$OUT/schedule-union.json" 2>&1 | tee -a "$OUT/run.log" || exit 1
for N in 1 2 4 8 16 32; do
  Q=$(quiet_wait)
  $RUN $BIN stroke "$OUT/schedule-union.json" "$MANIFEST" "$MODEL" "$OUT/stroke-n$N" --n $N --traces "$LIST" --positions $POS --workers 8 > "$OUT/stroke-n$N.log" 2>&1; rc=$?
  log "stroke N=$N ($Q) rc $rc: $(jq -c '{beats,misfires,gap:.gap_us.median,fill:.fill_us.median,fire:.fire_us.median,beat:.beat_us.median,bytes:.bytes_per_beat.median,union:.union_experts_per_beat.mean,tps:.tokens_per_s_total,gate:.gate.identical_tokens_every_head}' $OUT/stroke-n$N/summary.json 2>/dev/null) $(tail -1 $OUT/stroke-n$N.log | cut -c1-160)"
done
log "done"
