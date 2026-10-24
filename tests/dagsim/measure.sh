#!/bin/sh
# measure.sh PROBE_BIN EVIDENCE_DIR [SUBDIR]   (KS="1 8" env selects thread counts; PIN="5-9,15-19" pins with taskset)
# Runs the held-out memcpy rows under the GB10 lock, held only for the
# measurement (about 20 s total). Wrap the call in quiet-measure so builds
# drain first. If the GB10 lock is not free within 20 minutes, the
# spark-measure rule applies: run anyway and record the load (the header
# lines of every CSV carry it).
set -u
PROBE=${1:?probe}; EV=${2:?evidence dir}; SUB=${3:-measured}; KS=${KS:-2 3 5 6 4 16}
mkdir -p "$EV/$SUB"
runs() {
  for k in $KS; do
    # 12 GiB headroom rule (spark-measure): the probe takes 2 GB, so wait for 14.5 GiB, up to 10 min, else skip.
    i=0
    while [ $i -lt 60 ]; do
      avail=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
      [ "$avail" -ge 15204352 ] && break
      sleep 10; i=$((i+1))
    done
    if [ "$avail" -lt 15204352 ]; then echo "# SKIPPED k=$k: MemAvailable ${avail} kB under the 12 GiB headroom rule plus 2 GB for the probe" > "$EV/$SUB/memcpy_k$k.csv"; continue; fi
    if [ -n "${PIN:-}" ]; then nice -n 10 taskset -c "$PIN" "$PROBE" 1 $k 10 > "$EV/$SUB/memcpy_k$k.csv" 2>&1
    else nice -n 10 "$PROBE" 1 $k 10 > "$EV/$SUB/memcpy_k$k.csv" 2>&1; fi
  done
}
exec 9>/tmp/aien-gb10.lock
if flock -w 1200 9; then
  echo "gb10 lock held for the runs ($(date -u +%FT%TZ))" > "$EV/$SUB/LOCK.txt"
  runs
  flock -u 9
else
  echo "gb10 lock not acquired within 1200 s; ran without it ($(date -u +%FT%TZ)); load is in each CSV header" > "$EV/$SUB/LOCK.txt"
  runs
fi
cat "$EV/$SUB/LOCK.txt"
