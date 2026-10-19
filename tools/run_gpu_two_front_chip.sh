#!/bin/bash
# run_gpu_two_front_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# omega#571 chip gate for the two-front arena (FORTRAN idea 3): KV from one end, DeltaNet state + snapshots from the other.
# Caller holds the GB10 lock: flock /tmp/aien-gb10.lock tools/run_gpu_two_front_chip.sh EVID
#   0. host proofs first (never hardware results): --host-only, --sim, and --break-overflow, which MUST fail (red)
#   1. chip: exact-recall gate (decode + prefill attention plane vs arena bit for bit up to MAX_POS, DeltaNet live set in
#      the arena + snapshot/restore), the fronts meeting + the next block refused with the snapshot intact, the inline
#      control (check skipped: snapshot overrun), the two block-layout kernel mutants caught, driver bytes, timing.
#   The test refuses any allocation that would leave less than 12 GiB MemAvailable.
# Env: OUT_DIR (default build), MAX_POS (default 131072), REPS (default 15). Verdict line last: "GPU_TWO_FRONT_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-6d7cf0d4}"; OUT="${OUT_DIR:-build}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; grep MemAvailable /proc/meminfo
} > "$EVID/device.txt"
git diff HEAD > "$EVID/worktree.diff"
make OUT_DIR="$OUT" PHYSICS_DIR="$PHYS" -j"${JOBS:-4}" "$OUT/gpu_two_front_test" > "$EVID/build.log" 2>&1 || { echo "GPU_TWO_FRONT_CHIP: FAIL build"; exit 1; }
T="$OUT/gpu_two_front_test"
sha256sum "$T" "$OUT/libomega_gpu.a" > "$EVID/binaries.sha256"
fail=0
$T --host-only > "$EVID/host-only.log" 2>&1 || fail=1; tail -1 "$EVID/host-only.log"
$T --sim > "$EVID/sim.log" 2>&1 || fail=1; tail -1 "$EVID/sim.log"
if $T --host-only --break-overflow > "$EVID/red-break-overflow.log" 2>&1; then echo "red control did NOT go red"; fail=1; else echo "red control: $(tail -1 "$EVID/red-break-overflow.log")"; fi
[ $fail = 0 ] || { echo "GPU_TWO_FRONT_CHIP: FAIL host proofs"; exit 1; }
$T --max-pos "${MAX_POS:-131072}" --reps "${REPS:-15}" 2>&1 | tee "$EVID/chip.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
echo "end $(date -u +%FT%TZ) fail=$fail" >> "$EVID/device.txt"; grep MemAvailable /proc/meminfo >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.log *.txt *.diff *.sha256 > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_TWO_FRONT_CHIP: PASS"; else echo "GPU_TWO_FRONT_CHIP: FAIL"; exit 1; fi
