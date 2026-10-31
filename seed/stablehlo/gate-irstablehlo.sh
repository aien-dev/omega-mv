#!/bin/sh
# Host-only verification of seed/stablehlo/irstablehlo.om on AArch64 Linux.
# IREE (iree-compile, iree-run-module) and the XLA CPU PJRT client in jaxlib are
# interop oracles, not Omega-native: they only read the module Omega wrote and
# never generate kernels. No result here is hardware qualification.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN (an oracle or the host is absent).
# Oracle locations: IREE_COMPILE, IREE_RUN, PYTHON (defaults from PATH).
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/stablehlo
D=seed/stablehlo
fail() { printf 'FAIL %s\n' "$*"; exit 1; }
notrun() { printf 'NOT_RUN %s\n' "$*"; exit 3; }
[ "$(uname -sm)" = 'Linux aarch64' ] || notrun 'requires AArch64 Linux'
IREE_COMPILE=${IREE_COMPILE:-$(command -v iree-compile || true)}
IREE_RUN=${IREE_RUN:-$(command -v iree-run-module || true)}
PYTHON=${PYTHON:-$(command -v python3 || true)}
[ -n "$IREE_COMPILE" ] && [ -x "$IREE_COMPILE" ] || notrun 'requires iree-compile oracle'
[ -n "$IREE_RUN" ] && [ -x "$IREE_RUN" ] || notrun 'requires iree-run-module oracle'
[ -n "$PYTHON" ] && "$PYTHON" -c 'import jax, jaxlib, numpy' 2>/dev/null || notrun 'requires jaxlib PJRT CPU oracle'
OMC=seed/out/omc
pin=469532a50ecb8a0e5ca8e914dd46754bb3fbde7825fb33b7037208caf09779bc
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
rm -rf "$B"
mkdir -p "$B"
printf 'SOURCE %s\n' "$(git rev-parse HEAD)"
printf 'COMPILER %s\n' "$pin"
printf 'ORACLE iree-compile %s\n' "$("$IREE_COMPILE" --version 2>&1 | sed -n 's/^ *IREE compiler version //p' | head -n 1)"
printf 'ORACLE iree-run-module %s\n' "$(sha256sum "$IREE_RUN" | cut -d ' ' -f 1)"
printf 'ORACLE pjrt %s\n' "$(JAX_PLATFORMS=cpu "$PYTHON" -c 'import jax, jaxlib; print("jax", jax.__version__, "jaxlib", jaxlib.__version__)')"
uname -sm

nice -n 19 "$OMC" < "$D/irstablehlo.om" > "$B/irstablehlo" 2> "$B/compile.err" || fail 'translator compile'
[ ! -s "$B/compile.err" ] || fail 'translator compiler stderr'
chmod +x "$B/irstablehlo"
T="$B/irstablehlo"
for unit in vecadd refuse; do
    { printf 'ir\n'; cat "$D/$unit.unit.om"; } | nice -n 19 "$OMC" > "$B/$unit.ir" || fail "$unit ir"
done
# Wire v7 fixtures (spec/one-layer-format.md section 10): omc ir refuses float
# words at this pin, so the f32/f64 scalar IR is written byte by byte here.
# Header: magic OSC1IR\0, version, nstructs 0, nregions 0, nfuncs 1. Function:
# name, nparams 2, return tag, nvregs 3 with tags, one block (first 0, count 2),
# BIN add r2 = r0 + r1, RET r2, empty requires and ensures.
bytes() { for b in "$@"; do printf "\\$(printf %03o "$b")"; done; }
fixture() {
    ver=$1 tag=$2; shift 2
    bytes 79 83 67 49 73 82 0 "$ver"
    [ "$ver" = 7 ] && bytes 0 0 0
    bytes 1 0 "$#"; bytes "$@"
    bytes 2 "$tag" 3 0 "$tag" "$tag" "$tag" 1 0 0 0 0 0 2 0 0 0 2 0 0 0
    bytes 3 1 2 0 0 0 1 0 15 2 0 0 0 0 0
}
fixture 7 13 118 97 100 100 102 51 50 > "$B/f32.ir"
fixture 7 14 118 97 100 100 102 54 52 > "$B/f64.ir"
fixture 1 13 118 97 100 100 102 51 50 > "$B/v1f32.ir"

