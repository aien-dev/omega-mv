#!/bin/bash
# L312 H4 head swap mid-run on the 30B host (movement engines + stand-in piston, like run-engines): head A (8 experts x
# 48 layers, fixed plan) is resident in host RAM before the run; from token 12 of 24 the tokens fire on head B (= head A
# with every expert id shifted by 8: a disjoint head of the same size). Lead = how many tokens before the swap the loader
# starts bringing head B into RAM: -1 (never: every B part comes from the disk), 0 (at the swap), 1, 2, 4.
# Receipt per lead: steady ms/token before the swap, extra ms on the first token after it, total extra the user sees,
# hit rate (parts served from RAM) after the swap, the first token after the swap served fully from RAM.
set -u
MODEL=${MODEL:-$HOME/models/qwen3-30b-a3b-instruct-2507-fp8}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-30b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/30b-swap-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}   # e.g. quiet-measure: holds the quiet lock exclusive per measured cell only
FIRE_US=${FIRE_US:-2000}
TOKENS=${TOKENS:-24}
SWAP_AT=${SWAP_AT:-12}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
log "model $MODEL bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) out $OUT fire_us $FIRE_US tokens $TOKENS swap_at $SWAP_AT load $(cut -d' ' -f1-3 /proc/loadavg)"
$BIN plan fixed "$MODEL" "$OUT/plan-a.json" --layers 48 --experts 8 --buffers 2 > /dev/null 2>&1 || { log "plan FAILED"; exit 1; }
for L in -1 0 1 2 4; do
  D="$OUT/lead$L"
  Q=$(quiet_wait)
  $RUN $BIN swap "$OUT/plan-a.json" shift:8 "$MANIFEST" "$D" --tokens $TOKENS --swap-at $SWAP_AT --lead $L --fire-us $FIRE_US --workers 8 > "$D.log" 2>&1; rc=$?
  log "lead $L ($Q) rc $rc: $(jq -c '{steady_ms:.steady_token_ms_before_swap, first_extra_ms:.first_token_after_swap_extra_ms, total_extra_ms:.user_visible_extra_ms_total, hit_before:.hit_rate_before_swap, hit_after:.hit_rate_after_swap, first_all_hit_token:.first_all_hit_token_after_swap, b_load_ms:.head_b_load_ms, b_start_vs_swap_ms:.head_b_load_start_vs_swap_ms, gap:.gap_us.median, beat:.beat_us.median}' $D/summary.json 2>/dev/null) $(tail -1 $D.log | cut -c1-160)"
done
log "done"
