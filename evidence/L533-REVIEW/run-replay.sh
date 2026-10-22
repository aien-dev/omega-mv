#!/bin/sh
set -eu
out=build/L533/replay-independent
mkdir -p "$out"
# Exact ranking/sha implementation only, discard unrelated selector entrypoints.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -Itools -Isrc evidence/L533-REVIEW/replay_probe.c tools/turing_gpu_replay.c src/turing/history_selector.c src/sha256.c -Wl,--gc-sections -lm -o "$out/probe"
"$out/probe"
