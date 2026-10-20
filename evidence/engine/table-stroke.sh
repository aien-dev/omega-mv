#!/bin/bash
# L312: a 30b-stroke-*/ folder -> the markdown table for ENGINE.md section 5 (one row per N heads on a stroke).
set -u
D=${1:?folder}
echo "| N heads | beats | misfires | identical tokens every head | expert union per beat (mean) | bytes per beat (median) | fill | fire | beat | ignition gap | tokens/s total |"
echo "|---|---|---|---|---|---|---|---|---|---|---|"
for N in 1 2 4 8 16 32; do
  f="$D/stroke-n$N/summary.json"; [ -f "$f" ] || continue
  jq -r --arg n "$N" 'def ms(x): if x == null then "-" else ((x/1000*100|round)/100|tostring) + " ms" end;
    "| \($n) | \(.beats) | \(.misfires) | \(.gate.identical_tokens_every_head) | \((.union_experts_per_beat.mean*10|round)/10) | \((.bytes_per_beat.median/1e6*10|round)/10) MB | \(ms(.fill_us.median)) | \(ms(.fire_us.median)) | \(ms(.beat_us.median)) | \(ms(.gap_us.median)) | \((.tokens_per_s_total*100|round)/100) |"' "$f"
done
[ -f "$D/union.json" ] && { echo; echo "Expert union vs N (measured mean vs independent-draw expectation):"; jq -r '.rows[] | "N=\(.n): measured \((.measured_union.mean*10|round)/10), independent \((.independent_draw_expectation*10|round)/10)"' "$D/union.json"; }
