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

# Synthetic input checks. These fixtures cannot qualify hardware measurement.
set -eu
RED=$1
OUT=$2
GEN=$3
mkdir -p "$OUT"
case_run() {
    name=$1; fault=$2; expected=$3
    dir=$OUT/$name
    mkdir -p "$dir"
    "$GEN" IDLE 0 none > "$dir/00-idle.jsonl"
    "$GEN" A 2000000000 none > "$dir/01-a.jsonl"
    "$GEN" B 4000000000 none > "$dir/02-b.jsonl"
    "$GEN" AB 6000000000 "$fault" > "$dir/03-ab.jsonl"
    "$RED" --check "$dir" > "$dir/result.txt"
    grep -q "$expected" "$dir/result.txt"
}
case_run valid none 'ROUND S1 r1 OK'
case_run sensor sensor SENSOR_UNAVAILABLE
case_run wrap wrap COUNTER_WRAP
case_run overflow overflow COUNTER_WRAP
case_run oracle oracle PARTIAL_RUN
case_run multiplexed multiplexed PMU_MULTIPLEXED
mkdir -p "$OUT/missing-edge"
printf '%s\n' '{"run":"bad","config":"S1","level":"AB","tool_cpu":0}' > "$OUT/missing-edge/bad.jsonl"
"$RED" --check "$OUT/missing-edge" > "$OUT/missing-edge/result.txt"
grep -q BAD_ARGUMENT "$OUT/missing-edge/result.txt"
mkdir -p "$OUT/empty" "$OUT/malformed"
printf '%s\n' '[]' > "$OUT/malformed/bad.jsonl"
if "$RED" --check "$OUT/empty" >/dev/null 2>&1; then exit 1; fi
if "$RED" --check "$OUT/malformed" >/dev/null 2>&1; then exit 1; fi
printf '%s\n' 'ty_energy fixtures: nine valid/refusal cases passed'
