#!/bin/bash
# run_gpu_q38_graph_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# sc #642 chip gate for omega_gpu_q38_graph (one Qwen3.8-27B decode token as one command list).
# Caller holds the GB10 lock: flock /tmp/aien-gb10.lock tools/run_gpu_q38_graph_chip.sh EVID
#   0. host proofs first (never hardware results): glue-kernel codegen + sim (gpu_cmdlist_test), the graph's --host-only
#   1. main: A (the APIs one call at a time, host glue) vs C (one list) vs B (one launch per submission), a 2-layer
#      synthetic model (GDN then attention) on random weights, vocab 248320, tokens at positions 0..2; token identical,
#      logits and x within tolerance, B == C bit for bit, the four graph mutants caught; REPS timed tokens.
#   2. LAYERS=4 MASK=0x8 (three GDN layers then attention, the checkpoint's period) as a second shape.
# Verdict line last: "GPU_Q38_GRAPH_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"
cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2
} > "$EVID/device.txt"
git diff HEAD > "$EVID/worktree.diff"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_q38_graph_test build/gpu_cmdlist_test build/bw_encode_control_test > "$EVID/build.log" 2>&1 || { echo "GPU_Q38_GRAPH_CHIP: FAIL build"; exit 1; }
T=./build/gpu_q38_graph_test
sha256sum "$T" build/gpu_cmdlist_test build/libomega_gpu.a > "$EVID/binaries.sha256"
fail=0
./build/bw_encode_control_test 2>&1 | tee "$EVID/encode-control.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
./build/gpu_cmdlist_test --host-only > "$EVID/glue-host.log" 2>&1 || fail=1; tail -1 "$EVID/glue-host.log"
./build/gpu_cmdlist_test --sim > "$EVID/glue-sim.log" 2>&1 || fail=1; tail -1 "$EVID/glue-sim.log"
$T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
[ $fail = 0 ] || { echo "GPU_Q38_GRAPH_CHIP: FAIL host proofs"; exit 1; }
$T --reps "${REPS:-10}" 2>&1 | tee "$EVID/main.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --layers 4 --mask 0x8 --reps "${REPS:-10}" 2>&1 | tee "$EVID/layers4.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
echo "end $(date -u +%FT%TZ) fail=$fail" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.log *.txt *.diff *.sha256 > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_Q38_GRAPH_CHIP: PASS"; else echo "GPU_Q38_GRAPH_CHIP: FAIL"; exit 1; fi
