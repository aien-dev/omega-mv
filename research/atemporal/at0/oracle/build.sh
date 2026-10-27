#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build the AT-0 reference oracle with a direct rustc invocation (no cargo, no crates).
# Usage: ./build.sh            -> target/at0-oracle
#        ./build.sh test       -> builds and runs target/at0-oracle-test
set -eu
cd "$(dirname "$0")"
mkdir -p target
RUSTC_VERSION="$(rustc -V)"
RUSTC_HOST="$(rustc -vV | sed -n 's/^host: //p')"
FLAGS="--edition 2021 -C opt-level=2 -C codegen-units=1 -C debuginfo=0 -C panic=abort -D warnings"
export AT0_RUSTC_VERSION="$RUSTC_VERSION" AT0_RUSTC_HOST="$RUSTC_HOST" AT0_BUILD_FLAGS="$FLAGS"
case "${1:-build}" in
  build)
    rustc $FLAGS --crate-name at0_oracle src/main.rs -o target/at0-oracle
    echo "built target/at0-oracle with $RUSTC_VERSION ($RUSTC_HOST)" ;;
  test)
    # tests need unwinding panics; everything else identical
    rustc --edition 2021 -C opt-level=2 -C codegen-units=1 -C debuginfo=0 -D warnings --test --crate-name at0_oracle src/main.rs -o target/at0-oracle-test
    ./target/at0-oracle-test "${2:-}" ;;
  lib)
    # compute crate alone, for the isolation gate (no main.rs, so no I/O layer)
    rustc $FLAGS --crate-type lib --crate-name at0 src/at0/mod.rs -o target/libat0.rlib
    echo "built target/libat0.rlib" ;;
  *) echo "usage: $0 [build|test|lib]" >&2; exit 1 ;;
esac
