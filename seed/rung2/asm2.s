; asm2.s -- asm2 written in its own language (made by seed/rung2/mks.sh from the rung-2 listing)
  .w 0x464c457f
  .w 0x00010102
  .w 0x00000000
  .w 0x00000000
  .w 0x00b70002
  .w 0x00000001
  .w 0x00400078
  .w 0x00000000
  .w 0x00000040
  .w 0x00000000
  .w 0x00000000
  .w 0x00000000
  .w 0x00000000
  .w 0x00380040
  .w 0x00000001
  .w 0x00000000
  .w 0x00000001
  .w 0x00000007
  .w 0x00000000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x000009a0
  .w 0x00000000
  .w 0x01200000
  .w 0x00000000
  .w 0x00001000
  .w 0x00000000
  movz.x x26 0x50 1
  movz.x x27 0x51 1
  movz.x x29 0x60 1
  movz.x x28 0xe0 1
  mov.x x19 x29
:RD
  movz.x x0 0 0
  mov.x x1 x19
  sub.x.i x2 x28 16
  sub.x x2 x2 x19
  movz.x x17 1 0
  cbz.x x2 ERR
  movz.x x8 63 0
  svc 0
  cmp.x.i x0 0
  b.eq RDDONE
  b.lt ERR
  add.x x19 x19 x0
  b.al RD
:RDDONE
  movz.x x0 10 0
  strb w0 x19 0
  strb wzr x19 1
  movz.x x21 0 0
:PASS
  mov.x x19 x29
  movz.x x22 0 0
  movz.x x25 1 0
:LINE
  adr x30 back1
  b.al SKIPSP
:back1
  cbz.x x0 PASSEND
  cmp.w.i w0 10
  b.eq NL
  cmp.w.i w0 59
  b.eq SKIPL
  cmp.w.i w0 61
  b.eq TABDEF
  cmp.w.i w0 58
  b.eq LABDEF
  cmp.w.i w0 46
  b.eq DIR
  b.al INSTR
:NL
  add.x.i x19 x19 1
  add.x.i x25 x25 1
  b.al LINE
:SKIPL
  ldrb w0 x19 0
  cbz.x x0 LINE
  cmp.w.i w0 10
  b.eq NL
  add.x.i x19 x19 1
  b.al SKIPL
:ENDL
  adr x30 back2
  b.al SKIPSP
:back2
  cbz.x x0 LINE
  cmp.w.i w0 10
  b.eq NL
  cmp.w.i w0 59
  b.eq SKIPL
  movz.x x17 5 0
  b.al ERR
:SKIPSP
  ldrb w0 x19 0
  cmp.w.i w0 32
  b.eq SS1
  cmp.w.i w0 9
  b.eq SS1
  cmp.w.i w0 44
  b.eq SS1
  ret
:SS1
  add.x.i x19 x19 1
  b.al SKIPSP
:TOKEN
:TK0
  ldrb w0 x19 0
  cmp.w.i w0 32
  b.eq TK1
  cmp.w.i w0 9
  b.eq TK1
  cmp.w.i w0 44
  b.eq TK1
  mov.x x12 x19
  movz.x x9 0 0
  movz.x x10 0 0
  movz.x x11 0 0
:TK3
  ldrb w0 x19 0
  cbz.x x0 TKR
  cmp.w.i w0 10
  b.eq TKR
  cmp.w.i w0 32
  b.eq TKR
  cmp.w.i w0 9
  b.eq TKR
  cmp.w.i w0 44
  b.eq TKR
  cmp.w.i w0 59
  b.eq TKR
  movz.x x17 13 0
  cmp.x.i x11 16
  b.eq ERR
  cmp.x.i x11 8
  b.hs TK4
  add.x x1 x11 x11
  add.x x1 x1 x1
  add.x x1 x1 x1
  lslv.x x2 x0 x1
  orr.x x9 x9 x2
  b.al TK5
:TK4
  sub.x.i x1 x11 8
  add.x x1 x1 x1
  add.x x1 x1 x1
  add.x x1 x1 x1
  lslv.x x2 x0 x1
  orr.x x10 x10 x2
:TK5
  add.x.i x11 x11 1
  add.x.i x19 x19 1
  b.al TK3
:TKR
  ret
:TK1
  add.x.i x19 x19 1
  b.al TK0
:NUM
  movz.x x17 6 0
  cbz.x x11 ERR
  mov.x x1 x12
  add.x x2 x12 x11
  movz.x x3 0 0
  movz.x x0 0 0
  ldrb w4 x1 0
  cmp.w.i w4 45
  b.ne N1
  movz.x x3 1 0
  add.x.i x1 x1 1
