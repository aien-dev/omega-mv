#!/bin/bash
set -euo pipefail
lib=(); for f in src/compiler/*.c; do [[ $f == */oscc_main.c ]] || lib+=("$f"); done
cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/compiler \
    tests/research/ex600/driver.c "${lib[@]}" src/sha256.c src/runtime/rx_cortex.c \
    -o /home/drakestapleton/workspace/targets/omega-exploration-600-L492/ex600-asan
