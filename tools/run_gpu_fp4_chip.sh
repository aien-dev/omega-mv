#!/bin/bash
# run_gpu_fp4_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# OWN-30B cut L3 chip gate for omega_gpu_fp4_api (FP4 OFP4v1 grouped GEMV decode kernel). KERNEL ONLY: nothing is wired into a server.
# Caller holds the GB10 lock and has left a whisper:
#   crumb whisper l3-fp4 <dir> - "fp4 kernel chip gate" ; flock /tmp/aien-gb10.lock tools/run_gpu_fp4_chip.sh EVID
#   0. host proofs first (never hardware results): --host-only and --sim (bit-identical to the emulation, mutants caught)
#   1. BASELINE FIRST: the existing FP8 grouped GEMV at the same 8-expert shapes (gate|up 1536x2048 and down 2048x768), 5 repeats
#   2. FP4 main: parity on the chip (bit-identical to the C emulation, one-hot code and scale tables, cache-hit determinism, mutants
#      1-5 caught) and the bandwidth table, 5 repeats of the whole run
#   3. one variable per run, informational: --rows 1, --warps 8, --no-evict (each compared with the FP4 main median)
# Verdict line last: "GPU_FP4_CHIP: PASS|FAIL". Steps 0 and 2 decide; 1 and 3 are recorded.
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"; cd "$HERE" || exit 2
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; uptime
  echo "cpu-heavy processes now:"; ps -eo pid,pcpu,comm --sort=-pcpu | head -5
} > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_fp4_test build/gpu_fp8_api_test > "$EVID/build.log" 2>&1 || { echo "GPU_FP4_CHIP: FAIL build"; exit 1; }
T=./build/gpu_fp4_test; B=./build/gpu_fp8_api_test; fail=0
$T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
$T --sim 2>&1 | tee "$EVID/sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
for i in 1 2 3 4 5; do
  $B --reps 50 --only "8 experts" --out "$EVID/receipt-baseline-fp8-$i.json" > "$EVID/baseline-fp8-$i.log" 2>&1; echo "baseline fp8 run $i exit $? (recorded)" | tee -a "$EVID/baseline-fp8-$i.log"
done
for i in 1 2 3 4 5; do
  $T --reps 50 --out "$EVID/receipt-main-$i.json" > "$EVID/main-$i.log" 2>&1; rc=$?; tail -1 "$EVID/main-$i.log"; [ $rc = 0 ] || fail=1
done
for v in "--rows 1:rows1" "--warps 8:warps8" "--no-evict:noevict"; do
  a="${v%%:*}"; n="${v##*:}"
  $T --reps 50 --only "experts" $a --out "$EVID/receipt-$n.json" > "$EVID/$n.log" 2>&1; echo "$a exit $? (informational)" | tee -a "$EVID/$n.log"
done
echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.json *.log device.txt > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_FP4_CHIP: PASS"; else echo "GPU_FP4_CHIP: FAIL"; exit 1; fi
