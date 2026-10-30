#!/bin/sh
# gate10.sh -- compile-time layout queries (OMEGESE 9.11): sizeof T / sizeof S and offsetof S f.
# Both are constants of type ulong over the same 8-byte-cell struct layout that new/get/put use.
# RC=0 only when every case below ran and passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r10
T=seed/omc/tests/layout
S=seed/out/omc
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

n=0
run_exit() { # name expect
    n=$((n+1))
    set +e
    "$S" < "$T/$1.om" > "$B/$1.bin" 2> "$B/$1.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] || bad "$1: compiler exit $crc ($(cat "$B/$1.err"))"
    chmod +x "$B/$1.bin"
    set +e
    "$B/$1.bin" > /dev/null
    rrc=$?
    set -e
    [ "$rrc" = "$2" ] || bad "$1: ran $rrc, wanted $2"
    say "PASS $1 exit $rrc"
}

refuse() { # name expect-error
    n=$((n+1))
    set +e
    "$S" < "$T/$1.om" > "$B/$1.bin" 2> "$B/$1.err"
    crc=$?
    set -e
    [ "$crc" = "$2" ] || bad "$1: compiler exit $crc, wanted $2 ($(cat "$B/$1.err"))"
    grep -q "error $2" "$B/$1.err" || bad "$1: diagnostic does not name error $2"
    say "PASS $1 error $2"
}

run_exit sizes 0
refuse neg_type 16
refuse neg_field 3
refuse neg_struct 3
refuse neg_void 9

say "PASS gate10 $n cases"
exit 0