iree_compile() { nice -n 19 "$IREE_COMPILE" --iree-hal-target-device=local \
    --iree-hal-local-target-device-backends=llvm-cpu --iree-input-type=stablehlo --iree-input-demote-f64-to-f32=false \
    --iree-input-demote-i64-to-i32=false "$1" -o "$2"; }
# iree_check MODULE.vmfb TYPE EXPECTED INPUT...; byte-equality compare.
iree_check() {
    vm=$1 ty=$2 ex=$3; shift 3
    set -- "$@"
    args=
    for x in "$@"; do args="$args --input=8x$ty=$x"; done
    # shellcheck disable=SC2086
    nice -n 19 "$IREE_RUN" --device=local-task --module="$vm" --function=main $args \
        --expected_output="8x$ty=$ex" --equality_mode=exact
}
pjrt() { PJRT_ORACLE_PLATFORM=cpu nice -n 19 "$PYTHON" "$D/pjrt-oracle.py" "$@" 2>> "$B/pjrt.stderr"; }
otype() { case "$1" in i32) echo int ;; ui32) echo uint ;; i64) echo long ;; *) echo - ;; esac; }

i=0
tab=$(printf '\t')
while IFS="$tab" read -r fx fn ty ex in1 in2 in3; do
    case "$fx" in ''|'#'*) continue ;; esac
    i=$((i+1))
    id="case-$i-$fn"
    nice -n 19 "$T" 8 "$fn" < "$B/$fx.ir" > "$B/$id.mlir" || fail "$id translate"
    nice -n 19 "$T" 8 "$fn" < "$B/$fx.ir" > "$B/$id.again.mlir" || fail "$id translate again"
    cmp -s "$B/$id.mlir" "$B/$id.again.mlir" || fail "$id nondeterministic"
    set -- "$in1"
    [ -z "$in2" ] || set -- "$@" "$in2"
    [ -z "$in3" ] || set -- "$@" "$in3"
    iree_compile "$B/$id.mlir" "$B/$id.vmfb" 2> "$B/$id.iree-compile.err" || fail "$id iree-compile refused Omega module"
    iree_check "$B/$id.vmfb" "$ty" "$ex" "$@" > "$B/$id.iree.out" 2>&1 || fail "$id IREE mismatch: $(grep -m1 FAILED "$B/$id.iree.out")"
    grep -q '^\[SUCCESS\]' "$B/$id.iree.out" || fail "$id IREE gave no SUCCESS line"
    pjrt "$B/$id.mlir" "$ty" 8 "$ex" "$@" > "$B/$id.pjrt.out" || fail "$id PJRT: $(tail -n 1 "$B/$id.pjrt.out")"
    # Omega-native differential: the same scalar function, compiled natively by
    # omc, must give the expected value for every element (integer rows).
    ot=$(otype "$ty")
    if [ "$ot" != - ]; then
        { cat "$D/vecadd.unit.om"; printf 'fn main to int\n'
          for k in 1 2 3 4 5 6 7 8; do
              call="$fn"
              for v in "$@"; do call="$call $ot $(printf '%s' "$v" | cut -d , -f "$k")"; done
              printf 'if ne %s %s %s\nexit 1\nend\n' "$call" "$ot" "$(printf '%s' "$ex" | cut -d , -f "$k")"
          done
          printf 'ret 0\nend\n'; } > "$B/$id.native.om"
        nice -n 19 "$OMC" < "$B/$id.native.om" > "$B/$id.native" 2> "$B/$id.native.err" || fail "$id native compile"
        chmod +x "$B/$id.native"
        nice -n 19 "$B/$id.native" || fail "$id Omega-native result differs from table"
        native=native
    else
        native='native NOT_RUN (omc ir has no float words at this pin)'
    fi
    printf 'PASS %s %s deterministic, IREE exact match, PJRT bitwise match, %s\n' "$id" "$ty" "$native"
done < "$D/cases.tsv"
[ "$i" = 7 ] || fail "expected 7 case rows, ran $i"

