#!/bin/sh
# r2 summary (AMENDMENTS A4): per R and held-out stream, lane CPU totals, d_L = A - L,
# noise n = |A - A2|, quality floors, the frozen verdict per arm per R, prediction scoring
# and the net(H) fit of the B lanes. Every difference and comparison in a verdict goes
# through aien-calc; receipts land in r2/calc/. Usage: aggregate-r2.sh > r2/summary.txt
set -u
cd "$(dirname "$0")/../.." || exit 2
EV=${EV:-evidence/LEARNING-SEARCH-584/r2}; D=$EV/record; K=$EV/calc; mkdir -p "$K"
BIN=${BIN:-$HOME/workspace/targets/omega-research-584-r2/tests-research/ls584}
HELD="5881 5882 5883 5884 5885"
g() { sed -n "s/.*\"$2\": \([0-9.-]*\).*/\1/p" "$1/summary.json" | head -1; }
calc() { # calc NAME EXPR -> prints the exact decimal; receipt json kept in r2/calc/NAME.json
    aien-calc eval --json "$2" > "$K/$1.json"
    aien-calc eval "$2" > "$K/$1.txt"
    awk '/^decimal/{print $2}' "$K/$1.txt"
}
cid() { awk '/^receipt/{print substr($2, 1, 13)}' "$K/$1.txt"; }
med5() { tr ' ' '\n' | grep -v '^$' | sort -n | sed -n 3p; }
mm() { tr ' ' '\n' | grep -v '^$' | sort -n | awk '{v[NR]=$1} END{printf "min %.1f / median %.1f / max %.1f ms", v[1]/1e6, v[3]/1e6, v[5]/1e6}'; }
csv=$EV/totals.csv
{ sed 's/^/# /' "$D/batch.txt"; sed 's/^/# /' "$D/wait.txt"
  echo "R,seed,lane,cpu_ns_total,objectives,success,wrong,inconsistent_commits,failed_search,verified_reuse_rows,hits,load1_start,load1_end"; } > "$csv"
for R in 1 3 6; do
    echo "== R = $R"
    for L in A A2 B C0; do eval "tot_$L=''"; done
    for L in B C0; do eval "pos_$L=0; neg_$L=0; floor_$L=ok"; done
    for s in $HELD; do
        d=$D/R$R-$s
        ls=$(sed -n 's/^start .*load1=\([0-9.]*\).*/\1/p' "$d/load.txt"); le=$(sed -n 's/^end .*load1=\([0-9.]*\).*/\1/p' "$d/load.txt")
        for L in A A2 B C0; do
            v=$(g "$d/$L" cpu_ns_total); eval "c_$L=$v; tot_$L=\"\$tot_$L $v\""
            vr=$(awk -F, 'NR>1 && $24==1{n++} END{print n+0}' "$d/$L/rows.csv"); h=$(awk -F, 'NR>1{n+=$10} END{print n+0}' "$d/$L/rows.csv")
            echo "$R,$s,$L,$v,$(g "$d/$L" objectives),$(g "$d/$L" success),$(g "$d/$L" wrong),$(g "$d/$L" inconsistent_commits),$(g "$d/$L" failed_search),$vr,$h,$ls,$le" >> "$csv"
        done
        if [ "$c_A" -ge "$c_A2" ]; then n=$(calc "noise_R${R}_$s" "$c_A - $c_A2"); else n=$(calc "noise_R${R}_$s" "$c_A2 - $c_A"); fi
        line="s=$s load1 $ls..$le A=$c_A A2=$c_A2 n=$n ($(cid noise_R${R}_$s))"
        for L in B C0; do
            eval "cl=\$c_$L"
            dd=$(calc "d_${L}_R${R}_$s" "$c_A - $cl")
            up=$(calc "margin_${L}_R${R}_$s" "($c_A - $cl) - $n")
            dn=$(calc "margin_neg_${L}_R${R}_$s" "($cl - $c_A) - $n")
            [ "${up#-}" = "$up" ] && [ "$up" != 0 ] && eval "pos_$L=\$((pos_$L + 1))"
            [ "${dn#-}" = "$dn" ] && [ "$dn" != 0 ] && eval "neg_$L=\$((neg_$L + 1))"
            eval "dl_$L=\"\${dl_$L:-} $dd\""
            # floors F1, F2 (per stream), F3 (stale control, wrong rows with path 2)
            inc=$(g "$d/$L" inconsistent_commits); su=$(g "$d/$L" success); ob=$(g "$d/$L" objectives); wl=$(g "$d/$L" wrong); wa=$(g "$d/A" wrong)
            f="F1 ok"; [ "$inc" = 0 ] || { f="F1 FAIL"; eval "floor_$L=fail"; }
            if [ $((su * 10)) -ge $((ob * 9)) ] && [ "$wl" -le $((wa + 1)) ]; then f="$f F2 ok($su/$ob, wrong $wl vs A $wa)"; else f="$f F2 FAIL($su/$ob, wrong $wl vs A $wa)"; eval "floor_$L=fail"; fi
            st=$EV/controls/$L-$s-R$R-stale/rows.csv
            if [ -f "$st" ]; then w3=$(awk -F, 'NR>1 && $4=="wrong" && $5==2{n++} END{print n+0}' "$st"); [ "$w3" = 0 ] || eval "floor_$L=fail"; f="$f F3 wrong-from-memory $w3"; else f="$f F3 NOT_RUN"; eval "[ \"\$floor_$L\" = fail ] || floor_$L=notrun"; fi
            line="$line | $L d=$dd ($(cid d_${L}_R${R}_$s)) d-n=$up $f"
        done
        echo "$line"
    done
    for L in A A2 B C0; do eval "echo \"  $L cpu: \$(echo \$tot_$L | mm)\""; done
    for L in B C0; do
        eval "p=\$pos_$L; q=\$neg_$L; fl=\$floor_$L; dl=\$dl_$L"
        if [ "$fl" = fail ] || [ "$q" = 5 ]; then v=FAIL; elif [ "$fl" = ok ] && [ "$p" = 5 ]; then v=PASS; elif [ "$fl" != ok ] && [ "$fl" != fail ]; then v="INCONCLUSIVE (F3 not run)"; else v=INCONCLUSIVE; fi
        echo "  verdict $L R=$R: $v (d > n on $p of 5, d < -n on $q of 5, floors $fl); d_$L: $(echo $dl | mm)"
        eval "dl_$L=''"
    done
