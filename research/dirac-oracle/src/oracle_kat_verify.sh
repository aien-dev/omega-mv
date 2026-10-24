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

# DIRAC-0 oracle corpus digest check. STATUS: NOT_RUN. POSIX sh plus sha256sum only.
# usage: oracle_kat_verify.sh dirac-kat-<sha256>.txt
set -eu
f="$1"
[ "$(tail -c1 "$f" | od -An -c | tr -d " ")" = "\\n" ] || { echo "FAIL missing trailing LF"; exit 1; }
[ "$(tail -n1 "$f")" = "end" ] || { echo "FAIL missing end marker"; exit 1; }
want=$(sed -n 's/^corpus_digest //p' "$f")
name=$(basename "$f" .txt)
name=${name#dirac-kat-}
got=$( { printf 'omega.dirac.kat.v1\000'; sed '/^corpus_digest /,$d' "$f"; } | sha256sum | cut -d' ' -f1 )
[ "$got" = "$want" ] || { echo "FAIL digest line mismatch"; exit 1; }
[ "$got" = "$name" ] || { echo "FAIL file name mismatch"; exit 1; }
echo PASS
