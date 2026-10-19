#!/usr/bin/env bash
# run_arm.sh: one arm of the #249 run of record (ledger L059), REPS repetitions of
# search -> independent check (warm contract cache) -> native execution -> accounting.
# Usage: run_arm.sh ARM REPS SET BLOB OUTDIR CACHE BINDIR
# Writes OUTDIR/runs.csv with load and MemAvailable at start and end in its header lines.
set -u
ARM=$1 REPS=$2 SET=$3 BLOB=$4 OUT=$5 CACHE=$6 BIN=$7
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CON=$ROOT/tools/rw249/contracts
HEAD=$(git -C "$ROOT" rev-parse HEAD)
mem() { awk '/MemAvailable/{print $2 * 1024}' /proc/meminfo; }
mkdir -p "$OUT"
CSV=$OUT/runs.csv
echo "# arm=$ARM reps=$REPS set=$SET blob=$BLOB head=$HEAD start=$(date -u +%FT%TZ) load_start=$(cut -d' ' -f1-3 /proc/loadavg | tr ' ' '/') memavail_bytes_start=$(mem)" > "$CSV"
echo "rep,arm,search_cpu_ns,search_wall_ns,search_peak_rss_bytes,check_cpu_ns,check_peak_rss_bytes,accepted,failed_candidates,unchanged,removed_ops,best_found_under_budget,unranked,exec_timed,sum_saving_ps_per_call,sum_null_ps,exec_cpu_ns,exec_peak_rss_bytes,outputs_sha256" >> "$CSV"
kv() { grep -o "$1=[^ ]*" "$2" | head -1 | cut -d= -f2; }
for r in $(seq 1 "$REPS"); do
  d=$OUT/rep$r
  "$BIN/rwsearch" run --arm "$ARM" --set "$SET" --out "$d" --model "$BLOB" > "$OUT/rep$r.search.log" || exit 1
  "$BIN/rwcheck" derivations --axioms "$CON/axioms.rwa" --contracts "$CON" --cache "$CACHE" "$d/derivs.rwd" > "$d/verdicts.txt" || exit 1
  "$BIN/rwsearch" exec --dir "$d" --verdicts "$d/verdicts.txt" > /dev/null || exit 1
  "$BIN/rwsearch" account --dir "$d" --verdicts "$d/verdicts.txt" --oev "$d/oev" --build "$HEAD" --model evidence/REWRITE-249/calibration/model.blob --load1 "${CAL_LOAD1:-unknown}" --proof "evidence/REWRITE-249/runs/$ARM/rep$r/derivs.rwd" > "$d/account.txt" || exit 1
  s=$d/summary.txt; c=$d/verdicts.txt; e=$d/exec_summary.txt; a=$d/account.txt
  sha=$( (cut -f1-11 "$d/results.tsv"; cat "$d/derivs.rwd"; grep '^RWDERIV ' "$c") | sha256sum | cut -c1-16)
  echo "$r,$ARM,$(kv search_cpu_ns $s),$(kv search_wall_ns $s),$(kv peak_rss_bytes $s),$(kv cpu_ns $c),$(kv peak_rss_bytes $c),$(kv accepted $a),$(kv failed_candidates $a),$(kv unchanged $a),$(kv removed_ops $a),$(kv best_found_under_budget $s),$(kv unranked $s),$(kv timed $e),$(kv sum_saving_ps_per_call $e),$(kv sum_null_ps $e),$(kv exec_cpu_ns $e),$(kv peak_rss_bytes $e),$sha" >> "$CSV"
done
echo "# end=$(date -u +%FT%TZ) load_end=$(cut -d' ' -f1-3 /proc/loadavg | tr ' ' '/') memavail_bytes_end=$(mem)" >> "$CSV"
