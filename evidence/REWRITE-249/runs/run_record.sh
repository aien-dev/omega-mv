#!/usr/bin/env bash
# #249 run of record driver (ledger L059). Run from the worktree root. Copied into evidence afterwards.
set -u
B=$HOME/workspace/targets/omega-rw-249
R=$B/record
SET=evidence/REWRITE-249/sets/heldout-seed249.set
BLOB=evidence/REWRITE-249/calibration/model.blob
export CAL_LOAD1=10.52
mkdir -p "$R"
stamp() { echo "# $1=$(date -u +%FT%TZ) load=$(cut -d' ' -f1-3 /proc/loadavg | tr ' ' '/') memavail_kb=$(awk '/MemAvailable/{print $2}' /proc/meminfo)"; }
cold() {
  stamp start
  "$B/rw249/rwcheck" contracts --axioms tools/rw249/contracts/axioms.rwa --cache "$R/contracts.cache" tools/rw249/contracts/R*.rw | tail -1
  stamp end
}
if [ "${1:-}" = cold ]; then cold; exit; fi
"$HOME/.local/bin/spark-receipt" rw-249-coldcheck -- "$HOME/.local/bin/quiet-measure" flock /tmp/aien-gb10.lock "$0" cold > "$R/coldcheck.txt" 2>&1
for a in M G Bi B; do
  "$HOME/.local/bin/spark-receipt" rw-249-$a -- "$HOME/.local/bin/quiet-measure" flock /tmp/aien-gb10.lock \
    tools/rw249/run_arm.sh $a 5 "$SET" "$BLOB" "$R/$a" "$R/contracts.cache" "$B/rw249" > "$R/$a.receipt.log" 2>&1 || echo "ARM $a FAILED"
done
"$HOME/.local/bin/spark-receipt" rw-249-secondary -- "$HOME/.local/bin/quiet-measure" flock /tmp/aien-gb10.lock \
  tools/rw249/run_secondary.sh "$SET" "$BLOB" "$R/secondary" "$B/rw249" > "$R/secondary.receipt.log" 2>&1 || echo "SECONDARY FAILED"
echo RECORD-DONE
