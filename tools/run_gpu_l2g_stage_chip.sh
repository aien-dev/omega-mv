#!/bin/bash
# run_gpu_l2g_stage_chip.sh EVIDENCE_DIR [PHYSICS_DIR]   (OWN-30B W-L2g, 2026-10-14; run under flock /tmp/aien-gb10.lock)
# Where does the time inside one real resident stage go? gpu_resident_chain_test with RES_ARGS (default: --quick --only-real
# --stamps): launched / resident / red / stamped interleaved per rep, bit-identical gate, red must differ, stamped camera
# records per (stage, CTA). Then the cmdlist chip test (regression). Verdict line last: "GPU_L2G_STAGE_CHIP: PASS|FAIL".
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-own30b-k}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$EVID"; cd "$HERE" || exit 2
{ echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) branch=$(git rev-parse --abbrev-ref HEAD) dirty=$(git status --porcelain | wc -l) physics_commit=$(git -C "$PHYS" rev-parse HEAD) args=${RES_ARGS:---quick --only-real --stamps}"
  uname -a; nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null; free -g | head -2; uptime; } > "$EVID/device.txt"
make PHYSICS_DIR="$PHYS" -j10 build/gpu_resident_chain_test build/gpu_cmdlist_test > "$EVID/build.log" 2>&1 || { echo "GPU_L2G_STAGE_CHIP: FAIL build"; exit 1; }
sha256sum build/gpu_resident_chain_test build/gpu_cmdlist_test build/libomega_gpu.a > "$EVID/binaries.sha256"
fail=0
./build/gpu_resident_chain_test --host > "$EVID/host.log" 2>&1 || fail=1
timeout 900 ./build/gpu_resident_chain_test ${RES_ARGS:---quick --only-real --stamps} --reps "${REPS:-9}" --stamps-raw "$EVID/stamps.bin" --out "$EVID/receipt.json" > "$EVID/resident.log" 2>&1 || fail=1
grep -E "^PROBE|^RES|^SLOPE|^STAMPSUM|^OVERSIZE|^CANCEL|^capacity|FAIL|checks" "$EVID/resident.log" | grep -v "^STAMPSUM.*rep=[0-7] "
timeout 300 ./build/gpu_cmdlist_test > "$EVID/cmdlist.log" 2>&1 || fail=1; echo "cmdlist: $(tail -1 "$EVID/cmdlist.log")"
echo "end $(date -u +%FT%TZ)" >> "$EVID/device.txt"
(cd "$EVID" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
if [ $fail = 0 ]; then echo "GPU_L2G_STAGE_CHIP: PASS"; else echo "GPU_L2G_STAGE_CHIP: FAIL"; exit 1; fi
