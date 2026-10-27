#!/usr/bin/env bash
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

# tools/exp445/check_weights.sh <config> <run dir> -- the operator's weight and server check, recorded with
# the run (omega#445, docs/one_layer/EXP445_PROTOCOL.md section 9). Writes
#   <run dir>/weights.check   sha256sum -c of the config's frozen hash list (PARAMS config.<X>.weights),
#                             run in PARAMS models_root
#   <run dir>/server.json     the server's identity: GET <config base_url>/models
# Starts and stops nothing. Exit 0 only when every listed file is OK and the server lists the config's
# frozen model id; run.sh refuses to generate and score.sh refuses the run otherwise. EXP445_PARAMS
# names another PARAMS file (tests only).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
die() { echo "check_weights.sh: $*" >&2; exit 2; }
PARAMS="${EXP445_PARAMS:-$HERE/PARAMS}"
[ -f "$PARAMS" ] || die "no PARAMS file $PARAMS"
kv() { awk -F= -v k="$1" '$1 == k { print substr($0, length(k) + 2); exit }' "$PARAMS"; }
[ $# -eq 2 ] || die "usage: check_weights.sh <config> <run dir>"
c="$1" o="$2"
list="$(kv "config.$c.weights")" root="$(kv models_root)" base="$(kv "config.$c.base_url")" model="$(kv "config.$c.model")"
[ -n "$list" ] && [ -n "$root" ] && [ -n "$base" ] && [ -n "$model" ] || die "config $c (weights, models_root, base_url, model) is not named in $PARAMS"
case "$list" in /*) ;; *) list="$ROOT/$list" ;; esac
[ -s "$list" ] || die "no hash list $list"
. "$HERE/reader.sh"
mkdir -p "$o"
(cd "$root" && sha256sum -c "$list") > "$o/weights.check" 2>&1 || true
curl -sS -m 30 "$base/models" > "$o/server.json" || true
if weights_ok "$o" "$list" "$model"; then
  echo "check_weights.sh: config $c: $(wc -l < "$o/weights.check") files OK in $root; $base/models lists $model"
else
  grep -v ': OK$' "$o/weights.check" | head -5 >&2 || true
  die "config $c: weights or server check FAILED (see $o/weights.check, $o/server.json)"
fi
