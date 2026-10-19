#!/bin/bash
# run_gpu_nv4_chip.sh EVIDENCE_DIR [PHYSICS_DIR] [OUT_DIR] [FIXTURE_DIR]
# flashnext/port (2026-10-19): chip run for omega_gpu_nv4_api (grouped NVFP4 GEMV for the Qwen3.8-Flash-Next experts).
# Caller holds the locks: flock -x /tmp/aien-quiet.lock flock /tmp/aien-gb10.lock nice -n 10 tools/run_gpu_nv4_chip.sh EVID
#   0. host proofs (never hardware results): --host-only (e4m3 kernel bits for every code, codegen budgets, refusals) and
#      --sim (warp simulator: one-hot incl. tail lanes, parity over shapes and groups, every mutant caught on some shape)
#   1. chip: one-hot codes incl. the k 640 tail lanes, parity at the Flash-Next expert shapes (11 groups gate|up 1280x2560,
#      11 groups down 2560x640 with distinct x rows, and 1 group of each), mutants red on the chip, NaN-scale refusal,
#      timing (median of REPS, GB/s of the bytes each launch streams); with FIXTURE_DIR also real NVFP4 expert tensors
#      against the sc host reference's outputs (flashnext_check nv4-fixture).
# Verdict line last: "GPU_NV4_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-6d7cf0d4}"; OUT="${3:-build}"; FIX="${4:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; grep MemAvailable /proc/meminfo; echo "loadavg start $(cat /proc/loadavg)"
} > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" OUT_DIR="$OUT" -j6 "$OUT/gpu_nv4_api_test" > "$EVID/build.log" 2>&1 || { echo "GPU_NV4_CHIP: FAIL build"; exit 1; }
T="$OUT/gpu_nv4_api_test"
sha256sum "$T" "$OUT/libomega_gpu.a" > "$EVID/binaries.sha256"
fail=0
$T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --sim 2>&1 | tee "$EVID/sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
FA=(); [ -n "$FIX" ] && FA=(--fixture "$FIX")
$T --chip --reps "${REPS:-30}" --out "$EVID/receipt-chip.json" "${FA[@]}" 2>&1 | tee "$EVID/chip.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
{ echo "loadavg end $(cat /proc/loadavg)"; echo "end $(date -u +%FT%TZ)"; } >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.json *.log device.txt binaries.sha256 > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_NV4_CHIP: PASS"; else echo "GPU_NV4_CHIP: FAIL"; exit 1; fi
