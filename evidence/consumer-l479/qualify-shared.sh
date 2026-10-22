#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# One serial run of the existing complete CUDApp host suite and carried checks.
set -euo pipefail
cudapp=$(realpath "${1:?prepared pinned CUDApp checkout}")
out=${2:?receipt directory}
mkdir -p "$out"
out=$(realpath "$out")
[[ $(git -C "$cudapp" rev-parse HEAD) = "$(cat cudapp.lock)" ]]
[[ -z $(git -C "$cudapp" status --porcelain --untracked-files=normal) ]]
args=(make -C "$cudapp" -k -j1 --trace OUT_DIR=build/l479 test test-canonical-qual-host test-kernel-dump test-isolation-dump zero-cuda-gate)
printf '%q ' timeout 2400 quiet-build "${args[@]}" > "$out/shared-host.command"
printf '\n' >> "$out/shared-host.command"
git -C "$cudapp" rev-parse HEAD > "$out/shared-host.source"
rc=0
timeout 2400 quiet-build "${args[@]}" > "$out/shared-host.log" 2>&1 || rc=$?
printf '%s\n' "$rc" > "$out/shared-host.exit"
printf 'SHARED_HOST_EXIT=%s\n' "$rc"
exit "$rc"
