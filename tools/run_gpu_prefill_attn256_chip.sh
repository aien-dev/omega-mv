#!/bin/bash
# run_gpu_prefill_attn256_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# Qwen3.8-27B chip gate for omega_gpu_prefill_attn256_api (omega-mv#545: causal prefill attention, head_dim 256, 24 q / 4 kv
# heads, prep launch = the decode kernel's prologue, attend launch on tensor cores).
# Caller holds the GB10 lock: flock /tmp/aien-gb10.lock tools/run_gpu_prefill_attn256_chip.sh EVID
#   0. host proofs first (never hardware results): --host-only (codegen budgets, refusals, layout, reference sanity) and
#      --sim (warp-simulator parity on 7 points, rows bit-equal to the decode program, mutants)
#   1. baseline: the already-proven decode attention chip test at its quick points (chip and session path work first)
#   2. main: parity, rows bit-identical to m decode calls, next decode step identical, second call identical, out_scale,
#      device entry, mutants, timing of 2048-token chunks at contexts 2k, 8k, 32k
#   3. informational: --timing-only --max-pos 131072 (the chunk that ends at context 128k)
# Verdict line last: "GPU_PREFILL_ATTN256_CHIP: PASS|FAIL". Steps 0 and 2 decide; 1 and 3 are recorded.
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD) physics_lock=$(cat physics.lock 2>/dev/null | head -1)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2
} > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_prefill_attn256_test build/gpu_decode_attn256_test > "$EVID/build.log" 2>&1 || { echo "GPU_PREFILL_ATTN256_CHIP: FAIL build"; exit 1; }
T=./build/gpu_prefill_attn256_test
fail=0
$T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --sim 2>&1 | tee "$EVID/sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
./build/gpu_decode_attn256_test --reps 5 --only "context 301" --out "$EVID/receipt-baseline-decode.json" 2>&1 | tee "$EVID/baseline-decode.log"
echo "baseline exit ${PIPESTATUS[0]} (recorded)" | tee -a "$EVID/baseline-decode.log"
$T --reps 9 --out "$EVID/receipt-main.json" 2>&1 | tee "$EVID/main.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --reps 5 --timing-only --max-pos 131072 --out "$EVID/receipt-maxpos131072.json" 2>&1 | tee "$EVID/maxpos131072.log"; echo "max-pos 131072 exit ${PIPESTATUS[0]} (informational)" | tee -a "$EVID/maxpos131072.log"

echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.json *.log device.txt > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_PREFILL_ATTN256_CHIP: PASS"; else echo "GPU_PREFILL_ATTN256_CHIP: FAIL"; exit 1; fi
