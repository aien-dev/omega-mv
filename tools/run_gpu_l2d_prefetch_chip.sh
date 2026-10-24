#!/bin/bash
# run_gpu_l2d_prefetch_chip.sh EVIDENCE_DIR [PHYSICS_DIR]   (OWN-30B W-L2d, 2026-10-14; run under flock /tmp/aien-gb10.lock)
# One variable: the o_proj weight prefetch launch beside the attention (decode graph --prefetch), on GPU-cached weights.
# Synthetic 2-layer token (no model load). Interleaved A/B: cached base, cached prefetch, cached base, cached prefetch;
# control: uncached + prefetch (no L2 keep possible: expect no o_proj gain); red: --prefetch-red MUST FAIL;
# cmdlist chip test (regression of the cmdlist change). Every graph run checks B == C bit for bit + mutants and prints the
# per-kind TIMELINE and the logits hash. Verdict line last: "GPU_L2D_PREFETCH_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
# Classification is implemented by native/prefetch/classify.om. Require the
# explicitly built native classifier before any device work; never fall back
# to accepting a nonzero child exit. An exclusive directory prevents stale
# receipts from an earlier run from being paired with a failed new invocation.
CLASSIFIER="${PREFETCH_CLASSIFIER:?path to compiled native/prefetch/classify.om}"
CLASSIFIER="$(realpath "$CLASSIFIER")" || exit 2
[ -x "$CLASSIFIER" ] || { echo "GPU_L2D_PREFETCH_CHIP: INVALID classifier unavailable"; exit 2; }
EXPECTED="$(cat "$HERE/native/prefetch/classify.sha256")" || exit 2
[[ "$EXPECTED" =~ ^[0-9a-f]{64}$ ]] || exit 2
mkdir "$EVID" || { echo "GPU_L2D_PREFETCH_CHIP: INVALID evidence directory must be new"; exit 2; }
EVID="$(cd "$EVID" && pwd)" || exit 2
cd "$HERE" || exit 2
cp "$CLASSIFIER" "$EVID/classifier.elf" || exit 2
CLASSIFIER="$EVID/classifier.elf"
ACTUAL="$(sha256sum "$CLASSIFIER")" || exit 2
[ "${ACTUAL%% *}" = "$EXPECTED" ] || { echo "GPU_L2D_PREFETCH_CHIP: INVALID classifier digest"; exit 2; }
sha256sum -c native/prefetch/source.sha256 > "$EVID/classifier-source.log" 2>&1 || { echo "GPU_L2D_PREFETCH_CHIP: INVALID classifier source"; exit 2; }
{ echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics_commit=$(git -C "$PHYS" rev-parse HEAD)"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; uptime; } > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_decode_graph_test build/gpu_cmdlist_test > "$EVID/build.log" 2>&1 || { echo "GPU_L2D_PREFETCH_CHIP: INVALID build"; exit 2; }
D=./build/gpu_decode_graph_test
sha256sum "$D" build/gpu_cmdlist_test build/libomega_gpu.a > "$EVID/binaries.sha256" || exit 2
sha256sum "$CLASSIFIER" native/prefetch/classify.om >> "$EVID/binaries.sha256" || exit 2
fail=0
run() { local name="$1"; shift
  timeout --kill-after=10 600 "$D" "$@" --reps "${REPS:-10}" --out "$EVID/receipt-$name.json" > "$EVID/$name.log" 2>&1 || fail=1
  echo "$name ($*): $(grep -E 'TIMELINE|PER TOKEN|^chip:' "$EVID/$name.log" | tr '\n' ' ')"
  grep -o '"C0_logits_fnv":"[0-9a-f]\{16\}"' "$EVID/receipt-$name.json" || fail=1; }
run 1-cached-base --cached-weights
run 2-cached-prefetch --cached-weights --prefetch
run 3-cached-base --cached-weights
run 4-cached-prefetch --cached-weights --prefetch
run 5-uncached-prefetch --prefetch
timeout --kill-after=10 600 "$D" --cached-weights --prefetch-red --reps 3 --out "$EVID/receipt-red.json" > "$EVID/red.log" 2>&1; rc=$?
printf '%s\n' "$rc" > "$EVID/red.exit"
echo "red (prefetch-red): exit $rc ($(tail -1 "$EVID/red.log"))"
timeout --kill-after=2 10 "$CLASSIFIER" "$rc" "$EVID/red.log" "$EVID/receipt-red.json" > "$EVID/red-classification.log" 2>&1; classification=$?
cat "$EVID/red-classification.log"
if [ "$classification" != 0 ]; then
  fail=1
  if [ "$classification" != 1 ]; then
    echo "GPU_L2D_PREFETCH_CHIP: INVALID prefetch control"
    exit 2
  fi
fi
timeout --kill-after=10 600 ./build/gpu_cmdlist_test > "$EVID/cmdlist.log" 2>&1 || fail=1; echo "cmdlist: $(tail -1 "$EVID/cmdlist.log")"
echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt" || exit 2
# The directory is exclusive and SHA256SUMS does not exist yet. Glob expansion
# precedes creation of that output, so it cannot hash itself. Preserve hash and
# write failures instead of masking them behind grep's successful exit.
(cd "$EVID" && sha256sum ./* > SHA256SUMS) || { echo "GPU_L2D_PREFETCH_CHIP: INVALID evidence manifest"; exit 2; }
if [ $fail = 0 ]; then echo "GPU_L2D_PREFETCH_CHIP: PASS"; else echo "GPU_L2D_PREFETCH_CHIP: FAIL"; exit 1; fi
