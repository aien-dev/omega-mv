#!/usr/bin/env bash
# aggregate.sh MEASURE_OUT -- min/median/max of every cost column per step across the five
# runs of record (r1..r5), for the run1 pilot and restart and the cold restart. Selection
# only (sort, pick); sums and ratios that enter a verdict go through aien-calc.
set -u
OUT=${1:?}
agg() {  # agg LABEL CSV_RELATIVE_PATH
  local label=$1 rel=$2
  awk -F, -v label="$label" '
    FNR==1 || /^#/ || $1=="step" { if ($1=="step") for (i=2;i<=NF;i++) col[i]=$i; next }
    { key=$1; steps[key]=1; for (i=2;i<=NF;i++) { n=++cnt[key,i]; v[key,i,n]=$i+0 } ncol=NF }
    END {
      for (s in steps) for (i=2;i<=ncol;i++) {
        n=cnt[s,i]; for (a=1;a<=n;a++) w[a]=v[s,i,a]
        for (a=1;a<=n;a++) for (b=a+1;b<=n;b++) if (w[b]<w[a]) { t=w[a]; w[a]=w[b]; w[b]=t }
        printf "%s,%s,%s,%d,%.0f,%.0f,%.0f\n", label, s, col[i], n, w[1], w[int((n+1)/2)], w[n]
      }
    }' "$OUT"/r[1-5]/"$rel" | sort -t, -k1,1 -k2,2 -k3,3
}
echo "set,step,column,n,min,median,max"
agg pilot run1/steps-pilot.csv
agg restart run1/steps-restart.csv
agg restart_cold cold/steps-restart-cold.csv
agg control_idle idle/steps-control-idle.csv
