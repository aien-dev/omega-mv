#!/bin/bash
# tools/run_gpu_roll_joules.sh EVIDENCE_DIR: omega-mv#580 step 5, joules per job from the UPS over 60 s runs.
# Call through ~/.local/bin/quiet-measure; this script takes the GB10 lock itself and holds it for about 4 minutes:
# 20 s idle (lock held, nothing of ours running), 60 s one-note engine, 20 s idle, 60 s roll, 20 s idle, 60 s chord.
# Each run is wrapped by ~/.local/bin/spark-receipt (--idle 20 = the 20 s just before it).
set -u
E="${1:?evidence dir}"; SECS="${2:-60}"
mkdir -p "$E"
cd "$(dirname "$0")/.." || exit 2
D=$(date +%Y%m%dT%H%M)
echo "# joules queued $(date -Is) head=$(git rev-parse --short HEAD)" > "$E/joules.log"
exec 9>/tmp/aien-gb10.lock
flock -w 3000 9 || { echo "# lock wait timed out" >> "$E/joules.log"; exit 3; }
{
  echo "# lock acquired $(date -Is) loadavg=$(cut -d' ' -f1-3 /proc/loadavg) MemAvailable_kB=$(awk '/MemAvailable/{print $2}' /proc/meminfo)"
  for what in note roll chord; do
    sleep 20
    nice -n 10 "$HOME/.local/bin/spark-receipt" "omega580-$what-$D" --idle 20 -- ./build/gpu_roll_test --joules "$SECS" --what "$what"
    echo "# $what exit=$? $(date -Is)"
    cp "$HOME/yardsticks/receipts/$(date +%F)-omega580-$what-$D.json" "$E/" 2>/dev/null
  done
} >> "$E/joules.log" 2>&1
exec 9>&-
echo "# done $(date -Is)" >> "$E/joules.log"
