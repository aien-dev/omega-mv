#!/bin/sh
# Host-only glow parity gate. Referees only: the pinned original C ck_glow
# (oracle-glow.c links it unchanged) and Node as a read-only wasm executor.
# Neither generates any Omega output.
#   ALLEN_NATIVE_DIR  directory holding the pinned paint.c paint.h allen_font.c
#                     allen_font.h (checked against native-pin.sha256)
#   AIENUX_GIT        optional aienux clone; also byte-compares those files
#                     with commit da55660ea7337f2b6a42095fcd1fb6ddebe1cc7c
#   GLOW_WASM=1       run the wasm half; set only once the IR memory backend
#                     is integrated. Unset: wasm is NOT_RUN and the gate exits 3.
# Exit 0 all PASS, 1 FAIL, 3 NOT_RUN (missing prerequisite or wasm half off).
set -eu
cd "$(dirname "$0")/../../.."
R=seed/wasm/renderer
B=build/seed/wasm/renderer
mkdir -p "$B"
fail() { printf 'FAIL %s\n' "$*"; exit 1; }
quiet() { nice -n 19 ionice -c3 "$@"; }
[ "$(uname -m)" = aarch64 ] || { echo 'NOT_RUN requires AArch64 Linux'; exit 3; }
command -v cc >/dev/null || { echo 'NOT_RUN requires a host C compiler for the oracle'; exit 3; }
[ -n "${ALLEN_NATIVE_DIR:-}" ] || { echo 'NOT_RUN set ALLEN_NATIVE_DIR to the pinned native sources'; exit 3; }
N=$(cd "$ALLEN_NATIVE_DIR" && pwd)
OMC=seed/out/omc
# Keep equal to the pin in seed/wasm/gate-irwasm.sh.
pin=befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
printf 'SOURCE %s\n' "$(git rev-parse HEAD)"
printf 'COMPILER %s\n' "$pin"
uname -sm
(cd "$N" && sha256sum -c "$OLDPWD/$R/native-pin.sha256") || fail 'native sources differ from pin'
if [ -n "${AIENUX_GIT:-}" ]; then
    for f in paint.c paint.h allen_font.c allen_font.h; do
        git -C "$AIENUX_GIT" show "da55660ea7337f2b6a42095fcd1fb6ddebe1cc7c:native/kernel/ui/$f" > "$B/aienux-$f"
        cmp "$B/aienux-$f" "$N/$f" || fail "$f differs from aienux da55660"
    done
    echo 'PASS native sources equal aienux da55660 native/kernel/ui'
else
    echo 'NOT_RUN aienux blob comparison (AIENUX_GIT unset); digest pin checked'
fi

# 1. Oracle: the pinned C ck_glow, undefined behaviour fatal, run twice.
quiet cc -std=gnu11 -O1 -fsanitize=undefined -fno-sanitize-recover=all -I "$N" \
    "$R/oracle-glow.c" "$N/paint.c" "$N/allen_font.c" -o "$B/oracle"
quiet "$B/oracle" "$R/glow-cases.tsv" > "$B/oracle.bin" || fail "oracle exit $?"
quiet "$B/oracle" "$R/glow-cases.tsv" > "$B/oracle.again.bin" || fail 'oracle rerun'
cmp "$B/oracle.bin" "$B/oracle.again.bin" || fail 'oracle nondeterministic'
want=$(awk -F '\t' '$1 == "frame" { s += $3 * $4 * 4 } END { print s + 0 }' "$R/glow-cases.tsv")
[ "$(wc -c < "$B/oracle.bin")" = "$want" ] || fail "oracle wrote $(wc -c < "$B/oracle.bin") bytes, want $want"
echo "PASS oracle bytes=$want deterministic, no undefined behaviour"

# 2. Native Omega: the same cases as a generated main.
awk -F '\t' '
    /^#/ || NF == 0 { next }
    $1 == "frame" { if (seen) print "fbdump"; seen = 1; printf "fbsize int %s int %s\n", $3, $4; next }
    $1 == "clip"  { printf "fbclip int %s int %s int %s int %s\n", $2, $3, $4, $5; next }
    $1 == "grad"  { print "fbgrad"; next }
    $1 == "glow"  { printf "ckglow int %s int %s int %s ckrgb uint %s uint %s uint %s uint %s\n", $2, $3, $4, $5, $6, $7, $8; next }
    { exit 2 }
    END { print "fbdump" }
