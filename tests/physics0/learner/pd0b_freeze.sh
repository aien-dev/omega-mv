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

# Print the PD0B_FREEZE facts in a stable text format (print only; nothing is written).
# usage: pd0b_freeze.sh <harness-binary>
# Output: git tree hash of each frozen directory, sha256 of every tracked file under them
# (sorted by path, C locale), a dirty-tree flag, and the sha256 of the harness binary
# (which embeds the learner, the ladder checker, the scorer and the controls).
set -eu
H=${1:?usage: pd0b_freeze.sh <harness-binary>}
[ -f "$H" ] || { echo "pd0b_freeze: harness binary not found: $H" >&2; exit 2; }
export LC_ALL=C
DIRS="src/physics0/learner src/physics0/ladder src/physics0/score src/physics0/controls"
echo "freeze_head: $(git rev-parse HEAD)"
for d in $DIRS; do echo "tree $d $(git rev-parse HEAD:$d)"; done
dirty=$(git status --porcelain -- $DIRS tests/physics0/verify | wc -l | tr -d ' ')
echo "frozen_dirs_modified_in_worktree: $dirty"
for d in $DIRS; do git ls-files "$d"; done | sort | while read -r f; do echo "file $(sha256sum "$f" | cut -d' ' -f1) $f"; done
echo "harness_binary_sha256: $(sha256sum "$H" | cut -d' ' -f1)"
