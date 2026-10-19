#!/usr/bin/env bash
# test-rw249: checks for #249 E06 (ledger L059) and the mutants that must turn each check red.
# Usage: test_rw249.sh <dir with rwcheck rwsearch rwprobe>
# Every check prints PASS/FAIL; every mutant prints RED (good: the check caught it) or GREEN-BAD.
set -u
BIN=$1
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CON=$ROOT/tools/rw249/contracts
AX=$CON/axioms.rwa
T=$BIN/test-scratch
rm -rf "$T"; mkdir -p "$T"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
red() { echo "RED $1 (mutant caught, bad rows $2)"; }
notred() { echo "GREEN-BAD $1 (mutant not caught: the check is broken)"; fails=$((fails + 1)); }

# --- 1. axiom base sound; M5 unsound axiom refused ----------------------------------------------
"$BIN/rwcheck" axioms --axioms "$AX" > "$T/ax.txt" && grep -q 'RWAXIOM-BASE ok' "$T/ax.txt" && pass "axiom base sound" || fail "axiom base sound"
{ cat "$AX"; echo "axiom bogus-and-is-or domains=wrap : (and ?p ?q) = (or ?p ?q)"; } > "$T/ax_m5.rwa"
"$BIN/rwcheck" axioms --axioms "$T/ax_m5.rwa" > "$T/m5.txt"; rc=$?
n=$(grep -c 'UNSOUND' "$T/m5.txt"); [ "$rc" = 3 ] && [ "$n" -gt 0 ] && red "M5 unsound axiom in base" "$n" || notred "M5 unsound axiom in base"

