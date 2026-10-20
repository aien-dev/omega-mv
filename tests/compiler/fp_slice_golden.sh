#!/bin/sh
# Re-assemble the OSC-4 float-slice reference instructions with GNU as and check that
# every word objdump prints is in the golden table of tests/compiler/test_osc_backend.c
# (section d3). GNU as is the reference oracle only; nothing here feeds the compiler.
# Usage: sh tests/compiler/fp_slice_golden.sh [scratch-dir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
dir=${1:-build/osc/fp_slice_golden}
mkdir -p "$dir"
as -march=armv8.2-a+fp16+bf16 "$here/fp_slice_golden.s" -o "$dir/g.o"
objdump -d "$dir/g.o" | awk '/^ *[0-9a-f]+:/ {print $2}' > "$dir/words.txt"
n=0; bad=0
while read -r w; do
    n=$((n + 1))
    if ! grep -qi "0x${w}u" "$here/test_osc_backend.c"; then
        echo "MISSING: GNU as word 0x$w is not in test_osc_backend.c"; bad=$((bad + 1))
    fi
done < "$dir/words.txt"
[ "$n" -gt 0 ] || { echo "no words assembled"; exit 1; }
[ "$bad" -eq 0 ] || { echo "FP_SLICE_GOLDEN_FAIL words=$n missing=$bad"; exit 1; }
echo "FP_SLICE_GOLDEN_PASS words=$n missing=0 ($(as --version | head -1))"
