#!/bin/bash
# run_flashnext_chip.sh EVIDENCE_DIR [PHYSICS_DIR] [OUT_DIR] [NV4_FIXTURE_DIR] [ROUTER_FIXTURE_ROOT]
# flashnext/port (2026-10-19): ONE GPU slot for the three Flash-Next chip gates, because slots behind other measurements are scarce:
#   nv4        grouped NVFP4 GEMV (omega_gpu_nv4_api): host proofs, simulator, chip parity at the expert shapes, mutants red on the chip,
#              timing; with NV4_FIXTURE_DIR the real NVFP4 expert tensors against the sc host reference (flashnext_check nv4-fixture)
#   router_fn  top-10 of 512 router + shared slot (omega_gpu_router_fn_api): same, with ROUTER_FIXTURE_ROOT/moe/L<l> real routers
#   fnattn     decode attention, 2 KV heads, sparse position list (omega_gpu_fnattn_api): same, no fixture
# Caller holds the locks: quiet-measure flock /tmp/aien-gb10.lock nice -n 10 tools/run_flashnext_chip.sh EVID ...
# Builds are NOT done here (use quiet-build make ... first); the binaries must exist in OUT_DIR. Verdict line last: FLASHNEXT_CHIP: PASS|FAIL.
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-pf-6d7cf0d4}"; OUT="${3:-build}"; NVFIX="${4:-}"; RFIX="${5:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; grep MemAvailable /proc/meminfo
  echo "loadavg start $(cat /proc/loadavg)"
} > "$EVID/device.txt"
fail=0
for t in gpu_nv4_api_test gpu_router_fn_test gpu_fnattn_test; do
  [ -x "$OUT/$t" ] || { echo "FLASHNEXT_CHIP: FAIL missing binary $OUT/$t"; exit 1; }
done
sha256sum "$OUT"/gpu_nv4_api_test "$OUT"/gpu_router_fn_test "$OUT"/gpu_fnattn_test "$OUT"/libomega_gpu.a > "$EVID/binaries.sha256"
run() { # name, then the test's chip arguments
  local name="$1"; shift
  local T="$OUT/gpu_${name}_test"
  $T --host-only 2>&1 | tee "$EVID/$name-host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  $T --sim 2>&1 | tee "$EVID/$name-sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  $T --chip "$@" 2>&1 | tee "$EVID/$name-chip.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
}
FA=(); [ -n "$NVFIX" ] && FA=(--fixture "$NVFIX")
run nv4 --reps "${REPS:-30}" --out "$EVID/receipt-nv4-chip.json" "${FA[@]}"
if [ -n "$RFIX" ] && [ -d "$RFIX/moe" ]; then
  for L in $(ls "$RFIX/moe" | sed 's/^L//' | sort -n); do
    "$OUT/gpu_router_fn_test" --chip --reps 5 --out "$EVID/receipt-router-fn-fixture-L$L.json" --fixture "$RFIX/moe/L$L" 2>&1 | tee "$EVID/router_fn-fixture-L$L.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  done
fi
run router_fn --reps "${REPS:-30}" --out "$EVID/receipt-router-fn-chip.json"
# fnattn: the test has no --chip flag (the default mode is the chip)
T="$OUT/gpu_fnattn_test"
$T --host-only 2>&1 | tee "$EVID/fnattn-host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --sim 2>&1 | tee "$EVID/fnattn-sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --reps 20 --out "$EVID/receipt-fnattn-chip.json" 2>&1 | tee "$EVID/fnattn-chip.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
{ echo "loadavg end $(cat /proc/loadavg)"; echo "end $(date -u +%FT%TZ)"; } >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.json *.log device.txt binaries.sha256 > SHA256SUMS)
if [ $fail = 0 ]; then echo "FLASHNEXT_CHIP: PASS"; else echo "FLASHNEXT_CHIP: FAIL"; exit 1; fi
