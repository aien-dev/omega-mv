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

# leakcheck.sh FILE...: fail if any learner-visible output carries a forbidden token.
# The forbidden list is the PD-1 label-stripping checklist (physics docs/PD1_MACHINE_LAW_BENCHMARK.md).
set -u
PAT='cache|cached|uncached|coheren|stale|flush|l2|nvidia|nvrm|nvos|gpu|cuda|marker|sem|arm|baseline|fix|variant|oldstream|sleep|m18|world|probe|chipwait|astra|e1|commit|omega|physics|allocation|attr'
rc=0
for f in "$@"; do
  hits=$(tr 'A-Z' 'a-z' < "$f" | grep -cE "\\b($PAT)\\b" || true)
  if [ "$hits" -ne 0 ]; then echo "LEAK: $f has $hits line(s) with a forbidden token"; rc=1; else echo "clean: $f"; fi
done
exit $rc
