#!/bin/sh
# Complete exact PR499 input closure; private extraction only.
set -eu
out=build/L533/pr499-complete-reproduction
mkdir -p "$out"
git archive 1e4cdbe91b444efe51af4e745f366e07d54de030 Makefile mk src/compiler src/sha256.c src/sha256.h tests/compiler tools/quietlock src/osh tests/physics0/osc | tar -x -C "$out"
sh evidence/L533-REVIEW/check-decoder-closure.sh "$out"
quiet-build make -C "$out" OUT_DIR=build/L533-review test-osc-ir-decode
