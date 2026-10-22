#!/bin/bash
set -eu
d=${1:?output dir}
mkdir -p "$d"
# Mutation only in a private copy: never free expired physical registers.
sed 's/gpr_busy\[freed + k\] = false;/(void)freed; \/\* L520 no-reuse mutant *\//' src/omega_blackwell_codegen.c > "$d/no-reuse.c"
quiet-build cc -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -ffunction-sections -fdata-sections -Wl,--gc-sections -o "$d/no-reuse" tests/gpu_regalloc_lifetime_oracle.c "$d/no-reuse.c"
quiet-build "$d/no-reuse"
