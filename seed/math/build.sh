#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
cd "$(dirname "$0")/../.."
B=build/math-native
mkdir -p "$B"
S=${OMEGA_SEED:-/home/drakestapleton/workspace/omega-toc/seed/out/omc}
H=191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9
[ "$(sha256sum "$S" | cut -d' ' -f1)" = "$H" ]
readelf -h "$S" | grep -q 'Machine:.*AArch64'
# Compile the committed compiler source twice. No downloaded or committed executable.
"$S" < seed/omc/omc.om > "$B/omc1"
test -s "$B/omc1"
readelf -h "$B/omc1" | grep -q 'Machine:.*AArch64'
[ "$(sha256sum "$B/omc1" | cut -d' ' -f1)" = "$H" ]
chmod +x "$B/omc1"
"$B/omc1" < seed/omc/omc.om > "$B/omc2"
cmp "$B/omc1" "$B/omc2"
cat seed/math/core.om seed/math/cli.om > "$B/math.om"
"$B/omc1" < "$B/math.om" > "$B/omega-math.tmp"
test -s "$B/omega-math.tmp"
readelf -h "$B/omega-math.tmp" | grep -q 'Machine:.*AArch64'
chmod +x "$B/omega-math.tmp"
mv "$B/omega-math.tmp" "$B/omega-math"
sha256sum seed/math/*.om "$B/omc1" "$B/omega-math"
