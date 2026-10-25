#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# ldst_nvdisasm_check.sh -- offline provenance for the E1 row 2 load/store kernels.
# Builds the CPU-only test, dumps every table kernel (src/omega_numeric_ldst_gb10.c: load/store
# kinds, offsets, strides), disassembles each with Omega's own sm121dis and compares the text line by
# line with omega_ldst_listing (what the encoder says each word is). Any difference fails.
# The disassembler is an offline decoder only; nothing it prints is copied into a kernel. No device.
# Run: make test-ldst-nvdisasm   Writes only to a temp dir.
set -u
HERE=$(cd -P "$(dirname "$0")/.." && pwd)
DISASM=${DISASM:-$HERE/build/sm121dis}   # Omega's own sm_121 disassembler (C6, src/omega_sm121_disasm.c)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
make -s -C "$HERE" build/sm121dis >/dev/null || { echo "VERDICT FAIL: build sm121dis"; exit 1; }
make -s -C "$HERE" build/test_omega_ldst_gb10_cpu >/dev/null || { echo "VERDICT FAIL: build"; exit 1; }
"$HERE/build/test_omega_ldst_gb10_cpu" --dump "$TMP" >/dev/null || { echo "VERDICT FAIL: dump"; exit 1; }
echo "disassembler: $("$DISASM" --version)"
rc=0; n=0; bad=0
for bin in "$TMP"/k*.bin; do
    k=$(basename "$bin" .bin)
    "$DISASM" -b SM121 "$bin" > "$TMP/$k.raw" 2> "$TMP/$k.err" || { echo "[FAIL] $k: disassembler error"; cat "$TMP/$k.err"; rc=1; bad=$((bad+1)); continue; }
    sed -n 's|^[[:space:]]*/\*\([0-9a-f]\{4\}\)\*/[[:space:]]*\(.*;\).*$|\1 \2|p' "$TMP/$k.raw" \
        | sed 's/[[:space:]]\{1,\}/ /g; s/ ;$/ ;/' > "$TMP/$k.got"
    n=$((n+1))
    if ! diff -u "$TMP/$k.lst" "$TMP/$k.got" > "$TMP/$k.diff"; then
        echo "[FAIL] $k: disassembler text differs from the encoder's listing"; head -20 "$TMP/$k.diff"; rc=1; bad=$((bad+1))
    fi
done
echo "[$([ $bad = 0 ] && echo PASS || echo FAIL)] ldst: $((n-bad)) of $n kernels decode, every word, to the encoder's text"
[ $n -gt 0 ] || rc=1
[ $rc = 0 ] && echo "VERDICT PASS" || echo "VERDICT FAIL"
exit $rc
