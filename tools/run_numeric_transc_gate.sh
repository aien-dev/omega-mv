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

# run_numeric_transc_gate.sh -- E1 row 10 chip gate: GB10 realizations of the
# frozen FP32 transcendental sequences (src/omega_numeric_transc.c), every
# 2^32 input of each op compared bit for bit with the CPU tier.
#
# Usage: tools/run_numeric_transc_gate.sh [OP ...]   (default: EXP2 LOG2 SIGMOID TANH SIN COS ERF GELU RSQRT)
# Exit: 0 PASS; 1 anything else (FAIL, NOT_RUN, refused). Refusals print
# "REFUSED: why" and "VERDICT NOT_RUN". Last stdout line: "VERDICT <word>".
#
# Thin wrapper: refusals, host tier (host build, host run and the offline disassembler
# provenance check), quiet flag, GPU lock, chip build, never-killed run and the
# content-addressed receipt <evidence-dir>/<sha256>.json plus blobs/<sha256>.log live
# in tools/chip_run.sh, configured by tools/manifests/numeric_transc.chiprun. This
# wrapper keeps the op-name check and the default ops. Env: PHYSICS_DIR (default
# ~/workspace/hive-worktrees/physics-gate14-e95e3ed, the module's own default differs)
# and TRANSC_GB10_EVIDENCE_DIR (read by the manifest).
#
# Run it detached so a closed terminal cannot cut the chip run:
#   setsid nohup tools/run_numeric_transc_gate.sh > /tmp/transc-gate.out 2>&1 < /dev/null &
# Shell + coreutils + git + jq + gcc. No Python.
set -u
HERE=$(cd -P "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PHYSICS=${PHYSICS_DIR:-$HOME/workspace/hive-worktrees/physics-gate14-e95e3ed}
OPS=("$@"); [ ${#OPS[@]} -gt 0 ] || OPS=(EXP2 LOG2 SIGMOID TANH SIN COS ERF GELU RSQRT)
refuse() { echo "REFUSED: $*"; echo "VERDICT NOT_RUN"; exit 1; }
for op in "${OPS[@]}"; do case "$op" in EXP2|LOG2|SIGMOID|TANH|SIN|COS|ERF|GELU|RSQRT) ;; *) refuse "unknown op $op" ;; esac; done
exec bash "$HERE/tools/chip_run.sh" "$HERE/tools/manifests/numeric_transc.chiprun" --physics-dir "$PHYSICS" -- --chip "${OPS[@]}"
