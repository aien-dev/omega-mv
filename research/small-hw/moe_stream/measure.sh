#!/usr/bin/env bash
# L273 timed probe. Run as: quiet-measure flock /tmp/aien-gb10.lock ./measure.sh OUT_DIR
# M1: 2 prompts x 64 new words: ref, then streamed with the Pi 5 8 GB cache (1995 slots = 9.41474 GB of FP8 slots,
#     calc:9e9f4602) uncapped, 900 MB/s (Pi NVMe Gen3 class), 450 MB/s (Pi NVMe Gen2 class).
# M2: 1 prompt x 16 new words: ref, then the same cache at 96 MB/s (the Pi SD card read we measured).
set -u
OUT=${1:?out dir}
B=${MOE_STREAM:-$HOME/workspace/targets/small-hw/target/release/moe_stream}
CK=${CKPT:-$HOME/models/qwen3-30b-a3b-instruct-2507-fp8}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
{ date -u +%FT%TZ; uptime; grep -E "MemAvailable" /proc/meminfo; taskset -p $$; sha256sum "$B"; } > "$OUT/env.txt" 2>&1
"$B" run --checkpoint "$CK" --prompts "$HERE/prompts.txt" --new 64 --out "$OUT/m1" \
  --modes ref,stream:0:9.41474,stream:900:9.41474,stream:450:9.41474 > "$OUT/m1.log" 2>&1
echo "m1 rc=$?" >> "$OUT/env.txt"
tail -n 1 "$HERE/prompts.txt" > "$OUT/m2-prompt.txt"
"$B" run --checkpoint "$CK" --prompts "$OUT/m2-prompt.txt" --new 16 --out "$OUT/m2" \
  --modes ref,stream:96:9.41474 > "$OUT/m2.log" 2>&1
echo "m2 rc=$?" >> "$OUT/env.txt"
{ date -u +%FT%TZ; uptime; grep -E "MemAvailable" /proc/meminfo; } >> "$OUT/env.txt"
