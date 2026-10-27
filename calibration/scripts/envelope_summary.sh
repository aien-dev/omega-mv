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

# Summarise turing-coder envelope CSVs (DIR/seed-<S>.<candidate>.csv) per candidate and coder:
# the largest |overhead - 448| in bits over every file and every crumb, and the worst margin
#   |overhead - 448| - (64 + 1.0e-3 x N)      (<= 0 means inside the envelope; CODER_SPEC section 9).
# overhead = 8 x coded_bytes - ideal_ub / 1e6. No Python: POSIX sh + awk.
set -eu
dir="$1"
echo "candidate coder units max_abs_dev_bits worst_margin_bits violations"
for f in "$dir"/seed-*.*.csv; do
    c="${f##*/seed-}"; c="${c#*.}"; c="${c%.csv}"
    awk -F, -v c="$c" 'NR > 1 { print c, $1, $4, $5, $6, $7 }' "$f"
done | awk '
function upd(k, n, ub, b,   o, d, m) {
    o = 8 * b - ub / 1e6; d = o - 448; if (d < 0) d = -d; m = d - (64 + 1.0e-3 * n)
    if (!(k in U) || d > D[k]) D[k] = d
    if (!(k in U) || m > M[k]) M[k] = m
    U[k]++; if (m > 0) V[k]++
}
{ upd($1 " range", $3, $4, $5); upd($1 " rans", $3, $4, $6) }
END { for (k in U) printf "%s %d %.3f %.3f %d\n", k, U[k], D[k], M[k], V[k] + 0 }' | sort
