#!/bin/sh
# Host-only CPU verification of seed/spirv/irspirv.om. spirv-val, the OpenCL CPU
# host and parity.py are read-only oracles, never generators of IR or SPIR-V.
# Inputs (all optional): OMC (pinned compiler), SPIRV_VAL, OPENCL_HOST, PYTHON,
# CC, OPENCL_CFLAGS, OPENCL_LIBS, plus the OpenCL runtime environment
# (OCL_ICD_VENDORS, OCL_ICD_FILENAMES, POCL_*) supplied by the caller.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN. Every artifact stays under build/seed/spirv.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/spirv
mkdir -p "$B"
# Drop the previous run's artifacts, keeping only a prebuilt default host.
find "$B" -mindepth 1 -maxdepth 1 ! -name opencl-host -exec rm -rf {} +
: > "$B/gate.log"
say() { printf '%s\n' "$*" | tee -a "$B/gate.log"; }
fail() { say "FAIL $*"; exit 1; }
notrun() { say "NOT_RUN $*"; exit 3; }
[ "$(uname -m)" = aarch64 ] || notrun 'requires AArch64 Linux'
OMC=${OMC:-seed/out/omc}
SPIRV_VAL=${SPIRV_VAL:-spirv-val}
PYTHON=${PYTHON:-python3}
command -v "$SPIRV_VAL" >/dev/null || notrun "requires spirv-val ($SPIRV_VAL)"
command -v "$PYTHON" >/dev/null || notrun "requires Python parity oracle ($PYTHON)"
pin=befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
[ -f "$OMC" ] || fail "compiler $OMC missing"
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
if [ -z "${OPENCL_HOST:-}" ]; then
    # Demo/verification host only; the translator never links or calls it.
    CC=${CC:-cc}
    command -v "$CC" >/dev/null || notrun "requires a C compiler ($CC) for the OpenCL demo host"
    [ -f seed/spirv/opencl-host.c ] || fail 'seed/spirv/opencl-host.c missing'
    # shellcheck disable=SC2086 # flag lists are intentionally word-split
    "$CC" -std=c11 -O2 -Wall -Wextra ${OPENCL_CFLAGS:-} seed/spirv/opencl-host.c \
        -o "$B/opencl-host" ${OPENCL_LIBS:--lOpenCL} > "$B/opencl-host.build.log" 2>&1 ||
        { cat "$B/opencl-host.build.log"; fail 'OpenCL host build failed'; }
    OPENCL_HOST=$B/opencl-host
    say "HOST_BUILD $("$CC" --version | sed -n 1p) flags=${OPENCL_CFLAGS:-} libs=${OPENCL_LIBS:--lOpenCL}"
