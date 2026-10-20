#!/usr/bin/env bash
# Builds moe_stream inside a read-only export of aien-sovereign-core (CPU only, no GPU library).
# Usage: build.sh [SC_SHA]   (default 1e6ae85c, sc main on 2026-10-20)
set -euo pipefail
SHA=${1:-1e6ae85c}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${L273_TARGETS:-$HOME/workspace/targets/small-hw}
SRC=$ROOT/sc-$SHA
if [ ! -d "$SRC" ]; then
  mkdir -p "$SRC"
  git -C "${SC_REPO:-$HOME/workspace/sc-wormhole-30b}" archive "$SHA" | tar -x -C "$SRC"
fi
cp "$HERE/moe_stream.rs" "$SRC/crates/aien-inference-abi/src/bin/moe_stream.rs"
cd "$SRC"
AIEN_FORCE_CPU_STUB=1 CARGO_TARGET_DIR=$ROOT/target cargo build --offline --release -p aien-inference-abi --bin moe_stream
sha256sum "$ROOT/target/release/moe_stream"
