#!/bin/bash
# L321 E1 + E1b: the shared point and the second road (probe tests/cpu_piston_share_test.c).
#   bash run-e1.sh host   CPU + NVMe arms, no chip: waits for load < LOAD_MAX, then runs under quiet-measure.
#   bash run-e1.sh chip   every arm incl. the GPU ones: run ONLY inside a measurement window (spark-window run), which holds
#                         the chip lock and the quiet lock; a one-size smoke run first, the full sweep only if it passes.
# Out: e1-<mode>-<UTC>/ with run.log (CELL lines), box.txt (box state), and the binary's sha256 in the header line.
set -u
MODE=${1:?host|chip}
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../../.." && pwd)
BIN=${BIN:-$REPO/build/l321/cpu_piston_share_test}
CPU=${CPU:-19}; BG=${BG:-15}; REPS=${REPS:-9}; LOAD_MAX=${LOAD_MAX:-50}
NVME=${NVME:-/var/tmp/l321-nvme.bin}
OUT=$HERE/e1-$MODE-$(date -u +%Y%m%dT%H%MZ); mkdir -p "$OUT"
{ echo "omega $(git -C "$REPO" rev-parse HEAD) binary sha256 $(sha256sum "$BIN" | cut -c1-64)"
  echo "start $(date -u +%FT%TZ) load $(cut -d' ' -f1-3 /proc/loadavg) MemAvailable $(awk '/MemAvailable/{print $2}' /proc/meminfo) kB"
  echo "perf_event_paranoid $(cat /proc/sys/kernel/perf_event_paranoid) window ${SPARK_WINDOW:-none}"
  lscpu -C; echo "cpu$CPU L3 shared with: $(cat /sys/devices/system/cpu/cpu$CPU/cache/index3/shared_cpu_list)"
  echo "busiest processes at start (pid pcpu core comm):"; ps -eo pid,pcpu,psr,comm --sort=-pcpu | head -16; } > "$OUT/box.txt"
if [ "$MODE" = host ]; then
  while :; do l=$(cut -d' ' -f1 /proc/loadavg); awk -v l="$l" -v m="$LOAD_MAX" 'BEGIN{exit !(l < m)}' && break; echo "$(date -u +%TZ) waiting: load $l" >> "$OUT/wait.log"; sleep 60; done
  echo "measured at load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$OUT/box.txt"
  quiet-measure "$BIN" --host-only --cpu "$CPU" --reps "$REPS" --nvme "$NVME" > "$OUT/run.log" 2>&1; rc=$?
else
  [ -n "${SPARK_WINDOW:-}" ] || { echo "chip mode runs only inside spark-window run" >&2; exit 2; }
  "$BIN" --cpu "$CPU" --bg-cpu "$BG" --reps 1 --size 0x400000 --nvme "$NVME" > "$OUT/smoke.log" 2>&1; rc=$?
  tail -1 "$OUT/smoke.log"
  # full sweep when the smoke run finished (PASS, or FAIL from a finding such as a wrong XOR); not after a crash or refusal
  if grep -q "^CPU_PISTON_SHARE: " "$OUT/smoke.log" && ! grep -q -E "chip alloc refused|gpu stream refused|host memory refused" "$OUT/smoke.log"; then echo "e1 start $(date -u +%TZ)" >> "$OUT/box.txt"; "$BIN" --cpu "$CPU" --bg-cpu "$BG" --reps "$REPS" --nvme "$NVME" > "$OUT/run.log" 2>&1; rc=$?; echo "e1 end $(date -u +%TZ) rc $rc" >> "$OUT/box.txt"; fi
  # E2 phase sweep: GPU 256 MB + CPU 64 MB (9 X925 workers) per beat, CPU start offset swept; BEAT lines = timing diagram.
  # Runs even when E1 failed a cell (a wrong XOR there is a finding, not a reason to skip E2); its own rc is kept apart.
  "$BIN" --cpu "$CPU" --phase "${BEATS:-200}" --timeline "$OUT/phase-timeline.txt" > "$OUT/phase.log" 2>&1; echo "phase rc $?" >> "$OUT/box.txt"
  # E3a rerun inside the window (quiet lock held: no quiet-build builds beside it): the CPU state piston, bit-equal check included
  echo "e3a start $(date -u +%TZ)" >> "$OUT/box.txt"; "$REPO/build/l321/cpu_piston_state_test" --tokens 64 > "$OUT/e3a.log" 2>&1; echo "e3a rc $? end $(date -u +%TZ)" >> "$OUT/box.txt"
fi
echo "end $(date -u +%FT%TZ) rc $rc load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$OUT/box.txt"
echo "$OUT rc $rc"; exit $rc
