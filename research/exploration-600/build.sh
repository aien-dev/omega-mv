#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${EX600_OUT:-$HOME/workspace/targets/omega-exploration-600-L492}
mkdir -p "$out"
# Existing compiler and Cortex host tooling only. The policy is OSC.
lib=(); for f in src/compiler/*.c; do [[ $f == */oscc_main.c ]] || lib+=("$f"); done
flags=(); [[ ${EX600_HOST_MUTANT:-none} == skip-context ]] && flags+=(-DEX600_SKIP_CONTEXT=1)
cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Isrc/compiler \
    "${flags[@]}" "${EX600_DRIVER:-tests/research/ex600/driver.c}" "${lib[@]}" src/sha256.c src/runtime/rx_cortex.c \
    -o "$out/ex600"