# Negative controls: each must be caught.
neg=0
expect_mismatch() {
    label=$1 mod=$2 ty=$3 ex=$4; shift 4
    iree_compile "$mod" "$mod.vmfb" 2> "$mod.iree-compile.err" || fail "$label: iree-compile refused instead of mismatching"
    if iree_check "$mod.vmfb" "$ty" "$ex" "$@" > "$mod.iree.out" 2>&1; then fail "$label: IREE accepted"; fi
    grep -q '^\[FAILED\]' "$mod.iree.out" || fail "$label: IREE failed without a mismatch line"
    set +e; pjrt "$mod" "$ty" 8 "$ex" "$@" > "$mod.pjrt.out"; rc=$?; set -e
    [ "$rc" = 1 ] || fail "$label: PJRT rc $rc, expected 1 (mismatch)"
    neg=$((neg+1))
    printf 'PASS negative %s: IREE [FAILED], PJRT MISMATCH\n' "$label"
}
expect_refused() {
    label=$1 mod=$2
    if iree_compile "$mod" "$mod.vmfb" 2> "$mod.iree-compile.err"; then fail "$label: iree-compile accepted"; fi
    set +e; pjrt "$mod" i32 8 0,0,0,0,0,0,0,0 0,0,0,0,0,0,0,0 0,0,0,0,0,0,0,0 > "$mod.pjrt.out"; rc=$?; set -e
    [ "$rc" = 2 ] || fail "$label: PJRT rc $rc, expected 2 (module refused)"
    neg=$((neg+1))
    printf 'PASS negative %s: iree-compile and PJRT refused\n' "$label"
}
I32A=0,1,-1,2147483646,-2147483647,123456,-98765,7
I32B=0,2,-1,1,-1,654321,98765,-10
I32E=0,3,-2,2147483647,-2147483648,777777,0,-3
F32A=1.5,0.25,-2,1048576,0.1,16777216,1e38,-7.75
F32B=2.25,0.5,2,0.5,0.2,1,1e38,0.125
sed 's/stablehlo\.add/stablehlo.subtract/' "$B/case-1-vaddi32.mlir" > "$B/neg-wrongop.mlir"
cmp -s "$B/neg-wrongop.mlir" "$B/case-1-vaddi32.mlir" && fail 'mutant identical'
expect_mismatch 'wrong op (add->subtract) i32' "$B/neg-wrongop.mlir" i32 "$I32E" "$I32A" "$I32B"
sed 's/stablehlo\.add/stablehlo.multiply/' "$B/case-6-vaddf32.mlir" > "$B/neg-wrongop-f32.mlir"
expect_mismatch 'wrong op (add->multiply) f32' "$B/neg-wrongop-f32.mlir" f32 \
    3.75,0.75,0,1048576.5,0.300000012,16777216,1.99999994e38,-7.625 "$F32A" "$F32B"
cp "$B/case-1-vaddi32.mlir" "$B/neg-wrongexp.mlir"
expect_mismatch 'wrong expected (element 0 = 1) i32' "$B/neg-wrongexp.mlir" i32 1,3,-2,2147483647,-2147483648,777777,0,-3 "$I32A" "$I32B"
cp "$B/case-6-vaddf32.mlir" "$B/neg-wrongexp-f32.mlir"
expect_mismatch 'wrong expected (element 4 one ulp up) f32' "$B/neg-wrongexp-f32.mlir" f32 \
    3.75,0.75,0,1048576.5,0.300000042,16777216,1.99999994e38,-7.625 "$F32A" "$F32B"
sed '$d' "$B/case-1-vaddi32.mlir" > "$B/neg-nobrace.mlir"
expect_refused 'malformed (closing brace dropped)' "$B/neg-nobrace.mlir"
sed 's/stablehlo\.add/stablehlo.addx/' "$B/case-1-vaddi32.mlir" > "$B/neg-badop.mlir"
expect_refused 'malformed (unknown op stablehlo.addx)' "$B/neg-badop.mlir"
sed 's/-> tensor<8xi32>/-> tensor<8xf32>/' "$B/case-1-vaddi32.mlir" > "$B/neg-badtype.mlir"
expect_refused 'malformed (return type f32 vs i32 value)' "$B/neg-badtype.mlir"

