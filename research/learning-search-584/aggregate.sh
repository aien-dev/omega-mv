#!/bin/sh
# Aggregate LS584 runs: one line per run dir (summary.json), then per-arm min/median/max
# over seeds, and the net(H) inputs (FREEZE.md section 10). Usage: aggregate.sh DIR [DIR...]
# Output is plain text, deterministic, suitable for the evidence README.
set -u
tmp=$(mktemp)
for d in "$@"; do
    s=$d/summary.json; r=$d/rows.csv
    [ -f "$s" ] || continue
    g() { sed -n "s/.*\"$1\": \([0-9.-]*\).*/\1/p" "$s" | head -1; }
    arm=$(sed -n 's/.*"arm": "\([^"]*\)".*/\1/p' "$s"); seed=$(sed -n 's/.*"seed": "\([^"]*\)".*/\1/p' "$s")
    ctl=$(sed -n 's/.*"control": "\([^"]*\)".*/\1/p' "$s"); mut=$(sed -n 's/.*"mutant": "\([^"]*\)".*/\1/p' "$s")
    # per-block medians of cpu_ns and hit counts from rows.csv
    medb() { awk -F, -v b="$1" 'NR>1 && $2==b {print $16}' "$r" | sort -n | awk '{v[NR]=$1} END{ if(!NR){print 0} else print v[int(NR/2)+1] }'; }
    hits() { awk -F, -v b="$1" 'NR>1 && $2==b {h+=$10; n++} END{printf "%d/%d", h, n}' "$r"; }
    succ() { awk -F, -v b="$1" 'NR>1 && $2==b {s+=($4=="success"); n++} END{printf "%d/%d", s, n}' "$r"; }
    printf '%s %s %s %s obj=%s succ=%s wrong=%s abst=%s fsearch=%s refr=%s clarify=%s cpu_ms=%.1f cands=%s real=%s peakB=%s persistB=%s pre_abst=%s/%s incons=%s mutbad=%s | med_ns b1=%s b2=%s b3=%s b4=%s b5=%s b6=%s b7=%s | hits b2=%s b5=%s | succ b3=%s b4=%s b6=%s b7=%s | load1=%s\n' \
        "$arm" "$seed" "$ctl" "$mut" "$(g objectives)" "$(g success)" "$(g wrong)" "$(g abstain)" "$(g failed_search)" "$(g refused_realize)" "$(g clarify)" \
        "$(echo "$(g cpu_ns_total) / 1000000" | bc -l)" "$(g candidates_total)" "$(g realizations_total)" "$(g peak_bytes)" "$(g persist_bytes)" \
        "$(sed -n 's/.*"abstained": \([0-9]*\).*/\1/p' "$s")" "$(sed -n 's/.*"preface": {"n": \([0-9]*\).*/\1/p' "$s")" "$(g inconsistent_commits)" "$(g mutant_bad_rows)" \
        "$(medb 1)" "$(medb 2)" "$(medb 3)" "$(medb 4)" "$(medb 5)" "$(medb 6)" "$(medb 7)" "$(hits 2)" "$(hits 5)" "$(succ 3)" "$(succ 4)" "$(succ 6)" "$(succ 7)" "$(g load1_at_end)"
done | sort | tee "$tmp"
echo
echo "# per arm/control over seeds: cpu_ms min/median/max, success min, wrong max"
awk '{ key=$1" "$3" "$4; for(i=5;i<=NF;i++){ if($i ~ /^cpu_ms=/){ split($i,a,"="); c[key][++n[key]]=a[2] } if($i ~ /^succ=/){ split($i,a,"="); if(!(key in smin)||a[2]<smin[key]) smin[key]=a[2] } if($i ~ /^wrong=/){ split($i,a,"="); if(a[2]>wmax[key]) wmax[key]=a[2] } } }
END{ for(k in c){ m=n[k]; for(i=1;i<=m;i++) for(j=i+1;j<=m;j++) if(c[k][j]<c[k][i]){t=c[k][i];c[k][i]=c[k][j];c[k][j]=t}; printf "%s n=%d cpu_ms min=%.1f med=%.1f max=%.1f success_min=%d wrong_max=%d\n", k, m, c[k][1], c[k][int((m+1)/2)], c[k][m], smin[k], wmax[k]+0 } }' "$tmp" | sort
rm -f "$tmp"