:N1
  cmp.x x1 x2
  b.eq ERR
  ldrb w4 x1 0
  cmp.w.i w4 48
  b.ne NDEC
  add.x.i x5 x1 1
  cmp.x x5 x2
  b.eq NDEC
  ldrb w4 x5 0
  cmp.w.i w4 120
  b.ne NDEC
  add.x.i x1 x1 2
  cmp.x x1 x2
  b.eq ERR
:NHEX
  cmp.x x1 x2
  b.eq NEND
  ldrb w4 x1 0
  subs.w.i w5 w4 48
  cmp.w.i w5 10
  b.lo NH1
  subs.w.i w5 w4 97
  cmp.w.i w5 6
  b.hs ERR
  add.x.i x5 x5 10
:NH1
  movz.x x6 60 0
  lsrv.x x6 x0 x6
  cbnz.x x6 ERR
  add.x x0 x0 x0
  add.x x0 x0 x0
  add.x x0 x0 x0
  add.x x0 x0 x0
  orr.x x0 x0 x5
  add.x.i x1 x1 1
  b.al NHEX
:NDEC
  cmp.x x1 x2
  b.eq NEND
  ldrb w4 x1 0
  subs.w.i w5 w4 48
  cmp.w.i w5 10
  b.hs ERR
  movz.x x6 10 0
  mul.x x0 x0 x6
  add.x x0 x0 x5
  add.x.i x1 x1 1
  b.al NDEC
:NEND
  cbz.x x3 NRET
  sub.x x0 xzr x0
:NRET
  ret
:REG
  movz.x x17 7 0
  movz.x x1 0x7a78 0
  movk.x x1 0x72 1
  cmp.x x9 x1
  b.eq R31
  movz.x x1 0x7a77 0
  movk.x x1 0x72 1
  cmp.x x9 x1
  b.eq R31
  movz.x x1 0x7073 0
  cmp.x x9 x1
  b.eq R31
  movz.x x1 0x7377 0
  movk.x x1 0x70 1
  cmp.x x9 x1
  b.eq R31
  cmp.x.i x11 2
  b.lo ERR
  cmp.x.i x11 3
  b.hi ERR
  ldrb w1 x12 0
  cmp.w.i w1 120
  b.eq R1
  cmp.w.i w1 119
  b.ne ERR
:R1
  ldrb w1 x12 1
  subs.w.i w1 w1 48
  cmp.w.i w1 10
  b.hs ERR
  cmp.x.i x11 2
  b.eq R2
  ldrb w2 x12 2
  subs.w.i w2 w2 48
  cmp.w.i w2 10
  b.hs ERR
  cbz.x x1 ERR
  movz.x x3 10 0
  mul.x x1 x1 x3
  add.x x1 x1 x2
:R2
  cmp.x.i x1 31
  b.hs ERR
  mov.x x0 x1
  ret
:R31
  movz.x x0 31 0
  ret
:FINDTAB
  mov.x x13 x26
  movz.x x15 0 0
:FT1
  cmp.x x15 x23
  b.eq FT0
  ldr.x x1 x13 0
  ldr.x x2 x13 1
  cmp.x x1 x9
  b.ne FT2
  cmp.x x2 x10
  b.ne FT2
  ret
:FT2
  add.x.i x13 x13 64
  add.x.i x15 x15 1
  b.al FT1
:FT0
  movz.x x13 0 0
  ret
:FINDSYM
  mov.x x13 x27
  movz.x x15 0 0
:FS1
  cmp.x x15 x24
  b.eq FS0
  ldr.x x1 x13 0
  ldr.x x2 x13 1
  cmp.x x1 x9
  b.ne FS2
  cmp.x x2 x10
  b.ne FS2
  ret
:FS2
  add.x.i x13 x13 32
  add.x.i x15 x15 1
  b.al FS1
:FS0
  movz.x x13 0 0
  ret
:ERR
  movz.x x15 0x5f 1
  adr x1 MSG1
  adr x30 back3
  b.al COPYZ
:back3
  mov.x x0 x25
  adr x30 back4
  b.al PUTDEC
:back4
  adr x1 MSG2
  adr x30 back5
  b.al COPYZ
:back5
  mov.x x0 x17
  adr x30 back6
  b.al PUTDEC
:back6
  movz.x x0 10 0
  strb w0 x15 0
  add.x.i x15 x15 1
  movz.x x0 2 0
  movz.x x1 0x5f 1
  sub.x x2 x15 x1
  movz.x x8 64 0
  svc 0
  mov.x x0 x17
  movz.x x8 93 0
  svc 0
:COPYZ
  ldrb w2 x1 0
  cbz.x x2 CZR
  strb w2 x15 0
  add.x.i x15 x15 1
  add.x.i x1 x1 1
  b.al COPYZ
:CZR
  ret
:PUTDEC
  movz.x x3 0x140 0
  movk.x x3 0x5f 1
  movz.x x4 10 0
  mov.x x5 x3
