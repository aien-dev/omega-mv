#!/bin/bash
# L312 chip piston on the GB10: power engine alone (pre: all fuel in place) and both engines (live: a host thread copies each
# beat's image into a slot while the chip fires), slot sizes of one MoE layer's 8 routed experts: iters 24 = 37.7 MB (30B,
# 8 x 4.72 MB) and iters 17 = 26.7 MB (35B, 8 x 3.22 MB = 25.8 MB). Runs inside a spark-window (chip lock held by the window):
#   spark-window open l312-piston --ledger L312 --lease 30 --wait; spark-window run bash run-chip-piston.sh; spark-window close
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$HERE/../../build/l312/gpu_engine_piston_test}
OUT=${OUT:-$HERE/chip-piston-$(date -u +%Y%m%dT%H%MZ)}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
log "bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) window ${SPARK_WINDOW:-none} load $(cut -d' ' -f1-3 /proc/loadavg) memavail_kb $(grep MemAvailable /proc/meminfo | awk '{print $2}')"
$BIN --host-only > "$OUT/host-only.log" 2>&1; log "host-only rc $?: $(tail -1 $OUT/host-only.log)"
cell() { local name=$1; shift; $BIN "$@" --out "$OUT/$name" > "$OUT/$name.log" 2>&1; local rc=$?
  log "$name rc $rc: $(grep -E '^PISTON (mode|chip_fire_us|chip_idle|chip_beat|host_fill|host_slot|fire_gbps)' $OUT/$name.log | sed -E 's/PISTON //; s/ (n|min|p10|p90|max)=[^ ]*//g' | tr '\n' ' ' | cut -c1-600) $(tail -1 $OUT/$name.log)"; }
cell pre-35b  --mode pre  --iters 17 --beats 2000
cell pre-30b  --mode pre  --iters 24 --beats 2000
cell live-35b-s2 --mode live --slots 2 --iters 17 --beats 4000
cell live-35b-s3 --mode live --slots 3 --iters 17 --beats 4000
cell live-35b-s4 --mode live --slots 4 --iters 17 --beats 4000
cell live-30b-s2 --mode live --slots 2 --iters 24 --beats 4000
log "done"
