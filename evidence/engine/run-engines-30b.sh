#!/bin/bash
# L312 engine count + coupling on the 30B host, movement engines alone (stand-in piston holds FIRE_US per beat):
# engines 1 (NVMe -> cylinder) vs 2 (NVMe engine -> staging pair -> RAM engine -> cylinder), coupling brigade | clock |
# reservoir, buffers 2 and 3. Same schedule (8 experts x 48 layers x 40 tokens = 1920 beats) for every cell.
set -u
MODEL=${MODEL:-$HOME/models/qwen3-30b-a3b-instruct-2507-fp8}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-30b-manifest.json}
BIN=${BIN:-$HOME/workspace/targets/sc-engine/release/engine}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/30b-engines-$(date -u +%Y%m%dT%H%MZ)}
RUN=${RUN:-}   # e.g. quiet-measure: holds the quiet lock exclusive per measured cell only
FIRE_US=${FIRE_US:-2000}
PERIOD_US=${PERIOD_US:-10000}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
. "$HERE/quiet-wait.sh"
log "model $MODEL bin $BIN out $OUT fire_us $FIRE_US period_us $PERIOD_US load $(cut -d' ' -f1-3 /proc/loadavg)"
for B in 2 3; do
  $BIN plan fixed "$MODEL" "$OUT/plan-b$B.json" --layers 48 --experts 8 --buffers $B > /dev/null 2>&1
  $BIN schedule "$OUT/plan-b$B.json" "$MANIFEST" 40 "$OUT/schedule-b$B.json" 2>&1 | tail -1 | cut -c1-200 | tee -a "$OUT/run.log"
  for E in 1 2; do for C in brigade clock reservoir; do
    D="$OUT/b$B-e$E-$C"
    Q=$(quiet_wait)
    $RUN $BIN move "$OUT/schedule-b$B.json" "$MANIFEST" "$D" --workers 8 --fire-us $FIRE_US --engines $E --coupling $C --period-us $PERIOD_US > "$D.log" 2>&1; rc=$?
    log "buffers $B engines $E $C ($Q) rc $rc: $(jq -c '{beats,late_beats,gap:.gap_us.median,gap_p90:.gap_us.p90,idle:.idle_us.median,fill:.fill_us.median,beat:.beat_us.median,spread:.beat_us.spread,gbps:.payload_gbps_during_fill,nvme:.stages.nvme_us.median,ram:.stages.ram_us.median,handoff:.stages.handoff_wait_us.median}' $D/summary.json 2>/dev/null) $(tail -1 $D.log | cut -c1-120)"
  done; done
done
log "done"