:PD1
  udiv.x x6 x0 x4
  msub.x x7 x6 x4 x0
  add.x.i x7 x7 48
  sub.x.i x5 x5 1
  strb w7 x5 0
  mov.x x0 x6
  cbnz.x x0 PD1
:PD2
  cmp.x x5 x3
  b.eq PDR
  ldrb w7 x5 0
  strb w7 x15 0
  add.x.i x15 x15 1
  add.x.i x5 x5 1
  b.al PD2
:PDR
  ret
:TABDEF
  add.x.i x19 x19 1
  cbnz.x x21 SKIPL
  adr x30 back7
  b.al TOKEN
:back7
  movz.x x17 12 0
  cbz.x x11 ERR
  adr x30 back8
  b.al FINDTAB
:back8
  movz.x x17 11 0
  cbnz.x x13 ERR
  movz.x x17 16 0
  movz.x x1 1024 0
  cmp.x x23 x1
  b.hs ERR
  movz.x x1 64 0
  mul.x x13 x23 x1
  add.x x13 x13 x26
  str.x x9 x13 0
  str.x x10 x13 1
  adr x30 back9
  b.al TOKEN
:back9
  adr x30 back10
  b.al NUM
:back10
  str.x x0 x13 2
  movz.x x14 0 0
:TD1
  adr x30 back11
  b.al TOKEN
:back11
  cbz.x x11 TD9
  movz.x x17 16 0
  cmp.x.i x14 8
  b.hs ERR
  movz.x x17 12 0
  ldrb w1 x12 0
  cmp.w.i w1 114
  b.eq TD2
  cmp.w.i w1 105
  b.eq TD2
  cmp.w.i w1 115
  b.eq TD2
  cmp.w.i w1 108
  b.eq TD2
  cmp.w.i w1 97
  b.ne ERR
:TD2
  add.x.i x2 x12 1
  add.x x3 x12 x11
  movz.x x4 0 0
  movz.x x6 10 0
  cmp.x x2 x3
  b.eq TD5
:TD3
  cmp.x x2 x3
  b.eq TD5
  ldrb w5 x2 0
  cmp.w.i w5 46
  b.eq TD4
  subs.w.i w5 w5 48
  cmp.w.i w5 10
  b.hs ERR
  mul.x x4 x4 x6
  add.x x4 x4 x5
  add.x.i x2 x2 1
  b.al TD3
:TD4
  add.x.i x2 x2 1
  movz.x x7 0 0
:TD4A
  cmp.x x2 x3
  b.eq TD6
  ldrb w5 x2 0
  subs.w.i w5 w5 48
  cmp.w.i w5 10
  b.hs ERR
  mul.x x7 x7 x6
  add.x x7 x7 x5
  add.x.i x2 x2 1
  b.al TD4A
:TD5
  movz.x x7 5 0
:TD6
  add.x x5 x4 x7
  cmp.x.i x5 33
  b.hs ERR
  add.x x8 x14 x14
  add.x x8 x8 x8
  add.x x8 x8 x13
  strb w1 x8 32
  strb w4 x8 33
  strb w7 x8 34
  add.x.i x14 x14 1
  b.al TD1
:TD9
  str.x x14 x13 3
  add.x.i x23 x23 1
  b.al ENDL
:LABDEF
  add.x.i x19 x19 1
  adr x30 back12
  b.al TOKEN
:back12
  movz.x x17 12 0
  cbz.x x11 ERR
  cbnz.x x21 ENDL
  adr x30 back13
  b.al FINDSYM
:back13
  movz.x x17 10 0
  cbnz.x x13 ERR
  movz.x x17 16 0
  movz.x x1 8192 0
  cmp.x x24 x1
  b.hs ERR
  movz.x x1 32 0
  mul.x x13 x24 x1
  add.x x13 x13 x27
  str.x x9 x13 0
  str.x x10 x13 1
  str.x x22 x13 2
  add.x.i x24 x24 1
  b.al ENDL
:DIR
  adr x30 back14
  b.al TOKEN
:back14
  movz.x x17 15 0
  cmp.x.i x11 2
  b.ne ERR
  movz.x x1 0x772e 0
  cmp.x x9 x1
  b.eq DW
  movz.x x1 0x622e 0
  cmp.x x9 x1
  b.ne ERR
  adr x30 back15
  b.al TOKEN
:back15
  adr x30 back16
  b.al NUM
:back16
  movz.x x17 8 0
  movz.x x1 256 0
  cmp.x x0 x1
  b.hs ERR
  movz.x x17 16 0
  movz.x x1 0x80 1
  cmp.x x22 x1
  b.hs ERR
  add.x x1 x28 x22
  strb w0 x1 0
  add.x.i x22 x22 1
  b.al ENDL
