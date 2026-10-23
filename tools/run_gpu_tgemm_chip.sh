#!/bin/bash
# run_gpu_tgemm_chip.sh EVIDENCE_DIR [PHYSICS_DIR] [STEPS]
# Trainer lane E1 chip gate for the device-resident bf16 training GEMM (omega_gpu_tgemm_api.h, cut 2: NN / NT / TN layouts, F32,
# F32 accumulate and BF16 epilogues, deterministic split-K for the dW shapes).
# Caller holds the GB10 quiet lock: quietlock run -- tools/run_gpu_tgemm_chip.sh EVID [PHYS] [STEPS]
# STEPS (default "base nn nt bf16 acc dw info"; windows are at most 20 minutes, so the steps may be spread over several calls
# into the same folder; every call appends to device.txt and rewrites SHA256SUMS):
#   base  host proofs first (never hardware results): --host-only (instruction / register budgets) and --sim (bit-identical to the
#         HMMA-chain emulation in both fragment orders, tile K ranges, every layout / epilogue / split path, mutants caught); then the
#         yardstick: tests/gpu_tc_peak_test (HMMA.16816.F32.BF16 dense throughput ceiling); the best "peak ..." line's TFLOPS is the
#         100% every percentage below is measured against (saved in yardstick.txt and read back by the later steps)
#   nn    gate, NN F32 (cut 1 path, regression): 8192x768x{2304,768,3072} and 8192x3072x768, uncached then GPU-cached buffers:
#         every output within 1e-5 of the double reference, repeats bit-identical, poison scan clean, mutants caught, chip-time
#         TFLOPS >= 40% of the yardstick on cached buffers
#   nt    gate, NT F32 (forward layout, weights stored [N][K]): the same four shapes and rules
#   bf16  gate, NT BF16 output (activations for the next layer): the same four shapes; bf16 outputs bit-identical to the rounded emulation
#   acc   gate, NN F32 accumulate in place (C += A B, backward dX): the same four shapes; the old C restored before every timed call
#   dw    TN split-K (dW = At G, K = 8192): 768x768, 2304x768, 3072x768, 768x3072 (M x N x K 8192), F32 and F32 accumulate:
#         correctness rules as above, run-to-run bit-identical (deterministic partials + fixed-order reduce); TFLOPS informational
#   info  one variable per run: 1024x50304x768 NT (lm_head forward, B-slab grid order), 8192x768x2304 NT forced to rt 8 x 8 warps
#         (2 CTAs per SM), and to rt 4 x 8 warps
# Verdict line last per call: "GPU_TGEMM_CHIP: PASS|FAIL". The gate steps and dw decide; info is informational.
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"; STEPS="${3:-base nn nt bf16 acc dw info}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE" || exit 2
DIRTY=$(git status --porcelain | wc -l) # before the evidence folder exists (it would count as untracked)
mkdir -p "$EVID"
{
  echo "start $(date -u +%FT%TZ) steps=[$STEPS] omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$DIRTY physics=$PHYS physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version,clocks.sm,clocks.max.sm,temperature.gpu --format=csv,noheader 2>/dev/null; free -g | head -2
} >> "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_tgemm_test build/gpu_tc_peak_test > "$EVID/build.log" 2>&1 || { echo "GPU_TGEMM_CHIP: FAIL build"; exit 1; }
T=./build/gpu_tgemm_test; fail=0
has() { case " $STEPS " in *" $1 "*) return 0;; *) return 1;; esac; }
if has base; then
  $T --host-only 2>&1 | tee "$EVID/host-only.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  $T --sim 2>&1 | tee "$EVID/sim.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  timeout 300 ./build/gpu_tc_peak_test --chip 2>&1 | tee "$EVID/peak.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
  PEAK=$(grep '^peak ' "$EVID/peak.log" | sed 's/.*us, \([0-9.]*\) TFLOPS.*/\1/' | sort -g | tail -1)
  echo "yardstick: bf16 HMMA dense peak $PEAK TFLOPS (best 'peak' line of peak.log)" | tee "$EVID/yardstick.txt"
fi
PEAK=$(sed -n 's/^yardstick: bf16 HMMA dense peak \([0-9.]*\) TFLOPS.*/\1/p' "$EVID/yardstick.txt" 2>/dev/null | tail -1)
if [ -z "$PEAK" ]; then echo "GPU_TGEMM_CHIP: FAIL no yardstick (run the base step first)"; exit 1; fi
gate() { # name, flags
  $T --chip --reps 9 --gate 40 --peak "$PEAK" $2 2>&1 | tee "$EVID/$1.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
}
has nn && gate nn "--layout NN --epi f32"
has nt && gate nt "--layout NT --epi f32"
has bf16 && gate bf16 "--layout NT --epi bf16"
has acc && gate acc "--layout NN --epi acc"
if has dw; then
  for s in 768x768x8192 2304x768x8192 3072x768x8192 768x3072x8192; do
    for e in f32 acc; do
      $T --chip --reps 9 --peak "$PEAK" --layout TN --epi $e --shape $s 2>&1 | tee "$EVID/dw-$s-$e.log"; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
    done
  done
fi
if has info; then
  for v in "--layout NT --shape 1024x50304x768:n50304" "--layout NT --shape 8192x768x2304 --shape-knob 8 8 1:nt-rt8-warps8" "--layout NT --shape 8192x768x2304 --shape-knob 4 8 1:nt-rt4-warps8"; do
    a="${v%%:*}"; n="${v##*:}"
    $T --chip --reps 9 --peak "$PEAK" $a 2>&1 | tee "$EVID/$n.log"; echo "$a exit ${PIPESTATUS[0]} (informational)" | tee -a "$EVID/$n.log"
  done
fi
echo "end $(date -u +%FT%TZ) steps=[$STEPS]" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum *.log *.txt > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_TGEMM_CHIP: PASS [$STEPS]" | tee -a "$EVID/verdicts.txt"; else echo "GPU_TGEMM_CHIP: FAIL [$STEPS]" | tee -a "$EVID/verdicts.txt"; exit 1; fi