# Documented divergence: Omega signed add traps on overflow; the lifted module does not.
{ cat "$D/vecadd.unit.om"; printf 'fn main to int\nx = vaddi32 int 2147483647 int 1\nret 0\nend\n'; } > "$B/overflow.native.om"
nice -n 19 "$OMC" < "$B/overflow.native.om" > "$B/overflow.native" 2> "$B/overflow.native.err" || fail 'overflow native compile'
chmod +x "$B/overflow.native"
set +e; nice -n 19 "$B/overflow.native" 2> /dev/null; nrc=$?; set -e
[ "$nrc" != 0 ] || fail 'Omega native signed overflow did not trap'
ONES=1,1,1,1,1,1,1,1
MAXS=2147483647,2147483647,2147483647,2147483647,2147483647,2147483647,2147483647,2147483647
MINS=-2147483648,-2147483648,-2147483648,-2147483648,-2147483648,-2147483648,-2147483648,-2147483648
iree_check "$B/case-1-vaddi32.vmfb" i32 "$MINS" "$MAXS" "$ONES" > "$B/overflow.iree.out" 2>&1 || fail 'IREE overflow result was not the wrapped value'
pjrt "$B/case-1-vaddi32.mlir" i32 8 "$MINS" "$MAXS" "$ONES" > "$B/overflow.pjrt.out" || fail 'PJRT overflow result was not the wrapped value'
printf 'DIVERGENCE signed overflow: Omega native exit %s (trap); IREE and PJRT return -2147483648 (StableHLO: implementation-defined)\n' "$nrc"

# Translator refusals: exact class on stderr, nonzero status, nothing on stdout.
refuse() {
    label=$1 class=$2 input=$3; shift 3
    set +e; nice -n 19 "$T" "$@" < "$input" > "$B/refuse.out" 2> "$B/refuse.err"; rc=$?; set -e
    [ "$rc" != 0 ] || fail "refusal $label exited 0"
    [ ! -s "$B/refuse.out" ] || fail "refusal $label wrote stdout"
    [ "$(cat "$B/refuse.err")" = "irstablehlo: $class" ] || fail "refusal $label said $(cat "$B/refuse.err")"
    neg=$((neg+1))
    printf 'PASS refusal %s -> %s (exit %s)\n' "$label" "$class" "$rc"
}
refuse 'integer div' UNSUPPORTED_OP "$B/refuse.ir" 8 divi32
refuse 'integer shl' UNSUPPORTED_OP "$B/refuse.ir" 8 shlu32
refuse 'branches' UNSUPPORTED_OP "$B/refuse.ir" 8 maxi32
refuse 'mixed element types' UNSUPPORTED_TAG "$B/refuse.ir" 8 widen
refuse 'bool result' UNSUPPORTED_TAG "$B/refuse.ir" 8 isodd
refuse 'f32 tag in wire v1' UNSUPPORTED_TAG "$B/v1f32.ir" 8 vaddf32
refuse 'unknown function' NO_FUNCTION "$B/vecadd.ir" 8 nosuch
refuse 'N zero' USAGE "$B/vecadd.ir" 0 vaddi32
refuse 'N leading zero' USAGE "$B/vecadd.ir" 08 vaddi32
refuse 'N above 16777216' USAGE "$B/vecadd.ir" 16777217 vaddi32
refuse 'N not decimal' USAGE "$B/vecadd.ir" 8x vaddi32
refuse 'missing function argument' USAGE "$B/vecadd.ir" 8
{ printf 'X'; tail -c +2 "$B/vecadd.ir"; } > "$B/bad-magic.ir"
refuse 'bad magic' MAGIC "$B/bad-magic.ir" 8 vaddi32
{ head -c 7 "$B/vecadd.ir"; bytes 2; tail -c +9 "$B/vecadd.ir"; } > "$B/bad-version.ir"
refuse 'wire version 2' VERSION "$B/bad-version.ir" 8 vaddi32
size=$(wc -c < "$B/vecadd.ir")
head -c $((size-1)) "$B/vecadd.ir" > "$B/truncated.ir"
refuse 'truncated' TRUNCATED "$B/truncated.ir" 8 vaddi32
{ cat "$B/vecadd.ir"; bytes 0; } > "$B/trailing.ir"
refuse 'trailing byte' TRAILING "$B/trailing.ir" 8 vaddi32
[ "$neg" = 23 ] || fail "expected 23 negative controls and refusals, ran $neg"

sha256sum "$B/irstablehlo" "$B"/case-*.mlir | sed 's/^/SHA256 /'
printf 'PASS irstablehlo: %s cases on two interop oracles (not Omega-native, not hardware), %s negatives caught\n' "$i" "$neg"
