#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# gate-smt.sh: contracts as verification conditions, checked by z3 (a host oracle, not part of the
# build). Every function of ok.om and okmore.om must be unsat (proved for every input); every broken
# contract below must be sat, with z3's counterexample printed, and where the unit's main reaches the
# broken contract the native program must stop with the contract trap (109 requires, 110 ensures or
# invariant), so the counterexample is real. A missing z3 is NOT_RUN (exit 3), never PASS.
# Run from the repository root. OMC (default seed/out/omc), Z3 (default z3), SCRATCH (default mktemp).
set -eu
OMC=${OMC:-seed/out/omc}
Z3=${Z3:-z3}
D=seed/omc/contracts
S=${SCRATCH:-$(mktemp -d)}
mkdir -p "$S"
command -v "$Z3" > /dev/null 2>&1 || { echo 'NOT_RUN gate-smt: z3 not found (set Z3)' >&2; exit 3; }
zp=$(command -v "$Z3")
echo "oracle $($Z3 --version) sha256 $(sha256sum "$zp" | cut -d' ' -f1)"
echo "omc sha256 $(sha256sum "$OMC" | cut -d' ' -f1)"
bad=0
n=0
fail() { echo "FAIL $*"; bad=1; }
verdict() { "$Z3" -smt2 "$1" 2>&1 | sed -n 1p; }
# check UNIT FN WANT [NATIVE-EXIT]
check() {
    u=$1; fn=$2; want=$3
    b=$(basename "$u" .om)
    o="$S/$b"
    if [ ! -d "$o" ]; then
        "$OMC" < "$u" > "$S/$b.bin" || { fail "$u: omc refused it natively"; return; }
        chmod +x "$S/$b.bin"
        OMC=$OMC sh "$D/smt/emit-smt.sh" "$u" "$o" > /dev/null || { fail "$u: emit"; return; }
    fi
    f="$o/$fn.smt2"
    [ -f "$f" ] || { fail "$u $fn: no script"; return; }
    got=$(verdict "$f")
    n=$((n + 1))
    if [ "$got" != "$want" ]; then
        fail "$u $fn: z3 says $got, want $want"
        return
    fi
    echo "PASS $u $fn $got"
    if [ "$want" = sat ]; then
        "$Z3" -smt2 "$f" 2>&1 | sed -n '2,$p' | tr -s ' \n' ' ' | sed 's/^/  counterexample /'
        echo
    fi
    if [ $# = 4 ]; then
        set +e
        "$S/$b.bin" > /dev/null 2>&1
        st=$?
        set -e
        [ "$st" = "$4" ] || { fail "$u: native exit $st, want $4"; return; }
        echo "PASS $u native exit $st"
    fi
}
# Every function of the accepted units: unsat.
check "$D/ok.om" step unsat
check "$D/ok.om" main unsat 0
check "$D/smt/okmore.om" absv unsat
check "$D/smt/okmore.om" sumto unsat
check "$D/smt/okmore.om" main unsat 0
check "$D/smt/overflow.om" twice unsat
check "$D/break.om" main unsat 0
check "$D/inline.om" f unsat
check "$D/inline.om" main unsat 0
check "$D/nestedinline.om" inner unsat
check "$D/nestedinline.om" outer unsat
check "$D/nestedinline.om" main unsat 0
check "$D/proved.om" main unsat 0
check "$D/plain.om" main unsat 0
# One broken contract per construct: sat, and the native run traps where main reaches it.
check "$D/smt/offbyone.om" inc sat
check "$D/smt/offbyone.om" main unsat 110
check "$D/smt/lastiter.om" main sat 110
check "$D/smt/overflow.om" wrap sat
check "$D/requires.om" f unsat
check "$D/requires.om" main sat 109
check "$D/ensures.om" f sat
check "$D/ensures.om" main unsat 110
check "$D/branchret.om" f sat 110
check "$D/forfinal.om" main sat 110
check "$D/continue.om" main sat 110
check "$D/zero.om" main sat 110
check "$D/diag.om" main sat 109
# Control: the oracle must refuse a contradiction and accept a tautology.
printf '(set-logic QF_BV)\n(assert false)\n(check-sat)\n' > "$S/false.smt2"
printf '(set-logic QF_BV)\n(assert true)\n(check-sat)\n' > "$S/true.smt2"
[ "$(verdict "$S/false.smt2")" = unsat ] || fail 'oracle control: assert false is not unsat'
[ "$(verdict "$S/true.smt2")" = sat ] || fail 'oracle control: assert true is not sat'
# Outside the subset: a unit with arrays is refused (error 9), never silently proved.
if { printf 'smt\n'; cat "$D/returnonce.om"; } | "$OMC" > /dev/null 2> "$S/refuse.err"; then
    fail 'returnonce.om (globals) was translated'
else
    echo "PASS refusal $(cat "$S/refuse.err")"
fi
[ "$bad" = 0 ] || { echo "FAIL gate-smt"; exit 1; }
echo "PASS gate-smt $n scripts"
