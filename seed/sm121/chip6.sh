#!/bin/sh
# chip6.sh OUTDIR -- rung 6 on the chip: the vector add that asm2 made (seed/sm121/vecadd.bin) runs on the
# GB10 through the bytes tool (tools/omega_bytes, ledger L098; no CUDA library). Run from the repo root,
# after gate6.sh and `make omega-bytes`, ONLY under the GPU lock:
#   quiet-measure flock /tmp/aien-gb10.lock spark-receipt seed-sm121-run -- sh seed/sm121/chip6.sh evidence/SEED-0/sm121/chip
# The words file is a plain rewrite of vecadd.bin (od: two 64-bit halves per 128-bit instruction, low half
# first, the bytes tool's format). Correctness only: one launch, the bytes tool checks out[i] = a[i] + b[i].
# Control: the bytes tool's mutant vector add (one word changed) must fail the same check.
set -u
OUT=$1
B=build/omega_bytes/omega_bytes
V=seed/sm121/vecadd.bin
N=1048576
mkdir -p "$OUT"
words() { od -An -v -tx8 -w16 "$1" | awk '{ print "0x" $1 " 0x" $2 }'; }
words $V > "$OUT/vecadd.words"
{
  echo "omega_commit $(git rev-parse HEAD) dirty=$(git status --porcelain | wc -l)"
  echo "vecadd_sha256 $(sha256sum $V | cut -d' ' -f1)"
  echo "tool_sha256 $(sha256sum $B | cut -d' ' -f1)"
  echo "uptime_start $(uptime)"
} > "$OUT/window.txt"
fail=0
step() { # name expected_exit cmd...
  name=$1; want=$2; shift 2
  "$@" > "$OUT/$name.json" 2> "$OUT/$name.log"
  rc=$?
  if [ "$rc" -eq "$want" ]; then r=ok; else r=UNEXPECTED; fail=1; fi
  echo "$name exit=$rc want=$want $r" >> "$OUT/steps.txt"
}
: > "$OUT/steps.txt"
step check_words    0 $B check gb10 "$OUT/vecadd.words"
step run_seed       0 $B gb10 "$OUT/vecadd.words" --n $N --reps 1
# control: the bytes tool's mutant (one word changed) must fail the same check
step control_mutant 1 $B gb10 tools/omega_bytes/kernels/gb10_vadd_mutant.words --n $N --reps 1
echo "uptime_end $(uptime)" >> "$OUT/window.txt"
echo "session $([ $fail -eq 0 ] && echo PASS || echo FAIL)" >> "$OUT/steps.txt"
cat "$OUT/steps.txt"
exit $fail
