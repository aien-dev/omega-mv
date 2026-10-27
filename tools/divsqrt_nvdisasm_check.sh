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

# divsqrt_nvdisasm_check.sh -- offline provenance for the E1 row 7 kernels.
# Builds the CPU-only test, dumps every whole-program kernel (DIV, SQRT and the transcendentals) Omega's own encoder
# emits, disassembles every word with Omega's own sm121dis (C6; it agrees with nvdisasm on every library
# kernel) and compares the text line by line with omega_ds_listing (what the encoder says each word is).
# Any difference fails. The disassembler is an offline decoder only; nothing it prints
# is copied into the kernel. No device is opened.
# Run: make test-divsqrt-nvdisasm   (or run this file). Writes only to a temp dir.
set -u
HERE=$(cd -P "$(dirname "$0")/.." && pwd)
DISASM=${DISASM:-$HERE/build/sm121dis}   # Omega's own sm_121 disassembler (C6, src/omega_sm121_disasm.c)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
make -s -C "$HERE" build/sm121dis >/dev/null || { echo "VERDICT FAIL: build sm121dis"; exit 1; }
make -s -C "$HERE" build/test_omega_divsqrt_gb10_cpu >/dev/null || { echo "VERDICT FAIL: build"; exit 1; }
"$HERE/build/test_omega_divsqrt_gb10_cpu" --dump "$TMP" || { echo "VERDICT FAIL: dump"; exit 1; }
echo "disassembler: $("$DISASM" --version)"
rc=0
for op in div sqrt exp2 log2 sigmoid tanh sin cos erf gelu rsqrt; do
    "$DISASM" -b SM121 "$TMP/$op.bin" > "$TMP/$op.raw" 2> "$TMP/$op.err" || { echo "[FAIL] $op: disassembler error"; cat "$TMP/$op.err"; rc=1; continue; }
    # keep "/*addr*/ text ;" lines, normalise spacing, drop the hex comments
    sed -n 's|^[[:space:]]*/\*\([0-9a-f]\{4\}\)\*/[[:space:]]*\(.*;\).*$|\1 \2|p' "$TMP/$op.raw" \
        | sed 's/[[:space:]]\{1,\}/ /g; s/ ;$/ ;/' > "$TMP/$op.got"
    words=$(wc -l < "$TMP/$op.lst")
    if diff -u "$TMP/$op.lst" "$TMP/$op.got" > "$TMP/$op.diff"; then
        echo "[PASS] $op: all $words words decode to the encoder's text"
    else
        echo "[FAIL] $op: disassembler text differs from the encoder's listing"; head -40 "$TMP/$op.diff"; rc=1
    fi
done
[ $rc = 0 ] && echo "VERDICT PASS" || echo "VERDICT FAIL"
exit $rc
