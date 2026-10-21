#!/bin/bash
# L321 E3a: the CPU state piston (tests/cpu_piston_state_test.c): one 35B DeltaNet layer (2 MiB state) or one conversation
# (30 layers, 60 MiB) held by 1..10 X925 cores, bit-equal to the reference order. Waits for load < LOAD_MAX, then quiet-measure.
# Out: e3a-<UTC>/run.log (STATE lines) + box.txt.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../../.." && pwd)
BIN=${BIN:-$REPO/build/l321/cpu_piston_state_test}; LOAD_MAX=${LOAD_MAX:-50}; TOKENS=${TOKENS:-64}
OUT=$HERE/e3a-$(date -u +%Y%m%dT%H%MZ); mkdir -p "$OUT"
{ echo "omega $(git -C "$REPO" rev-parse HEAD) binary sha256 $(sha256sum "$BIN" | cut -c1-64)"
  echo "start $(date -u +%FT%TZ) load $(cut -d' ' -f1-3 /proc/loadavg)"; } > "$OUT/box.txt"
while :; do l=$(cut -d' ' -f1 /proc/loadavg); awk -v l="$l" -v m="$LOAD_MAX" 'BEGIN{exit !(l < m)}' && break; echo "$(date -u +%TZ) waiting: load $l" >> "$OUT/wait.log"; sleep 60; done
echo "measured from load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$OUT/box.txt"
quiet-measure "$BIN" --tokens "$TOKENS" > "$OUT/run.log" 2>&1; rc=$?
echo "end $(date -u +%FT%TZ) rc $rc load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$OUT/box.txt"
echo "$OUT rc $rc"; exit $rc