# --- 2. contracts: R* and P* proven, N* rejected -------------------------------------------------
"$BIN/rwcheck" contracts --axioms "$AX" --cache "$T/c.cache" "$CON"/*.rw > "$T/con.txt"
bad=$(grep '^RWCHECK [RP][0-9]' "$T/con.txt" | grep -vc proven_in_scope)
[ "$bad" = 0 ] && pass "R01-R25 and P01-P03 proven_in_scope" || fail "R/P contracts ($bad not proven)"
bad=$(grep '^RWCHECK N[0-9]' "$T/con.txt" | grep -vc ' rejected ')
nn=$(grep -c '^RWCHECK N[0-9]' "$T/con.txt")
[ "$bad" = 0 ] && [ "$nn" = 7 ] && pass "N01-N07 rejected" || fail "negatives ($bad not rejected of $nn)"
# the proof layer alone rejects every negative that carries a derivation
bad=$(grep -E '^RWCHECK N0[1-46]' "$T/con.txt" | grep -vc 'also-invalid')
[ "$bad" = 0 ] && pass "derivation layer rejects N01-N04 N06 on its own" || fail "derivation layer ($bad negatives with a passing derivation)"

# M1: corrupt one derivation step of an accepted contract
sed 's/^  (add x (add (neg a) (neg b))) by add-assoc/  (add x (add (neg a) b)) by add-assoc/' "$CON/R02.rw" > "$T/M1.rw"
"$BIN/rwcheck" contracts --axioms "$AX" "$T/M1.rw" > "$T/m1.txt"
grep -q '^RWCHECK R02 rejected' "$T/m1.txt" && red "M1 corrupted derivation step" 1 || notred "M1 corrupted derivation step"
# M2: drop the assumption from R22
grep -v '^assume:' "$CON/R22.rw" | sed 's/^ops: -/ops: -\nassume: -/' > "$T/M2.rw"
"$BIN/rwcheck" contracts --axioms "$AX" "$T/M2.rw" > "$T/m2.txt"
grep -q '^RWCHECK R22 rejected' "$T/m2.txt" && red "M2 dropped assumption" 1 || notred "M2 dropped assumption"
# M6: semantic guard disabled: N05 (no derivation) must then escape rejection -> the negative check goes red
RW_MUTANT=no_semantic "$BIN/rwcheck" contracts --axioms "$AX" "$CON/N05.rw" > "$T/m6.txt" 2>/dev/null
grep -q '^RWCHECK N05 rejected' "$T/m6.txt" && notred "M6 semantic guard off" || red "M6 semantic guard off" 1
# M7: derivation step 2 skipped: a false claim with a bogus step 2 must lose its 'also-invalid' proof verdict
printf '%s\n' "rw-contract v1" "id: M7" "semantics: omega.v0.wrap" "domain: wrap" "widths: 8 16 32 64" "params: a b" "ops: -" "assume: -" \
  "lhs: (add (add x a) b)" "rhs: (add x (mul a b))" "derive:" "  (add x (add a b)) by add-assoc" "  (add x (mul a b)) by add-comm" "end" > "$T/M7.rw"
"$BIN/rwcheck" contracts --axioms "$AX" "$T/M7.rw" | grep -q 'also-invalid: step 2' && pass "M7 baseline: bogus step 2 flagged" || fail "M7 baseline"
RW_MUTANT=skip_step "$BIN/rwcheck" contracts --axioms "$AX" "$T/M7.rw" 2>/dev/null | grep -q 'also-invalid: step 2' && notred "M7 step skipped" || red "M7 step skipped" 1

# --- 3. cache: hit under the same semantics, miss under a changed one; M3 key forgets semantics ---
cache_case() {  # $1 = env prefix ("" or RW_MUTANT=cache_nosem); prints "hit miss" outcome of second+third run
  rm -f "$T/k.cache"
  env $1 "$BIN/rwcheck" contracts --axioms "$AX" --cache "$T/k.cache" "$CON/P03.rw" >/dev/null 2>&1
  a=$(env $1 "$BIN/rwcheck" contracts --axioms "$AX" --cache "$T/k.cache" "$CON/P03.rw" 2>/dev/null | grep -o 'cache=[a-z]*' | head -1)
  b=$(env $1 "$BIN/rwcheck" contracts --axioms "$AX" --cache "$T/k.cache" --semantics-version omega.v0.wrap.2 "$CON/P03.rw" 2>/dev/null | grep -o 'cache=[a-z]*' | head -1)
  echo "$a $b"
}
r=$(cache_case "")
[ "$r" = "cache=hit cache=miss" ] && pass "cache hit on same semantics, miss on changed semantics" || fail "cache ($r)"
r=$(cache_case "RW_MUTANT=cache_nosem")
[ "$r" = "cache=hit cache=miss" ] && notred "M3 cache key without semantics version" || red "M3 cache key without semantics version" 1

# --- 4. probe counterexamples still agree on probes and differ as functions ------------------------
"$BIN/rwprobe" verify-ce "$ROOT"/tests/research/rw249/counterexamples/CE*.txt > "$T/ce.txt" && pass "probe counterexamples ($(grep -c ' ok' "$T/ce.txt"))" || fail "probe counterexamples"

# --- 5. unknown cost is never ranked as free; M8 unknown -> zero ----------------------------------
"$BIN/rwsearch" unknown-test > "$T/u.txt" && pass "unknown cost ineligible (synthetic model)" || fail "unknown cost"
RW_MUTANT=unknown_zero "$BIN/rwsearch" unknown-test > "$T/u8.txt" 2>/dev/null; n=$(grep -o 'bad_rows=[0-9]*' "$T/u8.txt" | cut -d= -f2)
[ "${n:-0}" -gt 0 ] && red "M8 unknown cost counted as zero" "$n" || notred "M8 unknown cost counted as zero"

# --- 6. end to end on the development set with a probe-only bogus rule injected --------------------
"$BIN/rwsearch" gen --seed 1 --n 100 > "$T/dev.set"
echo "INJ-0001 w=8 add 0x5; and 0x3f" >> "$T/dev.set"
"$BIN/rwsearch" calibrate --out "$T/m.blob" --obs 12 > /dev/null
for arm in M G Bi B; do
  "$BIN/rwsearch" run --arm $arm --set "$T/dev.set" --out "$T/$arm" --model "$T/m.blob" --inject-probe-rule > /dev/null
  "$BIN/rwcheck" derivations --axioms "$AX" --contracts "$CON" --cache "$T/c.cache" "$T/$arm/derivs.rwd" > "$T/$arm/verdicts.txt"
  "$BIN/rwsearch" verify-accepted --dir "$T/$arm" --verdicts "$T/$arm/verdicts.txt" > "$T/$arm/e2e.txt" && pass "e2e arm $arm: every accepted output equal on omega's evaluator" || fail "e2e arm $arm"
done
grep -q 'RWDERIV INJ-0001 rejected' "$T/B/verdicts.txt" && pass "injected probe-only rule R99 rejected by the checker (failed candidate)" || fail "R99 not rejected"
# M4: bypass the checker -> the end-to-end check must go red
RW_MUTANT=bypass_checker "$BIN/rwsearch" verify-accepted --dir "$T/B" --verdicts "$T/B/verdicts.txt" > "$T/m4.txt" 2>/dev/null
n=$(grep -o 'bad_rows=[0-9]*' "$T/m4.txt" | tail -1 | cut -d= -f2)
[ "${n:-0}" -gt 0 ] && red "M4 checker bypassed" "$n" || notred "M4 checker bypassed"

# --- 7. determinism: two runs give identical outputs and derivations ------------------------------
for k in 1 2; do "$BIN/rwsearch" run --arm B --set "$T/dev.set" --out "$T/det$k" --model "$T/m.blob" > /dev/null; done
if cmp -s "$T/det1/derivs.rwd" "$T/det2/derivs.rwd" && cmp -s <(cut -f1-14 "$T/det1/results.tsv") <(cut -f1-14 "$T/det2/results.tsv"); then
  pass "determinism (same model blob, same set)"; else fail "determinism"; fi

echo "TEST-RW249 failures=$fails"
[ "$fails" = 0 ]
