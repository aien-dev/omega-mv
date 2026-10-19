#!/bin/bash
# run_gpu_w4_chip.sh EVIDENCE_DIR [PHYSICS_DIR] [OUT_DIR]
# qwen38/w4-gemv, cascade/w4-gemv (2026-10-19): chip gate for omega_gpu_w4_api (OCP MXFP4 GEMV: e2m1 nibbles, one E8M0
# scale per 32 weights along k) for the Qwen3.8-27B decode graph.
# Caller holds the GB10 lock: flock /tmp/aien-gb10.lock nice -n 10 tools/run_gpu_w4_chip.sh EVID
#   0. host proofs first (never hardware results): --host-only (e2m1 bits for all 16 codes, RNE rounding, converter
#      round trip, codegen budgets at every shape) and --sim (warp simulator: one-hot codes, parity over shapes and knobs,
#      mutants 1..6 red)
#   1. main: chip parity at every 27B shape (gdn mix, attn mix, o_proj, gate_up, down, lm_head) against the host oracle,
#      one-hot codes incl. the subnormal nibbles 1 and 9, mutants 1, 2, 4, 5 red on the chip, upload refusal of E > 192;
#      then timing per shape: W4 and FP8 (--fp8) in the same process and session, median of --reps, GB/s against 273.
#      receipt-main.json has every point.
#   2. in-graph (2 layers, GPU-stamped timeline): gpu_q38_graph_test --w4 --w4-head, the FP8 control inside it (layer 0
#      back on FP8 must fail the MXFP4 baseline), then the FP8 graph for the same timeline. The one-shot timings in step 1
#      are quantised by the host wait (~100 us); rank shapes by the timeline, not by step 1.
# Verdict line last: "GPU_W4_CHIP: PASS|FAIL". Steps 0, 1 and the graph checks decide; timings are recorded.
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-6d7cf0d4}"; OUT="${3:-build}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD) physics_lock=$(cat physics.lock 2>/dev/null | head -1)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; cat /proc/loadavg
} > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" OUT_DIR="$OUT" -j8 "$OUT/gpu_w4_api_test" "$OUT/gpu_q38_graph_test" > "$EVID/build.log" 2>&1 || { echo "GPU_W4_CHIP: FAIL build"; exit 1; }
T="$OUT/gpu_w4_api_test"; G="$OUT/gpu_q38_graph_test"
sha256sum "$T" "$G" "$OUT/libomega_gpu.a" > "$EVID/binaries.sha256"
fail=0
$T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --sim 2>&1 | tee "$EVID/sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --chip --fp8 --reps "${REPS:-30}" --out "$EVID/receipt-main.json" 2>&1 | tee "$EVID/main.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$G --layers 2 --w4 --w4-head --tl-reps "${TL_REPS:-21}" 2>&1 | tee "$EVID/graph-w4.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$G --layers 2 --fp8-head --tl-reps "${TL_REPS:-21}" 2>&1 | tee "$EVID/graph-fp8.log"; echo "fp8 graph exit ${PIPESTATUS[0]} (timeline control)" | tee -a "$EVID/graph-fp8.log"
echo "end $(date -u +%FT%TZ)"; cat /proc/loadavg >> "$EVID/device.txt"
echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.json *.log device.txt binaries.sha256 > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_W4_CHIP: PASS"; else echo "GPU_W4_CHIP: FAIL"; exit 1; fi
