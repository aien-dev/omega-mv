#!/bin/sh
# Usage: sh tests/osc_gpu/om_build_all.sh <gpu> <om_oracle> <ir-dir> <out-dir>
# For every <ir-dir>/kernel_<base>.ir and back-end mutant 0..8: the seeded .om lowering (<gpu>, built from
# seed/oscgpu) writes the BlackwellIR dump of the unit's first function (the one test_osc_gpu loads), and
# om_oracle encode (test-only oracle: the C register allocator and encoder) turns it into
# <out-dir>/<base>__m<mutant>.code and .man. A refused kernel leaves no files. test_osc_gpu reads them with
# OSC_GPU_OM_DIR=<out-dir> and compares them with the C back end's bytes.
set -u
GPU=$1; ORA=$2; IRD=$3; OUT=$4
mkdir -p "$OUT"
for ir in "$IRD"/kernel_*.ir; do
    base=$(basename "$ir" .ir); base=${base#kernel_}
    k=$("$ORA" names "$ir" | head -n 1)
    [ -n "$k" ] || { echo "om_build_all: no function in $ir"; exit 1; }
    for m in 0 1 2 3 4 5 6 7 8; do
        rm -f "$OUT/$base.bwir"
        "$GPU" "$ir" "$k" "$OUT/$base.bwir" $m > /dev/null || continue
        "$ORA" encode "$OUT/$base.bwir" "$OUT/${base}__m$m.code" "$OUT/${base}__m$m.man" > /dev/null || exit 1
    done
    rm -f "$OUT/$base.bwir"
done