fi
[ -x "$OPENCL_HOST" ] || notrun "OpenCL host $OPENCL_HOST is not executable"
say "SOURCE $(git rev-parse HEAD)"
say "COMPILER $OMC sha256=$pin"
say "VALIDATOR $(command -v "$SPIRV_VAL") $("$SPIRV_VAL" --version 2>&1 | tr '\n' ' ')"
say "OPENCL_HOST $OPENCL_HOST sha256=$(sha256sum "$OPENCL_HOST" | cut -d ' ' -f 1)"
say "ORACLE $("$PYTHON" --version 2>&1) parity.py sha256=$(sha256sum seed/spirv/parity.py | cut -d ' ' -f 1)"
say "RUNTIME_ENV OCL_ICD_VENDORS=${OCL_ICD_VENDORS:-} OCL_ICD_FILENAMES=${OCL_ICD_FILENAMES:-} POCL_DEVICES=${POCL_DEVICES:-}"
say "PLATFORM $(uname -srm)"
nice -n 19 "$OMC" < seed/spirv/irspirv.om > "$B/irspirv" 2> "$B/compile.err"
[ ! -s "$B/compile.err" ] || fail 'translator compiler stderr'
chmod +x "$B/irspirv"
validate() {
    "$SPIRV_VAL" --target-env opencl2.1 "$1" > "$1.val.log" 2>&1 ||
        { cat "$1.val.log"; fail "spirv-val rejected $1"; }
    validated=$((validated+1))
}
modules=0; deterministic=0; validated=0
for source in tests/one_layer/omegese/dev/*.omegese seed/wasm/*.unit.om seed/spirv/*.unit.om; do
    id=$(basename "$source" .omegese)
    id=${id%.unit.om}
    [ ! -e "$B/$id.source" ] || fail "duplicate unit id $id"
    { printf 'ir\n'; cat "$source"; } > "$B/$id.input"
    nice -n 19 "$OMC" < "$B/$id.input" > "$B/$id.ir" || fail "$id IR compile exited $?"
    nice -n 19 "$B/irspirv" < "$B/$id.ir" > "$B/$id.spv" 2> "$B/$id.translate.err" ||
        fail "$id translator exited $?"
    [ ! -s "$B/$id.translate.err" ] || fail "$id translator stderr"
    nice -n 19 "$B/irspirv" < "$B/$id.ir" > "$B/$id.again.spv" 2> "$B/$id.again.err" ||
        fail "$id translator rerun exited $?"
    [ ! -s "$B/$id.again.err" ] || fail "$id translator stderr on rerun"
    modules=$((modules+1))
    cmp "$B/$id.spv" "$B/$id.again.spv" || fail "$id nondeterministic"
    deterministic=$((deterministic+1))
    validate "$B/$id.spv"
    printf '%s\n' "$source" > "$B/$id.source"
    say "PASS $id deterministic spirv-val=opencl2.1"
done
# Each table row gets a real native program calling the same function and inputs.
# The dumper preserves every result bit, rather than comparing process-status bytes.
tab=$(printf '\t')
native_cases() {
    table=$1; prefix=$2; i=0; traps=0
    while IFS="$tab" read -r unit function result typed status; do
        case "$unit" in ''|'#'*) continue ;; esac
        i=$((i+1))
        [ "$status" = 0 ] || traps=$((traps+1))
        [ -f "$B/$unit.source" ] || fail "$table row $i names unknown unit $unit"
        case_om=$B/$prefix-$i.om
        cat "$(cat "$B/$unit.source")" > "$case_om"
        cat >> "$case_om" <<'DUMP'
global dumpbuf uchar 8
fn ir_dump x ulong to void
for j ulong 8
put dumpbuf j uchar and shr x mul j 8 255
end
p = ulong 0
while lt p 8 bound 8
k = write 1 dumpbuf p sub ulong 8 p
if le k 0
exit 90
end
p = add p ulong k
end
end
fn main to int
DUMP
        actual=
        if [ "$typed" != - ]; then
            for arg in $(printf '%s' "$typed" | tr ',' ' '); do
                type=${arg%%:*}; value=${arg#*:}
                if [ "$type" = bool ]; then actual="$actual $value"
                else actual="$actual $type $value"; fi
            done
        fi
        case "$result" in
            bool) printf 'if %s %s\nir_dump ulong 1\nelse\nir_dump ulong 0\nend\n' "$function" "$actual" ;;
            void) printf '%s %s\nir_dump ulong 0\n' "$function" "$actual" ;;
            byte|short|int|long) printf 'ir_dump bitcast ulong long %s %s\n' "$function" "$actual" ;;
            uchar|ushort|uint|ulong) printf 'ir_dump ulong %s %s\n' "$function" "$actual" ;;
            *) fail "unknown result $result" ;;
        esac >> "$case_om"
        printf 'ret 0\nend\n' >> "$case_om"
        nice -n 19 "$OMC" < "$case_om" > "$B/$prefix-$i.native"
        chmod +x "$B/$prefix-$i.native"
        set +e
        timeout 10 "$B/$prefix-$i.native" > "$B/$prefix-$i.out"
        rc=$?
        set -e
        printf '%s\n' "$rc" > "$B/$prefix-$i.status"
        [ "$rc" = "$status" ] || fail "$prefix $i $unit.$function native exit $rc expected $status"
    done < "$table"
}
native_cases seed/wasm/cases.tsv case
[ "$i" = 129 ] || fail "seed/wasm/cases.tsv denominator is $i, expected 129"
cases=$i; case_traps=$traps
native_cases seed/spirv/demo.tsv demo-case
demo_cases=$i
native_cases seed/spirv/regression.tsv regression-case
regression_cases=$i
say "PASS native dumps cases=$cases traps=$case_traps demo=$demo_cases"
# The pinned simple fixture has BIN ADD at offsets 38,39. Assert before mutating.
[ "$(od -An -j 38 -N 2 -t u1 "$B/mutant.ir" | tr -s ' ' | tr -d '\n')" = ' 3 1' ] || fail 'mutant IR layout changed'
cp "$B/mutant.ir" "$B/mutant-flipped.ir"
printf '\002' | dd of="$B/mutant-flipped.ir" bs=1 seek=39 conv=notrunc status=none
"$B/irspirv" < "$B/mutant-flipped.ir" > "$B/mutant-flipped.spv" || fail "mutant translator exited $?"
cmp -s "$B/mutant.spv" "$B/mutant-flipped.spv" && fail 'mutant IR byte did not change the module'
validate "$B/mutant-flipped.spv"
say 'PASS add-to-sub mutant module emitted and accepted by spirv-val'
# Validator negative control: a valid module always ends in OpFunctionEnd; dropping it must be rejected.
size=$(wc -c < "$B/mutant.spv")
[ "$(od -An -t x4 -j $((size-4)) -N 4 "$B/mutant.spv" | tr -d ' \n')" = 00010038 ] ||
    fail 'mutant module does not end in OpFunctionEnd'
dd if="$B/mutant.spv" of="$B/validator-negative.spv" bs=4 count=$((size/4-1)) status=none
if "$SPIRV_VAL" --target-env opencl2.1 "$B/validator-negative.spv" > "$B/validator-negative.log" 2>&1; then
    fail 'spirv-val accepted a module without OpFunctionEnd'
fi
say 'PASS spirv-val rejects truncated module (negative control)'
oracle() {
    set +e
    "$PYTHON" seed/spirv/parity.py "$B" "$@" > "$B/$log" 2>&1
    rc=$?
    set -e
    tee -a "$B/gate.log" < "$B/$log"
}
log=parity.log; oracle seed/wasm/cases.tsv "$OPENCL_HOST" case
[ "$rc" = 0 ] || fail "parity checker exited $rc"
say "RUNTIME $(tr '\n' ' ' < "$B/case-1.spirv.err")"
matched=$(grep -c "${tab}match\$" "$B/case-spirv-parity.tsv" || :)
[ "$matched" = "$cases" ] || fail "parity table matched=$matched expected $cases"
log=demo.log; oracle seed/spirv/demo.tsv "$OPENCL_HOST" demo-case
[ "$rc" = 0 ] || fail "demo parity checker exited $rc"
log=regression.log; oracle seed/spirv/regression.tsv "$OPENCL_HOST" regression-case
[ "$rc" = 0 ] || fail "regression parity checker exited $rc"
log=negative.log; oracle seed/wasm/cases.tsv "$OPENCL_HOST" case --mutant-baseline
[ "$rc" = 1 ] || fail "mutant comparison exited $rc instead of 1"
grep -q '^ERROR' "$B/negative.log" && fail 'mutant run hit an oracle or API failure'
[ "$(grep -c '^result mismatch' "$B/negative.log")" = 1 ] || fail 'mutant must mismatch exactly one row'
grep -q '^result mismatch at case [0-9]* mutant\.mutadd:' "$B/negative.log" || fail 'mutant failed for unrelated reason'
say 'PASS parity checker rejects successfully executed mutant substituted as baseline (exit 1)'
refusals=0
refuse() {
    name=$1; expected=$2; class=$3
    set +e
    "$B/irspirv" < "$B/$name.ir" > "$B/$name.refused" 2> "$B/$name.err"
    rc=$?
    set -e
    [ "$rc" = "$expected" ] || fail "$name exit $rc expected $expected"
    [ ! -s "$B/$name.refused" ] || fail "$name emitted partial module"
    printf 'irspirv: %s\n' "$class" > "$B/expected.err"
    cmp "$B/expected.err" "$B/$name.err" || fail "$name wrong refusal name"
    refusals=$((refusals+1))
    say "PASS $name $class exit=$rc stdout=empty"
}
bytecase() {
    name=$1; offset=$2; bytes=$3; code=$4; class=$5
    cp "$B/mutant.ir" "$B/$name.ir"
    printf '%b' "$bytes" | dd of="$B/$name.ir" bs=1 seek="$offset" conv=notrunc status=none
    refuse "$name" "$code" "$class"
}
bytecase bad-magic 0 '\130' 20 MAGIC
bytecase version2 7 '\002' 21 VERSION
bytecase version6 7 '\006' 21 VERSION
bytecase unsupported-op 38 '\007' 24 UNSUPPORTED_OP
bytecase unknown-op 38 '\036' 24 UNSUPPORTED_OP
bytecase unsupported-tag 18 '\012' 25 UNSUPPORTED_TAG
bytecase unknown-tag 18 '\015' 25 UNSUPPORTED_TAG
bytecase bad-register 44 '\003' 26 INVALID
bytecase undefined-register 42 '\002' 26 INVALID
bytecase nonascii-name 11 '\377' 26 INVALID
bytecase oversized-count 8 '\041' 23 CAPACITY
size=$(wc -c < "$B/mutant.ir")
dd if="$B/mutant.ir" of="$B/truncated.ir" bs=1 count=$((size-1)) status=none
refuse truncated 22 TRUNCATED
: > "$B/empty.ir"
refuse empty 22 TRUNCATED
cp "$B/mutant.ir" "$B/trailing.ir"
printf x >> "$B/trailing.ir"
refuse trailing 27 TRAILING
dd if=/dev/zero of="$B/oversized.ir" bs=2620252 count=1 status=none
refuse oversized 23 CAPACITY
# A valid OSC CFG may number a dominator after its dominated block.
# This independent wire fixture is 0 -> 2 -> 1, with a void return in block 1.
"$PYTHON" - "$B" <<'PY'
import pathlib, struct, sys
b = pathlib.Path(sys.argv[1])
header = b'OSC1IR\0\x01'
unit = header + struct.pack('<H', 1)
unit += b'\x05reord' + struct.pack('<BBHH', 0, 0, 0, 3)
unit += b''.join(struct.pack('<II', first, 1) for first in (0, 1, 2))
unit += struct.pack('<I', 3)
unit += struct.pack('<BH', 13, 2) + struct.pack('<BH', 15, 65535) + struct.pack('<BH', 13, 1)
unit += struct.pack('<HH', 0, 0)
(b / 'reordered.ir').write_bytes(unit)
(b / 'zero-functions.ir').write_bytes(header + struct.pack('<H', 0))
PY
refuse zero-functions 26 INVALID
"$B/irspirv" < "$B/reordered.ir" > "$B/reordered.spv"
"$B/irspirv" < "$B/reordered.ir" > "$B/reordered.again.spv"
cmp "$B/reordered.spv" "$B/reordered.again.spv" || fail 'reordered CFG nondeterministic'
modules=$((modules+1)); deterministic=$((deterministic+1))
validate "$B/reordered.spv"
"$OPENCL_HOST" "$B/reordered.spv" reord > "$B/reordered.out" 2> "$B/reordered.err" ||
    fail 'reordered CFG CPU execution failed'
printf '0 0\n' > "$B/reordered.expected"
cmp "$B/reordered.out" "$B/reordered.expected" || fail 'reordered CFG result differs'
say 'PASS reordered CFG 0->2->1 deterministic validated CPU result=0 status=0'
say "SUPPLEMENTAL nested_trap_cases=$regression_cases reordered_cfg=1"
say "SUMMARY cases=$cases matched=$matched traps=$case_traps modules=$modules deterministic=$deterministic validated=$validated validator_negative=rejected mutant=rejected refusals=$refusals demo=$demo_cases"
say "PASS IRSPIRV CPU OpenCL parity cases=$cases deterministic=$deterministic validated=$validated refusals=$refusals"
sha256sum seed/spirv/irspirv.om "$B/irspirv" seed/spirv/gate-irspirv.sh \
  seed/wasm/cases.tsv seed/spirv/demo.tsv seed/spirv/regression.tsv \
  seed/spirv/demo.unit.om seed/spirv/traps.unit.om seed/spirv/parity.py \
  seed/spirv/opencl-host.c "$OPENCL_HOST" | tee -a "$B/gate.log"
