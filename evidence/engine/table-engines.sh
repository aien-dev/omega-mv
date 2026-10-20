#!/bin/bash
# L312: turn a 30b-engines-*/ or 35b-engines-*/ folder into the markdown table ENGINE.md section 4 shows.
# Columns straight from each cell's summary.json (medians unless named): beat time, its spread (p90/p10), ignition gap
# (fire start minus fill done), idle (piston waiting), fill, payload GB/s during fill, NVMe-engine and RAM-engine stage
# times and the hand-off wait between them (0 on the 1-engine path), late beats.
set -u
D=${1:?folder}
echo "| buffers | engines | coupling | beats | late | beat (median) | spread | ignition gap | idle | fill | GB/s in fill | NVMe stage | RAM stage | hand-off wait | load at start |"
echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
for f in "$D"/b*-e*-*/summary.json; do
  [ -f "$f" ] || continue
  cell=$(basename "$(dirname "$f")"); b=${cell#b}; b=${b%%-*}; e=${cell#*-e}; e=${e%%-*}; c=${cell##*-}
  load=$(grep -E "buffers $b engines $e $c \(load " "$D/run.log" | sed -E 's/.*\(load ([0-9.]+)\).*/\1/' | head -1)
  jq -r --arg b "$b" --arg e "$e" --arg c "$c" --arg l "${load:-?}" '
    def ms(x): if x == null then "-" else ((x/1000*100|round)/100|tostring) + " ms" end;
    def us(x): if x == null then "-" else ((x|round)|tostring) + " us" end;
    "| \($b) | \($e) | \($c) | \(.beats) | \(.late_beats) | \(ms(.beat_us.median)) | \((.beat_us.spread*100|round)/100) | \(us(.gap_us.median)) (p90 \(us(.gap_us.p90))) | \(ms(.idle_us.median)) | \(ms(.fill_us.median)) | \((.payload_gbps_during_fill*100|round)/100) | \(ms(.stages.nvme_us.median)) | \(ms(.stages.ram_us.median)) | \(us(.stages.handoff_wait_us.median)) | \($l) |"' "$f"
done
