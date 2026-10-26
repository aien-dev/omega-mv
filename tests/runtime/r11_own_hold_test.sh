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

# R11 living run inside its own quiet hold (tests/runtime/rx_r11_aien.c
# quiet_flag_is_mine). With a fake HOME whose ~/workspace/.spark-quiet carries
# hold=q1-abc: no QUIETLOCK_HOLD, a prefix and an extension of the id must all
# refuse the living run because of that flag (someone else's measurement);
# QUIETLOCK_HOLD=q1-abc must not be refused because of the flag. The binary also
# skips the living run on a 1-minute load above 2 (other_load), which is the
# normal state of the shared Spark while the judge runs gates in parallel, so a
# refusal for load is not the hold logic under test and is not counted here.
# usage: r11_own_hold_test.sh <rx_r11_aien_test>
B=${1:?rx_r11_aien_test binary}; H=$(mktemp -d); fail=0
trap 'rm -rf "$H"' EXIT
mkdir -p "$H/workspace"
echo "other quietlock X start=2026-10-05T00:00:00Z expected_end=2099-01-01T00:00:00Z pid=1 hold=q1-abc" > "$H/workspace/.spark-quiet"
for hold in "" q1-ab q1-abcd q1-abc; do
    out=$(HOME=$H QUIETLOCK_HOLD=$hold "$B" 2>&1)
    if printf '%s\n' "$out" | grep -Fq "an R15 timed measurement holds ~/workspace/.spark-quiet"; then got=refused; else got=ran; fi
    if [ "$hold" = q1-abc ]; then want=ran; else want=refused; fi
    printf '%s\n' "$out" | grep -Eq '^checks [0-9]+ failures 0$' || { got="$got+failures"; }
    if [ "$got" = "$want" ]; then echo "[+] hold='$hold': $got"; else echo "[-] hold='$hold': $got, want $want"; fail=$((fail + 1)); fi
done
echo "failures $fail"; [ "$fail" = 0 ]
