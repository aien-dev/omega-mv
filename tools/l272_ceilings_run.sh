#!/bin/sh
# l272_ceilings_run.sh OUT_DIR EVIDENCE_DIR: the GB10 ceiling measurements of docs/speed/GB10_CEILINGS.md, one at a time,
# each under quiet-measure holding /tmp/aien-gb10.lock only for that run. Writes <name>.log and <name>.rc per run plus
# box.txt (load, memory, lock holders at start). Build first (quiet-build make OUT_DIR=... gpu_ceilings_test
# gpu_tc_peak_test gpu_qmma_probe_test).
set -u
B=${1:?build dir}; E=${2:?evidence dir}
mkdir -p "$E"
{ date -u +%FT%TZ; uptime; grep -E 'MemTotal|MemAvailable' /proc/meminfo; git rev-parse HEAD; spark-window status 2>&1 | head -5; } > "$E/box.txt" 2>&1
run() {
  name=$1; shift
  echo "$(date -u +%FT%TZ) start $name: $*" >> "$E/runs.txt"
  quiet-measure flock /tmp/aien-gb10.lock "$@" > "$E/$name.log" 2>&1
  echo $? > "$E/$name.rc"
  echo "$(date -u +%FT%TZ) end $name rc=$(cat "$E/$name.rc")" >> "$E/runs.txt"
}
run ceilings "$B/gpu_ceilings_test"
run tc_peak_bf16 "$B/gpu_tc_peak_test"
run qmma_fp8 "$B/gpu_qmma_probe_test"
