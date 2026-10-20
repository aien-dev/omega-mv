#!/bin/sh
# L202 rerun after the test-side fixes: contract test gate, seat gate, and 20x old-vs-new seat comparison.
out=$HOME/handoffs/L202/after2; mkdir -p "$out"
cd $HOME/workspace/omega-omega-native-platform-services || exit 9
P=$HOME/workspace/physics-pf-6d7cf0d4
M="make PHYSICS_DIR=$P AIENOS_LOCK_REPO=$HOME/workspace/pipeline/clones/aienos"
: > "$out/summary.txt"
for t in test-platform-services test-rx-seat-fake; do $M $t > "$out/$t.log" 2>&1; echo "$t $?" >> "$out/summary.txt"; done
INC="-Isrc -Itests -I$P/m16 -I$P/nvrm -I$P/third_party/nvidia-open-580.173.02/src/common/sdk/nvidia/inc -I$P/third_party/nvidia-open-580.173.02/kernel-open/common/inc -I$P/third_party/nvidia-open-580.173.02/kernel-open/nvidia-uvm -I$P/third_party/nvidia-open-580.173.02/src/nvidia/arch/nvalloc/unix/include"
gcc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -iquote src/runtime $INC -pthread -o /tmp/l202-old/seat_old \
  src/runtime/rx_caproot.c src/runtime/rx_world.c src/runtime/rx_coherent.c src/runtime/rx_native_bind.c \
  /tmp/l202-old/src/runtime/rx_resident_gpu.c src/sha256.c src/omega_evidence.c src/omega_blackwell_codegen.c \
  src/omega_blackwell_encoder.c src/omega_blackwell_qmd.c src/omega_blackwell_matmul.c tests/fake_m16_native.c \
  tests/runtime/rx_resident_gpu_fake_test.c build/aienos-cap/b84c0a67590a934f3f3e001b12ec85ebc086a9eb/libaienos_capability.a -ldl -lm \
  > "$out/seat_old_build.log" 2>&1; echo "seat_old_build $?" >> "$out/summary.txt"
for v in old new; do
  b=/tmp/l202-old/seat_old; [ $v = new ] && b=build/rx_resident_gpu_fake_test
  for i in $(seq 1 20); do $b > "$out/$v-$i.out" 2> "$out/$v-$i.err"; echo "$v run $i rc $?" >> "$out/runs.txt"; done
done
( cd "$out"; for i in $(seq 1 20); do
    cmp -s old-$i.out new-$i.out && cmp -s old-$i.err new-$i.err && echo "pair $i identical" || echo "pair $i DIFFERENT"
  done; for i in $(seq 1 20); do cmp -s new-1.out new-$i.out || echo "new $i differs from new 1"; done ) > "$out/compare.txt"
echo done > "$out/DONE"
