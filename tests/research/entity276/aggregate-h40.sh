#!/usr/bin/env bash
# aggregate-h40.sh MEASURE_OUT -- per run, variant and arm: cumulative process CPU (steps 5a
# + every 5_obj step) and planning CPU (cog over the objectives + rebuild; lazy: cog only),
# each sum through aien-calc with its receipt. Then per variant and arm min/median/max and
# the FREEZE range rule (selection and comparison only, no arithmetic outside aien-calc).
# Writes MEASURE_OUT/sums.csv and MEASURE_OUT/verdicts.txt.
set -u
OUT=${1:?}
calc() { aien-calc eval "$1" | awk '/^value/{v=$2} /^receipt/{r=$2} END{print v "," r}'; }
jf() { sed -n "s/.*\"$2\":\([0-9]*\).*/\1/p" "$1"; }
S="$OUT/sums.csv"
echo "rep,variant,arm,proc_cpu_cum_ns,proc_receipt,planning_cpu_ns,planning_receipt,searches,hits,rebuild_cpu_ns,bad_rows" > "$S"
for d in "$OUT"/r[1-9]*/h*; do
  rep=$(basename "$(dirname "$d")"); rep=${rep#r}; var=$(basename "$d")
  for arm in warm cold lazy forget; do
    csv="$d/$arm/steps-horizon-$arm.csv"; [ -f "$csv" ] || continue
    pe=$(awk -F, '$1=="5a_replay_rebuild" || $1 ~ /^5_obj[0-9]+$/ {printf "%s%s", s, $3; s="+"}' "$csv")
    ce=$(awk -F, '$1 ~ /^5_obj[0-9]+$/ {printf "%s%s", s, $4; s="+"}' "$csv")
    rb=$(jf "$d/$arm/identity-horizon-$arm.json" rebuild_cpu_ns)
    [ "$arm" = lazy ] || ce="$ce+$rb"
    sm="$d/$arm/summary-horizon-$arm.json"
    echo "$rep,$var,$arm,$(calc "$pe"),$(calc "$ce"),$(jf "$sm" after_restart_arrange),$(jf "$sm" after_restart_hits),$rb,$(jf "$sm" bad_rows)" >> "$S"
  done
done
{
echo "variant,arm,column,n,min,median,max"
awk -F, 'NR>1 { k=$2","$3; p[k]=p[k]" "$4; q[k]=q[k]" "$6 }
  function mmm(s,   a, n, i, j, t) { n=split(s, a, " "); for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (a[j]+0<a[i]+0) { t=a[i]; a[i]=a[j]; a[j]=t }
    return n "," a[1] "," a[int((n+1)/2)] "," a[n] }
  END { for (k in p) { print k ",proc_cpu_cum_ns," mmm(p[k]); print k ",planning_cpu_ns," mmm(q[k]) } }' "$S" | sort
echo
echo "comparison,variant,column,a_min,a_max,b_min,b_max,range_verdict,a_lower_in_runs"
awk -F, 'NR>1 { v[$2]=1; P[$2","$3","$1]=$4; Q[$2","$3","$1]=$6; R[$1]=1; srch[$2","$3]+=$8 }
  END {
    split("warm:cold lazy:cold warm:forget lazy:warm", cmp, " ")
    for (vv in v) for (c in cmp) { split(cmp[c], ab, ":"); for (col=1; col<=2; col++) {
      amin=""; amax=""; bmin=""; bmax=""; low=0; n=0
      for (r in R) { a=(col==1?P[vv","ab[1]","r]:Q[vv","ab[1]","r]); b=(col==1?P[vv","ab[2]","r]:Q[vv","ab[2]","r]); if (a==""||b=="") continue
        n++; a+=0; b+=0; if (amin==""||a<amin) amin=a; if (amax==""||a>amax) amax=a; if (bmin==""||b<bmin) bmin=b; if (bmax==""||b>bmax) bmax=b; if (a<b) low++ }
      verdict = amax<bmin ? "a_lower(PASS-shape)" : amin>bmax ? "a_higher(FAIL-shape)" : "overlap(INCONCLUSIVE)"
      if (ab[1]=="warm" && srch[vv",warm"]>0) verdict=verdict"+warm_searched"
      printf "%s_vs_%s,%s,%s,%d,%d,%d,%d,%s,%d of %d\n", ab[1], ab[2], vv, col==1?"proc_cpu_cum_ns":"planning_cpu_ns", amin, amax, bmin, bmax, verdict, low, n } } }' "$S" | sort
} > "$OUT/verdicts.txt"
cat "$OUT/verdicts.txt"
