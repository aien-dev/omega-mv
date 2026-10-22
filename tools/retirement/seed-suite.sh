#!/usr/bin/env bash
# Existing seed tests run in prepare.sh's isolated exact-source tree.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
source tools/retirement/pins.sh
B=$ROOT/build/retirement
E=$ROOT/evidence/seed-retirement
printf '%s  %s\n' "$SEED_SHA" "$B/seed-src/seed/out/omc" | sha256sum -c -
(cd "$B/seed-src" && make -s build/omegese/omegese build/omega_emit/omega_emit && sh seed/omc/gate5.sh) > "$E/seed-suite.log" 2>&1
printf '%s  %s\n' "$SEED_SHA" "$B/seed-src/seed/out/omc" | sha256sum -c -
printf 'SEED SUITE PASS\n'
