# ---- program: the listing of asm2, one helper call per instruction (read by rung2-worksheet.sh for
# the hex1 words and by seed/rung2/mks.sh for the same program in asm2's own language) ----
# x19 input pointer, x21 pass (0 count, 1 write), x22 output position in bytes, x23 table entries,
# x24 labels, x25 line number, x26 table 0x500000, x27 labels 0x510000, x28 output 0xe00000,
# x29 input 0x600000; x9:x10 current word packed (16 characters at most), x11 its length, x12 its start;
# x16 instruction word being built, x17 error code for ERR. Calls: adr x30, +2 then b.al (no nesting).
movz 26 0x50 1; movz 27 0x51 1; movz 29 0x60 1; movz 28 0xe0 1; movx 19 29
lab RD
movz 0 0 0; movx 1 19; subi 2 28 16; subx 2 2 19; movz 17 1 0; cbz 2 ERR; movz 8 63 0; svc
cmpi 0 0; bc $EQ eq RDDONE; bc $LT lt ERR; addx 19 19 0; bc $AL al RD
lab RDDONE
movz 0 10 0; strb 0 19 0; strb 31 19 1; movz 21 0 0
lab PASS
movx 19 29; movz 22 0 0; movz 25 1 0
lab LINE
call SKIPSP; cbz 0 PASSEND; cmpw 0 10; bc $EQ eq NL; cmpw 0 59; bc $EQ eq SKIPL; cmpw 0 61; bc $EQ eq TABDEF
cmpw 0 58; bc $EQ eq LABDEF; cmpw 0 46; bc $EQ eq DIR; bc $AL al INSTR
lab NL
addi 19 19 1; addi 25 25 1; bc $AL al LINE
lab SKIPL
ldrb 0 19 0; cbz 0 LINE; cmpw 0 10; bc $EQ eq NL; addi 19 19 1; bc $AL al SKIPL
lab ENDL
call SKIPSP; cbz 0 LINE; cmpw 0 10; bc $EQ eq NL; cmpw 0 59; bc $EQ eq SKIPL; movz 17 5 0; bc $AL al ERR
# SKIPSP: skip spaces, tabs and commas; w0 = the character under the pointer
lab SKIPSP
ldrb 0 19 0; cmpw 0 32; bc $EQ eq SS1; cmpw 0 9; bc $EQ eq SS1; cmpw 0 44; bc $EQ eq SS1; ret
lab SS1
addi 19 19 1; bc $AL al SKIPSP
# TOKEN: skip separators, then read one word into x9:x10 (x11 length, x12 start)
lab TOKEN
lab TK0
ldrb 0 19 0; cmpw 0 32; bc $EQ eq TK1; cmpw 0 9; bc $EQ eq TK1; cmpw 0 44; bc $EQ eq TK1
movx 12 19; movz 9 0 0; movz 10 0 0; movz 11 0 0
lab TK3
ldrb 0 19 0; cbz 0 TKR; cmpw 0 10; bc $EQ eq TKR; cmpw 0 32; bc $EQ eq TKR; cmpw 0 9; bc $EQ eq TKR
cmpw 0 44; bc $EQ eq TKR; cmpw 0 59; bc $EQ eq TKR; movz 17 13 0; cmpi 11 16; bc $EQ eq ERR
cmpi 11 8; bc $HS hs TK4; addx 1 11 11; addx 1 1 1; addx 1 1 1; lslv 2 0 1; orrx 9 9 2; bc $AL al TK5
lab TK4
subi 1 11 8; addx 1 1 1; addx 1 1 1; addx 1 1 1; lslv 2 0 1; orrx 10 10 2
lab TK5
addi 11 11 1; addi 19 19 1; bc $AL al TK3
lab TKR
ret
lab TK1
addi 19 19 1; bc $AL al TK0
# NUM: the word as a number: [-]digits or [-]0x hex digits (lower case) -> x0
lab NUM
movz 17 6 0; cbz 11 ERR; movx 1 12; addx 2 12 11; movz 3 0 0; movz 0 0 0
ldrb 4 1 0; cmpw 4 45; bc $NE ne N1; movz 3 1 0; addi 1 1 1
lab N1
cmpx 1 2; bc $EQ eq ERR; ldrb 4 1 0; cmpw 4 48; bc $NE ne NDEC; addi 5 1 1; cmpx 5 2; bc $EQ eq NDEC
ldrb 4 5 0; cmpw 4 120; bc $NE ne NDEC; addi 1 1 2; cmpx 1 2; bc $EQ eq ERR
lab NHEX
cmpx 1 2; bc $EQ eq NEND; ldrb 4 1 0; subsw 5 4 48; cmpw 5 10; bc $LO lo NH1; subsw 5 4 97; cmpw 5 6; bc $HS hs ERR; addi 5 5 10
lab NH1
movz 6 60 0; lsrv 6 0 6; cbnz 6 ERR; addx 0 0 0; addx 0 0 0; addx 0 0 0; addx 0 0 0; orrx 0 0 5; addi 1 1 1; bc $AL al NHEX
lab NDEC
cmpx 1 2; bc $EQ eq NEND; ldrb 4 1 0; subsw 5 4 48; cmpw 5 10; bc $HS hs ERR; movz 6 10 0; mulx 0 0 6; addx 0 0 5; addi 1 1 1; bc $AL al NDEC
lab NEND
cbz 3 NRET; subx 0 31 0
lab NRET
ret
# REG: x0..x30 / w0..w30, or xzr wzr sp wsp (31) -> x0
lab REG
movz 17 7 0; movz 1 0x7a78 0; movk 1 0x72 1; cmpx 9 1; bc $EQ eq R31; movz 1 0x7a77 0; movk 1 0x72 1; cmpx 9 1; bc $EQ eq R31
movz 1 0x7073 0; cmpx 9 1; bc $EQ eq R31; movz 1 0x7377 0; movk 1 0x70 1; cmpx 9 1; bc $EQ eq R31
cmpi 11 2; bc $LO lo ERR; cmpi 11 3; bc $HI hi ERR; ldrb 1 12 0; cmpw 1 120; bc $EQ eq R1; cmpw 1 119; bc $NE ne ERR
lab R1
ldrb 1 12 1; subsw 1 1 48; cmpw 1 10; bc $HS hs ERR; cmpi 11 2; bc $EQ eq R2
ldrb 2 12 2; subsw 2 2 48; cmpw 2 10; bc $HS hs ERR; cbz 1 ERR; movz 3 10 0; mulx 1 1 3; addx 1 1 2
lab R2
cmpi 1 31; bc $HS hs ERR; movx 0 1; ret
lab R31
movz 0 31 0; ret
# FINDTAB / FINDSYM: entry whose name is x9:x10 -> x13 (0 if none); x15 counts
lab FINDTAB
movx 13 26; movz 15 0 0
lab FT1
cmpx 15 23; bc $EQ eq FT0; ldrx 1 13 0; ldrx 2 13 1; cmpx 1 9; bc $NE ne FT2; cmpx 2 10; bc $NE ne FT2; ret
lab FT2
addi 13 13 64; addi 15 15 1; bc $AL al FT1
lab FT0
movz 13 0 0; ret
lab FINDSYM
movx 13 27; movz 15 0 0
lab FS1
cmpx 15 24; bc $EQ eq FS0; ldrx 1 13 0; ldrx 2 13 1; cmpx 1 9; bc $NE ne FS2; cmpx 2 10; bc $NE ne FS2; ret
lab FS2
addi 13 13 32; addi 15 15 1; bc $AL al FS1
lab FS0
movz 13 0 0; ret
# ERR: write "asm2: line <x25>: error <x17>" to stderr, exit with x17
lab ERR
movz 15 0x5f 1; adrl 1 MSG1; call COPYZ; movx 0 25; call PUTDEC; adrl 1 MSG2; call COPYZ; movx 0 17; call PUTDEC
movz 0 10 0; strb 0 15 0; addi 15 15 1; movz 0 2 0; movz 1 0x5f 1; subx 2 15 1; movz 8 64 0; svc
movx 0 17; movz 8 93 0; svc
lab COPYZ
ldrb 2 1 0; cbz 2 CZR; strb 2 15 0; addi 15 15 1; addi 1 1 1; bc $AL al COPYZ
lab CZR
ret
lab PUTDEC
movz 3 0x140 0; movk 3 0x5f 1; movz 4 10 0; movx 5 3
lab PD1
udiv 6 0 4; msub 7 6 4 0; addi 7 7 48; subi 5 5 1; strb 7 5 0; movx 0 6; cbnz 0 PD1
lab PD2
cmpx 5 3; bc $EQ eq PDR; ldrb 7 5 0; strb 7 15 0; addi 15 15 1; addi 5 5 1; bc $AL al PD2
lab PDR
ret
# TABDEF: "= name 0xbase field..." ; field = kind letter (r i s l a), bit position, optional .width
lab TABDEF
addi 19 19 1; cbnz 21 SKIPL; call TOKEN; movz 17 12 0; cbz 11 ERR; call FINDTAB; movz 17 11 0; cbnz 13 ERR
movz 17 16 0; movz 1 1024 0; cmpx 23 1; bc $HS hs ERR; movz 1 64 0; mulx 13 23 1; addx 13 13 26; strx 9 13 0; strx 10 13 1
call TOKEN; call NUM; strx 0 13 2; movz 14 0 0
lab TD1
call TOKEN; cbz 11 TD9; movz 17 16 0; cmpi 14 8; bc $HS hs ERR; movz 17 12 0; ldrb 1 12 0
cmpw 1 114; bc $EQ eq TD2; cmpw 1 105; bc $EQ eq TD2; cmpw 1 115; bc $EQ eq TD2; cmpw 1 108; bc $EQ eq TD2; cmpw 1 97; bc $NE ne ERR
lab TD2
addi 2 12 1; addx 3 12 11; movz 4 0 0; movz 6 10 0; cmpx 2 3; bc $EQ eq TD5
lab TD3
cmpx 2 3; bc $EQ eq TD5; ldrb 5 2 0; cmpw 5 46; bc $EQ eq TD4; subsw 5 5 48; cmpw 5 10; bc $HS hs ERR; mulx 4 4 6; addx 4 4 5; addi 2 2 1; bc $AL al TD3
lab TD4
addi 2 2 1; movz 7 0 0
lab TD4A
cmpx 2 3; bc $EQ eq TD6; ldrb 5 2 0; subsw 5 5 48; cmpw 5 10; bc $HS hs ERR; mulx 7 7 6; addx 7 7 5; addi 2 2 1; bc $AL al TD4A
lab TD5
movz 7 5 0
lab TD6
addx 5 4 7; cmpi 5 33; bc $HS hs ERR; addx 8 14 14; addx 8 8 8; addx 8 8 13; strb 1 8 32; strb 4 8 33; strb 7 8 34; addi 14 14 1; bc $AL al TD1
lab TD9
strx 14 13 3; addi 23 23 1; bc $AL al ENDL
# LABDEF: ":name" -> the current output position (pass 1 only)
lab LABDEF
addi 19 19 1; call TOKEN; movz 17 12 0; cbz 11 ERR; cbnz 21 ENDL; call FINDSYM; movz 17 10 0; cbnz 13 ERR
movz 17 16 0; movz 1 8192 0; cmpx 24 1; bc $HS hs ERR; movz 1 32 0; mulx 13 24 1; addx 13 13 27
strx 9 13 0; strx 10 13 1; strx 22 13 2; addi 24 24 1; bc $AL al ENDL
# DIR: ".w number" one 32-bit word, ".b number" one byte
lab DIR
call TOKEN; movz 17 15 0; cmpi 11 2; bc $NE ne ERR; movz 1 0x772e 0; cmpx 9 1; bc $EQ eq DW; movz 1 0x622e 0; cmpx 9 1; bc $NE ne ERR
call TOKEN; call NUM; movz 17 8 0; movz 1 256 0; cmpx 0 1; bc $HS hs ERR
movz 17 16 0; movz 1 0x80 1; cmpx 22 1; bc $HS hs ERR; addx 1 28 22; strb 0 1 0; addi 22 22 1; bc $AL al ENDL
lab DW
call TOKEN; call NUM; movz 17 8 0; movz 1 32 0; lsrv 2 0 1; cbz 2 DW1; movz 1 31 0; asrv 2 0 1; addi 2 2 1; cbnz 2 ERR
lab DW1
movx 16 0
lab STORE
movz 17 14 0; movz 1 62 0; lslv 2 22 1; cbnz 2 ERR; movz 17 16 0; movz 1 0x80 1; cmpx 22 1; bc $HS hs ERR
addx 1 28 22; strw 16 1 0; addi 22 22 4; bc $AL al ENDL
# INSTR: "name operand..." looked up in the table, one operand per field
lab INSTR
call TOKEN; call FINDTAB; movz 17 3 0; cbz 13 ERR; ldrx 16 13 2; ldrx 14 13 3; addi 18 13 32
lab IF1
cbz 14 IDONE; call TOKEN; movz 17 4 0; cbz 11 ERR; ldrb 1 18 0
cmpw 1 114; bc $EQ eq FR; cmpw 1 105; bc $EQ eq FI; cmpw 1 115; bc $EQ eq FS; cmpw 1 108; bc $EQ eq FL; bc $AL al FA
lab FR
call REG; bc $AL al FU
lab FI
call NUM
lab FU
ldrb 3 18 2; movz 4 1 0; lslv 4 4 3; movz 17 8 0; cmpx 0 4; bc $HS hs ERR; bc $AL al FINS
lab FS
call NUM
lab FSC
ldrb 3 18 2; movz 4 1 0; lslv 4 4 3; movz 5 1 0; lsrv 5 4 5; addx 6 0 5; movz 17 8 0; cmpx 6 4; bc $HS hs ERR
subi 4 4 1; andx 0 0 4; bc $AL al FINS
lab FL
cbz 21 FL0; call FINDSYM; movz 17 9 0; cbz 13 ERR; ldrx 0 13 2; subx 0 0 22
movz 17 14 0; movz 1 62 0; lslv 2 0 1; cbnz 2 ERR; movz 1 2 0; asrv 0 0 1; bc $AL al FSC
lab FL0
movz 0 0 0; bc $AL al FINS
lab FA
cbz 21 FNEXT; call FINDSYM; movz 17 9 0; cbz 13 ERR; ldrx 0 13 2; subx 0 0 22
movz 4 0x20 1; movz 5 0x10 1; addx 6 0 5; movz 17 8 0; cmpx 6 4; bc $HS hs ERR
movz 1 62 0; lslv 2 0 1; movz 1 33 0; lsrv 2 2 1; orrx 16 16 2
movz 1 2 0; asrv 3 0 1; movz 1 45 0; lslv 3 3 1; movz 1 40 0; lsrv 3 3 1; orrx 16 16 3; bc $AL al FNEXT
lab FINS
ldrb 2 18 1; lslv 0 0 2; orrx 16 16 0
lab FNEXT
addi 18 18 4; subi 14 14 1; bc $AL al IF1
lab IDONE
bc $AL al STORE
# PASSEND: after pass 1 run pass 2; after pass 2 write the output
lab PASSEND
cbnz 21 OUTP; movz 21 1 0; bc $AL al PASS
lab OUTP
movx 20 28; movx 14 22
lab WL
cbz 14 EXIT0; movz 0 1 0; movx 1 20; movx 2 14; movz 8 64 0; svc; movz 17 2 0; cmpi 0 0; bc $LE le ERR
addx 20 20 0; subx 14 14 0; bc $AL al WL
lab EXIT0
movz 0 0 0; movz 8 93 0; svc
# messages (data, 0-terminated, word-aligned)
lab MSG1
d 0x326d7361 '"asm2"'; d 0x696c203a '": li"'; d 0x0020656e '"ne " 0'
lab MSG2
d 0x7265203a '": er"'; d 0x20726f72 '"ror "'; d 0x00000000 '0'
