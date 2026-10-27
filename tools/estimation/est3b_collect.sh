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

# EST-3b fresh data collection (protocol v2, docs/estimation/EST23_PROTOCOL_V2.md;
# protocol v1 section 7): a 1 Hz shell reader of /sys/class/thermal.
#   tools/estimation/est3b_collect.sh <fit|heldout|smoke> <seconds> <raw-root>
# Writes <raw-root>/<UTC>-est3b-<role>-silicon/ with machine-state.ndjson
# (same line shape as R15: {"t":<s.ns>,"thermal_mc":"<zone0> <zone1> ..."}),
# an empty machine-state-marks.txt (no trials), machine-state-start.txt and
# SHA256SUMS. No root, no perf, no /dev/null: read errors go to stderr.
set -u
role=${1:?fit, heldout or smoke}
secs=${2:?seconds}
root=${3:?raw root}
d=$root/$(date -u +%Y%m%dT%H%M%SZ)-est3b-$role-silicon
mkdir -p "$d"
date -u +%FT%TZ > "$d/machine-state-start.txt"
end=$(( $(date +%s) + secs ))
while [ "$(date +%s)" -lt "$end" ]; do
    t=$(date +%s.%N)
    th=$(cat /sys/class/thermal/thermal_zone*/temp | tr '\n' ' ')
    printf '{"t":%s,"thermal_mc":"%s"}\n' "$t" "$th" >> "$d/machine-state.ndjson"
    sleep 1
done
: > "$d/machine-state-marks.txt"
(cd "$d" && sha256sum machine-state.ndjson machine-state-marks.txt machine-state-start.txt > SHA256SUMS)
echo "$d"
