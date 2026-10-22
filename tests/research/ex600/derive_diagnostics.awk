# Convert the exact sealed L492 v1 CSV schema to diagnostic schema v2.
BEGIN { FS=","; OFS=","; bad=0; rows=0 }
FNR==1 {
    expected="family,seed,arm,horizon,stop,trials,best_node,best_cost,optimum,regret,spent,search,checking,checkpoint,storage,peak_branches,checkpoint_bytes,abandoned,refused,false_pruned,lifecycle,net,break_even,journal_bytes"
    if ($0 != expected) { print "refused unexpected frozen L492 row schema" > "/dev/stderr"; bad=1; exit 2 }
    print "family,seed,arm,horizon,stop,stop_reason,trials,best_node,best_cost,optimum,regret,spent,search,checking,checkpoint,storage,peak_branches,checkpoint_bytes,abandoned,refused,unexplored_better,lifecycle,net,break_even,journal_bytes"
    next
}
NF != 24 { print "refused malformed v1 row at line " FNR > "/dev/stderr"; bad=1; exit 2 }
{
    reason=($5==1 ? "frontier_empty" : ($5==2 ? "budget_exhausted" : ($5==3 ? "invalid_state" : "active_or_unknown")))
    printf "%s,%s,%s,%s,%s,%s",$1,$2,$3,$4,$5,reason
    for (i=6;i<=19;i++) printf ",%s",$i
    printf ",%s",$20
    for (i=21;i<=24;i++) printf ",%s",$i
    printf "\n"
    rows++
}
END { if (!bad && rows!=300) { print "refused row count " rows ", expected 300" > "/dev/stderr"; exit 2 } }
