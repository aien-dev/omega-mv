#!/bin/sh
# Host-only verification. Node is a read-only wasm oracle, never a generator.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/wasm
mkdir -p "$B"
fail() { printf 'FAIL %s\n' "$*"; exit 1; }
[ "$(uname -m)" = aarch64 ] || { echo 'NOT_RUN requires AArch64 Linux'; exit 3; }
command -v node >/dev/null || { echo 'NOT_RUN requires Node WebAssembly oracle'; exit 3; }
OMC=seed/out/omc
pin=befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
printf 'SOURCE %s\n' "$(git rev-parse HEAD)"
printf 'COMPILER %s\n' "$pin"
node --version
uname -sm
nice -n 19 "$OMC" < seed/wasm/irwasm.om > "$B/irwasm" 2> "$B/compile.err"
[ ! -s "$B/compile.err" ] || fail 'translator compiler stderr'
chmod +x "$B/irwasm"
for source in tests/one_layer/omegese/dev/*.omegese seed/wasm/*.unit.om; do
    id=$(basename "$source" .omegese)
    id=${id%.unit.om}
    { printf 'ir\n'; cat "$source"; } > "$B/$id.input"
    nice -n 19 "$OMC" < "$B/$id.input" > "$B/$id.ir"
    nice -n 19 "$B/irwasm" < "$B/$id.ir" > "$B/$id.wasm"
    nice -n 19 "$B/irwasm" < "$B/$id.ir" > "$B/$id.again.wasm"
    cmp "$B/$id.wasm" "$B/$id.again.wasm" || fail "$id nondeterministic"
    printf '%s\n' "$source" > "$B/$id.source"
    echo "PASS $id deterministic"
done
# Each table row gets a real native program calling the same function and inputs.
# The dumper preserves every result bit, rather than comparing process-status bytes.
i=0
tab=$(printf '\t')
while IFS="$tab" read -r unit function result typed status; do
    case "$unit" in ''|'#'*) continue ;; esac
    i=$((i+1))
    source=$(cat "$B/$unit.source")
    cat "$source" > "$B/case-$i.om"
    cat >> "$B/case-$i.om" <<'DUMP'
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
    esac >> "$B/case-$i.om"
    printf 'ret 0\nend\n' >> "$B/case-$i.om"
    nice -n 19 "$OMC" < "$B/case-$i.om" > "$B/case-$i.native"
    chmod +x "$B/case-$i.native"
    set +e
    timeout 10 "$B/case-$i.native" > "$B/case-$i.out"
    rc=$?
    set -e
    printf '%s\n' "$rc" > "$B/case-$i.status"
    [ "$rc" = "$status" ] || fail "case $i $unit.$function native exit $rc expected $status"
done < seed/wasm/cases.tsv
# The pinned simple fixture has BIN ADD at offsets 38,39. Assert before mutating.
[ "$(od -An -j 38 -N 2 -t u1 "$B/mutant.ir" | tr -s ' ' | tr -d '\n')" = ' 3 1' ] || fail 'mutant IR layout changed'
cp "$B/mutant.ir" "$B/mutant-flipped.ir"
printf '\002' | dd of="$B/mutant-flipped.ir" bs=1 seek=39 conv=notrunc status=none
"$B/irwasm" < "$B/mutant-flipped.ir" > "$B/mutant-flipped.wasm"
node seed/wasm/oracle.js "$B" seed/wasm/cases.tsv
set +e
node seed/wasm/oracle.js "$B" seed/wasm/cases.tsv --mutant-baseline > "$B/negative.log" 2>&1
rc=$?
set -e
[ "$rc" = 1 ] || fail "mutant comparison exited $rc instead of 1"
grep -q 'result mismatch' "$B/negative.log" || fail 'mutant failed for unrelated reason'
echo 'PASS parity checker rejects mutant substituted as baseline (exit 1)'
refuse() {
    name=$1; expected=$2; class=$3
    set +e
    "$B/irwasm" < "$B/$name.ir" > "$B/$name.refused" 2> "$B/$name.err"
    rc=$?
    set -e
    [ "$rc" = "$expected" ] || fail "$name exit $rc expected $expected"
    [ ! -s "$B/$name.refused" ] || fail "$name emitted partial module"
    printf 'irwasm: %s\n' "$class" > "$B/expected.err"
    cmp "$B/expected.err" "$B/$name.err" || fail "$name wrong refusal name"
    echo "PASS $name $class exit=$rc stdout=empty"
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
printf 'PASS IRWASM native parity cases=%s deterministic mutant refusals\n' "$i"
sha256sum seed/wasm/irwasm.om "$B/irwasm" seed/wasm/cases.tsv
