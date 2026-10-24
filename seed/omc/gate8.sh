#!/bin/sh
# gate8.sh -- f32 and f64 (OMEGESE 9.9). seed/out/omc is the compiler under test; it is built and
# proved self-identical by seed/omega-from-seed.sh. Expected bit patterns were computed outside this
# compiler (IEEE-754), never copied from it. om4 refuses every float word, which is checked here too.
# RC=0 only when every case below ran and passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r8
T=seed/omc/tests/float
S=seed/out/omc
OM4=seed/rung4/om4.bin
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

for f in f64_bits.om f32_bits.om; do
    set +e
    "$OM4" < "$T/$f" > "$B/om4-$f.bin" 2> "$B/om4-$f.err"
    orc=$?
    set -e
    [ "$orc" != 0 ] || bad "om4 accepted float syntax in $f"
done
say "PASS om4 refuses float syntax"

n=0
run_ok() { # name
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
    [ "$rrc" = 0 ] || bad "$1: ran $rrc, wanted 0"
    say "PASS $1 exit 0"
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

run_ok f64_bits
run_ok f32_bits
run_ok arith
run_ok compare
run_ok convert
run_ok arrays
run_ok globals
run_ok params
run_ok structs
run_ok intrinsics
refuse neg_intr_int 7
refuse neg_intr_mixed 7
refuse neg_intr_ambig 11
refuse neg_mixed 7
refuse neg_index 7
refuse neg_for 7
refuse neg_read 7
refuse neg_ambiguous 11
refuse neg_dot 6
refuse neg_overflow 8
refuse neg_overflow2 8
refuse neg_overflow3 8
refuse neg_ir 3

say "PASS gate8 $n cases"
exit 0
