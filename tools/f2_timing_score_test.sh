#!/usr/bin/env bash
# Self test of tools/f2_timing_score (omega-mv#570) on synthetic traces: a share effect of 40 us must give T > 0,
# no share effect must give T < 0 (the extra coefficients are paid for and buy nothing), and shuffling the shares
# must take the positive case back below 0. Usage: f2_timing_score_test.sh <f2_timing_score binary> <scratch dir>
set -euo pipefail
B=$1; D=$2; mkdir -p "$D"
gen() { # gen <effect_ns> <seed> -> TSV on stdout: 2 positions x 3 shares x 40 reps, noise uniform +-5 us
  awk -v eff="$1" -v seed="$2" 'BEGIN { srand(seed); split("1023 4095", P, " "); split("100 5000 10000", R, " ");
    for (p = 1; p <= 2; p++) for (r = 1; r <= 3; r++) for (k = 1; k <= 40; k++) {
      S = 10000; t = 100000 * p + eff * R[r] / S + int((rand() - 0.5) * 10000);
      printf "syn\tc%d\t1\t%d\t1\t0\t%d\t%d\t0\t%d\t%d\t0000000000000000\n", r, P[p], k, t, S, R[r] } }'
}
gen 40000 1 > "$D/eff-train.tsv"; gen 40000 2 > "$D/eff-held.tsv"; gen 0 3 > "$D/nul-train.tsv"; gen 0 4 > "$D/nul-held.tsv"
t_of() { "$B" score "$@" | awk '/T\(M; B, D\)/ { print $5 }'; }
"$B" fit "$D/eff-train.tsv" "$D/eff.model" > /dev/null; "$B" fit "$D/nul-train.tsv" "$D/nul.model" > /dev/null
te=$(t_of "$D/eff.model" "$D/eff-held.tsv"); tn=$(t_of "$D/nul.model" "$D/nul-held.tsv"); ts=$(t_of "$D/eff.model" "$D/eff-held.tsv" --shuffle 7)
echo "effect T=$te ub  null T=$tn ub  shuffled T=$ts ub"
fail=0
[ "$te" -gt 0 ] || { echo "FAIL: a 40 us share effect must score T > 0"; fail=1; }
[ "$tn" -lt 0 ] || { echo "FAIL: no effect must score T < 0"; fail=1; }
[ "$ts" -lt "$te" ] || { echo "FAIL: shuffling the shares must lower T"; fail=1; }
[ "$ts" -lt 0 ] || { echo "FAIL: shuffled shares must score T < 0"; fail=1; }
"$B" bits "$D/eff-held.tsv" > /dev/null || { echo "FAIL: bits on identical outputs"; fail=1; }
[ $fail = 0 ] && echo "f2_timing_score_test: PASS" || { echo "f2_timing_score_test: FAIL"; exit 1; }
