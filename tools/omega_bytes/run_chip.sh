#!/bin/sh
# run_chip.sh OUTDIR  --  the BYTES-0 chip session for tools/omega_bytes (ledger L098).
# Run it ONLY under the locks, from the omega root, after `make omega-bytes`:
#   quiet-measure flock /tmp/aien-gb10.lock tools/omega_bytes/run_chip.sh evidence/BYTES-0/<stamp>
# One window: gates first (hand words, mutant must fail, empty kernel must leave poison),
# then the timed runs one after another (hand, encoder, empty, CPU C loop). Every step's
# output is kept, pass or fail. Exit 0 only if every gate and control behaved.
set -u
OUT=$1
B=build/omega_bytes/omega_bytes
K=tools/omega_bytes/kernels
N=1048576
mkdir -p "$OUT"
WIN="BYTES0-$(date -u +%Y%m%dT%H%M%SZ)-$$"
{
  echo "window_id $WIN"
  echo "omega_commit $(git rev-parse HEAD) dirty=$(git status --porcelain | wc -l)"
  echo "tool_sha256 $(sha256sum $B | cut -d' ' -f1)"
  echo "uptime_start $(uptime)"
  grep -E 'MemAvailable' /proc/meminfo
} > "$OUT/window.txt"
fail=0
step() { # name expected_exit cmd...
  name=$1; want=$2; shift 2
  "$@" > "$OUT/$name.json" 2> "$OUT/$name.log"
  rc=$?
  if [ "$rc" -eq "$want" ]; then r=ok; else r=UNEXPECTED; fail=1; fi
  echo "$name exit=$rc want=$want $r" >> "$OUT/steps.txt"
}
step gate_hand        0 $B gb10 $K/gb10_vadd_hand.words --n $N --reps 1
step control_mutant   1 $B gb10 $K/gb10_vadd_mutant.words --n $N --reps 1
step control_empty    0 $B gb10 $K/gb10_empty.words --n $N --reps 1 --expect unchanged
step gate_encoder     0 $B gb10 $K/gb10_vadd_encoder.words --n $N --reps 1
step time_hand        0 $B gb10 $K/gb10_vadd_hand.words --n $N --reps 11
step time_encoder     0 $B gb10 $K/gb10_vadd_encoder.words --n $N --reps 11
step time_empty       0 $B gb10 $K/gb10_empty.words --n $N --reps 11 --expect unchanged
step time_cpu_c       0 $B cpu-vadd --n $N --reps 21
echo "uptime_end $(uptime)" >> "$OUT/window.txt"
echo "session $([ $fail -eq 0 ] && echo PASS || echo FAIL)" >> "$OUT/steps.txt"
cat "$OUT/steps.txt"
exit $fail
