#!/bin/sh
# net(H) inputs from one stream pair (FREEZE.md section 10): netfit.sh A_DIR ARM_DIR
# Prints C, p, Delta, r (CPU ns) and the observed cumulative saving on the stream, plus
# the harness's net(H) series for H = 1..H_MAX. Every number names its rows.
set -u
A=$1; L=$2; HMAX=${3:-24}
BIN=${BIN:-build/tests-research/ls584}
med() { sort -n | awk '{v[NR]=$1} END{ if(!NR){print 0} else print v[int(NR/2)+1] }'; }
# block-1 first solve cost: median over block 1 rows
a_first=$(awk -F, 'NR>1 && $2==1 {print $16}' "$A/rows.csv" | med)
l_first=$(awk -F, 'NR>1 && $2==1 {print $16}' "$L/rows.csv" | med)
# first reuse of a CANDIDATE (path 2, verified_reuse 0): what the reference-first re-realization costs
l_cand=$(awk -F, 'NR>1 && $5==2 && $24==0 {print $16}' "$L/rows.csv" | med)
n_cand=$(awk -F, 'NR>1 && $5==2 && $24==0 {n++} END{print n+0}' "$L/rows.csv")
# Cost of a repeated objective solved by search: the learning arm's own path-1 rows in
# blocks 2 and 5 (same code path as A, same process and time window, so not subject to
# cross-process load noise); A's own median on those blocks is printed beside it.
a_rep=$(awk -F, 'NR>1 && ($2==2||$2==5) && $5==1 {print $16}' "$L/rows.csv" | med)
a_rep_x=$(awk -F, 'NR>1 && ($2==2||$2==5) {print $16}' "$A/rows.csv" | med)
l_ver=$(awk -F, 'NR>1 && $24==1 {print $16}' "$L/rows.csv" | med)
n_ver=$(awk -F, 'NR>1 && $24==1 {n++} END{print n+0}' "$L/rows.csv")
# applicability p: hits over reuse opportunities (blocks 2 and 5)
p=$(awk -F, 'NR>1 && ($2==2||$2==5) {o++; h+=$10} END{printf "%.4f", o?h/o:0}' "$L/rows.csv")
# retention r: block-5 recall hit rate h at median interference distance d (objective positions)
h5=$(awk -F, 'NR>1 && $2==5 {o++; h+=$10} END{printf "%.4f", o?h/o:0}' "$L/rows.csv")
d=$(awk -F, 'NR>1 && $2==5 {print $1} ' "$L/rows.csv" | med); d0=$(awk -F, 'NR>1 && $2==1 {print $1}' "$L/rows.csv" | med); dist=$((d - d0))
r=$(awk -v h="$h5" -v d="$dist" 'BEGIN{ if (h<=0) print 0; else if (h>=1) print 1; else printf "%.6f", exp(log(h)/d) }')
# persistence per key: persist_cpu_ns total / stored
persist=$(sed -n 's/.*"persist_cpu_ns": \([0-9]*\).*/\1/p' "$L/summary.json"); stored=$(sed -n 's/.*"stored": \([0-9]*\).*/\1/p' "$L/summary.json")
pk=$(awk -v p="$persist" -v s="$stored" 'BEGIN{printf "%.0f", s?p/s:0}')
# C = (candidate first reuse - search path) + persistence per key, within the learning arm;
# the first solve itself costs the same as A (same path) plus the store, which is in persist.
C=$(awk -v lc="$l_cand" -v ar="$a_rep" -v pk="$pk" 'BEGIN{v=(lc-ar)+pk; if (v<0) v=0; printf "%.0f", v}')
Delta=$(awk -v ar="$a_rep" -v lv="$l_ver" 'BEGIN{printf "%.0f", ar-lv}')
# observed: cumulative cpu of the stream, both arms
a_tot=$(sed -n 's/.*"cpu_ns_total": \([0-9]*\).*/\1/p' "$A/summary.json" | head -1); l_tot=$(sed -n 's/.*"cpu_ns_total": \([0-9]*\).*/\1/p' "$L/summary.json" | head -1)
echo "inputs A=$A L=$L"
echo "a_first_med=$a_first l_first_med=$l_first l_candidate_reuse_med=$l_cand(n=$n_cand) search_repeat_med(L path1)=$a_rep A_repeat_med(cross-process)=$a_rep_x l_verified_reuse_med=$l_ver(n=$n_ver) persist_per_key=$pk"
echo "C=$C p=$p Delta=$Delta r=$r (h5=$h5 at distance $dist) observed_stream_cpu_ns A=$a_tot L=$l_tot diff=$((a_tot - l_tot))"
"$BIN" neth "$C" "$p" "$Delta" "$r" "$HMAX"
