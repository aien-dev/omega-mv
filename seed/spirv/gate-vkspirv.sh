#!/bin/sh
# Host-only CPU verification of seed/spirv/vkspirv.om (OSC IR v1 -> SPIR-V 1.3
# Shader/GLCompute) on Mesa lavapipe. spirv-val, the Vulkan loader, lavapipe and
# the interim vk-host.c are read-only oracles, never generators of IR or SPIR-V.
# Native results come from omc-built programs calling the same functions.
# Optional: OMC, SPIRV_VAL, LVP_ICD, CC, VULKAN_CFLAGS, VULKAN_LIBS, PORTLAB_DIR.
# PORTLAB_DIR (default bench/portlab) holds gen.om and kernels/*.om, consumed
# read-only; without it the portlab-* rows are NOT_RUN and the gate exits 3.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN. Every artifact stays under build/seed/vkspirv.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/vkspirv
mkdir -p "$B"
find "$B" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
: > "$B/gate.log"
say() { printf '%s\n' "$*" | tee -a "$B/gate.log"; }
fail() { say "FAIL $*"; exit 1; }
notrun() { say "NOT_RUN $*"; exit 3; }
[ "$(uname -m)" = aarch64 ] || notrun 'requires AArch64 Linux'
OMC=${OMC:-seed/out/omc}
SPIRV_VAL=${SPIRV_VAL:-spirv-val}
LVP_ICD=${LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
PORTLAB_DIR=${PORTLAB_DIR:-bench/portlab}
CC=${CC:-cc}
command -v "$SPIRV_VAL" >/dev/null || notrun "requires spirv-val ($SPIRV_VAL)"
command -v "$CC" >/dev/null || notrun "requires a C compiler ($CC) for the interim Vulkan host"
case "$LVP_ICD" in /*lvp_icd*.json) ;; *) notrun 'LVP_ICD must name the absolute lavapipe ICD manifest' ;; esac
[ -f "$LVP_ICD" ] || notrun "lavapipe ICD manifest $LVP_ICD missing"
pin=62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b
[ -f "$OMC" ] || fail "compiler $OMC missing"
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
hostbuild() {
    # shellcheck disable=SC2086 # flag lists are intentionally word-split
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror ${VULKAN_CFLAGS:-} "$1" seed/spirv/vk-host.c \
        -o "$2" ${VULKAN_LIBS:--lvulkan} > "$2.build.log" 2>&1
}
hostbuild "-DVKSPIRV_ICD=\"$LVP_ICD\"" "$B/vk-host" ||
    { cat "$B/vk-host.build.log"; fail 'Vulkan host build failed'; }
H=$B/vk-host
say "SOURCE $(git rev-parse HEAD)"
say "COMPILER $OMC sha256=$pin"
say "VALIDATOR $(command -v "$SPIRV_VAL") $("$SPIRV_VAL" --version 2>&1 | tr '\n' ' ')"
say "HOST_BUILD $("$CC" --version | sed -n 1p) flags=${VULKAN_CFLAGS:-} libs=${VULKAN_LIBS:--lvulkan} sha256=$(sha256sum "$H" | cut -d ' ' -f 1)"
say "LVP_ICD $LVP_ICD sha256=$(sha256sum "$LVP_ICD" | cut -d ' ' -f 1)"
if command -v dpkg-query >/dev/null; then
    say "PACKAGES $(dpkg-query -W -f '${Package}=${Version} ' libvulkan1 mesa-vulkan-drivers 2>/dev/null || :)"
fi
say "PLATFORM $(uname -srm)"
nice -n 19 "$OMC" < seed/spirv/vkspirv.om > "$B/vkspirv" 2> "$B/compile.err"
[ ! -s "$B/compile.err" ] || fail 'translator compiler stderr'
chmod +x "$B/vkspirv"
validate() {
    "$SPIRV_VAL" --target-env vulkan1.3 "$1" > "$1.val.log" 2>&1 ||
        { cat "$1.val.log"; fail "spirv-val rejected $1"; }
    validated=$((validated+1))
}
modules=0; deterministic=0; validated=0
translate() {
    id=$1; source=$2
    [ ! -e "$B/$id.source" ] || fail "duplicate unit id $id"
    { printf 'ir\n'; cat "$source"; } > "$B/$id.input"
    nice -n 19 "$OMC" < "$B/$id.input" > "$B/$id.ir" || fail "$id IR compile exited $?"
    nice -n 19 "$B/vkspirv" < "$B/$id.ir" > "$B/$id.spv" 2> "$B/$id.translate.err" ||
        fail "$id translator exited $?"
    [ ! -s "$B/$id.translate.err" ] || fail "$id translator stderr"
    nice -n 19 "$B/vkspirv" < "$B/$id.ir" > "$B/$id.again.spv" || fail "$id translator rerun exited $?"
    modules=$((modules+1))
    cmp "$B/$id.spv" "$B/$id.again.spv" || fail "$id nondeterministic"
    deterministic=$((deterministic+1))
    validate "$B/$id.spv"
    printf '%s\n' "$source" > "$B/$id.source"
    say "PASS $id deterministic spirv-val=vulkan1.3"
}
for source in tests/one_layer/omegese/dev/*.omegese seed/wasm/*.unit.om seed/spirv/*.unit.om; do
    id=$(basename "$source" .omegese)
    translate "${id%.unit.om}" "$source"
done
portlab=0
if [ -f "$PORTLAB_DIR/gen.om" ] && [ -d "$PORTLAB_DIR/kernels" ]; then
    portlab=1
    say "PORTLAB_DIR $PORTLAB_DIR gen.om sha256=$(sha256sum "$PORTLAB_DIR/gen.om" | cut -d ' ' -f 1)"
    for kernel in "$PORTLAB_DIR"/kernels/*.om; do
        k=$(basename "$kernel" .om)
        say "PORTLAB_KERNEL $k sha256=$(sha256sum "$kernel" | cut -d ' ' -f 1)"
        cat "$PORTLAB_DIR/gen.om" "$kernel" > "$B/portlab-$k.unit.om"
        translate "portlab-$k" "$B/portlab-$k.unit.om"
    done
fi

# Validator negative controls on the vk fixture module. Patch positions are
# located by word pattern, asserted, and the rejection reason is checked.
od -An -v -t u4 -w4 "$B/vk.spv" | tr -d ' ' > "$B/vk.words"
w=$(awk 'p3==262203 && p2==20 && p1==21 && $1==12 {print NR; exit} {p3=p2; p2=p1; p1=$1}' "$B/vk.words")
[ -n "$w" ] || fail 'binding-0 OpVariable StorageBuffer not found'
cp "$B/vk.spv" "$B/neg-storage.spv"
printf '\002\000\000\000' | dd of="$B/neg-storage.spv" bs=4 seek=$((w-1)) conv=notrunc status=none
if "$SPIRV_VAL" --target-env vulkan1.3 "$B/neg-storage.spv" > "$B/neg-storage.log" 2>&1; then
    fail 'spirv-val accepted a StorageBuffer variable declared Uniform'
fi
grep -q 'Storage class must match result type storage class' "$B/neg-storage.log" ||
    fail 'wrong-storage-class module rejected for an unrelated reason'
say 'PASS spirv-val rejects wrong storage class (binding 0 StorageBuffer -> Uniform)'
d=$(awk '{w[NR]=$1} END {for (i=1;i<=NR;i++) if (w[i]==262215 && w[i+1]==21 && w[i+2]==34 && w[i+3]==0) {print i; exit}}' "$B/vk.words")
[ -n "$d" ] || fail 'OpDecorate DescriptorSet 0 on binding 0 not found'
{ dd if="$B/vk.spv" bs=4 count=$((d-1)) status=none; dd if="$B/vk.spv" bs=4 skip=$((d+3)) status=none; } \
    > "$B/neg-descset.spv"
[ $(($(wc -c < "$B/vk.spv") - $(wc -c < "$B/neg-descset.spv"))) = 16 ] || fail 'descriptor-set cut size'
if "$SPIRV_VAL" --target-env vulkan1.3 "$B/neg-descset.spv" > "$B/neg-descset.log" 2>&1; then
    fail 'spirv-val accepted a StorageBuffer without DescriptorSet'
fi
grep -q 'missing DescriptorSet decoration' "$B/neg-descset.log" ||
    fail 'dropped-DescriptorSet module rejected for an unrelated reason'
say 'PASS spirv-val rejects dropped OpDecorate DescriptorSet (VUID-StandaloneSpirv-UniformConstant-06677)'

# Host controls: inherited loader/driver/layer variables cannot select another
# ICD; unconfigured and missing-manifest builds fail closed with empty stdout.
printf '7 10\n' > "$B/avg.rows"
env VK_ICD_FILENAMES=/nonexistent/vk-icd.json VK_DRIVER_FILES=/nonexistent/vk-drv.json \
    VK_ADD_DRIVER_FILES=/nonexistent/vk-add.json VK_LAYER_PATH=/nonexistent/vk-layers \
    VK_INSTANCE_LAYERS=VK_LAYER_untrusted VK_LOADER_DRIVERS_SELECT='*nvidia*' \
    "$H" "$B/demo.spv" avgu32 2 < "$B/avg.rows" > "$B/host-environment.out" 2> "$B/host-environment.err" ||
    fail 'inherited discovery changed the configured lavapipe oracle'
printf '8 0\n' | cmp - "$B/host-environment.out" || fail 'environment control changed result'
for control in unconfigured missing-manifest; do
    definition='-DVKSPIRV_ICD=""'
    [ "$control" = unconfigured ] || definition='-DVKSPIRV_ICD="/nonexistent/vkspirv/lvp_icd.json"'
    hostbuild "$definition" "$B/host-$control" || fail "host $control control build failed"
    set +e
    (ulimit -c 0; "$B/host-$control" "$B/demo.spv" avgu32 2 < "$B/avg.rows") \
        > "$B/host-$control.out" 2> "$B/host-$control.err"
    rc=$?
    set -e
    [ "$rc" -ne 0 ] || fail "$control silently fell back to a working ICD"
    [ ! -s "$B/host-$control.out" ] || fail "$control printed a result"
    [ "$control" != unconfigured ] || [ "$rc" = 3 ] || fail "unconfigured host exited $rc rather than 3"
done
say 'PASS host controls: inherited discovery ignored; unconfigured and missing manifest fail closed'

# Each table row gets a real native program calling the same function and inputs.
# Expected line: the native 8-byte result as u64 decimal and its exit status;
# a trapped row has no native result word and the Vulkan ABI writes 0.
tab=$(printf '\t')
canon() {
    case "$1" in
        bool:true) printf 1 ;;
        bool:false) printf 0 ;;
        *) printf '%u' "${1#*:}" ;;
    esac
}
native_cases() {
    table=$1; prefix=$2; i=0; traps=0; skipped=0
    : > "$B/$prefix.rows"
    while IFS="$tab" read -r unit function result typed status; do
        case "$unit" in ''|'#'*) continue ;; esac
        if [ "${unit#portlab-}" != "$unit" ] && [ "$portlab" = 0 ]; then
            skipped=$((skipped+1)); continue
        fi
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
        actual=; row=; np=0
        if [ "$typed" != - ]; then
            for arg in $(printf '%s' "$typed" | tr ',' ' '); do
                type=${arg%%:*}; value=${arg#*:}
                if [ "$type" = bool ]; then actual="$actual $value"
                else actual="$actual $type $value"; fi
                row="$row${row:+ }$(canon "$arg")"; np=$((np+1))
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
        timeout 60 "$B/$prefix-$i.native" > "$B/$prefix-$i.out"
        rc=$?
        set -e
        [ "$rc" = "$status" ] || fail "$prefix $i $unit.$function native exit $rc expected $status"
        value=0
        if [ "$rc" = 0 ]; then
            [ "$(wc -c < "$B/$prefix-$i.out")" = 8 ] || fail "$prefix $i native dump is not 8 bytes"
            value=$(od -An -t u8 "$B/$prefix-$i.out" | tr -d ' \n')
        else
            [ ! -s "$B/$prefix-$i.out" ] || fail "$prefix $i trapped native wrote a result"
        fi
        printf '%s\t%s\t%s\t%s %s\n' "$unit.$function" "$np" "$row" "$value" "$rc" >> "$B/$prefix.rows"
    done < "$table"
}
# One dispatch per unit.function group (one invocation per row, in table order).
# The concatenated Vulkan output must be byte-identical to the native lines.
vk_cases() {
    prefix=$1; module_override=${2:-}
    : > "$B/$prefix.expected"; : > "$B/$prefix.vk"; groups=0
    for key in $(cut -f1 "$B/$prefix.rows" | awk '!seen[$0]++'); do
        unit=${key%.*}; function=${key##*.}
        awk -F "$tab" -v k="$key" '$1 == k' "$B/$prefix.rows" > "$B/$prefix.$key.group"
        np=$(sed -n 1p "$B/$prefix.$key.group" | cut -f2)
        cut -f3 "$B/$prefix.$key.group" > "$B/$prefix.$key.in"
        cut -f4 "$B/$prefix.$key.group" > "$B/$prefix.$key.expected"
        cat "$B/$prefix.$key.expected" >> "$B/$prefix.expected"
        module=$B/$unit.spv
        [ -z "$module_override" ] || [ "$unit" != mutant ] || module=$module_override
        nice -n 19 "$H" "$module" "$function" "$np" < "$B/$prefix.$key.in" \
            >> "$B/$prefix.vk" 2> "$B/$prefix.$key.err" ||
            { cat "$B/$prefix.$key.err"; fail "$prefix $key Vulkan run exited $?"; }
        groups=$((groups+1))
        [ -n "$module_override" ] ||
            printf '%s\t%s\t%s\t%s\t%s\tmatch\n' "$unit.spv" "$function" "$np" \
                "$prefix.$key.in" "$prefix.$key.expected" >> "$B/replay.tsv"
    done
}
: > "$B/replay.tsv"
native_cases seed/wasm/cases.tsv case
[ "$i" = 129 ] || fail "seed/wasm/cases.tsv denominator is $i, expected 129"
cases=$i; case_traps=$traps
native_cases seed/spirv/demo.tsv demo; demo_cases=$i
native_cases seed/spirv/regression.tsv regression; regression_cases=$i
native_cases seed/spirv/vk-regression.tsv vkreg
vk_rows=$i; vk_traps=$traps; vk_skipped=$skipped
say "PASS native dumps cases=$cases traps=$case_traps demo=$demo_cases regression=$regression_cases vk=$vk_rows vk_traps=$vk_traps"
vk_cases case
say "RUNTIME $(grep -h 'vk-host: VK_ICD\|vk-host: device' "$B"/case.*.err | sort -u | tr '\n' ' ')"
cmp "$B/case.expected" "$B/case.vk" || fail 'seed/wasm/cases.tsv Vulkan output differs from native'
[ "$(wc -l < "$B/case.vk")" = "$cases" ] || fail 'cases.tsv row count differs'
say "PASS cases.tsv lavapipe == native rows=$cases dispatches=$groups"
for t in demo regression vkreg; do
    vk_cases "$t"
    cmp "$B/$t.expected" "$B/$t.vk" || fail "$t Vulkan output differs from native"
    say "PASS $t lavapipe == native rows=$(wc -l < "$B/$t.vk") dispatches=$groups"
done

# Opcode mutant: BIN ADD -> SUB in the mutant fixture IR (offsets 38,39 asserted),
# translated, validated and run in place of the baseline; exactly the mutadd
# rows must mismatch, so the comparison provably catches a wrong opcode.
[ "$(od -An -j 38 -N 2 -t u1 "$B/mutant.ir" | tr -s ' ' | tr -d '\n')" = ' 3 1' ] || fail 'mutant IR layout changed'
cp "$B/mutant.ir" "$B/mutant-flipped.ir"
printf '\002' | dd of="$B/mutant-flipped.ir" bs=1 seek=39 conv=notrunc status=none
"$B/vkspirv" < "$B/mutant-flipped.ir" > "$B/mutant-flipped.spv" || fail "mutant translator exited $?"
cmp -s "$B/mutant.spv" "$B/mutant-flipped.spv" && fail 'mutant IR byte did not change the module'
validate "$B/mutant-flipped.spv"
vk_cases case "$B/mutant-flipped.spv"
mism=$(paste -d '|' "$B/case.expected" "$B/case.vk" | awk -F '|' '$1 != $2' | wc -l)
mutrows=$(grep -c '^mutant\.' "$B/case.rows" || :)
[ "$mism" -ge 1 ] || fail 'mutated opcode produced no mismatch'
[ "$(paste "$B/case.rows" "$B/case.vk" | awk -F "$tab" '$4 != $5 {print $1}' | sort -u)" = mutant.mutadd ] ||
    fail 'mutant mismatch outside mutant.mutadd'
if cmp -s "$B/case.expected" "$B/case.vk"; then fail 'gate comparison accepted the mutant'; fi
printf 'mutant-flipped.spv\tmutadd\t2\tcase.mutant.mutadd.in\tcase.mutant.mutadd.expected\tmismatch\n' >> "$B/replay.tsv"
[ "$(sed -n 1p "$B/case.mutant.mutadd.group" | cut -f2)" = 2 ] || fail 'mutadd arity changed'
say "PASS opcode mutant add->sub caught: mismatched_rows=$mism of mutant_rows=$mutrows (all mutant.mutadd)"

# Refusals: invalid IR is rejected with a named class and empty stdout.
refusals=0
refusefile() {
    name=$1; code=$2; class=$3
    set +e
    "$B/vkspirv" < "$B/$name.ir" > "$B/$name.refused" 2> "$B/$name.err"
    rc=$?
    set -e
    [ "$rc" = "$code" ] || fail "$name exit $rc expected $code"
    [ ! -s "$B/$name.refused" ] || fail "$name emitted partial module"
    printf 'vkspirv: %s\n' "$class" | cmp - "$B/$name.err" || fail "$name wrong refusal name"
    refusals=$((refusals+1))
    say "PASS $name $class exit=$rc stdout=empty"
}
bytecase() {
    cp "$B/mutant.ir" "$B/$1.ir"
    printf '%b' "$3" | dd of="$B/$1.ir" bs=1 seek="$2" conv=notrunc status=none
    refusefile "$1" "$4" "$5"
}
bytecase bad-magic 0 '\130' 20 MAGIC
bytecase version2 7 '\002' 21 VERSION
bytecase unsupported-op 38 '\007' 24 UNSUPPORTED_OP
bytecase unsupported-tag 18 '\012' 25 UNSUPPORTED_TAG
bytecase undefined-register 42 '\002' 26 INVALID
size=$(wc -c < "$B/mutant.ir")
dd if="$B/mutant.ir" of="$B/truncated.ir" bs=1 count=$((size-1)) status=none
refusefile truncated 22 TRUNCATED
cp "$B/mutant.ir" "$B/trailing.ir"
printf x >> "$B/trailing.ir"
refusefile trailing 27 TRAILING

# A valid OSC CFG may number a dominator after its dominated block: 0 -> 2 -> 1,
# void return in block 1. The dispatcher must not depend on block order.
printf 'OSC1IR\000\001\001\000\005reord\000\000\000\000\003\000' > "$B/reordered.ir"
printf '\000\000\000\000\001\000\000\000\001\000\000\000\001\000\000\000\002\000\000\000\001\000\000\000' >> "$B/reordered.ir"
printf '\003\000\000\000\015\002\000\017\377\377\015\001\000\000\000\000\000' >> "$B/reordered.ir"
"$B/vkspirv" < "$B/reordered.ir" > "$B/reordered.spv" || fail "reordered translator exited $?"
"$B/vkspirv" < "$B/reordered.ir" > "$B/reordered.again.spv"
cmp "$B/reordered.spv" "$B/reordered.again.spv" || fail 'reordered CFG nondeterministic'
modules=$((modules+1)); deterministic=$((deterministic+1))
validate "$B/reordered.spv"
printf '\n' | "$H" "$B/reordered.spv" reord 0 > "$B/reordered.out" 2> "$B/reordered.err" ||
    fail 'reordered CFG lavapipe execution failed'
printf '0 0\n' | cmp - "$B/reordered.out" || fail 'reordered CFG result differs'
say 'PASS reordered CFG 0->2->1 deterministic validated lavapipe result=0 status=0'

# Vector phase: vecadd and saxpy over 4096 invocations (64 workgroups), and a
# two-stage reduce: 4096 rsum partials of 64 generated u32 elements, combined
# by a vadd tree, one dispatch per level, against the native sequential sum.
N=4096; SEED=99
vecprog() {
    name=$1; shift
    { cat seed/spirv/vk.unit.om seed/spirv/vk-native.om; printf 'fn main to int\n'; cat; printf 'vkflush\nret 0\nend\n'; } \
        > "$B/$name-native.om"
    nice -n 19 "$OMC" < "$B/$name-native.om" > "$B/$name-native" || fail "$name native compile"
    chmod +x "$B/$name-native"
    timeout 120 "$B/$name-native" > "$B/$name.native" || fail "$name native run exited $?"
}
vecprog vadd <<EOF
for i ulong $N
a = mix i
b = mix add i $N
vkword a
vkword b
vkword vadd a b
vklast 0
end
EOF
vecprog saxpy <<EOF
for i ulong $N
a = sub long and mix i 2097151 long 1048576
x = sub long and mix add i $N 2097151 long 1048576
y = sub long and mix add i $((2*N)) 1099511627775 long 549755813888
vkword bitcast ulong a
vkword bitcast ulong x
vkword bitcast ulong y
vkword bitcast ulong saxpy a x y
vklast 0
end
EOF
vecprog rsum <<EOF
for i ulong $N
vkword $SEED
vkword mul i 64
vkword 64
vkword rsum $SEED mul i 64 64
vklast 0
end
EOF
vecprog total <<EOF
vkword rsum $SEED 0 $((N*64))
vklast 0
EOF
for k in vadd:2 saxpy:3 rsum:3; do
    name=${k%:*}; np=${k#*:}
    [ "$(wc -l < "$B/$name.native")" = $N ] || fail "$name native row count"
    cut -d ' ' -f "1-$np" "$B/$name.native" > "$B/$name.in"
    cut -d ' ' -f "$((np+1))-" "$B/$name.native" > "$B/$name.expected"
    nice -n 19 "$H" "$B/vk.spv" "$name" "$np" < "$B/$name.in" > "$B/$name.vk" 2> "$B/$name.err" ||
        { cat "$B/$name.err"; fail "$name Vulkan run exited $?"; }
    cmp "$B/$name.expected" "$B/$name.vk" || fail "$name Vulkan output differs from native"
    printf 'vk.spv\t%s\t%s\t%s.in\t%s.expected\tmatch\n' "$name" "$np" "$name" "$name" >> "$B/replay.tsv"
    say "PASS $name lavapipe == native invocations=$N $(grep 'workgroups=' "$B/$name.err" | sed 's/^vk-host: //')"
done
cp "$B/rsum.vk" "$B/level-0.vk"; level=0; rows=$N
while [ "$rows" -gt 1 ]; do
    [ $((rows % 2)) = 0 ] || fail "tree level $level has odd row count $rows"
    awk '$2 != 0 {exit 1}' "$B/level-$level.vk" || fail "tree level $level carried a trap status"
    cut -d ' ' -f 1 "$B/level-$level.vk" | paste -d ' ' - - > "$B/level-$((level+1)).in"
    nice -n 19 "$H" "$B/vk.spv" vadd 2 < "$B/level-$((level+1)).in" > "$B/level-$((level+1)).vk" \
        2> "$B/level-$((level+1)).err" || fail "tree level $((level+1)) Vulkan run exited $?"
    level=$((level+1)); rows=$((rows/2))
done
for l in $(seq 1 "$level"); do
    printf 'vk.spv\tvadd\t2\tlevel-%s.in\tlevel-%s.vk\tmatch\n' "$l" "$l" >> "$B/replay.tsv"
done
cmp "$B/total.native" "$B/level-$level.vk" || fail 'tree reduce total differs from native sequential sum'
say "PASS reduce stage1=$N partials tree_levels=$level total=$(cut -d ' ' -f 1 "$B/total.native") lavapipe == native"

say "SUMMARY modules=$modules deterministic=$deterministic validated=$validated cases=$cases demo=$demo_cases regression=$regression_cases vk_rows=$vk_rows vk_traps=$vk_traps refusals=$refusals validator_negatives=2 mutant=caught"
sha256sum seed/spirv/vkspirv.om "$B/vkspirv" seed/spirv/gate-vkspirv.sh seed/spirv/vk-host.c "$H" \
  seed/spirv/vk-regression.tsv seed/spirv/vk.unit.om seed/spirv/vk-native.om \
  seed/wasm/cases.tsv seed/spirv/demo.tsv seed/spirv/regression.tsv | tee -a "$B/gate.log"
[ "$portlab" = 1 ] || notrun "portlab rows=$vk_skipped (PORTLAB_DIR $PORTLAB_DIR missing gen.om/kernels)"
say "PASS VKSPIRV lavapipe GLCompute parity modules=$validated rows=$((cases+demo_cases+regression_cases+vk_rows)) invocations_vector=$((3*N))"
