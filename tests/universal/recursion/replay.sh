#!/bin/bash
# Existing source-aware bootstrap and additive Node development gate.
# Run from the repository root. No installed compiler or service is modified.
set -euo pipefail
quiet-build bash tools/source-aware/build
mkdir -p build/L729
quiet-build node tests/universal/recursion/gate.mjs
