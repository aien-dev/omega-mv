#!/bin/sh
# build.sh -- builds the reference interpreter (an oracle, not part of the seed chain) through quiet-build.
# Run from the repo root: sh tools/omega_ref/build.sh   ->  build/omega_ref/omega_ref
set -eu
mkdir -p build/omega_ref
quiet-build cc -O2 -g -std=gnu11 -Wall -Wextra -Werror -o build/omega_ref/omega_ref tools/omega_ref/omega_ref.c -lpthread
