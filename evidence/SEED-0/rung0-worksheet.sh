#!/bin/sh
# Hand-calculation worksheet for seed0.hex (rung 0). Not part of the chain: it is the arithmetic I did
# while writing seed0.hex, kept so anyone can redo it. Each line is one field layout copied from the
# Arm ARM DDI 0487 M.d section named, with the operand values put into the fields. Output: one word per
# line, compared by eye and by `cmp` against the words typed into seed/rung0/seed0.hex.
h() { printf '%08x\n' $(( $1 & 0xffffffff )); }
movz() { h $(( 0xD2800000 | ($3<<21) | ($2<<5) | $1 )); }        # C6.2.285 sf=1 opc=10 100101 hw imm16 Rd
movk() { h $(( 0xF2800000 | ($3<<21) | ($2<<5) | $1 )); }        # C6.2.283 sf=1 opc=11 100101 hw imm16 Rd
orrx() { h $(( 0xAA000000 | ($3<<16) | ($2<<5) | $1 )); }        # C6.2.302 sf=1 01 01010 shift=00 N=0 Rm imm6=0 Rn Rd
addx() { h $(( 0x8B000000 | ($3<<16) | ($2<<5) | $1 )); }        # C6.2.6   sf=1 0 0 01011 shift=00 0 Rm imm6=0 Rn Rd
addw() { h $(( 0x0B000000 | ($3<<16) | ($2<<5) | $1 )); }        # C6.2.6   sf=0
subsw(){ h $(( 0x71000000 | ($3<<10) | ($2<<5) | $1 )); }        # C6.2.464 sf=0 1 1 100010 0 sh=0 imm12 Rn Rd
subsx(){ h $(( 0xF1000000 | ($3<<10) | ($2<<5) | $1 )); }        # C6.2.464 sf=1
bcond(){ h $(( 0x54000000 | (($2 & 0x7ffff)<<5) | $1 )); }       # C6.2.34  0101010 0 imm19 0 cond
b()    { h $(( 0x14000000 | ($1 & 0x3ffffff) )); }               # C6.2.35  000101 imm26
cbzx() { h $(( 0xB4000000 | (($2 & 0x7ffff)<<5) | $1 )); }       # C6.2.78  sf=1 011010 0 imm19 Rt
ldrb() { h $(( 0x39400000 | ($3<<10) | ($2<<5) | $1 )); }        # C6.2.220 unsigned offset: 00 111 0 01 01 imm12 Rn Rt
strw() { h $(( 0xB9000000 | ($3<<10) | ($2<<5) | $1 )); }        # C6.2.415 unsigned offset: size=10 111 0 01 00 imm12 Rn Rt
svc()  { h $(( 0xD4000001 | ($1<<5) )); }                        # C6.2.466 11010100 000 imm16 000 01
EQ=0; NE=1; LO=3; LT=11
movz 9 10 0; movz 25 1 0; movz 23 0x41 1; movz 24 4 0; movk 24 0x41 1; movz 19 0 0; movz 20 0 0; movz 21 0 0
movz 0 0 0; orrx 1 31 23; movz 2 1 0; movz 8 63 0; svc 0; subsx 31 0 0; bcond $EQ 49; bcond $LT 52
ldrb 0 23 0; cbzx 21 5; subsw 31 0 10; bcond $NE -11; movz 21 0 0; b -13
subsw 31 0 35; bcond $NE 3; movz 21 1 0; b -17
subsw 1 0 48; subsw 31 1 10; bcond $LO 16; subsw 1 0 97; subsw 31 1 6; bcond $LO 12; subsw 1 0 65; subsw 31 1 6; bcond $LO 9
subsw 31 0 32; bcond $EQ -28; subsw 31 0 10; bcond $EQ -30; subsw 31 0 9; bcond $EQ -32; movz 0 4 0; b 28
addw 1 1 9; addx 19 19 19; addx 19 19 19; addx 19 19 19; addx 19 19 19; orrx 19 19 1; addx 20 20 25; subsx 31 20 8; bcond $NE -43
strw 19 24 0; movz 0 1 0; orrx 1 31 24; movz 2 4 0; movz 8 64 0; svc 0; subsx 31 0 4; bcond $NE 10; movz 19 0 0; movz 20 0 0; b -54
movz 0 0 0; cbzx 20 6; movz 0 1 0; b 4; movz 0 2 0; b 2; movz 0 3 0; movz 8 93 0; svc 0
