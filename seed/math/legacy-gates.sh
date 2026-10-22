#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Run unchanged seed gates in an owned snapshot so historical evidence is untouched.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$PWD
B=$ROOT/build/math-native/seed-suite
E=$ROOT/evidence/math-native/legacy
mkdir -p "$B" "$E"
git archive HEAD seed src tests tools mk Makefile evidence/SEED-0 docs/one_layer | tar -x -C "$B"
mkdir -p "$B/build"
for d in omegese omega_emit; do
  test -x "$ROOT/build/$d/$d"
  if test ! -e "$B/build/$d"; then ln -s "$ROOT/build/$d" "$B/build/$d"; fi
done
cd "$B"
sh seed/rung2/gate2.sh "$B/build/rung2" > "$E/gate2.log" 2>&1
cp "$B/build/rung2/gate1.log" "$E/gate1.log"
cp "$B/build/rung2/rung1/gate0.log" "$E/gate0.log"
sh seed/rung3/gate3.sh > "$E/gate3.log" 2>&1
sh seed/rung4/gate4.sh > "$E/gate4.log" 2>&1
sh seed/omc/gate5.sh > "$E/gate5.log" 2>&1
# Full raw gate logs above; detailed worksheets remain under the owned snapshot.
printf 'PASS seed gates 0,1,2,3,4,omc5\n'
