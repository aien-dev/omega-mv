#!/bin/bash
# L312 H4 head swap mid-run on the 35B host (movement engines + stand-in piston): head A = L308's measured prose head
# (90 percent coverage, evidence/engine/35b-plan-l308/plan-prose.json) resident in host RAM before the run; from token
# SWAP_AT of TOKENS every beat fires on head B = the measured code head. Lead = tokens before the swap at which the loader
# starts bringing head B into RAM: -1 (never: every B part comes from the disk), 0, 1, 2, 4. Receipt per lead: steady
# ms/token before the swap, extra ms on the first token after it, total extra the user sees, hit rate after the swap.
# Memory: both heads resident = 25.1 GB (3236 + 4729 parts x 3.146 MB); L306 has first claim on the box, so a cell starts
# only when no L306 window is open and MemAvailable leaves 60 GiB after our heads.
set -u
MODEL=${MODEL:-$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-35b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/35b-swap-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}   # e.g. taskset -c 5-9,15-19 (X925 cores) or quiet-measure
FIRE_US=${FIRE_US:-2000}
TOKENS=${TOKENS:-24}
SWAP_AT=${SWAP_AT:-12}
PLAN_A=${PLAN_A:-$HERE/35b-plan-l308/plan-prose.json}
PLAN_B=${PLAN_B:-$HERE/35b-plan-l308/plan-code.json}
NEED_KB=${NEED_KB:-88000000}   # 25.1 GB heads + 60 GiB left for L306
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
l306_wait() {  # L306's chip gate loads the whole 35B: never start a 35B host cell while its window is open or memory is short
  while spark-window status 2>/dev/null | grep -qE 'window .*owner=L306 .*state=(open|held)' || [ "$(awk '/MemAvailable/{print $2}' /proc/meminfo)" -lt "$NEED_KB" ]; do sleep 60; done
  echo "memavail_kb $(awk '/MemAvailable/{print $2}' /proc/meminfo)"
}
log "model $MODEL bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) out $OUT fire_us $FIRE_US tokens $TOKENS swap_at $SWAP_AT plan_a $PLAN_A plan_b $PLAN_B load $(cut -d' ' -f1-3 /proc/loadavg)"
for L in -1 0 1 2 4; do
  D="$OUT/lead$L"
  Q="$(quiet_wait) $(l306_wait)"
  $RUN $BIN swap "$PLAN_A" "$PLAN_B" "$MANIFEST" "$D" --tokens $TOKENS --swap-at $SWAP_AT --lead $L --fire-us $FIRE_US --workers 8 > "$D.log" 2>&1; rc=$?
  log "lead $L ($Q bin $(sha256sum "$BIN" | cut -c1-16)) rc $rc: $(jq -c '{steady_ms:.steady_token_ms_before_swap, first_extra_ms:.first_token_after_swap_extra_ms, total_extra_ms:.user_visible_extra_ms_total, hit_before:.hit_rate_before_swap, hit_after:.hit_rate_after_swap, first_all_hit_token:.first_all_hit_token_after_swap, b_load_ms:.head_b_load_ms, b_start_vs_swap_ms:.head_b_load_start_vs_swap_ms, gap:.gap_us.median, beat:.beat_us.median}' $D/summary.json 2>/dev/null) $(tail -1 $D.log | cut -c1-160)"
done
log "done"
