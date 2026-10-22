#!/bin/sh
# Read-only source accounting, not a profiler or a second cost model.
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
root=$PWD
mkdir -p "$out/baseline/source"
base=a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a
for f in omega_gpu_decode_graph.c omega_gpu_decode_graph.h omega_blackwell_codegen_dec.h omega_gpu_decode_attn_api.c omega_blackwell_codegen_attn.h omega_gpu_prefill.h omega_gpu_prefill_attn_api.h; do
 git show "$base:src/$f" > "$out/baseline/source/$f"
done
(cd "$out/baseline/source"; sha256sum *) > "$out/baseline/source.sha256"
printf '%s\n' "$base" > "$out/baseline/base.commit"
# Refuse applying this manually audited map to a changed source.
(cd "$out/baseline/source"; sha256sum -c "$root/research/locality_l491/source-lock.sha256") > "$out/baseline/source-check.log"
awk -v dir="$out/baseline" -v sha="$base" -f research/locality_l491/baseline.awk
