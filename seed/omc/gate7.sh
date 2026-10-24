#!/bin/sh
# gate7.sh -- break, continue and trap (OMEGESE 9.8). The compiler under test is seed/out/omc, which
# seed/omega-from-seed.sh has already built and proved self-identical. om4 refuses all three words
# (they are omc-only), and that refusal is checked here too. RC=0 only when every case passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r7
T=seed/omc/tests/loop
S=seed/out/omc
OM4=seed/rung4/om4.bin
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# om4 has neither word: each such line must be refused, never compiled.
for f in w1.om c2.om t1.om; do
    set +e
    "$OM4" < "$T/$f" > "$B/om4-$f.bin" 2> "$B/om4-$f.err"
    orc=$?
    set -e
    [ "$orc" != 0 ] || bad "om4 accepted $f (break/trap are omc-only)"
done
say "PASS om4 refuses break and trap"

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

run_exit w1 6
run_exit f1 3
run_exit n1 33
run_exit n2 4
run_exit n3 3
run_exit c1 18
run_exit c2 12
run_exit c3 36
run_exit c4 0
run_exit c5 104
run_exit t1 105
run_exit t2 107
refuse bad_outside 9
refuse bad_after 17
refuse bad_continue 9
refuse bad_after_continue 17
refuse bad_trap0 8
refuse bad_trap15 8

say "PASS gate7 $n cases"
exit 0
