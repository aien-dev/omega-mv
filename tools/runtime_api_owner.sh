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

# tools/runtime_api_owner.sh -- what omega still owns of the Omega Runtime API after the move to CUDApp (omega-mv#924).
#
#   tools/runtime_api_owner.sh
#
# CUDApp owns the Runtime API: docs/runtime/API.md, the manifest and its INTERNAL list, the manifest headers and the
# checkers (CUDApp gate CUDAPP-RUNTIME-API). Omega consumes the exact revision in cudapp.lock through check-cudapp.
# Portable (the tree, git, awk, sed, grep; no compiler, no sibling repository), this checks the omega side:
#   1. cudapp.lock names one 40-hex CUDApp commit;
#   2. no tracked file outside evidence/ is a second copy of the Runtime API (API.md or the manifest under
#      docs/runtime, omega_runtime_api.*, omega_gpu_*.[ch], tools/runtime_api_check.sh/_negative.sh/_semver.sh), so every
#      consumer #include of a Runtime API header resolves to the pinned CUDApp stage;
#   3. mk/runtime-r1.mk runs CUDApp's checker AND its negative controls on that stage, so the host gate
#      (OMEGA-RUNTIME-API-PIN, step runtime-api-pin) cannot quietly drop either.
# Then every check is broken once on a copy of its input (a planted copy per pattern, a bad lock, a recipe without the
# negative controls); each break must FAIL, and the script fails when one is not caught.
set -u
cd "$(dirname "$0")/.." || exit 2
WORK=$(mktemp -d "${TMPDIR:-/tmp}/runtime-api-owner.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

# check FILES LOCK MK -> prints FAIL lines, returns 1 on any
check() {
  files=$1; lockf=$2; mkf=$3; bad=0
  sha=$(awk 'NF && $1 !~ /^#/ { print $1; exit }' "$lockf" 2>/dev/null)
  if ! printf '%s\n' "$sha" | grep -Eqx '[0-9a-f]{40}'; then echo "FAIL: cudapp.lock does not name one 40-hex commit (read: ${sha:-nothing})"; bad=1; fi
  grep -v '^evidence/' "$files" | grep -E '(^|/)(omega_runtime_api\.[A-Za-z]+|omega_gpu_[A-Za-z0-9_]*\.[ch]|runtime_api_(check|negative|semver)\.sh)$|^docs/runtime/(API\.md|[^/]*\.manifest|[^/]*\.internal)$' \
    > "$WORK/copies.txt"
  if [ -s "$WORK/copies.txt" ]; then sed 's/^/FAIL: second copy of the CUDApp Runtime API in omega: /' "$WORK/copies.txt"; bad=1; fi
  for t in 'sh $(CUDAPP_SOURCE)/tools/runtime_api_check.sh $(RUNTIME_API_MANIFEST) $(CUDAPP_SOURCE)' \
           'sh $(CUDAPP_SOURCE)/tools/runtime_api_negative.sh $(RUNTIME_API_MANIFEST) $(CUDAPP_SOURCE)' \
           'RUNTIME_API_MANIFEST ?= $(CUDAPP_SOURCE)/docs/runtime/omega_runtime_api.manifest' \
           'RUNTIME_API_MD ?= $(CUDAPP_SOURCE)/docs/runtime/API.md' \
           'test-runtime-api: | check-cudapp'; do
    grep -qF -- "$t" "$mkf" 2>/dev/null || { echo "FAIL: mk/runtime-r1.mk no longer has: $t"; bad=1; }
  done
  return $bad
}

git ls-files > "$WORK/files.txt" || { echo "RESULT: FAIL (git ls-files failed)"; exit 1; }
[ -s "$WORK/files.txt" ] || { echo "RESULT: FAIL (git ls-files listed nothing)"; exit 1; }
if ! check "$WORK/files.txt" cudapp.lock mk/runtime-r1.mk; then
  echo "RESULT: FAIL (omega's side of the Runtime API is not clean)"; exit 1
fi
lsha=$(awk 'NF && $1 !~ /^#/ { print $1; exit }' cudapp.lock)
nconsumers=$(git grep -l -E '#[[:space:]]*include[[:space:]]*"(omega_runtime_api|omega_gpu_[A-Za-z0-9_]*)\.h"' -- ':!evidence' | wc -l)
echo "RESULT: PASS (Runtime API owned by CUDApp $lsha; no second copy in $(wc -l < "$WORK/files.txt") tracked files; $nconsumers omega files include its headers from the stage)"

# negative controls: one break per check, on copies
bad=0
control() { # control NAME EXPECTED_TEXT FILES LOCK MK
  out=$(check "$3" "$4" "$5"); rc=$?
  if [ $rc -ne 0 ] && printf '%s\n' "$out" | grep -qF "$2"; then echo "NEGATIVE CONTROL $1: FAILED as it must ($2)"
  else echo "NEGATIVE CONTROL $1: NOT CAUGHT (rc=$rc, wanted \"$2\")"; bad=1; fi
}
printf 'not-a-sha\n' > "$WORK/bad.lock"
printf '# comment only\n' > "$WORK/empty.lock"
grep -vF 'runtime_api_negative.sh' mk/runtime-r1.mk > "$WORK/noneg.mk"
grep -vF 'runtime_api_check.sh' mk/runtime-r1.mk > "$WORK/nocheck.mk"
sed 's/test-runtime-api: | check-cudapp/test-runtime-api:/' mk/runtime-r1.mk > "$WORK/nostage.mk"
for p in src/omega_gpu_session.h src/omega_runtime_api.h tests/omega_gpu_session_copy.c docs/runtime/API.md \
         docs/runtime/omega_runtime_api.manifest docs/runtime/omega_runtime_api.internal tools/runtime_api_check.sh \
         tools/runtime_api_negative.sh tools/runtime_api_semver.sh include/sub/omega_runtime_api.h; do
  n=$(printf '%s' "$p" | tr -c 'A-Za-z0-9' '-')
  { cat "$WORK/files.txt"; echo "$p"; } > "$WORK/planted-$n.txt"
  control "planted-$p" "second copy of the CUDApp Runtime API in omega: $p" "$WORK/planted-$n.txt" cudapp.lock mk/runtime-r1.mk
done
{ cat "$WORK/files.txt"; echo "evidence/old/src/omega_gpu_session.h"; } > "$WORK/evidence.txt"
if check "$WORK/evidence.txt" cudapp.lock mk/runtime-r1.mk > /dev/null; then echo "POSITIVE CONTROL evidence-copy-allowed: PASSED as it must (evidence/ is add-only history)"
else echo "POSITIVE CONTROL evidence-copy-allowed: FAILED (an evidence/ path must not count as a copy)"; bad=1; fi
control bad-lock      "cudapp.lock does not name one 40-hex commit" "$WORK/files.txt" "$WORK/bad.lock" mk/runtime-r1.mk
control empty-lock    "cudapp.lock does not name one 40-hex commit" "$WORK/files.txt" "$WORK/empty.lock" mk/runtime-r1.mk
control no-negatives  "mk/runtime-r1.mk no longer has: sh \$(CUDAPP_SOURCE)/tools/runtime_api_negative.sh" "$WORK/files.txt" cudapp.lock "$WORK/noneg.mk"
control no-check      "mk/runtime-r1.mk no longer has: sh \$(CUDAPP_SOURCE)/tools/runtime_api_check.sh" "$WORK/files.txt" cudapp.lock "$WORK/nocheck.mk"
control no-stage      "mk/runtime-r1.mk no longer has: test-runtime-api: | check-cudapp" "$WORK/files.txt" cudapp.lock "$WORK/nostage.mk"
if [ $bad -ne 0 ]; then echo "RESULT: FAIL (a negative control was not caught)"; exit 1; fi
echo "RESULT: PASS (every negative control was caught)"
