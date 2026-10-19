#!/usr/bin/env bash
# measure.sh -- run of record for ENTITY-276 (evidence/ENTITY-276/FREEZE.md section 4):
# run.sh five times, each into its own folder, with load and MemAvailable at start and end.
# Run inside: quiet-measure flock /tmp/aien-gb10.lock spark-receipt entity-276-pilot -- ...
set -u
BIN=${ENTITY276_BIN:?}; OUT=${PILOT_OUT:?}
mkdir -p "$OUT"
mem() { sed -n 's/^MemAvailable: *\([0-9]*\) kB/\1/p' /proc/meminfo; }
rc_all=0
echo "run,start_utc,load1_start,memavail_kib_start,end_utc,load1_end,memavail_kib_end,exit" > "$OUT/runs.csv"
for r in 1 2 3 4 5; do
  D="$OUT/r$r"; mkdir -p "$D"
  s="$(date -u +%FT%TZ),$(cut -d' ' -f1 /proc/loadavg),$(mem)"
  ENTITY276_BIN="$BIN" PILOT_OUT="$D" bash "$(dirname "$0")/run.sh" > "$D/run.out" 2>&1; rc=$?
  echo "$r,$s,$(date -u +%FT%TZ),$(cut -d' ' -f1 /proc/loadavg),$(mem),$rc" >> "$OUT/runs.csv"
  [ $rc = 0 ] || rc_all=1
done
cat "$OUT/runs.csv"
exit $rc_all