done
echo "== prediction scoring (A4: Q1 34,735,264..45,964,016; Q2 214,907,552..248,498,128; Q3 485,165,984..554,737,744 ns)"
for R in 1 3 6; do
    case $R in 1) lo=34735264; hi=45964016;; 3) lo=214907552; hi=248498128;; 6) lo=485165984; hi=554737744;; esac
    vals=$(for s in $HELD; do awk '/^decimal/{print $2}' "$K/d_B_R${R}_$s.txt"; done | tr '\n' ' ')
    inside=$(for v in $vals; do [ "$v" -ge $lo ] && [ "$v" -le $hi ] && echo x; done | wc -l)
    m=$(echo $vals | med5); mi=no; [ "$m" -ge $lo ] && [ "$m" -le $hi ] && mi=yes
    echo "R=$R d_B per stream: $vals; inside range $inside of 5; median $m inside: $mi"
done
echo "== net(H) fit of the held-out B lanes (netfit.sh A B; Q4 net(3) 2,483,167..2,748,295, net(6) 6,185,395..6,917,412 ns)"
for R in 3 6; do for s in $HELD; do
    f=$(BIN=$BIN sh research/learning-search-584/netfit.sh "$D/R$R-$s/A" "$D/R$R-$s/B" 6 | sed -n 's/^C=\([0-9]*\) p=\([0-9.]*\) Delta=\([-0-9]*\) r=\([0-9.]*\).*/\1 \2 \3 \4/p')
    set -- $f; C=$1; p=$2; De=$3; r=$4
    if [ "$r" = 1 ]; then e3="3 * $p * $De - $C"; e6="6 * $p * $De - $C"; else e3="$p * $De * (1 - $r^3) / (1 - $r) - $C"; e6="$p * $De * (1 - $r^6) / (1 - $r) - $C"; fi
    n3=$(calc "net3_B_R${R}_$s" "$e3"); n6=$(calc "net6_B_R${R}_$s" "$e6")
    echo "R=$R s=$s C=$C p=$p Delta=$De r=$r net(3)=$n3 ($(cid net3_B_R${R}_$s)) net(6)=$n6 ($(cid net6_B_R${R}_$s))"
done; done
echo "== carry-over diagnostic (A5): per-objective A - A2 cpu_ns by Williams row (seq - 1) mod 4, pooled over the 5 streams"
echo "   predecessor types (s = search lane A/A2, t = table lane B/C0): row0 A:s A2:s, row1 A:t A2:t, row2 A:s A2:t, row3 A:t A2:t"
for R in 1 3 6; do
    printf 'R=%s' "$R"
    for row in 0 1 2 3; do
        m=$(for s in $HELD; do cut -d, -f1,2,16 "$D/R$R-$s/A/rows.csv" > /tmp/ls584a.$$; cut -d, -f16 "$D/R$R-$s/A2/rows.csv" > /tmp/ls584b.$$; paste -d, /tmp/ls584a.$$ /tmp/ls584b.$$ | awk -F, -v w=$row 'NR>1 && $2>0 && ($1-1)%4==w { print $3 - $4 }'; done | sort -n | awk '{v[NR]=$1} END{print v[int((NR+1)/2)] " (n=" NR ")"}')
        printf ' | row%s median %s' "$row" "$m"
    done
    echo
done
rm -f /tmp/ls584a.$$ /tmp/ls584b.$$
