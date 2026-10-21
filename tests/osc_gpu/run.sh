#!/bin/sh
# Usage: sh tests/osc_gpu/run.sh <omega_emit> <checker2> <test_osc_gpu> <work-dir>
# Every kernel the back end is tested on is first emitted to IR and accepted by checker2: the back end
# never sees a kernel checker2 refuses.
set -u
OME=$1; C2=$2; TEST=$3; WORK=$4
rm -rf "$WORK"; mkdir -p "$WORK"
n=0
for f in tests/checker2/kernels/*.json tests/osc_gpu/kernels/*.json; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .json)
    if ! "$OME" emit "$f" "$WORK/kernel_$name.ir" >/dev/null 2>&1; then echo "FAIL: omega_emit refuses $f"; exit 1; fi
    if ! "$C2" check "$WORK/kernel_$name.ir" >/dev/null 2>&1; then echo "FAIL: checker2 refuses $f"; "$C2" check "$WORK/kernel_$name.ir"; exit 1; fi
    n=$((n + 1))
done
echo "run.sh: $n kernels emitted and accepted by checker2"
"$TEST" "$WORK"
