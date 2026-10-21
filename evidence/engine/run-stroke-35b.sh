#!/bin/bash
# L312 H6 on the 35B host (Drake 2026-10-20: the motor is the 35B): N heads on one stroke. 32 short traces (the exact
# 35B forward one token at a time, experts from the shards), one union plan + schedule covering all 32, then strokes at
# N = 1,2,4,8,16,32 through the ring (every head gated against its own trace: tokens identical, fail closed), and the
# offline union-vs-N receipt. CPU + NVMe only; the GPU is not touched. Same method as run-stroke-30b.sh (history).
set -u
MODEL=${MODEL:-$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-35b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/35b-stroke-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}   # e.g. quiet-measure, or taskset -c 5-9,15-19
GEN=${GEN:-2}
POS=${POS:-12}   # the first 3 positions of every prompt are the chat template (identical routes); 12 leaves 9 that differ
NS=${NS:-"1 2 4 8 16 32"}
EXPERT_BYTES=${EXPERT_BYTES:-3146112}   # one 35B routed expert (gate+up+down fp8 + scales), from the manifest
mkdir -p "$OUT/traces"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
log "model $MODEL bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) out $OUT load $(cut -d' ' -f1-3 /proc/loadavg) memavail_kb $(grep MemAvailable /proc/meminfo | awk '{print $2}')"
i=0; LIST=""
while IFS= read -r prompt; do
  i=$((i+1)); f="$OUT/traces/t$(printf %02d $i).json"
  [ -s "$f" ] || $BIN trace35 "$MODEL" "$f" --prompt "$prompt" --gen "$GEN" --positions "$POS" > "$f.log" 2>&1 || { log "trace $i FAILED: $(tail -1 $f.log)"; exit 1; }
  LIST="$LIST${LIST:+,}$f"
done < "$HERE/prompts-32.txt"
log "traces done: $i, positions per trace: $(for f in $OUT/traces/t??.json; do jq '.argmax|length' $f; done | sort -n | tr '\n' ' '), host ms/token: $(for f in $OUT/traces/t??.json; do jq '.host_ns_per_token/1e6|round' $f; done | sort -n | sed -n '1p;$p' | tr '\n' ' ')"
$BIN union "$OUT/union.json" --positions $POS --experts 256 --expert-bytes $EXPERT_BYTES $(echo $LIST | tr ',' ' ') > "$OUT/union.log" 2>&1 && log "union: $(jq -c '[.rows[] | {n, mean:.measured_union.mean, indep:.independent_draw_expectation}]' "$OUT/union.json")" || { log "union FAILED: $(tail -1 "$OUT/union.log")"; exit 1; }
$BIN plan union "$MODEL" "$OUT/plan-union.json" --buffers 2 $(echo $LIST | tr ',' ' ') 2>&1 | tee -a "$OUT/run.log"
$BIN schedule "$OUT/plan-union.json" "$MANIFEST" $POS "$OUT/schedule-union.json" 2>&1 | tee -a "$OUT/run.log" || exit 1
for N in $NS; do
  Q=$(quiet_wait)
  $RUN $BIN stroke35 "$OUT/schedule-union.json" "$MANIFEST" "$MODEL" "$OUT/stroke-n$N" --n $N --traces "$LIST" --positions $POS --workers 8 > "$OUT/stroke-n$N.log" 2>&1; rc=$?
  log "stroke N=$N ($Q bin $(sha256sum "$BIN" | cut -c1-16)) rc $rc: $(jq -c '{beats,misfires,gap:.gap_us.median,fill:.fill_us.median,fire:.fire_us.median,beat:.beat_us.median,bytes:.bytes_per_beat.median,union:.union_experts_per_beat.mean,attn_us:.attn_us.median,moe_us:.moe_us.median,tok_s:.tokens_per_s_total,gate:.gate.identical_tokens_every_head,eager:.gate.eager_expert_reads_at_open}' "$OUT/stroke-n$N/summary.json" 2>/dev/null || tail -1 "$OUT/stroke-n$N.log")"
done
log "done"
