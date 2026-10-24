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

# AT-1 Agent 5: oracle command for the evaluator's run.sh template '<oracle> {case} {out}'.
#   sh oracle_shim.sh <case-file> <out-file>   ->   at1-oracle emit <case-file> -o <out-file>
# Exit status and standard error are the oracle's own (2 and one `AT1_CASE_REFUSED <code>` line on a
# refusal); a usage or setup error here exits 1. Environment: AT1_ORACLE_BIN (set by run.sh).
set -u
if [ $# -ne 2 ]; then echo "usage: oracle_shim.sh <case-file> <out-file>" >&2; exit 1; fi
if [ -z "${AT1_ORACLE_BIN:-}" ]; then echo "oracle_shim.sh: AT1_ORACLE_BIN is not set" >&2; exit 1; fi
exec "$AT1_ORACLE_BIN" emit "$1" -o "$2"
