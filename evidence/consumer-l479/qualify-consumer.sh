#!/usr/bin/env bash
set -euo pipefail
omega=$(realpath "${1:?fresh Omega checkout}")
cudapp=$(realpath "${2:?prepared CUDApp checkout}")
physics=$(realpath "${3:?pinned Physics checkout}")
out=$(realpath "${4:?receipt directory}")
[[ $(git -C "$cudapp" rev-parse HEAD) = "$(cat "$omega/cudapp.lock")" ]]
[[ $(git -C "$physics" rev-parse HEAD) = "$(cat "$omega/physics.lock")" ]]
[[ -z $(git -C "$omega" status --porcelain --untracked-files=normal) ]]
printf '%s\n' "$(git -C "$omega" rev-parse HEAD)" > "$out/consumer.source"
printf '%s\n' "$(git -C "$cudapp" rev-parse HEAD)" > "$out/consumer.cudapp-source"
args=(make -C "$omega" -k -j1 --trace OUT_DIR=build/l479 CUDAPP_DIR="$cudapp" PHYSICS_DIR="$physics" build/l479/libomega_gpu.a check-cudapp-shared test-cudapp-link test-gpu-scan test-kernel-dump test-isolation-dump)
printf '%q ' timeout 1200 quiet-build "${args[@]}" > "$out/consumer.command"
printf '\n' >> "$out/consumer.command"
rc=0
timeout 1200 quiet-build "${args[@]}" > "$out/consumer.log" 2>&1 || rc=$?
printf '%s\n' "$rc" > "$out/consumer.exit"
printf 'CONSUMER_EXIT=%s\n' "$rc"
exit "$rc"
