#!/bin/sh
# tools/c2om/seed-omc.sh [outdir]: build the seed-born Omega compiler omc from nothing (ledger L418).
#
# The only outside tool is seed/rung0/load0.sh (POSIX printf), as in seed/README.md:
#   load0(seed0.hex) -> seed0 ; seed0(hex1.hex) -> hex1 ; hex1(asm2.hex1) -> asm2 ;
#   asm2(tables + om4.s) -> om4 ; om4(omc.om) -> stage1 ; stage1(omc.om) -> stage2 = omc
# Every rung is checked against the committed binary (same bytes), and omc must be a fixpoint
# (stage2(omc.om) = stage2). gcc, the C-built Omega and oscc are never run here.
# Prints the sha256 of omc on the last line; writes <outdir>/omc and <outdir>/omc.sha256.
set -eu
D=$(cd "$(dirname "$0")/../.." && pwd)
O=${1:-$D/build/c2om/seed}
mkdir -p "$O"
S=$D/seed
bad() { echo "seed-omc: $*" >&2; exit 1; }
same() { cmp -s "$1" "$2" || bad "$1 differs from the committed $2"; }

sh "$S/rung0/load0.sh" "$S/rung0/seed0.hex" "$O/seed0.load" && chmod +x "$O/seed0.load"
"$O/seed0.load" < "$S/rung0/seed0.hex" > "$O/seed0" && chmod +x "$O/seed0"
same "$O/seed0" "$S/rung0/seed0.bin"
"$O/seed0" < "$S/rung1/hex1.hex" > "$O/hex1" && chmod +x "$O/hex1"
same "$O/hex1" "$S/rung1/hex1.bin"
"$O/hex1" < "$S/rung2/asm2.hex1" > "$O/asm2" && chmod +x "$O/asm2"
same "$O/asm2" "$S/rung2/asm2.bin"
cat "$S/rung2/table.s2" "$S/rung3/table3.s2" "$S/rung4/table4.s2" "$S/rung4/om4.s" | "$O/asm2" > "$O/om4"
chmod +x "$O/om4"
same "$O/om4" "$S/rung4/om4.bin"
"$O/om4" < "$S/omc/omc.om" > "$O/stage1" || bad "om4 did not compile omc.om"
chmod +x "$O/stage1"
"$O/stage1" < "$S/omc/omc.om" > "$O/omc" || bad "stage1 did not compile omc.om"
chmod +x "$O/omc"
"$O/omc" < "$S/omc/omc.om" > "$O/omc.again" || bad "omc did not compile itself"
same "$O/omc.again" "$O/omc"
rm -f "$O/omc.again" "$O/seed0.load"
sha256sum "$O/omc" | cut -d' ' -f1 > "$O/omc.sha256"
echo "omc $(wc -c < "$O/omc") bytes, built from seed0.hex through hex1, asm2, om4; fixpoint holds"
cat "$O/omc.sha256"
