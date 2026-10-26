#!/bin/sh
# gate19.sh -- OM-2 implied index checks and loop-constant hoisting (omc.om: nm_bk, ixbound, hzopen,
# hzrem). The compiler under test is seed/out/omc, already built and proved self-identical by
# seed/omega-from-seed.sh. Cases: an index range implied by a constant-count `for` drops its check (p1)
# and every index whose range is not implied keeps its check and traps (n1..n5, exit 103); hoisted
# constants are reloaded after a call whose loop hoists into the same registers (h1). Three mutants of
# omc.om, each built by seed/out/omc, must be caught by a case: m1 accepts `sub C i` for any C (n5),
# m2 ignores an assignment to the counter (n3), m3 drops the reload after a BL (h1). m0 never sets the
# implied range, so p1 must grow under it: the elimination is real, not only harmless.
# RC=0 only when every case passed and every mutant was caught.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r19
T=seed/omc/tests/hoist
S=seed/out/omc
SRC=seed/omc/omc.om
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# runc compiler case: prints the run exit of case compiled by compiler
runc() {
    set +e
    "$1" < "$T/$2.om" > "$B/$3-$2.bin" 2> "$B/$3-$2.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] || bad "$2 under $3: compiler exit $crc ($(cat "$B/$3-$2.err"))"
    chmod +x "$B/$3-$2.bin"
    set +e
    "$B/$3-$2.bin" > /dev/null 2>&1
    rrc=$?
    set -e
    echo "$rrc"
}

n=0
for c in p1:120 h1:168 n1:103 n2:103 n3:103 n4:103 n5:103; do
    name=${c%%:*} want=${c#*:}
    got=$(runc "$S" "$name" omc)
    [ "$got" = "$want" ] || bad "$name: ran $got, wanted $want"
    n=$((n+1))
    say "PASS $name exit $got"
done

# mutant name anchor replacement: replace (or with an empty replacement, delete) the one line equal to
# anchor, build it with seed/out/omc, and refuse a mutant whose anchor is missing or not unique.
mutant() {
    k=$(grep -c -x -F -- "$2" "$SRC" || true)
    [ "$k" = 1 ] || bad "mutant $1: anchor '$2' found $k times in $SRC"
    awk -v a="$2" -v r="$3" '$0 == a { if (r != "") print r; next } { print }' "$SRC" > "$B/$1.om"
    set +e
    "$S" < "$B/$1.om" > "$B/$1.omc" 2> "$B/$1.err"
    mrc=$?
    set -e
    [ "$mrc" = 0 ] || bad "mutant $1 did not build: exit $mrc ($(cat "$B/$1.err"))"
    chmod +x "$B/$1.omc"
}
caught() { # mutant case wanted
    got=$(runc "$B/$1.omc" "$2" "$1")
    [ "$got" != "$3" ] || bad "mutant $1 not caught: $2 still exits $3"
    say "PASS mutant $1 caught by $2 (exit $got, wanted $3)"
}
mutant m1 'if lt c sub k 1' 'if eq c 987654321'
caught m1 n5 103
mutant m2 'if eq fwscan w 0' 'if eq w w'
caught m2 n3 103
mutant m3 'hzrem' ''
caught m3 h1 168

mutant m0 'if eq fwscan w 0' 'if ne w w'
runc "$B/m0.omc" p1 m0 > /dev/null
a=$(wc -c < "$B/omc-p1.bin") z=$(wc -c < "$B/m0-p1.bin")
[ "$a" -lt "$z" ] || bad "p1 is $a bytes with the implied range and $z without: no check was dropped"
say "PASS p1 drops checks: $a bytes, $z bytes with no implied range"

say "PASS gate19 $n cases, 3 mutants caught"
exit 0
