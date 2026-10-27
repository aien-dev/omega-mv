#!/bin/sh
# Usage: sh tests/osc_gpu/om_diff.sh <omc> <om_oracle> <test_osc_gpu> <ir-dir> <work-dir>
# Differential of the seeded .om GPU lowering (seed/oscgpu) against the C one (src/omega_osc_gpu.c through
# tests/osc_gpu/om_oracle, a test-only oracle). Every case compares bytes, fail closed:
#   dump    the .om's stdout, exit code and .bwir dump == om_oracle dump's, for every unit of <ir-dir> x every
#           function name in it (and one name it lacks) x back-end mutant 0..8, for damaged units, and for every
#           variant om_oracle makes of each unit (one field changed, still admitted by osc_ir_decode; mutant 0)
#   encode  where a dump was written: om_oracle encode of the .om's dump gives the .code/.man om_oracle build gives
#   embed   tests/osc_gpu/omegese/EMBEDDED: the code built from the .om's dump has the sha256 the embedding carries
#   chip    test_osc_gpu with OSC_GPU_OM_DIR: every kernel it loads has the bytes the .om's dump encodes to
# Prints OMDIFF PASS n=N same=N diff=0, or OMDIFF FAIL ... and exits 1.
set -u
OMC=$1; ORA=$2; TEST=$3; IRD=$4; WORK=$5
OMC_SHA=2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f
got=$(sha256sum "$OMC" | cut -d' ' -f1)
[ "$got" = "$OMC_SHA" ] || { echo "OMDIFF FAIL: $OMC has sha256 $got, not the pinned seed $OMC_SHA"; exit 1; }
rm -rf "$WORK"; mkdir -p "$WORK/var" "$WORK/bad" "$WORK/emb" "$WORK/om"
D=${OM_SRC_DIR:-seed/oscgpu} # OM_SRC_DIR: a copy of seed/oscgpu, for the red run of a mutated .om
SRC="$D/oscir.om $D/gpuir.om $D/gpulow.om $D/gpuemit.om $D/gpumain.om"
cat $SRC > "$WORK/gpu.om"
"$OMC" < "$WORK/gpu.om" > "$WORK/gpu" 2> "$WORK/gpu.err" || { echo "OMDIFF FAIL: omc refuses the .om lowering:"; cat "$WORK/gpu.err"; exit 1; }
chmod +x "$WORK/gpu"
GPU=$WORK/gpu
echo "om_diff: omc $got"
for f in $SRC; do echo "om_diff: $(sha256sum "$f")"; done
echo "om_diff: $(sha256sum "$GPU" | sed "s|$WORK/||")"
n=0; same=0; diff=0
bad() { diff=$((diff + 1)); [ $diff -le 20 ] && echo "DIFF $*"; }
# one case: unit, kernel name, mutant, label
one() {
    rm -f "$WORK/c.bwir" "$WORK/o.bwir"
    co=$("$ORA" dump "$1" "$2" "$WORK/c.bwir" "$3" 2>&1); cr=$?
    oo=$("$GPU" "$1" "$2" "$WORK/o.bwir" "$3" 2>&1); orc=$?
    n=$((n + 1))
    if [ "$cr" != "$orc" ] || [ "$co" != "$oo" ]; then bad "$4: C exit $cr '$co', .om exit $orc '$oo'"; return; fi
    if [ $cr = 0 ]; then
        cmp -s "$WORK/c.bwir" "$WORK/o.bwir" || { bad "$4: dumps differ"; return; }
        "$ORA" encode "$WORK/o.bwir" "$WORK/o.code" "$WORK/o.man" > "$WORK/oe.out" 2>&1; er=$?
        "$ORA" build "$1" "$2" "$WORK/c.code" "$WORK/c.man" "$3" > "$WORK/cb.out" 2>&1; br=$?
        [ "$er" = "$br" ] && cmp -s "$WORK/oe.out" "$WORK/cb.out" || { bad "$4: encode exit $er, build exit $br"; return; }
        if [ $br = 0 ]; then
            cmp -s "$WORK/o.code" "$WORK/c.code" && cmp -s "$WORK/o.man" "$WORK/c.man" || { bad "$4: encoded bytes differ"; return; }
        fi
    elif [ -e "$WORK/o.bwir" ]; then bad "$4: .om wrote a dump while refusing"; return
    fi
    same=$((same + 1))
}
# the corpus units x their functions and one missing name x mutants 0..8
nu=0
for ir in "$IRD"/*.ir; do
    names=$("$ORA" names "$ir")
    for m in 0 1 2 3 4 5 6 7 8; do
        for k in $names no_such_kernel; do one "$ir" "$k" $m "$(basename "$ir") $k m$m"; done
    done
    nu=$((nu + 1))
done
echo "om_diff: corpus $nu units x names x mutants 0..8: n=$n"
# damaged units: empty, truncated, a flipped header byte, a missing file (I/O: exit 2 on both sides, messages differ)
src=$IRD/kernel_vadd.ir
sz=$(wc -c < "$src")
: > "$WORK/bad/empty.ir"
head -c $((sz / 2)) "$src" > "$WORK/bad/half.ir"
head -c $((sz - 1)) "$src" > "$WORK/bad/short.ir"
{ cat "$src"; printf 'x'; } > "$WORK/bad/trail.ir"
{ printf 'X'; tail -c +2 "$src"; } > "$WORK/bad/magic.ir"
n0=$n
for b in "$WORK"/bad/*.ir; do one "$b" vadd 0 "damaged $(basename "$b")"; done
"$ORA" dump "$WORK/bad/none.ir" vadd "$WORK/c.bwir" 0 > /dev/null 2>&1; cr=$?
"$GPU" "$WORK/bad/none.ir" vadd "$WORK/o.bwir" 0 > /dev/null 2>&1; orc=$?
n=$((n + 1)); if [ $cr = 2 ] && [ $orc = 2 ]; then same=$((same + 1)); else bad "missing file: C exit $cr, .om exit $orc"; fi
for mu in 9 -1 x; do
    rm -f "$WORK/o.bwir"
    "$GPU" "$src" vadd "$WORK/o.bwir" $mu > /dev/null 2>&1; orc=$?
    n=$((n + 1)); if [ $orc = 2 ] && [ ! -e "$WORK/o.bwir" ]; then same=$((same + 1)); else bad "mutant $mu: .om exit $orc"; fi
done
echo "om_diff: damaged units and usage: n=$((n - n0))"
# variants: every unit one field away that decode still admits (mutant 0)
nv0=$n
for ir in "$IRD"/kernel_*.ir; do
    d=$WORK/var/$(basename "$ir" .ir); mkdir -p "$d"
    "$ORA" variants "$ir" "$d" > /dev/null || { bad "variants of $ir"; continue; }
    names=$("$ORA" names "$ir")
    for v in "$d"/*.ir; do
        [ -f "$v" ] || continue
        for k in $names; do one "$v" "$k" 0 "$(basename "$ir") $(basename "$v") $k"; done
    done
done
echo "om_diff: variants: n=$((n - nv0))"
# embedded kernels
while read -r file kname sha where; do
    ir=$IRD/kernel_om_$(basename "$file" .omegese).ir
    n=$((n + 1))
    if "$GPU" "$ir" "$kname" "$WORK/emb/o.bwir" 0 > /dev/null && "$ORA" encode "$WORK/emb/o.bwir" "$WORK/emb/o.code" "$WORK/emb/o.man" > /dev/null; then
        g=$(sha256sum "$WORK/emb/o.code" | cut -d' ' -f1)
        if [ "$g" = "$sha" ]; then same=$((same + 1)); echo "om_diff: embedded $file $kname $g"; else bad "embedded $file: $g, $where carries $sha"; fi
    else bad "embedded $file: the .om refuses it"; fi
done < tests/osc_gpu/omegese/EMBEDDED
# the kernel tests, each kernel loaded from the bytes the .om's dump encodes to
sh tests/osc_gpu/om_build_all.sh "$GPU" "$ORA" "$IRD" "$WORK/om" || { bad "om_build_all"; }
out=$(OSC_GPU_OM_DIR=$WORK/om "$TEST" "$IRD" 2>&1); tr=$?
line=$(printf '%s\n' "$out" | grep '^OMBYTES ')
n=$((n + 1))
case "$line" in
    "OMBYTES identical="*" differ=0") [ $tr = 0 ] && { same=$((same + 1)); echo "om_diff: test_osc_gpu: $line"; } || bad "test_osc_gpu exit $tr: $line" ;;
    *) bad "test_osc_gpu: '$line' (exit $tr)" ;;
esac
if [ $diff = 0 ] && [ $same = $n ]; then echo "OMDIFF PASS n=$n same=$same diff=0"; exit 0; fi
echo "OMDIFF FAIL n=$n same=$same diff=$diff"
exit 1
