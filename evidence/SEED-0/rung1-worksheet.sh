#!/bin/sh
# Hand-calculation worksheet for seed/rung1/hex1.hex (rung 1). Not part of the chain: it is my arithmetic,
# kept so anyone can redo it. Each helper is one field layout from Arm DDI 0487 M.d (section named); every
# branch distance (in words, from the branch itself to its target) was counted by me from the listing in
# the comments, not computed by a tool. Prints "word  # [index] instruction  section" lines.
i=0
e() { printf '%08x  # [%d] %s\n' $(( $1 & 0xffffffff )) $i "$2"; i=$((i + 1)); }
r() { [ "$1" = 31 ] && echo "$2"zr || echo "$2$1"; }
movz() { e $(( 0xD2800000 | ($3<<21) | ($2<<5) | $1 )) "movz x$1, #$2$( [ $3 != 0 ] && echo ", lsl #$(( $3 * 16 ))")  C6.2.285 MOVZ"; }
movk() { e $(( 0xF2800000 | ($3<<21) | ($2<<5) | $1 )) "movk x$1, #$2, lsl #$(( $3 * 16 ))  C6.2.283 MOVK"; }
orrx() { e $(( 0xAA000000 | ($3<<16) | ($2<<5) | $1 )) "orr x$1, $(r $2 x), x$3  C6.2.302 ORR (shifted register)"; }
addx() { e $(( 0x8B000000 | ($3<<16) | ($2<<5) | $1 )) "add x$1, x$2, x$3  C6.2.6 ADD (shifted register)"; }
addw() { e $(( 0x0B000000 | ($3<<16) | ($2<<5) | $1 )) "add w$1, w$2, w$3  C6.2.6 ADD (shifted register) 32-bit"; }
subx() { e $(( 0xCB000000 | ($3<<16) | ($2<<5) | $1 )) "sub x$1, x$2, x$3  C6.2.458 SUB (shifted register)"; }
andx() { e $(( 0x8A000000 | ($3<<16) | ($2<<5) | $1 )) "and x$1, x$2, x$3  C6.2.15 AND (shifted register)"; }
cmpx() { e $(( 0xEB000000 | ($2<<16) | ($1<<5) | 31 )) "subs xzr, x$1, x$2  C6.2.465 SUBS (shifted register), alias CMP C6.2.97"; }
subsw(){ e $(( 0x71000000 | ($3<<10) | ($2<<5) | $1 )) "subs $(r $1 w), w$2, #$3  C6.2.464 SUBS (immediate)$( [ $1 = 31 ] && echo ", alias CMP C6.2.96")"; }
subsx(){ e $(( 0xF1000000 | ($3<<10) | ($2<<5) | $1 )) "subs $(r $1 x), x$2, #$3  C6.2.464 SUBS (immediate)$( [ $1 = 31 ] && echo ", alias CMP C6.2.96")"; }
bc()   { e $(( 0x54000000 | (($2 & 0x7ffff)<<5) | $1 )) "b.$3 $(printf %+d $2) -> $4  C6.2.34 B.cond (cond $1, Table C1-1)"; }
cbzx() { e $(( 0xB4000000 | (($2 & 0x7ffff)<<5) | $1 )) "cbz x$1, $(printf %+d $2) -> $3  C6.2.78 CBZ"; }
cbnzx(){ e $(( 0xB5000000 | (($2 & 0x7ffff)<<5) | $1 )) "cbnz x$1, $(printf %+d $2) -> $3  C6.2.77 CBNZ"; }
ldrb() { e $(( 0x39400000 | ($2<<5) | $1 )) "ldrb w$1, [x$2]  C6.2.220 LDRB (immediate), unsigned offset 0"; }
ldrx() { e $(( 0xF9400000 | ($2<<5) | $1 )) "ldr x$1, [x$2]  C6.2.216 LDR (immediate) 64-bit, unsigned offset 0"; }
ldrw() { e $(( 0xB9400000 | ($2<<5) | $1 )) "ldr w$1, [x$2]  C6.2.216 LDR (immediate) 32-bit, unsigned offset 0"; }
strx() { e $(( 0xF9000000 | ($2<<5) | $1 )) "str x$1, [x$2]  C6.2.415 STR (immediate) 64-bit, unsigned offset 0"; }
strw() { e $(( 0xB9000000 | ($2<<5) | $1 )) "str w$1, [x$2]  C6.2.415 STR (immediate) 32-bit, unsigned offset 0"; }
svc()  { e $(( 0xD4000001 )) "svc #0  C6.2.466 SVC"; }
EQ=0; NE=1; LO=3; LE=13; LT=11; AL=14
# ---- listing (index: label) ----
movz 9 10 0; movz 25 1 0; movz 26 0x41 1; movz 23 0x42 1; movz 24 0x82 1; movz 10 0xffff 0; movk 10 7 1; orrx 22 31 23
# 8 readloop
movz 0 0 0; orrx 1 31 22; subx 2 24 22; cbzx 2 144 "[155] fail7"; movz 8 63 0; svc; subsx 31 0 0
bc $EQ 4 eq "[19] readdone"; bc $LT 129 lt "[145] fail2"; addx 22 22 0; bc $AL -10 al "[8] readloop"
# 19 readdone
orrx 11 31 22; movz 27 0 0
# 21 pass
orrx 22 31 23; movz 19 0 0; movz 20 0 0; movz 21 0 0
# 25 scan
cmpx 22 11; bc $EQ 98 eq "[124] passend"; ldrb 0 22; addx 22 22 25
subsw 31 0 35; bc $EQ 21 eq "[51] comment"; subsw 31 0 58; bc $EQ 26 eq "[58] deflabel"; subsw 31 0 64; bc $EQ 41 eq "[75] reflabel"
subsw 1 0 48; subsw 31 1 10; bc $LO 70 lo "[107] nibble"; subsw 1 0 97; subsw 31 1 6; bc $LO 66 lo "[106] letter"
subsw 1 0 65; subsw 31 1 6; bc $LO 63 lo "[106] letter"
subsw 31 0 32; bc $EQ -20 eq "[25] scan"; subsw 31 0 10; bc $EQ -22 eq "[25] scan"; subsw 31 0 9; bc $EQ -24 eq "[25] scan"
bc $AL 99 al "[149] fail4"
# 51 comment
cmpx 22 11; bc $EQ 72 eq "[124] passend"; ldrb 0 22; addx 22 22 25; subsw 31 0 10; bc $NE -5 ne "[51] comment"; bc $AL -32 al "[25] scan"
# 58 deflabel
cmpx 22 11; bc $EQ 98 eq "[157] fail8"; ldrb 3 22; addx 22 22 25; subsw 31 3 33; bc $LO 94 lo "[157] fail8"
cbnzx 20 95 "[159] fail9"; cbnzx 27 -40 "[25] scan"; addx 3 3 3; addx 3 3 3; addx 3 3 3; addx 3 3 26; ldrx 4 3
cbnzx 4 82 "[153] fail6"; addx 4 21 25; strx 4 3; bc $AL -49 al "[25] scan"
# 75 reflabel
cmpx 22 11; bc $EQ 81 eq "[157] fail8"; ldrb 3 22; addx 22 22 25; subsw 31 3 33; bc $LO 77 lo "[157] fail8"
cbnzx 20 78 "[159] fail9"; cbzx 27 -57 "[25] scan"; cbzx 21 78 "[161] fail10"
addx 3 3 3; addx 3 3 3; addx 3 3 3; addx 3 3 26; ldrx 4 3; cbzx 4 62 "[151] fail5"
subx 4 4 25; subx 5 21 25; subx 4 4 5; andx 4 4 10; addx 4 4 4; addx 4 4 4; addx 4 4 4; addx 4 4 4; addx 4 4 4
addx 5 5 5; addx 5 5 5; addx 5 5 24; ldrw 6 5; orrx 6 6 4; strw 6 5; bc $AL -80 al "[25] scan"
# 106 letter, 107 nibble
addw 1 1 9; addx 19 19 19; addx 19 19 19; addx 19 19 19; addx 19 19 19; orrx 19 19 1; addx 20 20 25; subsx 31 20 8
bc $NE -89 ne "[25] scan"; cbzx 27 5 "[120] countonly"; addx 5 21 21; addx 5 5 5; addx 5 5 24; strw 19 5
# 120 countonly
addx 21 21 25; movz 19 0 0; movz 20 0 0; bc $AL -98 al "[25] scan"
# 124 passend
cbnzx 20 19 "[143] fail1"; cbnzx 27 3 "[128] output"; movz 27 1 0; bc $AL -106 al "[21] pass"
# 128 output, 131 wloop
addx 2 21 21; addx 2 2 2; orrx 22 31 24
cbzx 2 10 "[141] exit0"; movz 0 1 0; orrx 1 31 22; movz 8 64 0; svc; subsx 31 0 0; bc $LE 10 le "[147] fail3"
addx 22 22 0; subx 2 2 0; bc $AL -9 al "[131] wloop"
# 141 exit0, 143.. fail1..fail10, 162 exit
movz 0 0 0; bc $AL 20 al "[162] exit"
movz 0 1 0; bc $AL 18 al "[162] exit"; movz 0 2 0; bc $AL 16 al "[162] exit"; movz 0 3 0; bc $AL 14 al "[162] exit"
movz 0 4 0; bc $AL 12 al "[162] exit"; movz 0 5 0; bc $AL 10 al "[162] exit"; movz 0 6 0; bc $AL 8 al "[162] exit"
movz 0 7 0; bc $AL 6 al "[162] exit"; movz 0 8 0; bc $AL 4 al "[162] exit"; movz 0 9 0; bc $AL 2 al "[162] exit"
movz 0 10 0
movz 8 93 0; svc
