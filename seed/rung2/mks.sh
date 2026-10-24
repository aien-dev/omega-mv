#!/bin/sh
# mks.sh -- CHECK INPUT ONLY. Writes asm2 itself in asm2's own language (asm2.s): the same listing
# (evidence/SEED-0/rung2-listing.sh) printed with table names instead of hex words, and labels by
# name instead of hex1's one-character marks. asm2 assembling table.s2 + asm2.s must give exactly the
# asm2.bin that hex1 built from the hand-written words: then the text table and my hand-computed
# words agree on every one of asm2's 580 instructions. It never makes a byte of seed/.
D=$(cd "$(dirname "$0")/../.." && pwd)
e() { echo "  $1"; }
r() { if [ "$1" = 31 ]; then echo "$2zr"; else echo "$2$1"; fi; }
lab() { echo ":$1"; }
d() { echo "  .w $1"; }
movz() { e "movz.x x$1 $2 $3"; }; movk() { e "movk.x x$1 $2 $3"; }; movx() { e "mov.x x$1 $(r $2 x)"; }
addi() { e "add.x.i x$1 x$2 $3"; }; subi() { e "sub.x.i x$1 x$2 $3"; }
addx() { e "add.x x$1 x$2 x$3"; }; subx() { e "sub.x x$1 $(r $2 x) x$3"; }; andx() { e "and.x x$1 x$2 x$3"; }
orrx() { e "orr.x x$1 x$2 x$3"; }; mulx() { e "mul.x x$1 x$2 x$3"; }; msub() { e "msub.x x$1 x$2 x$3 x$4"; }
udiv() { e "udiv.x x$1 x$2 x$3"; }; lslv() { e "lslv.x x$1 x$2 x$3"; }; lsrv() { e "lsrv.x x$1 x$2 x$3"; }; asrv() { e "asrv.x x$1 x$2 x$3"; }
cmpi() { e "cmp.x.i x$1 $2"; }; cmpw() { e "cmp.w.i w$1 $2"; }; subsw() { e "subs.w.i w$1 w$2 $3"; }; cmpx() { e "cmp.x x$1 x$2"; }
ldrb() { e "ldrb w$1 x$2 $3"; }; strb() { e "strb $(r $1 w) x$2 $3"; }; ldrx() { e "ldr.x x$1 x$2 $3"; }
strx() { e "str.x x$1 x$2 $3"; }; strw() { e "str.w w$1 x$2 $3"; }; svc() { e "svc 0"; }; ret() { e "ret"; }
bc() { e "b.$2 $3"; }; cbz() { e "cbz.x x$1 $2"; }; cbnz() { e "cbnz.x x$1 $2"; }; adrl() { e "adr x$1 $2"; }
n=0; call() { n=$((n + 1)); e "adr x30 back$n"; e "b.al $1"; echo ":back$n"; }
EQ=0; NE=1; HS=2; LO=3; HI=8; LE=13; LT=11; AL=14
echo "; asm2.s -- asm2 written in its own language (made by seed/rung2/mks.sh from the rung-2 listing)"
grep -o '^[0-9a-f]\{8\}' "$D/seed/rung2/asm2.hex1" | head -30 | sed 's/^/  .w 0x/'
. "$D/evidence/SEED-0/rung2-listing.sh"
