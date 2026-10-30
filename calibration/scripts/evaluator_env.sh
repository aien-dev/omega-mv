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

# Turing calibration (CAL-0 / EXP-001): Evaluator Environment.
#
# usage: evaluator_env.sh --frozen DIR --sealed DIR --out DIR [--ro DIR]... -- COMMAND [ARG...]
#
#   --frozen DIR  the frozen evaluation tree: a clean export of the freeze commit
#                 (candidate artifacts, reference coders, verifier, their builds).
#                 Bound READ-ONLY: nothing in the jail can alter a frozen artifact.
#   --sealed DIR  one sealed data set, ~/aien-data/turing-cal/sealed/<commit>. Read-only.
#   --out DIR     the only writable path (evaluation outputs). Must not overlap the
#                 frozen tree or the sealed data.
#   --ro DIR      extra read-only paths. Only the overlap audit uses this (to see
#                 development data next to sealed data); evaluation itself does not.
#
# The jail has no network (always), no user namespaces, a private /proc and pid
# space, an empty HOME and /tmp. The candidate side has no channel into it: it
# exposes no socket or service, and runs only after the candidate freeze.
#
# Exit code: COMMAND's exit code, or 3 when the jail refuses to start.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tc_jail_lib.sh
. "$here/tc_jail_lib.sh"

frozen="" sealed="" out=""
ros=()
while [ $# -gt 0 ]; do
    case "$1" in
    --frozen) frozen="$2"; shift 2 ;;
    --sealed) sealed="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --ro) ros+=("$2"); shift 2 ;;
    --) shift; break ;;
    *) tc_die "unknown option '$1' (use -- before the command)" ;;
    esac
done
[ $# -gt 0 ] || tc_die "no command given"
[ -n "$frozen" ] && [ -n "$sealed" ] && [ -n "$out" ] || tc_die "--frozen, --sealed and --out are required"
[ -d "$frozen" ] || tc_die "frozen tree '$frozen' missing"
[ -d "$sealed" ] || tc_die "sealed data '$sealed' missing"
tc_overlaps "$out" "$frozen" && tc_die "--out overlaps the frozen tree"
tc_overlaps "$out" "$sealed" && tc_die "--out overlaps the sealed data"
for d in "${ros[@]+"${ros[@]}"}"; do
    tc_overlaps "$d" "$out" && tc_die "--ro '$d' overlaps --out"
done
mkdir -p "$out"

tc_have_bwrap
tc_base_args
tc_ro "$frozen"
tc_ro "$sealed"
for d in "${ros[@]+"${ros[@]}"}"; do tc_ro "$d"; done
tc_rw "$out"
TC_ARGS+=(--setenv TC_ENV evaluator --chdir "$(tc_real "$out")")
exec "$TC_BWRAP" "${TC_ARGS[@]}" -- "$@"
