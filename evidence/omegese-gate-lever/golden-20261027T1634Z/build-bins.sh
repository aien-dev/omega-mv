#!/bin/bash
# C1Lang: two aien-serve-http builds of sc ship/motor-l387 f0230a59 (the live 18091 source, no aien-test-switches),
# linked with libomega_gpu.a from omega 2f6561d4b (= live omega 6de999da + the L386 gate lever, 3 commits):
#   green: the embedded gate is the checked Omegese gate (code sha256 fabdfc55)
#   red:   the same lib with the gate built from a mutated gate.omegese (exp neg g -> exp g; code sha256 09522207)
set -u
C=/home/drakestapleton/work/C1Lang; SC=$C/sc-live
log() { echo "$(date -u +%FT%TZ) $*" | tee -a $C/build-bins.log; }
cd $SC
for v in green red; do
  lib=$C/lib-$v/libomega_gpu.a; sha=$(sha256sum $lib | cut -d' ' -f1)
  export AIEN_OMEGA_GPU_LIB=$lib AIEN_OMEGA_GPU_LIB_SHA256=$sha CARGO_TARGET_DIR=$C/sc-target-$v
  log "$v: cargo build start (lib $sha)"
  taskset -c 10-14 nice -n 10 cargo build --release -p aien-serve-http > $C/cargo-$v.log 2>&1 || { log "$v: cargo build FAIL"; exit 1; }
  B=$C/bin-$v; mkdir -p $B; cp $CARGO_TARGET_DIR/release/aien-serve-http $lib $B/
  ( cd $B && sha256sum aien-serve-http libomega_gpu.a > SHA256SUMS )
  { echo "aien-serve-http C1Lang $v build for golden.sh (NOT live, NO aien-test-switches)"
    echo "sc f0230a59 (ship/motor-l387, the live bin-19173f9e source), cargo build --release -p aien-serve-http, AIEN_OMEGA_GPU_LIB prebuilt"
    echo "omega 2f6561d4b = 6de999da (live lib, rebuilt here to the same sha256 110295ef) + L386 gate lever da99bf8bc edd47302f 2f6561d4b"
    [ $v = red ] && echo "RED CONTROL: src/omega_q36_om_gate.h rebuilt from gate.omegese with 'exp neg get g i' -> 'exp get g i' (IR ff40bf55, code 09522207)"
    cat $B/SHA256SUMS; } > $B/BUILD.txt
  log "$v: done $(head -1 $B/SHA256SUMS | cut -c1-16)"
done
log "BUILD DONE"
