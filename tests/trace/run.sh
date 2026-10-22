#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} != --under-quiet ]]; then exec quiet-build "$0" --under-quiet; fi
cd "$(dirname "$0")/../.."
export OMEGA_TRACE_BUILD_DIR=${OMEGA_TRACE_BUILD_DIR:-build/L489}
make OUT_DIR="$OMEGA_TRACE_BUILD_DIR" searchtrace
chmod 755 "$OMEGA_TRACE_BUILD_DIR/searchtrace/searchtrace"
# Existing recorder fixture is frozen in evidence; never overwrite it.
"$OMEGA_TRACE_BUILD_DIR/searchtrace/searchtrace" verify evidence/L489/synthetic-searchtrace.txt tests/trace/synthetic-taskset.txt
"$OMEGA_TRACE_BUILD_DIR/searchtrace/searchtrace" hookcheck tests/trace/synthetic-taskset.txt
node --test --test-timeout=30000 tests/trace/*.test.mjs
ARGUS_TRACE_REPO=${ARGUS_REPO:-../aienos-argus}
ARGUS_TRACE_PIN=$(head -n 1 argus.lock)
mkdir -p "$OMEGA_TRACE_BUILD_DIR/native-oracle-src"
for source in argus_abi.h argus_event.c sha256.h; do
  git -C "$ARGUS_TRACE_REPO" show "$ARGUS_TRACE_PIN:native/argus/$source" > "$OMEGA_TRACE_BUILD_DIR/native-oracle-src/$source"
done
cc -std=c11 -Wall -Wextra -Werror -O2 -Isrc -I"$OMEGA_TRACE_BUILD_DIR/native-oracle-src" \
  tests/trace/native-oracle.c "$OMEGA_TRACE_BUILD_DIR/native-oracle-src/argus_event.c" \
  src/dual/rx_dual.c src/sha256.c -lm -o "$OMEGA_TRACE_BUILD_DIR/native-oracle"
node tests/trace/native-oracle.mjs
cc -std=c11 -Wall -Wextra -Werror -O2 tests/trace/network-deny.c -o "$OMEGA_TRACE_BUILD_DIR/network-deny"
# Positive control: the kernel policy must deny a deliberate IPv4 socket.
"$OMEGA_TRACE_BUILD_DIR/network-deny" node --input-type=module -e 'import net from "node:net";const s=net.connect({host:"127.0.0.1",port:9});s.on("error",e=>{if(e.code!=="EPERM")process.exit(1);console.log("network-denial positive control EPERM");});'
"$OMEGA_TRACE_BUILD_DIR/network-deny" env OMEGA_TRACE_NETWORK_DENIED=1 node tests/trace/offline.mjs
echo 'OMEGA-TRACE PASS (synthetic host correctness only; hardware and timings NOT_RUN)'
