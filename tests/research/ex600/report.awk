# Independent arithmetic/reachability check of published finite fixture rows.
# No OSC/compiler/search functions. Usage: awk -f report.awk landscapes.csv rows.csv
BEGIN { FS=","; OFS=","; bad=0 }
FNR==1 { next }
NR==FNR {
    k=$1 SUBSEP $2; n=$3; cost[k,n]=$6;
    live[k,n]=($7==1 && (n==0 || live[k,$4]));
    if(live[k,n] && (!(k in opt) || $6<opt[k]))opt[k]=$6;
    if($8!=live[k,n]) {print "FAIL independent reachability";bad++}
    next
}
{
    k=$1 SUBSEP $2; arm=$3; h=$4;
    key=k SUBSEP arm SUBSEP h;
    reason=($5==1 ? "frontier_empty" : ($5==2 ? "budget_exhausted" : ($5==3 ? "invalid_state" : "active_or_unknown")));
    if(seen[key]++ || $6!=reason || !live[k,$8] || cost[k,$8]!=$9 || opt[k]!=$10 || $11!=$9-$10 ||
       $12!=$13+$14+$15+$16 || $22!=$12+h*$9 || $23!=100*h-$22 ||
       $24!=($9<100 ? int($12/(100-$9))+1 : 0) || $25!=$18+872 ||
       $12>160 || $7>14 || $9>100 || $17>(arm<2?8:4)) {
        print "FAIL published arithmetic/identity row " FNR;bad++
    }
    if(h==32) {
        cases[arm]++;wins[arm]+=($23>0);net[arm]+=$23;life[k,arm]=$22;
        regret[arm]+=$11;abandon[arm]+=$19;unexplored[arm]+=$21;
        if(!(arm in minnet)||$23<minnet[arm])minnet[arm]=$23;
        if(!(arm in maxnet)||$23>maxnet[arm])maxnet[arm]=$23;
        fam=$1 SUBSEP arm; fcases[fam]++; fnet[fam]+=$23;
        fwin[fam]+=($23>0); fspent[fam]+=$12; fbest[fam]+=$9;
        if(arm==2){keys[k]=1;cn[k]=$23}
    }
    rows++
}
END {
    if(rows!=300){print "FAIL row count " rows;bad++}
    for(a=0;a<4;a++)if(cases[a]!=25){print "FAIL arm count";bad++}
    overall=1;beatA=0;beatD=0;positive=0;
    for(k in keys){beatA+=(life[k,2]<life[k,0]);beatD+=(life[k,2]<life[k,3]);positive+=(cn[k]>0);
        if(cn[k]<=0||life[k,2]>=life[k,0]||life[k,2]>=life[k,3])overall=0;}
    print "independent_rows="rows " arithmetic_failures="bad;
    print "arm,cases,positive_net_H32,total_net_H32,min_net_H32,max_net_H32,total_regret,total_abandoned,total_unexplored_better";
    for(a=0;a<4;a++)print a,cases[a],wins[a]+0,net[a],minnet[a],maxnet[a],regret[a],abandon[a],unexplored[a];
    print "family,arm,cases,positive_net_H32,total_net_H32,total_spent,total_best_cost";
    for(f=3;f<8;f++)for(a=0;a<4;a++){k=f SUBSEP a;print f,a,fcases[k],fwin[k]+0,fnet[k],fspent[k],fbest[k];}
    print "C_positive_H32="positive "/25 C_beats_A="beatA "/25 C_beats_D="beatD "/25";
    print "predeclared_hypothesis="(overall?"PASS":"FAIL");
    exit bad?1:0
}
