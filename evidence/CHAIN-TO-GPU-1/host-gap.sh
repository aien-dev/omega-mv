#!/usr/bin/env bash
# host-gap.sh -- per-token time the live 27B server spends outside the GPU wait (L097, CHAIN_TO_GPU.md 7).
# Reads the server's own per-request lines (aien-serve-http, sc qwen38/serve-fast 46134247):
#   "qwen38: ... decode N tok in M ms, graph chip C ms/token, wall W ms/token"   (qwen38_engine.rs)
#   "chatcmpl-...: ... sampling mode=greedy|sampled ..."                          (server.rs, the next line)
# "graph chip" is the host clock from the doorbell to the completion marker (omega_gpu_cmdlist.c, submit),
# so wall - chip is every per-token step outside that window: embedding lookup, recording the command list,
# reading the logits back, sampling, text assembly, the hand-off to the HTTP thread.
# Only lines after the first "start 27B (serve-fast 46134247)" count (one binary). Requests with fewer than
# MIN_STEPS decode steps are left out (their 0.1 ms log rounding is larger than the gap).
# Usage: host-gap.sh <serve.log> [MIN_STEPS=16]
set -euo pipefail
log="${1:?serve.log}" min="${2:-16}"
awk -v min="$min" '
  /start 27B \(serve-fast 46134247\)/ { on = 1; next }
  !on { next }
  /qwen38: lock wait/ && /graph chip/ {
    n = $0; sub(/.*decode /, "", n); split(n, a, " "); tok = a[1]
    c = $0; sub(/.*graph chip /, "", c); sub(/ ms.*/, "", c)
    w = $0; sub(/.*wall /, "", w); sub(/ ms.*/, "", w)
    pend = 1; next
  }
  pend && /sampling mode=/ {
    pend = 0; m = $0; sub(/.*sampling mode=/, "", m); sub(/ .*/, "", m)
    steps = tok - 1; if (steps < min) { skip++; next }
    k[m]++; S[m] += steps; C[m] += c * steps; W[m] += w * steps
    g = w - c; if (!(m in lo) || g < lo[m]) lo[m] = g; if (!(m in hi) || g > hi[m]) hi[m] = g
  }
  END {
    printf "min_steps %d, requests left out %d\n", min, skip
    print "| sampling | requests | decode steps | GPU wait ms/token | wall ms/token | outside the wait ms/token (min / max per request) | share of wall |"
    print "|---|---|---|---|---|---|---|"
    for (m in k) {
      ct = C[m] / S[m]; wt = W[m] / S[m]
      printf "| %s | %d | %d | %.2f | %.2f | %.2f (%.1f / %.1f) | %.4f |\n", m, k[m], S[m], ct, wt, wt - ct, lo[m], hi[m], (wt - ct) / wt
    }
  }' "$log"
