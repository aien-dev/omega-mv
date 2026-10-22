#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Run from any directory. --full adds the existing native/seed regression suites.
set -eu
cd "$(dirname "$0")/../.."
E=evidence/math-native
mkdir -p "$E"
quiet-build sh seed/math/bootstrap.sh > "$E/bootstrap.log" 2>&1
OMEGA_SEED=build/math-native/bootstrap/omc2 quiet-build sh seed/math/build.sh > "$E/build.log" 2>&1
quiet-build node seed/math/gate.mjs > "$E/golden.log" 2>&1
quiet-build node seed/math/oracle.mjs > "$E/oracle.log" 2>&1
quiet-build node seed/math/checks.mjs > "$E/checks.log" 2>&1
quiet-build node seed/math/robustness.mjs > "$E/robustness.log" 2>&1
quiet-build node seed/math/calc-oracle.mjs > "$E/calc-oracle.log" 2>&1
if [ "${1:-}" = --full ]; then
  quiet-build make -j2 test-omegese test-osc-ir-decode test-checker2 > "$E/native-suites.log" 2>&1
  quiet-build sh seed/math/legacy-gates.sh > "$E/legacy.log" 2>&1
fi
printf 'PASS native exact/symbolic math gates\n'
