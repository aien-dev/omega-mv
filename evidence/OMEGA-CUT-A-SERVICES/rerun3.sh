#!/bin/sh
# L202 rerun after the review fixes (join keeps the handle on failure, world-owned image copy, glibc-exact
# atoi, omega_psvc_diag): every gate those files reach, then 20x old-vs-new seat over the simulated driver.
out=$HOME/handoffs/L202/after3; mkdir -p "$out"
cd $HOME/workspace/omega-omega-native-platform-services || exit 9
P=$HOME/workspace/physics-pf-6d7cf0d4
M="make PHYSICS_DIR=$P AIENOS_LOCK_REPO=$HOME/workspace/pipeline/clones/aienos"
: > "$out/summary.txt"
for t in test-platform-services test-rx-seat-fake test-gpu-engine-freestanding test-gpu-engine test-gpu-elementwise test-gpu-encoding test-r16-authpath-linkmap-silicon; do
  $M $t > "$out/$t.log" 2>&1; echo "$t $?" >> "$out/summary.txt"; done
for b in build/rx_r12_silicon_test build/rx_r13_living_silicon build/rx_r14_recovery_silicon; do
  rm -f "$b"; $M "$b" > "$out/build_$(basename $b).log" 2>&1; echo "$b $?" >> "$out/summary.txt"; done
: > "$out/runs.txt"
for v in old new; do
  b=/tmp/l202-old/seat_old; [ $v = new ] && b=build/rx_resident_gpu_fake_test
  for i in $(seq 1 20); do $b > "$out/$v-$i.out" 2> "$out/$v-$i.err"; echo "$v run $i rc $?" >> "$out/runs.txt"; done
done
( cd "$out"; for i in $(seq 1 20); do
    cmp -s old-$i.out new-$i.out && cmp -s old-$i.err new-$i.err && echo "pair $i identical" || echo "pair $i DIFFERENT"
  done ) > "$out/compare.txt"
echo done > "$out/DONE"
