#!/bin/bash
# tools/symbolic_measure.sh KIND : one OMEGA_SYMBOLIC_REWRITE measurement into evidence/symbolic-rewrite/KIND-<ts>.
# KIND: action-graphs | q38-chip | cascade. Timing under ~/.local/bin/quiet-measure (GPU lock inside for q38-chip),
# receipt by spark-receipt, loadavg at start and end.
set -u
KIND="$1"; cd "$(dirname "$0")/.." || exit 2
TS=$(date -u +%Y%m%dT%H%MZ); EV=evidence/symbolic-rewrite/$KIND-$TS; mkdir -p "$EV"
F7=$HOME/workspace/sc-f7/evidence/qwen38-spec-20261019T0323Z
case "$KIND" in
  action-graphs) BIN=build/rx_symbolic_test; ARGS="--runs 400 --out $EV"; GPU="";;
  q38-chip) BIN=build/gpu_q38_symbolic_test; ARGS="--layers 4 --mask 0x8 --reps 21 --model $HOME/models/qwen3.8-27b-fp8-017b9c7a --out $EV"; GPU="nice -n 10 flock /tmp/aien-gb10.lock";;
  cascade) BIN=build/rx_cascade_test; ARGS="--f7 $F7 --out $EV"; GPU="";
           (cd "$F7" && sha256sum gen-codelong.json gen-prose.json gen-code-golden.json) > "$EV/inputs.sha256"
           echo "inputs from aien-sovereign-core-mv qwen38/mtp-spec-decode $(git -C ~/workspace/sc-f7 rev-parse HEAD): $F7" >> "$EV/inputs.sha256";;
  *) echo "unknown kind"; exit 2;;
esac
cp "$BIN" "$EV/"; B="$EV/$(basename "$BIN")"
sha256sum "$B" > "$EV/binaries.sha256"; [ "$KIND" = q38-chip ] && sha256sum build/libomega_gpu.a >> "$EV/binaries.sha256"
{ date -u +%FT%TZ; echo "omega $(git rev-parse HEAD) dirty $(git status --porcelain -- src tests Makefile | wc -l)"; echo "loadavg_queued $(cat /proc/loadavg)"; grep MemAvailable /proc/meminfo; } > "$EV/ground.txt"
echo "~/.local/bin/spark-receipt symbolic-$KIND-$TS -- ~/.local/bin/quiet-measure $GPU sh -c 'echo loadavg_start \$(cat /proc/loadavg) >> $EV/ground.txt; $B $ARGS; rc=\$?; echo loadavg_end \$(cat /proc/loadavg) >> $EV/ground.txt; exit \$rc'" > "$EV/command.txt"
~/.local/bin/spark-receipt "symbolic-$KIND-$TS" -- ~/.local/bin/quiet-measure $GPU sh -c "echo loadavg_start \$(cat /proc/loadavg) >> $EV/ground.txt; $B $ARGS; rc=\$?; echo loadavg_end \$(cat /proc/loadavg) >> $EV/ground.txt; exit \$rc" > "$EV/run.log" 2>&1
echo "rc=$?" >> "$EV/run.log"
cp ~/yardsticks/receipts/*"symbolic-$KIND-$TS"*.json "$EV/" 2>/dev/null
touch "$EV/DONE"
