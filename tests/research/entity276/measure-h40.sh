#!/usr/bin/env bash
# measure-h40.sh -- run of record for ENTITY-276 h40 (evidence/ENTITY-276/h40/FREEZE.md):
# run.sh once (pilot journal, every mutant), then REPS repetitions of the five horizon
# variants, each its own process on a fresh copy of that pilot journal.
# Run inside: quiet-measure flock /tmp/aien-gb10.lock spark-receipt entity-276-h40 -- ...
set -u
BIN=${ENTITY276_BIN:?}; OUT=${PILOT_OUT:?}; REPS=${REPS:-5}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
mem() { sed -n 's/^MemAvailable: *\([0-9]*\) kB/\1/p' /proc/meminfo; }
stamp() { echo "$(date -u +%FT%TZ),$(cut -d' ' -f1 /proc/loadavg),$(mem)"; }
rc_all=0
echo "rep,variant,start_utc,load1_start,memavail_kib_start,end_utc,load1_end,memavail_kib_end,exit" > "$OUT/runs.csv"
s=$(stamp)
ENTITY276_BIN="$BIN" PILOT_OUT="$OUT/r0" bash "$HERE/run.sh" > "$OUT/r0.out" 2>&1; rc=$?
echo "0,run.sh,$s,$(stamp),$rc" >> "$OUT/runs.csv"
[ $rc = 0 ] || rc_all=1
J="$OUT/r0/run1/journal-after-pilot.cx"; X="$OUT/r0/run1/identity-pilot.json"
for r in $(seq 1 "$REPS"); do
  for v in h3:3: h6:6: h6r:6:--release h10r:10:--release h40r:40:--release; do
    name=${v%%:*}; rest=${v#*:}; H=${rest%%:*}; extra=${rest#*:}
    D="$OUT/r$r/$name"; mkdir -p "$D"
    s=$(stamp)
    env PILOT_OUT="$D" "$BIN" horizon "$H" "$J" "$X" --interleave --lazy-rebuild --forget $extra \
      > "$D/horizon.out" 2> "$D/horizon.err"; rc=$?
    echo "$r,$name,$s,$(stamp),$rc" >> "$OUT/runs.csv"
    [ $rc = 0 ] || rc_all=1
  done
done
cat "$OUT/runs.csv"
exit $rc_all
