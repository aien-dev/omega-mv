#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
mkdir -p "$out/583"
pin=3bd56731c5517e2a9c8f195955c71e73dc6f9269
git archive "$pin" tools/dagsim src/runtime/rx_costmodel.c src/runtime/rx_costmodel.h src/sha256.c src/sha256.h | tar -x -C "$out/583"
cc -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -I"$out/583/src" -I"$out/583/tools/dagsim" \
 "$out/583/tools/dagsim/dagsim.c" "$out/583/tools/dagsim/dagsim_cli.c" "$out/583/src/runtime/rx_costmodel.c" "$out/583/src/sha256.c" -lm -o "$out/dagsim"
set +e
"$out/dagsim" eval "$out/baseline/decode.oev" --asof 2026-10-22 > "$out/baseline/dagsim.json"
rc=$?
set -e
test "$rc" = 1
jq -e '.status == "REFUSED_UNKNOWN"' "$out/baseline/dagsim.json" > /dev/null
# A second optimizer or made-up calibration would defeat this gate.
! rg -q 'makespan_ns' "$out/baseline/dagsim.json"
printf 'OEV1: actual #583 parser/evaluator refuses unknown decode costs; no ranking PASS\n'
