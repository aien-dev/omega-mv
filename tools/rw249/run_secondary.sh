#!/usr/bin/env bash
# run_secondary.sh: secondary measurements of #249 (ledger L059), not of record:
# (1) arm B at node budgets 16 64 256 512 2048, one repetition each, held-out set;
# (2) matching profile: arm B plain versus every match done twice, 5 pairs, held-out set.
# Usage: run_secondary.sh SET BLOB OUTDIR BINDIR
set -u
SET=$1 BLOB=$2 OUT=$3 BIN=$4
mem() { awk '/MemAvailable/{print $2 * 1024}' /proc/meminfo; }
mkdir -p "$OUT"
CSV=$OUT/secondary.csv
echo "# start=$(date -u +%FT%TZ) load_start=$(cut -d' ' -f1-3 /proc/loadavg | tr ' ' '/') memavail_bytes_start=$(mem)" > "$CSV"
echo "kind,param,search_cpu_ns,changed,removed_ops,best_found_under_budget,peak_rss_bytes,match_attempts" >> "$CSV"
kv() { grep -o "$1=[^ ]*" "$2" | head -1 | cut -d= -f2; }
for nodes in 16 64 256 512 2048; do
  d=$OUT/sweep$nodes
  "$BIN/rwsearch" run --arm B --set "$SET" --out "$d" --model "$BLOB" --nodes "$nodes" > /dev/null || exit 1
  s=$d/summary.txt
  echo "sweep,$nodes,$(kv search_cpu_ns $s),$(kv changed $s),$(kv removed_ops $s),$(kv best_found_under_budget $s),$(kv peak_rss_bytes $s),$(kv match_attempts $s)" >> "$CSV"
done
for i in 1 2 3 4 5; do
  for p in plain doubled; do
    d=$OUT/prof-$p-$i
    if [ $p = doubled ]; then "$BIN/rwsearch" run --arm B --set "$SET" --out "$d" --model "$BLOB" --profile > /dev/null || exit 1
    else "$BIN/rwsearch" run --arm B --set "$SET" --out "$d" --model "$BLOB" > /dev/null || exit 1; fi
    s=$d/summary.txt
    echo "profile,$p,$(kv search_cpu_ns $s),$(kv changed $s),$(kv removed_ops $s),$(kv best_found_under_budget $s),$(kv peak_rss_bytes $s),$(kv match_attempts $s)" >> "$CSV"
  done
done
echo "# end=$(date -u +%FT%TZ) load_end=$(cut -d' ' -f1-3 /proc/loadavg | tr ' ' '/') memavail_bytes_end=$(mem)" >> "$CSV"
