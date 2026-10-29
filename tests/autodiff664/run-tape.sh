#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Build and run the native tape test and its three compiled mutants.
#   OMC=<pinned seed/out/omc> OUT=<scratch dir> sh tests/autodiff664/run-tape.sh
# Host build glue only: concatenation and process exits. All logic is in .om.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${OMC:?set OMC to the pinned seed/out/omc}"
: "${OUT:?set OUT to a scratch directory}"
mkdir -p "$OUT"
META=$ROOT/native/ecosystem663/metadata.om
TAPE=$ROOT/native/autodiff664/tape.om
TEST=$ROOT/tests/autodiff664/tape-test.om
fail() { echo "TAPE_SUITE_FAIL $1"; exit 1; }

build() { # $1 unit.om  $2 out.elf  $3 tag
    "$OMC" < "$1" > "$2" 2> "$OUT/$3.compile.stderr"
    echo $? > "$OUT/$3.compile.rc"
    [ "$(cat "$OUT/$3.compile.rc")" = 0 ] || return 1
    [ "$(od -An -tx1 -N4 "$2" | tr -d ' ')" = 7f454c46 ] || return 1
    chmod 700 "$2"
}

sha256sum "$OMC" > "$OUT/omc.sha256"
cat "$META" "$TAPE" "$TEST" > "$OUT/tape-test.unit.om"
build "$OUT/tape-test.unit.om" "$OUT/tape-test.elf" tape-test || fail compile
"$OUT/tape-test.elf" > "$OUT/tape-test.stdout" 2> "$OUT/tape-test.stderr"
echo $? > "$OUT/tape-test.rc"
[ "$(cat "$OUT/tape-test.rc")" = 0 ] || fail tape-test
grep -q '^NATIVE_TAPE_PASS checks=' "$OUT/tape-test.stdout" || fail tape-test-output

build "$ROOT/tests/autodiff664/mutate.om" "$OUT/mutate.elf" mutate || fail mutate-compile
for mode in 0 1 2; do
    { printf '%s' "$mode"; cat "$TAPE"; } | "$OUT/mutate.elf" > "$OUT/mutant$mode.tape.om"
    echo $? > "$OUT/mutant$mode.mutate.rc"
    [ "$(cat "$OUT/mutant$mode.mutate.rc")" = 0 ] || fail "mutant$mode-inject"
    cat "$META" "$OUT/mutant$mode.tape.om" "$TEST" > "$OUT/mutant$mode.unit.om"
    build "$OUT/mutant$mode.unit.om" "$OUT/mutant$mode.elf" "mutant$mode" || fail "mutant$mode-compile"
    "$OUT/mutant$mode.elf" > "$OUT/mutant$mode.stdout" 2> "$OUT/mutant$mode.stderr"
    echo $? > "$OUT/mutant$mode.rc"
    [ "$(cat "$OUT/mutant$mode.rc")" = 1 ] || fail "mutant$mode-survived"
done
echo "TAPE_SUITE_PASS $(cat "$OUT/tape-test.stdout")"
