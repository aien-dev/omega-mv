#!/bin/bash
# run_gpu_fp8_slots_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# Expert Ribbon P1 chip gate (aienix#4 / omega#533): versioned replaceable FP8 expert slots on the GB10.
#   host       tests/gpu_fp8_slots_test.c --host-only (state machine, threaded race, mutant)
#   chip r1-3  --chip: parity vs a resident tensor after every reload (gate|up 1536x2048 and down 2048x768), refusals,
#              reader fence through a recorded command list, memory accounting. Must PASS (exit 0) 3/3.
#   control    --chip --mutant: the pool ignores pins; the recorded list must read replaced weights and the parity
#              check must catch it (exit 1, "CAUGHT"). A control that is NOT caught fails the gate.
# Caller holds the GB10 lock: flock /tmp/aien-gb10.lock tools/run_gpu_fp8_slots_chip.sh EVID
# Verdict line last: "GPU_FP8_SLOTS_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD) physics_lock=$(head -1 physics.lock)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2
} > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_fp8_slots_test > "$EVID/build.log" 2>&1 || { echo "GPU_FP8_SLOTS_CHIP: FAIL build"; exit 1; }
T=./build/gpu_fp8_slots_test
fail=0
$T --host-only > "$EVID/host.log" 2>&1; e=$?; echo "# exit $e" >> "$EVID/host.log"; [ "$e" = 0 ] || fail=1
echo "host $(tail -2 "$EVID/host.log" | head -1) exit=$e" | tee "$EVID/summary.txt"
for r in 1 2 3; do
  f="$EVID/chip-r$r.log"
  $T --chip --rounds 64 > "$f" 2>&1; e=$?; echo "# exit $e" >> "$f"
  [ "$e" = 0 ] || fail=1
  echo "chip r$r $(grep -h '^GPU_FP8_SLOTS' "$f" | tail -1) exit=$e" | tee -a "$EVID/summary.txt"
done
f="$EVID/control-mutant.log"
$T --chip --mutant > "$f" 2>&1; e=$?; echo "# exit $e" >> "$f"
grep -q 'NEGATIVE_CONTROL: CAUGHT' "$f" && [ "$e" = 1 ] || fail=1
echo "control $(grep -h 'NEGATIVE_CONTROL' "$f" | tail -1) exit=$e" | tee -a "$EVID/summary.txt"
( cd "$EVID" && sha256sum device.txt build.log summary.txt host.log chip-r*.log control-mutant.log > SHA256SUMS )
if [ $fail = 0 ]; then echo "GPU_FP8_SLOTS_CHIP: PASS"; else echo "GPU_FP8_SLOTS_CHIP: FAIL"; exit 1; fi
