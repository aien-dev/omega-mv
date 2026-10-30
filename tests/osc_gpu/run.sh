#!/bin/sh
# Usage: sh tests/osc_gpu/run.sh <omegese> <omega_emit> <checker2> <test_osc_gpu> <work-dir> [<osc_gpu_build>]
# Every kernel the back end is tested on is first emitted to IR and accepted by checker2: the back end
# never sees a kernel checker2 refuses. The kernels written in Omegese (tests/osc_gpu/omegese, C1 of
# docs/OMEGA.md) go through omegese emit first and land as kernel_om_<name>.ir.
set -u
OMS=$1; OME=$2; C2=$3; TEST=$4; WORK=$5
BUILD=${6:-}
rm -rf "$WORK"; mkdir -p "$WORK"
n=0
for f in tests/osc_gpu/omegese/*.omegese; do
    [ -f "$f" ] || continue
    name=om_$(basename "$f" .omegese)
    if ! "$OMS" emit "$f" "$WORK/$name.json" >/dev/null 2>&1; then echo "FAIL: omegese refuses $f"; "$OMS" emit "$f" "$WORK/$name.json"; exit 1; fi
done
for f in tests/checker2/kernels/*.json tests/osc_gpu/kernels/*.json "$WORK"/om_*.json; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .json)
    if ! "$OME" emit "$f" "$WORK/kernel_$name.ir" >/dev/null 2>&1; then echo "FAIL: omega_emit refuses $f"; exit 1; fi
    if ! "$C2" check "$WORK/kernel_$name.ir" >/dev/null 2>&1; then echo "FAIL: checker2 refuses $f"; "$C2" check "$WORK/kernel_$name.ir"; exit 1; fi
    n=$((n + 1))
done
echo "run.sh: $n kernels emitted and accepted by checker2"
# broken kernels (tests/osc_gpu/omegese/bad/EXPECT: file, the stage that refuses it, the reason it gives)
r=0
while read -r file stage reason; do
    f=tests/osc_gpu/omegese/bad/$file
    if [ "$stage" = omegese ]; then
        out=$("$OMS" emit "$f" "$WORK/bad.json"); rc=$?
        [ $rc = 1 ] || { echo "FAIL: omegese did not refuse $file (exit $rc)"; exit 1; }
    else
        "$OMS" emit "$f" "$WORK/bad.json" >/dev/null || { echo "FAIL: omegese refused $file, which only checker2 should"; exit 1; }
        "$OME" emit "$WORK/bad.json" "$WORK/bad.ir" >/dev/null 2>&1 || { echo "FAIL: omega_emit refused $file"; exit 1; }
        out=$("$C2" check "$WORK/bad.ir" 2>&1); rc=$?
        [ $rc != 0 ] || { echo "FAIL: checker2 accepted $file"; exit 1; }
    fi
    case "$out" in *"$reason"*) ;; *) echo "FAIL: $file refused for another reason: $out"; exit 1 ;; esac
    r=$((r + 1))
done < tests/osc_gpu/omegese/bad/EXPECT
echo "run.sh: $r broken kernels refused with their expected reasons"
# kernels whose bytes are embedded elsewhere (tests/osc_gpu/omegese/EMBEDDED: file, kernel, code sha256, where): the code built
# from today's source must still be the bytes the embedding carries, or the embedding is stale
if [ -n "$BUILD" ]; then
    e=0
    while read -r file kname sha where; do
        ir=$WORK/kernel_om_$(basename "$file" .omegese).ir
        "$BUILD" "$ir" "$kname" "$WORK/emb.bin" "$WORK/emb.h" EMB >/dev/null || { echo "FAIL: osc_gpu_build refuses $file"; exit 1; }
        got=$(sed -n "s/^#define EMB_CODE_SHA256 \"\(.*\)\"/\1/p" "$WORK/emb.h")
        [ "$got" = "$sha" ] || { echo "FAIL: $file builds to code sha256 $got, but $where embeds $sha"; exit 1; }
        e=$((e + 1))
    done < tests/osc_gpu/omegese/EMBEDDED
    echo "run.sh: $e embedded kernels still build to the bytes they embed"
fi
"$TEST" "$WORK"