' "$R/glow-cases.tsv" > "$B/main.body" || fail 'unknown case row'
native() { # $1 glow source, $2 output stem
    { cat "$1" "$R/background.om" "$R/native-runner.om"
      printf 'fn main to int\n'; cat "$B/main.body"; printf 'ret 0\nend\n'; } > "$B/$2.om"
    quiet "$OMC" < "$B/$2.om" > "$B/$2" 2> "$B/$2.err" || fail "$2 compile exit $?"
    [ ! -s "$B/$2.err" ] || fail "$2 compiler stderr"
    chmod +x "$B/$2"
    set +e
    quiet timeout 60 "$B/$2" > "$B/$2.bin"
    rc=$?
    set -e
    [ "$rc" = 0 ] || fail "$2 run exit $rc"
}
native "$R/glow.om" native
cp "$B/native" "$B/native.first"
native "$R/glow.om" native
cmp "$B/native" "$B/native.first" || fail 'native compile nondeterministic'
cmp "$B/native.bin" "$B/oracle.bin" || fail 'native frames differ from pinned C ck_glow'
echo "PASS native Omega glow frames identical to pinned C ck_glow bytes=$want"

# 3. Mutants: each changes one line of glow.om and must still run but differ.
mutate() { # $1 name, $2 exact line, $3 replacement
    awk -v a="$2" -v b="$3" '$0 == a { $0 = b; c++ } { print } END { exit c != 1 }' \
        "$R/glow.om" > "$B/glow-$1.om" || fail "mutant $1 anchor not found once"
}
mutate falloff 'f = sub uint 256 uint shr mul ulong d2 ulong get gl 1 32' \
    'f = sub uint 256 uint shr mul ulong d2 ulong get gl 1 33'
mutate saturate 'ret uint 255' 'ret uint 254'
mutate clipright 'x1 = imin imin add shr add cx r 4 1 add get cf 2 get cf 4 get cf 0' \
    'x1 = imin add shr add cx r 4 1 get cf 0'
for m in falloff saturate clipright; do
    native "$B/glow-$m.om" "mutant-$m"
    [ "$(wc -c < "$B/mutant-$m.bin")" = "$want" ] || fail "mutant $m wrong length"
    if cmp -s "$B/mutant-$m.bin" "$B/oracle.bin"; then fail "mutant $m escaped"; fi
    echo "PASS native mutant $m differs from oracle"
done

# 4. Wasm through the IR memory backend.
if [ "${GLOW_WASM:-0}" != 1 ]; then
    echo 'NOT_RUN wasm half (GLOW_WASM unset: IR memory backend not integrated)'
    printf 'GLOW NATIVE PASS bytes=%s mutants=3; WASM NOT_RUN\n' "$want"
    exit 3
fi
command -v node >/dev/null || { echo 'NOT_RUN requires Node WebAssembly oracle'; exit 3; }
node --version
quiet "$OMC" < seed/wasm/irwasm.om > "$B/irwasm" 2> "$B/irwasm.err" || fail 'irwasm compile'
[ ! -s "$B/irwasm.err" ] || fail 'irwasm compiler stderr'
chmod +x "$B/irwasm"
towasm() { # $1 glow source, $2 output stem
    { printf 'ir\n'; cat "$1" "$R/background.om"; } > "$B/$2.input"
    quiet "$OMC" < "$B/$2.input" > "$B/$2.ir" 2> "$B/$2.ir.err" || fail "$2 IR exit $?: $(cat "$B/$2.ir.err")"
    quiet "$B/irwasm" < "$B/$2.ir" > "$B/$2.wasm" 2> "$B/$2.wasm.err" || fail "$2 irwasm exit $?: $(cat "$B/$2.wasm.err")"
}
towasm "$R/glow.om" glow
cp "$B/glow.wasm" "$B/glow.first.wasm"
towasm "$R/glow.om" glow
cmp "$B/glow.wasm" "$B/glow.first.wasm" || fail 'wasm translation nondeterministic'
quiet node "$R/compare-glow.js" "$B/glow.wasm" "$R/glow-cases.tsv" "$B/oracle.bin" || fail 'wasm frames differ'
for m in falloff saturate clipright; do
    towasm "$B/glow-$m.om" "mutant-$m"
    set +e
    quiet node "$R/compare-glow.js" "$B/mutant-$m.wasm" "$R/glow-cases.tsv" "$B/oracle.bin" > "$B/mutant-$m.log" 2>&1
    rc=$?
    set -e
    [ "$rc" = 1 ] || fail "wasm mutant $m exit $rc instead of 1"
    grep -q 'pixel mismatch' "$B/mutant-$m.log" || fail "wasm mutant $m failed for an unrelated reason"
    echo "PASS wasm mutant $m rejected"
done
printf 'GLOW PASS native and wasm identical to pinned C ck_glow bytes=%s mutants=3\n' "$want"
sha256sum "$R/glow.om" "$R/background.om" "$R/glow-cases.tsv" "$B/glow.wasm" "$B/native"
