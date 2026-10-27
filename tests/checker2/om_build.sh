#!/bin/sh
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

# Builds the seeded-Omega port of checker2 (`checker2 check <unit.ir>`, seed/oscgpu/c2*.om) with the
# pinned compiler seed/out/omc. The units are one program: they are concatenated in the order below
# (c2glob first for the constants and globals, c2sha second for its layout) and compiled as one file.
# With --mutant phase-apart the build drops the GUARD:phase-apart test of kernel_races (c2_rpq: two
# shared accesses in disjoint sync phases cannot race), a negative control for om_diff.sh.
# Usage: sh tests/checker2/om_build.sh <out-binary> [--mutant phase-apart]
set -eu
OUT=$1
MUT=""
if [ "${2:-}" = --mutant ]; then MUT=${3:-}; fi
OMC=seed/out/omc
PIN=2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f
UNITS="c2glob c2sha c2out c2dec c2chk c2ins c2cfg c2gpu c2race c2own c2main"
GUARD='if eq and get cxm p get cxm q ulong 0'

got=$(sha256sum "$OMC" | cut -c1-64)
if [ "$got" != "$PIN" ]; then
    echo "om_build: $OMC sha256 $got, pinned $PIN" >&2
    exit 2
fi
mkdir -p "$(dirname "$OUT")"
src=$OUT.om
: >"$src"
for u in $UNITS; do cat "seed/oscgpu/$u.om" >>"$src"; done
case $MUT in
'') ;;
phase-apart)
    # the guard is one `if` line, its `ret 0` and its `end`; exactly one copy must be removed
    n=$(grep -c -x "$GUARD" "$src" || true)
    if [ "$n" != 1 ]; then
        echo "om_build: phase-apart guard found $n times, expected 1" >&2
        exit 2
    fi
    sed "/^$GUARD\$/,+2d" "$src" >"$src.m" && mv "$src.m" "$src"
    if grep -q -x "$GUARD" "$src"; then
        echo "om_build: phase-apart guard still present" >&2
        exit 2
    fi
    ;;
*)
    echo "om_build: unknown mutant '$MUT'" >&2
    exit 2
    ;;
esac
run=""
if command -v taskset >/dev/null 2>&1 && taskset -c "${OM_CPUS:-10-14}" true 2>/dev/null; then
    run="taskset -c ${OM_CPUS:-10-14}"
fi
# shellcheck disable=SC2086
if ! nice -n 19 $run "$OMC" <"$src" >"$OUT.tmp" 2>"$OUT.err"; then
    echo "om_build: omc failed:" >&2
    cat "$OUT.err" >&2
    rm -f "$OUT.tmp"
    exit 1
fi
chmod +x "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
rm -f "$OUT.err"
echo "om_build: $OUT sha256 $(sha256sum "$OUT" | cut -c1-64)${MUT:+ (mutant $MUT)}"
