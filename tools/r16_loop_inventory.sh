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

# R16-G2 code-search gate, the entry point named in spec/r16-orchestrator-retirement.md.
# Builds the C tool (tools/r16_loop_inventory.c) if needed and runs it from the omega
# root. Arguments pass through (--map, --json, --skeleton, --patterns). Exit status is
# the tool's: 0 PASS, 1 FAIL, 2 usage/I-O error.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=${OUT_DIR:-build}
make -s "$OUT/r16_loop_inventory" >/dev/null
exec "$OUT/r16_loop_inventory" "$@"
