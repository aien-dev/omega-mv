#!/bin/sh
set -eu
root=$(pwd)
cd build/L533/pr499-source
set -- src/compiler/*.c
sources=
for file in "$@"; do
 case "$file" in */oscc_main.c) continue;; esac
 sources="$sources $file"
done
cc -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc -Isrc/compiler -Isrc/compiler/model "$root/evidence/L533-REVIEW/decoder_probe.c" $sources src/sha256.c -o "$root/build/L533/decoder-independent"
"$root/build/L533/decoder-independent"
