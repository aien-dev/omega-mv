#!/bin/sh
# measure.sh PROBE_BIN EVIDENCE_DIR
# Runs the held-out memcpy rows under the GB10 lock, held only for the
# measurement (about 20 s total). Wrap the call in quiet-measure so builds
# drain first. If the GB10 lock is not free within 20 minutes, the
# spark-measure rule applies: run anyway and record the load (the header
# lines of every CSV carry it).
set -u
PROBE=${1:?probe}; EV=${2:?evidence dir}
mkdir -p "$EV/measured"
runs() {
  for k in 2 3 5 6 4 16; do
    nice -n 10 "$PROBE" 1 $k 10 > "$EV/measured/memcpy_k$k.csv" 2>&1
  done
}
exec 9>/tmp/aien-gb10.lock
if flock -w 1200 9; then
  echo "gb10 lock held for the runs ($(date -u +%FT%TZ))" > "$EV/measured/LOCK.txt"
  runs
  flock -u 9
else
  echo "gb10 lock not acquired within 1200 s; ran without it ($(date -u +%FT%TZ)); load is in each CSV header" > "$EV/measured/LOCK.txt"
  runs
fi
cat "$EV/measured/LOCK.txt"
