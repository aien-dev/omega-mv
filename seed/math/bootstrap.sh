#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Rebuild in owned outputs from readable source, starting with POSIX seed loader.
set -eu
cd "$(dirname "$0")/../.."
B=build/math-native/bootstrap
mkdir -p "$B"
elf() {
  test -s "$1"
  readelf -h "$1" | grep -q 'Machine:.*AArch64'
  chmod +x "$1"
}
sh seed/rung0/load0.sh seed/rung0/seed0.hex "$B/seed0"
elf "$B/seed0"
"$B/seed0" < seed/rung0/seed0.hex > "$B/seed0.self"
cmp "$B/seed0" "$B/seed0.self"
"$B/seed0" < seed/rung1/hex1.hex > "$B/hex1"
elf "$B/hex1"
"$B/hex1" < seed/rung2/asm2.hex1 > "$B/asm2"
elf "$B/asm2"
cat seed/rung2/table.s2 seed/rung3/table3.s2 seed/rung4/table4.s2 seed/rung4/om4.s > "$B/om4.input"
"$B/asm2" < "$B/om4.input" > "$B/om4"
elf "$B/om4"
[ "$(stat -c %s "$B/om4")" = "$(od -An -tu4 -j96 -N4 "$B/om4" | tr -d ' ')" ]
"$B/om4" < seed/omc/omc.om > "$B/omc1"
elf "$B/omc1"
"$B/omc1" < seed/omc/omc.om > "$B/omc2"
elf "$B/omc2"
cmp "$B/omc1" "$B/omc2"
[ "$(sha256sum "$B/omc2" | cut -d' ' -f1)" = 191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9 ]
sha256sum seed/rung0/load0.sh seed/rung0/seed0.hex seed/rung1/hex1.hex seed/rung2/asm2.hex1 seed/rung2/table.s2 seed/rung3/table3.s2 seed/rung4/table4.s2 seed/rung4/om4.s seed/omc/omc.om "$B/seed0" "$B/hex1" "$B/asm2" "$B/om4" "$B/omc1" "$B/omc2"
printf 'PASS source-only seed bootstrap, qualified hash and compiler fixpoint\n'
