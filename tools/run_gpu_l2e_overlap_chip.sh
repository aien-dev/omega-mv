#!/bin/bash
# run_gpu_l2e_overlap_chip.sh EVIDENCE_DIR [PHYSICS_DIR]   (OWN-30B W-L2e, 2026-10-14; run under flock /tmp/aien-gb10.lock)
# Overlapped dependent FP8 GEMV launches: base (WFI) vs wait (semaphore spin, no barrier) vs warm (cached weights) vs red
# (no barrier, no wait: MUST differ), two chains (single / grouped), interleaved per rep. Then the cmdlist chip test
# (regression of the cmdlist change). Verdict line last: "GPU_L2E_OVERLAP_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"; cd "$HERE" || exit 2
{ echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; uptime; } > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_fp8_overlap_test build/gpu_cmdlist_test > "$EVID/build.log" 2>&1 || { echo "GPU_L2E_OVERLAP_CHIP: FAIL build"; exit 1; }
sha256sum build/gpu_fp8_overlap_test build/gpu_cmdlist_test build/libomega_gpu.a > "$EVID/binaries.sha256"
fail=0
timeout 900 ./build/gpu_fp8_overlap_test --reps "${REPS:-10}" --n "${NL:-8}" --warm "${WARM:-2}" --out "$EVID/receipt.json" > "$EVID/overlap.log" 2>&1 || fail=1
grep -E "^OVL|FAIL|checks" "$EVID/overlap.log"
timeout 300 ./build/gpu_cmdlist_test > "$EVID/cmdlist.log" 2>&1 || fail=1; echo "cmdlist: $(tail -1 "$EVID/cmdlist.log")"
echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_L2E_OVERLAP_CHIP: PASS"; else echo "GPU_L2E_OVERLAP_CHIP: FAIL"; exit 1; fi