:DW
  adr x30 back17
  b.al TOKEN
:back17
  adr x30 back18
  b.al NUM
:back18
  movz.x x17 8 0
  movz.x x1 32 0
  lsrv.x x2 x0 x1
  cbz.x x2 DW1
  movz.x x1 31 0
  asrv.x x2 x0 x1
  add.x.i x2 x2 1
  cbnz.x x2 ERR
:DW1
  mov.x x16 x0
:STORE
  movz.x x17 14 0
  movz.x x1 62 0
  lslv.x x2 x22 x1
  cbnz.x x2 ERR
  movz.x x17 16 0
  movz.x x1 0x80 1
  cmp.x x22 x1
  b.hs ERR
  add.x x1 x28 x22
  str.w w16 x1 0
  add.x.i x22 x22 4
  b.al ENDL
:INSTR
  adr x30 back19
  b.al TOKEN
:back19
  adr x30 back20
  b.al FINDTAB
:back20
  movz.x x17 3 0
  cbz.x x13 ERR
  ldr.x x16 x13 2
  ldr.x x14 x13 3
  add.x.i x18 x13 32
:IF1
  cbz.x x14 IDONE
  adr x30 back21
  b.al TOKEN
:back21
  movz.x x17 4 0
  cbz.x x11 ERR
  ldrb w1 x18 0
  cmp.w.i w1 114
  b.eq FR
  cmp.w.i w1 105
  b.eq FI
  cmp.w.i w1 115
  b.eq FS
  cmp.w.i w1 108
  b.eq FL
  b.al FA
:FR
  adr x30 back22
  b.al REG
:back22
  b.al FU
:FI
  adr x30 back23
  b.al NUM
:back23
:FU
  ldrb w3 x18 2
  movz.x x4 1 0
  lslv.x x4 x4 x3
  movz.x x17 8 0
  cmp.x x0 x4
  b.hs ERR
  b.al FINS
:FS
  adr x30 back24
  b.al NUM
:back24
:FSC
  ldrb w3 x18 2
  movz.x x4 1 0
  lslv.x x4 x4 x3
  movz.x x5 1 0
  lsrv.x x5 x4 x5
  add.x x6 x0 x5
  movz.x x17 8 0
  cmp.x x6 x4
  b.hs ERR
  sub.x.i x4 x4 1
  and.x x0 x0 x4
  b.al FINS
:FL
  cbz.x x21 FL0
  adr x30 back25
  b.al FINDSYM
:back25
  movz.x x17 9 0
  cbz.x x13 ERR
  ldr.x x0 x13 2
  sub.x x0 x0 x22
  movz.x x17 14 0
  movz.x x1 62 0
  lslv.x x2 x0 x1
  cbnz.x x2 ERR
  movz.x x1 2 0
  asrv.x x0 x0 x1
  b.al FSC
:FL0
  movz.x x0 0 0
  b.al FINS
:FA
  cbz.x x21 FNEXT
  adr x30 back26
  b.al FINDSYM
:back26
  movz.x x17 9 0
  cbz.x x13 ERR
  ldr.x x0 x13 2
  sub.x x0 x0 x22
  movz.x x4 0x20 1
  movz.x x5 0x10 1
  add.x x6 x0 x5
  movz.x x17 8 0
  cmp.x x6 x4
  b.hs ERR
  movz.x x1 62 0
  lslv.x x2 x0 x1
  movz.x x1 33 0
  lsrv.x x2 x2 x1
  orr.x x16 x16 x2
  movz.x x1 2 0
  asrv.x x3 x0 x1
  movz.x x1 45 0
  lslv.x x3 x3 x1
  movz.x x1 40 0
  lsrv.x x3 x3 x1
  orr.x x16 x16 x3
  b.al FNEXT
:FINS
  ldrb w2 x18 1
  lslv.x x0 x0 x2
  orr.x x16 x16 x0
:FNEXT
  add.x.i x18 x18 4
  sub.x.i x14 x14 1
  b.al IF1
:IDONE
  b.al STORE
:PASSEND
  cbnz.x x21 OUTP
  movz.x x21 1 0
  b.al PASS
:OUTP
  mov.x x20 x28
  mov.x x14 x22
:WL
  cbz.x x14 EXIT0
  movz.x x0 1 0
  mov.x x1 x20
  mov.x x2 x14
  movz.x x8 64 0
  svc 0
  movz.x x17 2 0
  cmp.x.i x0 0
  b.le ERR
  add.x x20 x20 x0
  sub.x x14 x14 x0
  b.al WL
:EXIT0
  movz.x x0 0 0
  movz.x x8 93 0
  svc 0
:MSG1
  .w 0x326d7361
  .w 0x696c203a
  .w 0x0020656e
:MSG2
  .w 0x7265203a
  .w 0x20726f72
  .w 0x00000000
