#!/bin/sh
# L202 gate runner: gates.sh <tag>. Host-only targets, pinned physics, no device.
# Writes ~/handoffs/L202/<tag>/<target>.log and summary.txt (target exit-code).
tag=$1; out=$HOME/handoffs/L202/$tag; mkdir -p "$out"
cd $HOME/workspace/omega-omega-native-platform-services || exit 9
P="PHYSICS_DIR=$HOME/workspace/physics-pf-6d7cf0d4 AIENOS_LOCK_REPO=$HOME/workspace/pipeline/clones/aienos"
: > "$out/summary.txt"
for t in test-gpu-engine test-gpu-engine-freestanding test-codegen-freestanding test-gpu-session-platform \
         test-gpu-wait test-gpu-engine-sim test-gpu-serving-alloc test-gpu-elementwise test-gpu-cmdlist \
         test-gpu-matmul-api test-gpu-attention test-divsqrt-host test-ldst-host test-gpu-code-alloc \
         test-gpu-decode-graph test-gpu-engine-mutants test-gpu-wait-mutants $EXTRA_TARGETS \
         build/rx_r12_silicon_test build/rx_r13_living_silicon build/rx_r14_recovery_silicon; do
  name=$(echo $t | tr / _)
  make $P $t > "$out/$name.log" 2>&1; rc=$?
  echo "$t $rc" >> "$out/summary.txt"
done
# object code of the product library (the token path) for identity checks
mkdir -p "$out/objdump"
for o in build/*.o; do objdump -d --no-show-raw-insn "$o" 2>/dev/null | tail -n +3 > "$out/objdump/$(basename $o).dis"; done
( cd "$out/objdump" && sha256sum *.dis > ../objdump.sha256 )
echo done > "$out/DONE"
