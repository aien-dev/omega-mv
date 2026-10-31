#!/bin/sh
# C1Land chip run: test_osc_gpu_chip green (mutant 0) and red (mutant 1), with the kernels' bytes taken from the
# seeded .om lowering (OSC_GPU_OM_DIR, built by tests/osc_gpu/om_build_all.sh) and asserted equal to the C build.
# Called inside flock -w 900 /tmp/aien-gb10.lock; writes logs to $1 and BOARD GPU-START/GPU-END lines.
set -u
OUT=$1; OMD=$2
B=/home/drakestapleton/handoffs/BOARD.md
cd /home/drakestapleton/work/C1Land/omega || exit 2
mkdir -p "$OUT"
echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) C1Land: GPU-START test_osc_gpu_chip green+red with OSC_GPU_OM_DIR (.om lowering), c1/chip-fix $(git rev-parse --short HEAD), <=15 min" >> "$B"
for m in 0 1; do
    n=$([ $m = 0 ] && echo green || echo red-mutant1)
    {
        date -u
        git rev-parse HEAD
        sha256sum build/osc_gpu/test_osc_gpu_chip
        echo "COMMAND: timeout 300 stdbuf -oL -eL env OSC_GPU_OM_DIR=$OMD OSC_GPU_MUTANT=$m build/osc_gpu/test_osc_gpu_chip build/osc_gpu/ir"
        timeout 300 stdbuf -oL -eL env OSC_GPU_OM_DIR="$OMD" OSC_GPU_MUTANT=$m build/osc_gpu/test_osc_gpu_chip build/osc_gpu/ir 2>&1
        echo "EXIT: $?"
        date -u
    } > "$OUT/chip-$n.log" 2>&1
done
echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) C1Land: GPU-END test_osc_gpu_chip; logs $OUT" >> "$B"
