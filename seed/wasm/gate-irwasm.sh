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
# OSC IR v7 (spec 10): regions, f32/f64, RLOAD/RSTORE/BITCAST/RINIT. Units come
# from the independent hand writer seed/wasm/v7/writer.js and seed/wasm/v7cases.js;
# expected verdicts and outputs come from the independent oracle seed/wasm/v7/oracle.js.
V="$B/v7"
rm -rf "$V"
mkdir -p "$V/fx" "$V/rc"
[ -f seed/wasm/v7/oracle.js ] && [ -f seed/wasm/v7/writer.js ] || fail 'v7 oracle or writer missing'
node seed/wasm/v7/writer.js "$V/fx" > /dev/null
node seed/wasm/v7cases.js "$V/rc" > /dev/null
code() {
    case $1 in
    MAGIC) echo 20 ;; VERSION) echo 21 ;; TRUNCATED) echo 22 ;; CAPACITY) echo 23 ;;
    UNSUPPORTED_OP) echo 24 ;; UNSUPPORTED_TAG) echo 25 ;; INVALID) echo 26 ;; TRAILING) echo 27 ;;
    *) fail "oracle class $1" ;;
    esac
}
accepts=0
refusals=0
for unit in "$V"/fx/*.unit "$V"/rc/*.unit; do
    id=${unit%.unit}
    set +e
    verdict=$(node seed/wasm/v7/oracle.js check "$unit")
    "$B/irwasm" < "$unit" > "$id.wasm" 2> "$id.err"
    rc=$?
    set -e
    case $verdict in
    ACCEPT*)
        [ "$rc" = 0 ] || fail "v7 $id refused ($(cat "$id.err")) but oracle: $verdict"
        "$B/irwasm" < "$unit" > "$id.again.wasm"
        cmp "$id.wasm" "$id.again.wasm" || fail "v7 $id nondeterministic"
        node -e 'new WebAssembly.Module(require("fs").readFileSync(process.argv[1]))' "$id.wasm" ||
            fail "v7 $id invalid wasm"
        accepts=$((accepts + 1)) ;;
    REFUSE*)
        class=$(printf '%s\n' "$verdict" | cut -d ' ' -f 2)
        [ "$rc" = "$(code "$class")" ] || fail "v7 $id exit $rc, oracle: $verdict"
        [ ! -s "$id.wasm" ] || fail "v7 $id emitted partial module"
        printf 'irwasm: %s\n' "$class" > "$V/expected.err"
        cmp "$V/expected.err" "$id.err" || fail "v7 $id wrong refusal name"
        refusals=$((refusals + 1)) ;;
    *) fail "v7 $id oracle verdict: $verdict" ;;
    esac
done
[ "$accepts" -gt 0 ] && [ "$refusals" -gt 0 ] || fail 'v7 fixture set empty'
echo "PASS v7 verdict parity accepts=$accepts (valid, deterministic) refusals=$refusals (class, exit, stdout=empty)"
runs=0
for script in "$V"/rc/*.script; do
    id=${script%.script}
    node seed/wasm/v7/oracle.js run "$id.unit" "$script" > "$id.oracle"
    node seed/wasm/v7run.js "$id.wasm" "$script" > "$id.out"
    [ -s "$id.oracle" ] || fail "v7 $id oracle printed nothing"
    diff "$id.oracle" "$id.out" || fail "v7 $id run differs from oracle"
    runs=$((runs + 1))
done
lines=$(cat "$V"/rc/*.oracle | wc -l)
traps=$(cat "$V"/rc/*.oracle | grep -c ' TRAP$')
echo "PASS v7 run parity cases=$runs lines=$lines traps=$traps"
# RGBX gradient: the framebuffer bytes after fill() hash the same in wasm and the
# oracle, and a one-channel mutant and a channel-order mutant hash differently.
fbsum() {
    sed -n '3p' "$1" | grep '^region fb ' | cut -d ' ' -f 3 |
        node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const b=Buffer.from(s.trim(),"hex");if(b.length!==512)process.exit(1);console.log(require("crypto").createHash("sha256").update(b).digest("hex"))})'
}
rgbx=$(fbsum "$V/rc/rgbx.out") || fail 'rgbx framebuffer line'
[ "$rgbx" = "$(fbsum "$V/rc/rgbx.oracle")" ] || fail 'rgbx framebuffer hash differs'
for m in rgbx_mut_b rgbx_mut_order; do
    [ "$(fbsum "$V/rc/$m.out")" != "$rgbx" ] || fail "$m not distinguished"
done
echo "PASS v7 RGBX framebuffer sha256=$rgbx; channel and order mutants differ"
# Out-of-bounds: index == count, count + 1, negative and wide indexes trap in
# both (case oob above). A translator with the bounds check removed must differ.
[ "$(grep -c '^boundchk ' seed/wasm/irwasm.om)" = 2 ] || fail 'bounds check call sites changed'
sed '/^boundchk /d' seed/wasm/irwasm.om > "$V/nobound.om"
nice -n 19 "$OMC" < "$V/nobound.om" > "$V/nobound" 2> "$V/nobound.err"
[ ! -s "$V/nobound.err" ] || fail 'bounds mutant compile stderr'
chmod +x "$V/nobound"
"$V/nobound" < "$V/rc/oob.unit" > "$V/nobound.wasm"
node seed/wasm/v7run.js "$V/nobound.wasm" "$V/rc/oob.script" > "$V/nobound.out"
if diff "$V/rc/oob.oracle" "$V/nobound.out" > "$V/nobound.diff"; then
    fail 'bounds-check mutant not caught'
fi
grep -q '^< .* TRAP$' "$V/nobound.diff" || fail 'bounds mutant differs for an unrelated reason'
echo "PASS v7 OOB traps=$(grep -c ' TRAP$' "$V/rc/oob.oracle"); no-bounds mutant caught ($(grep -c '^>' "$V/nobound.diff") differing lines)"
sha256sum seed/wasm/v7cases.js seed/wasm/v7run.js seed/wasm/v7/oracle.js seed/wasm/v7/writer.js
sha256sum seed/wasm/irwasm.om "$B/irwasm" seed/wasm/cases.tsv
