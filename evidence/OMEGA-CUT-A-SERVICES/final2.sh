#!/bin/sh
# L202 final receipts on HEAD after the second review: old seat rebuilt with the current test, 20x old vs new,
# every gate (gates.sh final2), object bytes vs main.
out=$HOME/handoffs/L202/final2; mkdir -p "$out"
cd $HOME/workspace/omega-omega-native-platform-services || exit 9
git rev-parse HEAD > "$out/head-sha.txt"
EXTRA_TARGETS="test-gpu-encoding test-platform-services test-rx-seat-fake test-r16-authpath-linkmap-silicon" \
  $HOME/handoffs/L202/gates.sh final2
P=$HOME/workspace/physics-pf-6d7cf0d4
INC="-Isrc -Itests -I$P/m16 -I$P/nvrm -I$P/third_party/nvidia-open-580.173.02/src/common/sdk/nvidia/inc -I$P/third_party/nvidia-open-580.173.02/kernel-open/common/inc -I$P/third_party/nvidia-open-580.173.02/kernel-open/nvidia-uvm -I$P/third_party/nvidia-open-580.173.02/src/nvidia/arch/nvalloc/unix/include"
gcc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -iquote src/runtime $INC -pthread -o /tmp/l202-old/seat_old \
  src/runtime/rx_caproot.c src/runtime/rx_world.c src/runtime/rx_coherent.c src/runtime/rx_native_bind.c \
  /tmp/l202-old/src/runtime/rx_resident_gpu.c src/sha256.c src/omega_evidence.c src/omega_blackwell_codegen.c \
  src/omega_blackwell_encoder.c src/omega_blackwell_qmd.c src/omega_blackwell_matmul.c tests/fake_m16_native.c \
  tests/runtime/rx_resident_gpu_fake_test.c build/aienos-cap/b84c0a67590a934f3f3e001b12ec85ebc086a9eb/libaienos_capability.a -ldl -lm \
  > "$out/seat_old_build.log" 2>&1; echo "seat_old_build $?" > "$out/seat-summary.txt"
: > "$out/runs.txt"
for v in old new; do
  b=/tmp/l202-old/seat_old; [ $v = new ] && b=build/rx_resident_gpu_fake_test
  for i in $(seq 1 20); do $b > "$out/$v-$i.out" 2> "$out/$v-$i.err"; echo "$v run $i rc $?" >> "$out/runs.txt"; done
done
( cd "$out"; for i in $(seq 1 20); do
    cmp -s old-$i.out new-$i.out && cmp -s old-$i.err new-$i.err && echo "pair $i identical" || echo "pair $i DIFFERENT"
  done ) > "$out/compare.txt"
rm -rf /tmp/l202-ob/head /tmp/l202-ob/DONE && mkdir -p /tmp/l202-ob/head && git archive HEAD | tar -x -C /tmp/l202-ob/head
$HOME/handoffs/L202/objbytes.sh
echo done > "$out/DONE"
