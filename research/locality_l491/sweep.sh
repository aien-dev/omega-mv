#!/bin/sh
# Logical schedule accounting only. No timers or GPU code.
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
mkdir -p "$out/sweep"
printf 'n\tdepth\tarm\tlogical_transfer_B\tpure_additions\trecurrence_updates\tdeclared_fast_B\tinput_sha256\toutput_sha256\tprovenance\tchip_status\n' > "$out/sweep.tsv"
for n in 1 7 32 64; do
 for depth in 1 2 17 64; do
  for arm in 0 1 2 3 4; do
   req="$out/sweep/n${n}-d${depth}-a${arm}.req"
   result="$out/sweep/n${n}-d${depth}-a${arm}.out"
   awk -v n="$n" -v d="$depth" -v a="$arm" 'BEGIN {printf "3 %d %d %d 4096 99 1 ",n,d,a;for(i=0;i<n;i++)printf "%d ",1+i%3;for(i=0;i<n;i++)printf "%d ",491+i;print ""}' > "$req"
   "$out/locality" < "$req" > "$result"
   test "$(wc -l < "$result")" = "$((n+4))"
   if test "$arm" = 0; then tail -n "$n" "$result" > "$out/sweep/reference"; else
    tail -n "$n" "$result" > "$out/sweep/candidate"
    cmp "$out/sweep/reference" "$out/sweep/candidate"
   fi
   # Emit actual algorithm counters with named request/result identities.
   metrics=$(head -n 4 "$result" | tr '\n' '\t')
   printf '%s\t%s\t%s\t%s%s\t%s\tANALYTICALLY_ESTIMATED\tNOT_RUN\n' "$n" "$depth" "$arm" "$metrics" \
    "$(sha256sum "$req" | cut -d' ' -f1)" "$(sha256sum "$result" | cut -d' ' -f1)" >> "$out/sweep.tsv"
  done
 done
done
printf 'L491 sweep: 80 executed requests; all same-shape/depth arm states identical; logical counts only PASS\n'
