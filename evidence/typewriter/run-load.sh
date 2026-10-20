#!/bin/sh
# L308 H2: time to bring one head (prose, 90% coverage, 35B) from NVMe into memory: scattered
# per-tensor O_DIRECT reads where the checkpoint keeps them vs one packed beat-order file, then a
# memory-to-memory copy into a resident region. The packed file is written first (not timed).
set -e
E=$(cd "$(dirname "$0")" && pwd)
sha256sum /home/drakestapleton/workspace/targets/sc-typewriter-b/release/typewriter > "$E/load/typewriter.sha256"
/home/drakestapleton/workspace/targets/sc-typewriter-b/release/typewriter load /home/drakestapleton/models/qwen3.6-35b-a3b-fp8-95a723d0 "$E/heads-0.90.json" prose /home/drakestapleton/workspace/targets/l308-packed-prose-0.90.bin 0 "$E/load/prep.json" 2> "$E/load/prep.log"
uptime > "$E/load/uptime-before.txt"
quiet-measure /home/drakestapleton/workspace/targets/sc-typewriter-b/release/typewriter load /home/drakestapleton/models/qwen3.6-35b-a3b-fp8-95a723d0 "$E/heads-0.90.json" prose /home/drakestapleton/workspace/targets/l308-packed-prose-0.90.bin 3 "$E/load/prose-0.90.json" 2> "$E/load/prose-0.90.log"
uptime > "$E/load/uptime-after.txt"
echo DONE >> "$E/load/prose-0.90.log"
