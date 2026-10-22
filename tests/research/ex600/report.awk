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
    if(seen[key]++ || !live[k,$7] || cost[k,$7]!=$8 || opt[k]!=$9 || $10!=$8-$9 ||
       $11!=$12+$13+$14+$15 || $21!=$11+h*$8 || $22!=100*h-$21 ||
       $23!=($8<100 ? int($11/(100-$8))+1 : 0) || $24!=$17+872 ||
       $11>160 || $6>14 || $8>100 || $16>(arm<2?8:4)) {
        print "FAIL published arithmetic/identity row " FNR;bad++
    }
    if(h==32) {
        cases[arm]++;wins[arm]+=($22>0);net[arm]+=$22;life[k,arm]=$21;
        regret[arm]+=$10;abandon[arm]+=$18;falseprune[arm]+=$20;
        if(!(arm in minnet)||$22<minnet[arm])minnet[arm]=$22;
        if(!(arm in maxnet)||$22>maxnet[arm])maxnet[arm]=$22;
        fam=$1 SUBSEP arm; fcases[fam]++; fnet[fam]+=$22;
        fwin[fam]+=($22>0); fspent[fam]+=$11; fbest[fam]+=$8;
        if(arm==2){keys[k]=1;cn[k]=$22}
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
    print "arm,cases,positive_net_H32,total_net_H32,min_net_H32,max_net_H32,total_regret,total_abandoned,total_false_pruned";
    for(a=0;a<4;a++)print a,cases[a],wins[a]+0,net[a],minnet[a],maxnet[a],regret[a],abandon[a],falseprune[a];
    print "family,arm,cases,positive_net_H32,total_net_H32,total_spent,total_best_cost";
    for(f=3;f<8;f++)for(a=0;a<4;a++){k=f SUBSEP a;print f,a,fcases[k],fwin[k]+0,fnet[k],fspent[k],fbest[k];}
    print "C_positive_H32="positive "/25 C_beats_A="beatA "/25 C_beats_D="beatD "/25";
    print "predeclared_hypothesis="(overall?"PASS":"FAIL");
    exit bad?1:0
}
