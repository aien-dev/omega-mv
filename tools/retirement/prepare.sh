#!/usr/bin/env bash
# Test infrastructure: reuse the existing seed birth helper in an isolated pinned tree.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
source tools/retirement/pins.sh
B=$ROOT/build/retirement
E=$ROOT/evidence/seed-retirement
SC=${RETIRE_SC_REPO:-$HOME/workspace/aien-sovereign-core}
PR=${RETIRE_PROTO_REPO:-$HOME/workspace/aien-protocols}
mkdir -p "$B/seed-src" "$B/rust/src/bin" "$B/rust/tests" "$B/protocol" "$B/protocol-current" "$B/ref-src" "$E"
git archive "$SEED_COMMIT" | tar -x -C "$B/seed-src"
(cd "$B/seed-src" && sh seed/omega-from-seed.sh) > "$E/seed-birth.log" 2>&1
OMC=$B/seed-src/seed/out/omc
printf '%s  %s\n' "$SEED_SHA" "$OMC" | sha256sum -c - > "$E/seed-hash.log"
readelf -h -l -d "$OMC" > "$E/compiler-elf.log"
for f in src/lib.rs tests/trn1_conformance.rs Cargo.lock; do
    git -C "$SC" show "$RUST_COMMIT:crates/aien-replay/$f" > "$B/rust/$f"
done
# Same library/dependency, exclude unrelated KV dev-dependencies; original source/test unchanged.
cat > "$B/rust/Cargo.toml" <<'TOML'
[package]
name = "aien-replay"
version = "0.1.0"
edition = "2021"
license = "AGPL-3.0-or-later"
[workspace]
[dependencies]
sha2 = "0.10"
TOML
cp tools/retirement/trn1/oracle.rs "$B/rust/src/bin/oracle.rs"
git -C "$PR" archive "$PROTO_COMMIT" specs/execution-transcript | tar -x -C "$B/protocol"
git -C "$PR" archive "$PROTO_CURRENT" specs/execution-transcript | tar -x -C "$B/protocol-current"
git archive "$REF_COMMIT" tools/omega_ref | tar -x -C "$B/ref-src"
CARGO_TARGET_DIR="$B/rust-target" cargo build --offline --release --manifest-path "$B/rust/Cargo.toml" --bin oracle > "$E/rust-build.log" 2>&1
TRN1_REQUIRE=1 TRN1_VECTORS="$B/protocol/specs/execution-transcript/vectors" CARGO_TARGET_DIR="$B/rust-target" cargo test --offline --manifest-path "$B/rust/Cargo.toml" --test trn1_conformance -- --nocapture > "$E/rust-suite.log" 2>&1
cc -std=c99 -O2 -Wall -Wextra -Werror "$B/protocol/specs/execution-transcript/tools/trn1_verify.c" -o "$B/trn1-c-oracle" > "$E/c-reference-build.log" 2>&1
cc -std=gnu11 -O2 -Wall "$B/ref-src/tools/omega_ref/omega_ref.c" -o "$B/omega_ref" > "$E/interpreter-build.log" 2>&1
sha256sum "$B/rust/src/lib.rs" "$B/rust/Cargo.lock" "$B/rust-target/release/oracle" "$B/trn1-c-oracle" "$B/omega_ref" > "$E/oracles.sha256"
rustc -vV > "$E/rust-toolchain.log"
printf 'PREPARE PASS\n'
