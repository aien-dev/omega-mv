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

# Stamp AT0_CASE_V1 identities onto a hand-written case body.
#   tests/mkcase.sh <body-file> > <case-file>
# The body holds every line from "OMEGA-AT0-CASE v1" through "end acceptance".
# Appends case_id, acceptance_id and "end" per AT0_CASE_V1 section 5
# (SHA-256 of domain tag, one zero byte, then the block lines). No Python.
set -eu
body=$1
tagged() { { printf '%s' "$1"; printf '\0'; sed -n "/^begin $2\$/,/^end $2\$/p" "$body"; } | sha256sum | cut -c1-64; }
cat "$body"
printf 'case_id %s\n' "$(tagged omega.at0.case.v1 semantic)"
printf 'acceptance_id %s\n' "$(tagged omega.at0.acceptance.v1 acceptance)"
printf 'end\n'
