#!/bin/bash
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

# C3b: build and run the GB10 unwritten-output trap. Forge queue only; the chip
# run is never killed and never timed out. No Python.
#   tools/run_unwritten_trap.sh [harness args]   (default: --chip --repeats 3000)
#   Exit: 0 = clean (no unwritten output), 1 = hits or device error, 2 = refused / NOT_RUN.
# Thin wrapper: build, refusals, evidence receipt and verdict live in tools/chip_run.sh,
# configured by tools/manifests/unwritten_trap.chiprun. PHYSICS env (default
# ~/workspace/physics) is honoured by the module.
set -u
HERE=$(cd -P "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec bash "$HERE/tools/chip_run.sh" "$HERE/tools/manifests/unwritten_trap.chiprun" -- "$@"
