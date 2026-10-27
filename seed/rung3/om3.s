; om3.s -- rung 3 of the Omega seed chain: a translator from a subset of Omegese to AArch64 machine
; words, written in asm2's language (seed/rung2) and built by asm2:
;   cat seed/rung2/table.s2 seed/rung3/table3.s2 seed/rung3/om3.s | seed/rung2/asm2.bin > om3.bin
; Use: om3.bin < task.omegese > code.bin. The output is the function's words only (word 0 = entry), to be
; called under the OSC-1 native rules (docs/osc/OSC-1-DESIGN.md section 7): parameters in x0..x5, result
; in x0 in canonical form, OscRt * in x7, a trap calls OscRt.trap (offset 16) with x0 = rt, x1 = code.
;
; The subset (what the 10 dev tasks use): one function; types bool uchar ushort uint ulong byte short int
; long (and u8 .. i64); statements  name [=] value, if/else/end, while c [bound N], for i T <number>, ret v;
; values: names, numbers, typed numbers, conversions, add sub mul div rem and or xor shl shr, eq ne lt le
; gt ge, neg, not, ( v ). Refused (error 9): signed div/rem, signed shl, bool conversions, a for count that
; is not a number, void, break, trap, arrays, calls, more than one function.
; Semantics follow the C path's interpreter (src/compiler/osc_interp.c): unsigned ops wrap to the width,
; signed add/sub/mul/neg trap 1 on overflow, div/rem trap 2 on zero, a loop bound trap 4, a conversion out
; of range trap 5, a shift amount outside 0..width-1 trap 7.
;
; Code generation: every name lives in its own register for its whole scope (the C back end keeps every
; value in a stack slot, OSC-1 section 7); temporaries are registers freed at the end of each statement.
; Registers handed out: x0..x6 not taken by parameters, x8..x15, x17 (never x7 = rt, x16, x18, x19..x30).
;
; Every machine word om3 writes is built from a fixed part read from the encoding diagram of the Arm ARM
; DDI 0487 M.d section named beside it, ORed with register and number fields.
;
; om3's own registers: x19 input cursor, x20 output cursor, x21 output start, x22 line number,
; x23 names in scope, x24 next free pool slot, x25 block stack top, x26 trap list end, x27 return type
; (0 before fn), x28 pool slots held by names, x29 pool start (after the parameter registers).
; Memory: input 0x500000, output 0x600000, names 0x700000 (16 bytes: packed name, register, type),
; blocks 0x710000 (64 bytes), trap list 0x720000 (8 bytes: site, code), message 0x730000.
; Errors: "om3: line N: error E" on stderr, exit E: 1 read, 2 write, 3 unknown name, 4 line ends too
; early, 5 extra words, 6 bad number, 7 type mismatch, 8 number out of range for its type, 9 not in the
; rung-3 subset, 10 name defined twice, 11 number with no type, 12 out of registers, 13 word over 8
; characters, 14 table full, 15 block structure, 16 syntax.
;
; ---- ELF-64 file header (words as in seed/rung0/seed0.hex) ----
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
; ---- program header: one PT_LOAD, read/write/execute, file at 0x400000 ----
  .w 0x00000001
  .w 0x00000007
  .w 0x00000000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x00001bb4 ; p_filesz = 7092 bytes, the size of om3.bin (asm2 has no size arithmetic; gate3.sh checks it)
  .w 0x00000000
  .w 0x00340000 ; p_memsz: up to 0x740000
  .w 0x00000000
  .w 0x00001000
  .w 0x00000000
; ---- 0x400078: entry ----
  b START
  .w 0
; ---- 0x400080: keyword table, 16 bytes per entry: packed word (8), code (4), 0 (4); made with
; evidence/SEED-0/rung3-worksheet.sh. Codes: 1..9 types, 33..50 ops, 64.. statements, 77 not in subset.
:KWTAB
  .w 0x6c6f6f62
  .w 0x00000000
  .w 1
  .w 0
  .w 0x61686375
  .w 0x00000072
  .w 2
  .w 0
  .w 0x00003875
  .w 0x00000000
  .w 2
  .w 0
  .w 0x6f687375
  .w 0x00007472
  .w 3
  .w 0
  .w 0x00363175
  .w 0x00000000
  .w 3
  .w 0
  .w 0x746e6975
  .w 0x00000000
  .w 4
  .w 0
  .w 0x00323375
  .w 0x00000000
  .w 4
  .w 0
  .w 0x6e6f6c75
  .w 0x00000067
  .w 5
  .w 0
  .w 0x00343675
  .w 0x00000000
  .w 5
  .w 0
  .w 0x65747962
  .w 0x00000000
  .w 6
  .w 0
  .w 0x00003869
  .w 0x00000000
  .w 6
  .w 0
  .w 0x726f6873
  .w 0x00000074
  .w 7
  .w 0
  .w 0x00363169
  .w 0x00000000
  .w 7
  .w 0
  .w 0x00746e69
  .w 0x00000000
  .w 8
  .w 0
  .w 0x00323369
  .w 0x00000000
  .w 8
  .w 0
  .w 0x676e6f6c
  .w 0x00000000
  .w 9
  .w 0
  .w 0x00343669
  .w 0x00000000
  .w 9
  .w 0
  .w 0x00646461
  .w 0x00000000
  .w 33
  .w 0
  .w 0x00627573
  .w 0x00000000
  .w 34
  .w 0
  .w 0x006c756d
  .w 0x00000000
  .w 35
  .w 0
  .w 0x00766964
  .w 0x00000000
  .w 36
  .w 0
  .w 0x006d6572
  .w 0x00000000
  .w 37
  .w 0
  .w 0x00646e61
  .w 0x00000000
  .w 38
  .w 0
  .w 0x0000726f
  .w 0x00000000
  .w 39
  .w 0
  .w 0x00726f78
  .w 0x00000000
  .w 40
  .w 0
  .w 0x006c6873
  .w 0x00000000
  .w 41
  .w 0
  .w 0x00726873
  .w 0x00000000
  .w 42
  .w 0
  .w 0x00007165
  .w 0x00000000
  .w 43
  .w 0
  .w 0x0000656e
  .w 0x00000000
  .w 44
  .w 0
  .w 0x0000746c
  .w 0x00000000
  .w 45
  .w 0
  .w 0x0000656c
  .w 0x00000000
  .w 46
  .w 0
  .w 0x00007467
  .w 0x00000000
  .w 47
  .w 0
  .w 0x00006567
  .w 0x00000000
  .w 48
  .w 0
  .w 0x0067656e
  .w 0x00000000
  .w 49
  .w 0
  .w 0x00746f6e
  .w 0x00000000
  .w 50
  .w 0
  .w 0x00006e66
  .w 0x00000000
  .w 64
  .w 0
  .w 0x00006f74
  .w 0x00000000
  .w 65
  .w 0
  .w 0x00646e65
  .w 0x00000000
  .w 66
  .w 0
  .w 0x00006669
  .w 0x00000000
  .w 67
  .w 0
  .w 0x65736c65
  .w 0x00000000
  .w 68
  .w 0
  .w 0x6c696877
  .w 0x00000065
  .w 69
  .w 0
  .w 0x6e756f62
  .w 0x00000064
  .w 70
  .w 0
  .w 0x00726f66
  .w 0x00000000
  .w 71
  .w 0
  .w 0x00746572
  .w 0x00000000
  .w 72
  .w 0
  .w 0x0000003d
  .w 0x00000000
  .w 73
  .w 0
  .w 0x00000028
  .w 0x00000000
  .w 74
  .w 0
  .w 0x00000029
  .w 0x00000000
  .w 75
  .w 0
  .w 0x64696f76
  .w 0x00000000
  .w 76
  .w 0
  .w 0x61657262
  .w 0x0000006b
  .w 77
  .w 0
  .w 0x70617274
  .w 0x00000000
  .w 77
  .w 0
  .w 0x00747570
  .w 0x00000000
  .w 77
  .w 0
  .w 0x65657266
  .w 0x00000000
  .w 77
  .w 0
  .w 0x00746567
  .w 0x00000000
  .w 77
  .w 0
  .w 0x0077656e
  .w 0x00000000
  .w 77
  .w 0
  .w 0x006e776f
  .w 0x00000000
  .w 77
  .w 0
  .w 0x74696465
  .w 0x00000000
  .w 77
  .w 0
  .w 0x77656976
  .w 0x00000000
  .w 77
  .w 0
  .w 0x65757274
  .w 0x00000000
  .w 77
  .w 0
  .w 0x736c6166
  .w 0x00000065
  .w 77
  .w 0
  .w 0
  .w 0
  .w 0
  .w 0
; ---- messages ("om3: line ", ": error "), NUL-ended ----
:MSG1
  .w 0x3a336d6f
  .w 0x6e696c20
  .w 0x00002065
:MSG2
  .w 0x7265203a
  .w 0x20726f72
  .w 0x00000000
; ---- register pool: x0..x6, x8..x15, x17; the pool starts after the parameter registers ----
:POOLC
  .b 0
  .b 1
  .b 2
  .b 3
  .b 4
  .b 5
  .b 6
  .b 8
  .b 9
  .b 10
  .b 11
  .b 12
  .b 13
  .b 14
  .b 15
  .b 17
:POOLEND
; ---- width in bits by type code 0..9 (bool 1) ----
:TWID
  .b 0
  .b 1
  .b 8
  .b 16
  .b 32
  .b 64
  .b 8
  .b 16
  .b 32
  .b 64
; ---- condition for eq ne lt le gt ge, unsigned then signed (Arm ARM Table C1-1, C1.2.4):
; EQ 0, NE 1, LO 3 / LT 11, LS 9 / LE 13, HI 8 / GT 12, HS 2 / GE 10. The opposite condition is cond ^ 1.
:CONDS
  .b 0
  .b 0
  .b 1
  .b 1
  .b 3
  .b 11
  .b 9
  .b 13
  .b 8
  .b 12
  .b 2
  .b 10
  .b 0 ; pad to a word boundary
  .b 0
; ======================================================================================================
; start: read all of stdin to 0x500000 (at most 1 MiB - 1 bytes), NUL at the end
:START
  movz.x x19 0x50 1
  movz.x x12 0 0
:RD
  movz.x x0 0 0
  add.x x1 x19 x12
  movz.x x2 0x10 1
  sub.x.i x2 x2 1
  sub.x x2 x2 x12
  cbz.x x2 E_FULL
  movz.x x8 63 0
  svc 0
  cmp.x.i x0 0
  b.eq RDDONE
  b.lt E_READ
  add.x x12 x12 x0
  b RD
:RDDONE
  add.x x0 x19 x12
  strb wzr x0 0
  movz.x x20 0x60 1
  mov.x x21 x20
  movz.x x22 1 0
  movz.x x23 0 0
  movz.x x24 0 0
  movz.x x25 0x71 1
  movz.x x26 0x72 1
  movz.x x27 0 0
  movz.x x28 0 0
  adr x29 POOLC
; ---- one statement per line ----
:STMT
  mov.x x24 x28
  bl TOK
  cbz.x x0 STMT
  cmp.x.i x0 3
  b.eq E_BLOCK
  cmp.x.i x0 2
  b.eq E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  mov.x x12 x1
  bl KW
  cmp.x.i x0 64
  b.eq S_FN
  cbz.x x27 E_SYNTAX
  cbz.x x0 S_ASSIGN
  cmp.x.i x0 66
  b.eq S_END
  cmp.x.i x0 67
  b.eq S_IF
  cmp.x.i x0 68
  b.eq S_ELSE
  cmp.x.i x0 69
  b.eq S_WHILE
  cmp.x.i x0 71
  b.eq S_FOR
  cmp.x.i x0 72
  b.eq S_RET
  cmp.x.i x0 77
  b.eq E_SUBSET
  b E_SYNTAX
; ---- fn name (param type)* to type ----
:S_FN
  cbnz.x x27 E_SUBSET
  bl TOK
  cmp.x.i x0 1
  b.ne E_SYNTAX
  movz.x x13 0 0
:FN_P
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  mov.x x12 x1
  bl KW
  cmp.x.i x0 65
  b.eq FN_TO
  cbnz.x x0 E_SYNTAX
  mov.x x1 x12
  bl FINDNAME
  cbnz.x x0 E_TWICE
  bl TYPEWORD
  cmp.x.i x13 6
  b.hs E_SUBSET
  mov.x x3 x0
  mov.x x1 x12
  mov.x x2 x13
  bl ADDNAME
  add.x.i x13 x13 1
  b FN_P
:FN_TO
  bl TYPEWORD
  mov.x x27 x0
  add.x x29 x29 x13
  movz.x x24 0 0
  movz.x x28 0 0
  bl EOL
  b STMT
; ---- name [=] value ----
:S_ASSIGN
  sub.x.i sp sp 32
  str.x x12 sp 0
  bl TOK
  mov.x x3 x0
  mov.x x4 x1
  mov.x x5 x2
  cmp.x.i x3 1
  b.ne SA1
  cmp.x.i x5 8
  b.hi SA1
  mov.x x1 x4
  bl KW
  cmp.x.i x0 73
  b.ne SA1
  bl TOK
  mov.x x3 x0
  mov.x x4 x1
  mov.x x5 x2
:SA1
  ldr.x x1 sp 0
  bl FINDNAME
  cbz.x x0 SA_NEW
  ldr.w w1 x0 2
  str.x x1 sp 1
  ldr.w w0 x0 3
  str.x x0 sp 2
  bl VALTOK
  ldr.x x9 sp 2
  cmp.x x2 x9
  b.ne E_TYPE
  ldr.x x3 sp 1
  bl FORCE
  b SA_END
:SA_NEW
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 1
  mov.x x1 x0
  movz.x x0 0 0
  bl VALTOK
  cmp.x.i x0 2
  b.eq E_BARE
  str.x x2 sp 2
  ldr.x x3 sp 1
  bl FORCE
  ldr.x x1 sp 0
  ldr.x x2 sp 1
  ldr.x x3 sp 2
  bl ADDNAME
:SA_END
  add.x.i sp sp 32
  bl EOL
  b STMT
; ---- ret value: the value into x0, then RET ----
:S_RET
  mov.x x0 x27
  movz.x x1 0 0
  bl VAL
  cmp.x x2 x27
  b.ne E_TYPE
  movz.x x3 0 0
  bl FORCE
  movz.x x0 0x03c0 0
  movk.x x0 0xd65f 1 ; RET (X30), C6.2.338
  bl EMIT
  bl EOL
  b STMT
; ---- if cond ----
:S_IF
  bl COND
  movz.x x1 1 0
  movz.x x2 0 0
  movz.x x3 0 0
  movz.x x4 0 0
  mov.x x5 x23
  mov.x x6 x28
  bl PUSHB
  bl EOL
  b STMT
; ---- else: jump over the else part; the if's false branch lands after that jump ----
:S_ELSE
  movz.x x9 0x71 1
  cmp.x x25 x9
  b.eq E_BLOCK
  sub.x.i x10 x25 64
  ldr.x x0 x10 0
  cmp.x.i x0 1
  b.ne E_BLOCK
  mov.x x11 x20
  movz.x x0 0x1400 1 ; B (distance filled at end), C6.2.35
  bl EMIT
  ldr.x x0 x10 1
  mov.x x1 x20
  bl PATCH19
  movz.x x0 2 0
  str.x x0 x10 0
  str.x x11 x10 1
  ldr.x x23 x10 5
  ldr.x x28 x10 6
  bl EOL
  b STMT
; ---- while cond [bound N]: hidden counter = 0; head: cond false -> exit; counter >= N -> trap 4;
; counter = counter + 1; body; B head ----
:S_WHILE
  sub.x.i sp sp 64
  str.x x23 sp 0
  str.x x28 sp 1
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 2
  movz.x x9 0xd280 1 ; MOVZ Xc, #0, C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  str.x x20 sp 3
  bl COND
  str.x x0 sp 4
  movz.x x9 0x4240 0
  movk.x x9 0x000f 1
  str.x x9 sp 5
  bl TOK
  cbz.x x0 SW_NOB
  cmp.x.i x0 3
  b.eq SW_NOB
  cmp.x.i x0 1
  b.ne E_EXTRA
  cmp.x.i x2 8
  b.hi E_EXTRA
  bl KW
  cmp.x.i x0 70
  b.ne E_EXTRA
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 2
  b.ne E_SYNTAX
  sub.x.i x9 x1 1
  movz.x x10 0x423f 0
  movk.x x10 0x000f 1
  cmp.x x9 x10
  b.hs E_RANGE
  str.x x1 sp 5
  bl EOL
:SW_NOB
  ldr.x x1 sp 5
  movz.x x9 4095 0
  cmp.x x1 x9
  b.hi SW_BIG
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP Xc, #N = SUBS XZR, Xc, #N, C6.2.96
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  ldr.x x1 sp 2
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  b SW_G
:SW_BIG
  bl ALLOC
  mov.x x13 x0
  ldr.x x1 sp 5
  bl MATCONST
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xc, Xn = SUBS XZR, Xc, Xn, C6.2.97
  ldr.x x1 sp 2
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ubfm.x x1 x13 48 47
  orr.x x0 x0 x1
  bl EMIT
:SW_G
  movz.x x0 2 0
  movz.x x1 4 0
  bl TRAPC
  movz.x x0 0x0400 0
  movk.x x0 0x9100 1 ; ADD Xc, Xc, #1, C6.2.5
  ldr.x x1 sp 2
  orr.x x0 x0 x1
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x0 sp 4
  movz.x x1 3 0
  ldr.x x2 sp 3
  ldr.x x3 sp 2
  movz.x x4 0 0
  ldr.x x5 sp 0
  ldr.x x6 sp 1
  bl PUSHB
  add.x.i sp sp 64
  b STMT
; ---- for i T K (K a number): i = 0; head: not i < K -> exit; body; latch i = add i 1; B head ----
:S_FOR
  sub.x.i sp sp 64
  str.x x23 sp 0
  str.x x28 sp 1
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  str.x x1 sp 2
  bl KW
  cbnz.x x0 E_SYNTAX
  ldr.x x1 sp 2
  bl FINDNAME
  cbnz.x x0 E_TWICE
  bl TYPEWORD
  str.x x0 sp 3
  cmp.x.i x0 1
  b.eq E_TYPE
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 2
  b.ne E_SUBSET
  ldr.x x2 sp 3
  bl RANGE
  str.x x1 sp 4
  bl EOL
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 5
  mov.x x2 x0
  ldr.x x1 sp 2
  ldr.x x3 sp 3
  bl ADDNAME
  ldr.x x0 sp 5
  movz.x x9 0xd280 1 ; MOVZ Xi, #0, C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  str.x x20 sp 6
  ldr.x x1 sp 4
  movz.x x9 4095 0
  cmp.x x1 x9
  b.hi SF_BIG
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP Xi, #K, C6.2.96
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  ldr.x x1 sp 5
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  b SF_B
:SF_BIG
  bl ALLOC
  mov.x x13 x0
  ldr.x x1 sp 4
  bl MATCONST
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xi, Xk, C6.2.97
  ldr.x x1 sp 5
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ubfm.x x1 x13 48 47
  orr.x x0 x0 x1
  bl EMIT
:SF_B
  mov.x x13 x20
  movz.x x0 0x5400 1 ; B.HS (unsigned) or B.GE (signed) to the exit, C6.2.34
  ldr.x x9 sp 3
  movz.x x1 2 0
  cmp.x.i x9 6
  b.lo SF1
  movz.x x1 10 0
:SF1
  orr.x x0 x0 x1
  bl EMIT
  mov.x x0 x13
  movz.x x1 4 0
  ldr.x x2 sp 6
  ldr.x x3 sp 5
  ldr.x x4 sp 3
  ldr.x x5 sp 0
  ldr.x x6 sp 1
  bl PUSHB
  add.x.i sp sp 64
  b STMT
; ---- end: close the innermost block, or the function ----
:S_END
  movz.x x9 0x71 1
  cmp.x x25 x9
  b.eq FINISH
  sub.x.i x25 x25 64
  ldr.x x0 x25 0
  cmp.x.i x0 1
  b.eq SE_IF
  cmp.x.i x0 2
  b.eq SE_ELSE
  cmp.x.i x0 3
  b.eq SE_WHILE
  sub.x.i sp sp 48
  movz.x x0 1 0
  str.x x0 sp 0
  ldr.x x0 x25 3
  str.x x0 sp 1
  ldr.x x0 x25 4
  str.x x0 sp 2
  movz.x x0 3 0
  str.x x0 sp 3
  movz.x x0 1 0
  str.x x0 sp 4
  ldr.x x0 x25 4
  str.x x0 sp 5
  movz.x x0 33 0
  ldr.x x1 x25 3
  add.x.i x2 sp 0
  bl BINOP
  add.x.i sp sp 48
:SE_WHILE
  ldr.x x0 x25 2
  bl BACK
:SE_IF
  ldr.x x0 x25 1
  mov.x x1 x20
  bl PATCH19
  b SE_X
:SE_ELSE
  ldr.x x0 x25 1
  mov.x x1 x20
  bl PATCH26
:SE_X
  ldr.x x23 x25 5
  ldr.x x28 x25 6
  bl EOL
  b STMT
; ---- end of the function: trap stubs, then write the words ----
; shared tail: MOV X0, X7; LDR X16, [X7, #16]; BR X16. Per trap code used: MOVZ X1, #code; B tail.
:FINISH
  bl EOL
  movz.x x9 0x72 1
  cmp.x x26 x9
  b.eq FI_T
  mov.x x13 x20
  movz.x x0 0x03e0 0
  movk.x x0 0xaa07 1 ; MOV X0, X7 = ORR X0, XZR, X7, C6.2.282
  bl EMIT
  movz.x x0 0x08f0 0
  movk.x x0 0xf940 1 ; LDR X16, [X7, #16], unsigned offset imm12 = 2, C6.2.216
  bl EMIT
  movz.x x0 0x0200 0
  movk.x x0 0xd61f 1 ; BR X16, C6.2.46
  bl EMIT
  movz.x x14 1 0
:FI_C
  movz.x x15 0 0
  movz.x x12 0x72 1
:FI_S
  cmp.x x12 x26
  b.hs FI_N
  ldr.w w0 x12 1
  cmp.x x0 x14
  b.ne FI_NX
  cbnz.x x15 FI_P
  mov.x x15 x20
  movz.x x0 0x0001 0
  movk.x x0 0xd280 1 ; MOVZ X1, #code, C6.2.285
  ubfm.x x1 x14 59 58
  orr.x x0 x0 x1
  bl EMIT
  mov.x x0 x13
  bl BACK
:FI_P
  ldr.w w0 x12 0
  mov.x x1 x15
  bl PATCH19
:FI_NX
  add.x.i x12 x12 8
  b FI_S
:FI_N
  add.x.i x14 x14 1
  cmp.x.i x14 14
  b.ls FI_C
:FI_T
  bl TOK
  cbz.x x0 FI_T
  cmp.x.i x0 3
  b.ne E_SUBSET
  mov.x x12 x21
:WR
  sub.x x2 x20 x12
  cbz.x x2 EXIT0
  movz.x x0 1 0
  mov.x x1 x12
  movz.x x8 64 0
  svc 0
  cmp.x.i x0 0
  b.le E_WRITE
  add.x x12 x12 x0
  b WR
:EXIT0
  movz.x x0 0 0
  movz.x x8 93 0
  svc 0
; ======================================================================================================
; VAL: read one value. x0 expected type (0 none), x1 destination register (255 none).
; Out: x0 kind (1 register, 2 bare number, 3 typed number, 4 flags), x1 register / number / condition,
; x2 type. Only the outermost op writes the destination. Clobbers x0..x17.
:VAL
  str.x.pre x30 sp -32
  str.x x0 sp 1
  str.x x1 sp 2
  bl TOK
  mov.x x3 x0
  mov.x x4 x1
  mov.x x5 x2
  ldr.x x0 sp 1
  ldr.x x1 sp 2
  ldr.x.post x30 sp 32
; VALTOK: the same with the first word already read (x3 kind, x4 word or number, x5 length).
; Frame (8-byte slots): 0 x30, 1 expected, 2 dest, 3 op, 4..6 a, 7..9 b, 10 type of a conversion.
:VALTOK
  str.x.pre x30 sp -96
  str.x x0 sp 1
  str.x x1 sp 2
  cbz.x x3 E_FEW
  cmp.x.i x3 3
  b.eq E_FEW
  cmp.x.i x3 2
  b.ne VT_W
  mov.x x1 x4
  movz.x x2 0 0
  movz.x x0 2 0
  ldr.x x9 sp 1
  cbz.x x9 VT_RET
  mov.x x2 x9
  bl RANGE
  movz.x x0 3 0
  b VT_RET
:VT_W
  cmp.x.i x5 8
  b.hi E_LONG
  mov.x x1 x4
  bl KW
  cbnz.x x0 VT_K
  bl FINDNAME
  cbz.x x0 E_UNDEF
  ldr.w w1 x0 2
  ldr.w w2 x0 3
  movz.x x0 1 0
  b VT_RET
:VT_K
  sub.x.i x9 x0 1
  cmp.x.i x9 8
  b.ls VT_TYPE
  cmp.x.i x0 74
  b.eq VT_PAREN
  cmp.x.i x0 77
  b.eq E_SUBSET
  sub.x.i x9 x0 33
  cmp.x.i x9 17
  b.hi E_SYNTAX
  str.x x0 sp 3
  cmp.x.i x0 49
  b.hs VT_UN
; binary op: a, then b; a comparison used as a value is turned into a register at once (flags would not
; survive the next value)
  movz.x x0 0 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x0 4
  b.ne VB1
  bl MATREG
  mov.x x1 x0
  movz.x x0 1 0
  movz.x x2 1 0
:VB1
  str.x x0 sp 4
  str.x x1 sp 5
  str.x x2 sp 6
  movz.x x0 0 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x0 4
  b.ne VB2
  bl MATREG
  mov.x x1 x0
  movz.x x0 1 0
  movz.x x2 1 0
:VB2
  str.x x0 sp 7
  str.x x1 sp 8
  str.x x2 sp 9
; a bare number takes the type of the other value (in a shift too: shl 1 sh)
  ldr.x x10 sp 4
  ldr.x x11 sp 7
  cmp.x.i x10 2
  b.ne VB3
  cmp.x.i x11 2
  b.eq E_BARE
  ldr.x x1 sp 5
  ldr.x x2 sp 9
  bl RANGE
  str.x x2 sp 6
  movz.x x0 3 0
  str.x x0 sp 4
:VB3
  cmp.x.i x11 2
  b.ne VB4
  ldr.x x1 sp 8
  ldr.x x2 sp 6
  bl RANGE
  str.x x2 sp 9
  movz.x x0 3 0
  str.x x0 sp 7
:VB4
  ldr.x x0 sp 3
  cmp.x.i x0 41
  b.eq VB5
  cmp.x.i x0 42
  b.eq VB5
  ldr.x x1 sp 6
  ldr.x x2 sp 9
  cmp.x x1 x2
  b.ne E_TYPE
:VB5
  ldr.x x0 sp 3
  ldr.x x1 sp 2
  add.x.i x2 sp 32
  bl BINOP
:VT_RET
  ldr.x.post x30 sp 96
  ret
; ( value )
:VT_PAREN
  ldr.x x0 sp 1
  ldr.x x1 sp 2
  bl VAL
  str.x x0 sp 4
  str.x x1 sp 5
  str.x x2 sp 6
  bl TOK
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_SYNTAX
  bl KW
  cmp.x.i x0 75
  b.ne E_SYNTAX
  ldr.x x0 sp 4
  ldr.x x1 sp 5
  ldr.x x2 sp 6
  b VT_RET
; neg a, not a (a bare number takes the expected type)
:VT_UN
  ldr.x x0 sp 1
  movz.x x1 255 0
  bl VAL
  ldr.x x9 sp 3
  cmp.x.i x9 50
  b.ne VU1
  cmp.x.i x0 4
  b.ne VU1
  movz.x x9 1 0
  eor.x x1 x1 x9
  b VT_RET
:VU1
  cmp.x.i x0 2
  b.eq E_BARE
  str.x x2 sp 6
  bl MATREG
  str.x x0 sp 5
  ldr.x x0 sp 2
  bl DESTREG
  str.x x0 sp 7
  ldr.x x9 sp 3
  cmp.x.i x9 50
  b.eq VU_NOT
  ldr.x x9 sp 6
  cmp.x.i x9 1
  b.eq E_TYPE
  cmp.x.i x9 9
  b.eq VN64
  movz.x x0 0x03e0 0
  movk.x x0 0xcb00 1 ; NEG Xd, Xm = SUB Xd, XZR, Xm, C6.2.458
  cmp.x.i x9 4
  b.ne VN1
  movz.x x0 0x03e0 0
  movk.x x0 0x4b00 1 ; NEG Wd, Wm (32-bit SUB, C6.2.458): the u32 result comes zero-extended
  bl VU_E
  b VU_R
:VN1
  bl VU_E
  ldr.x x0 sp 7
  ldr.x x1 sp 6
  cmp.x.i x1 6
  b.hs VN2
  bl CANON
  b VU_R
:VN2
  movz.x x2 1 0
  bl CHKEXT
  b VU_R
:VN64
  movz.x x0 0x03e0 0
  movk.x x0 0xeb00 1 ; NEGS Xd, Xm = SUBS Xd, XZR, Xm, C6.2.465
  bl VU_E
  movz.x x0 6 0
  movz.x x1 1 0
  bl TRAPC
  b VU_R
:VU_NOT
  ldr.x x9 sp 6
  cmp.x.i x9 1
  b.ne VNT1
  movz.x x0 0 0
  movk.x x0 0xd240 1 ; EOR Xd, Xn, #1 (immediate, N=1 immr=0 imms=0), C6.2.155
  ldr.x x1 sp 7
  ldr.x x2 sp 5
  movz.x x3 0 0
  bl E3
  b VU_R
:VNT1
  movz.x x0 0x03e0 0
  movk.x x0 0xaa20 1 ; MVN Xd, Xm = ORN Xd, XZR, Xm, C6.2.300
  cmp.x.i x9 4
  b.ne VNT2
  movz.x x0 0x03e0 0
  movk.x x0 0x2a20 1 ; MVN Wd, Wm (32-bit ORN, C6.2.300)
  bl VU_E
  b VU_R
:VNT2
  bl VU_E
  ldr.x x0 sp 7
  ldr.x x1 sp 6
  cmp.x.i x1 6
  b.hs VU_R
  bl CANON
:VU_R
  movz.x x0 1 0
  ldr.x x1 sp 7
  ldr.x x2 sp 6
  b VT_RET
:VU_E
  ldr.x x1 sp 7
  movz.x x2 0 0
  ldr.x x3 sp 5
  b E3
; a type word: a typed number (uint 5) or a conversion of the next value
:VT_TYPE
  str.x x0 sp 10
  bl TOK
  cmp.x.i x0 2
  b.ne VTT1
  ldr.x x2 sp 10
  bl RANGE
  movz.x x0 3 0
  b VT_RET
:VTT1
  mov.x x3 x0
  mov.x x4 x1
  mov.x x5 x2
  movz.x x0 0 0
  movz.x x1 255 0
  bl VALTOK
  cmp.x.i x0 2
  b.eq E_BARE
  str.x x2 sp 6
  bl MATREG
  str.x x0 sp 5
  ldr.x x1 sp 6
  ldr.x x2 sp 10
  cmp.x.i x1 1
  b.eq E_SUBSET
  cmp.x.i x2 1
  b.eq E_SUBSET
  adr x9 TWID
  ldrb.r w3 x9 x1
  ldrb.r w4 x9 x2
  cmp.x.i x1 6
  b.hs VC_SS
  cmp.x.i x2 6
  b.hs VC_US
  cmp.x x3 x4
  b.ls VC_OK
  mov.x x5 x4
  b VC_LSR
:VC_US
  cmp.x x3 x4
  b.lo VC_OK
  sub.x.i x5 x4 1
  b VC_LSR
:VC_SS
  cmp.x.i x2 6
  b.hs VC_S2
  mov.x x5 x4
  cmp.x.i x5 63
  b.ls VC_LSR
  movz.x x5 63 0
  b VC_LSR
:VC_S2
  cmp.x x3 x4
  b.ls VC_OK
  ldr.x x0 sp 5
  ldr.x x1 sp 10
  movz.x x2 5 0
  bl CHKEXT
  b VC_OK
; out of range when any bit at or above x5 is set: LSR Xt, Xa, #x5 = UBFM Xt, Xa, #x5, #63 (C6.2.487);
; CBNZ Xt -> trap 5 (C6.2.77)
:VC_LSR
  bl ALLOC
  str.x x0 sp 7
  movz.x x1 0xfc00 0
  movk.x x1 0xd340 1
  orr.x x1 x1 x0
  ubfm.x x5 x5 48 47
  orr.x x1 x1 x5
  ldr.x x2 sp 5
  ubfm.x x2 x2 59 58
  orr.x x0 x1 x2
  bl EMIT
  ldr.x x0 sp 7
  movz.x x9 0xb500 1 ; CBNZ Xt, C6.2.77
  orr.x x0 x0 x9
  movz.x x1 5 0
  bl TRAPSITE
:VC_OK
  movz.x x0 1 0
  ldr.x x1 sp 5
  ldr.x x2 sp 10
  b VT_RET
; ======================================================================================================
; BINOP: x0 op (33..48), x1 destination (255 none), x2 address of two values (kind, number/register,
; type) a then b, types already settled. Out: as VAL. Frame: 0 x30, 1 op, 2 rd, 3 values, 4 ra,
; 5 rb or immediate, 6 type, 7 1 if b is an immediate, 8 temporary.
:BINOP
  str.x.pre x30 sp -80
  str.x x0 sp 1
  str.x x1 sp 2
  str.x x2 sp 3
  ldr.x x9 x2 2
  str.x x9 sp 6
  ldr.x x0 x2 0
  ldr.x x1 x2 1
  ldr.x x2 x2 2
  bl MATREG
  str.x x0 sp 4
  movz.x x9 0 0
  str.x x9 sp 7
  ldr.x x10 sp 3
  ldr.x x0 x10 3
  cmp.x.i x0 3
  b.ne BO_BREG
  ldr.x x1 x10 4
  ldr.x x11 sp 1
  cmp.x.i x11 41
  b.eq BO_IS
  cmp.x.i x11 42
  b.eq BO_IS
  cmp.x.i x11 35
  b.lo BO_II
  cmp.x.i x11 43
  b.lo BO_BREG
:BO_II
  movz.x x9 4095 0
  cmp.x x1 x9
  b.hi BO_BREG
  b BO_IMM
:BO_IS
  ldr.x x9 sp 6
  adr x12 TWID
  ldrb.r w9 x12 x9
  cmp.x x1 x9
  b.hs BO_BREG
:BO_IMM
  str.x x1 sp 5
  movz.x x9 1 0
  str.x x9 sp 7
  b BO_OPS
:BO_BREG
  ldr.x x10 sp 3
  ldr.x x0 x10 3
  ldr.x x1 x10 4
  ldr.x x2 x10 5
  bl MATREG
  str.x x0 sp 5
:BO_OPS
  ldr.x x0 sp 1
  cmp.x.i x0 43
  b.hs BO_CMP
  ldr.x x9 sp 6
  cmp.x.i x9 1
  b.ne BO_NB
  sub.x.i x10 x0 38
  cmp.x.i x10 2
  b.hi E_TYPE
:BO_NB
  ldr.x x0 sp 2
  bl DESTREG
  str.x x0 sp 2
  ldr.x x0 sp 1
  cmp.x.i x0 35
  b.lo BO_ADD
  b.eq BO_MUL
  cmp.x.i x0 38
  b.lo BO_DIV
  cmp.x.i x0 41
  b.lo BO_LOG
  b.eq BO_SHL
  b BO_SHR
; add, sub. u32: 32-bit form (zero-extends); i64: flag-setting form + B.VS trap 1; u8/u16: wrap with
; UBFM; i8/i16/i32: exact in 64 bits, then the sign-extension check (trap 1)
:BO_ADD
  ldr.x x9 sp 1
  ldr.x x10 sp 7
  cbnz.x x10 BA_I
  movz.x x0 0x8b00 1 ; ADD (shifted register) 64-bit, C6.2.6
  cmp.x.i x9 33
  b.eq BA_S
  movz.x x0 0xcb00 1 ; SUB (shifted register) 64-bit, C6.2.458
  b BA_S
:BA_I
  movz.x x0 0x9100 1 ; ADD (immediate) 64-bit, C6.2.5
  cmp.x.i x9 33
  b.eq BA_S
  movz.x x0 0xd100 1 ; SUB (immediate) 64-bit, C6.2.457
:BA_S
  ldr.x x9 sp 6
  cmp.x.i x9 4
  b.ne BA2
  movz.x x10 0x8000 1 ; sf (bit 31) = 0: the 32-bit form
  eor.x x0 x0 x10
  bl BO_W3
  b BO_RET
:BA2
  cmp.x.i x9 9
  b.ne BA3
  movz.x x10 0x2000 1 ; S (bit 29) = 1: ADDS/SUBS, C6.2.10, C6.2.11, C6.2.464, C6.2.465
  orr.x x0 x0 x10
  bl BO_W3
  movz.x x0 6 0
  movz.x x1 1 0
  bl TRAPC
  b BO_RET
:BA3
  bl BO_W3
  bl BO_FIX
  b BO_RET
; mul. i64: SMULH t (C6.2.379), MUL, CMP t, rd, ASR #63 (C6.2.97), B.NE trap 1
:BO_MUL
  ldr.x x9 sp 6
  cmp.x.i x9 9
  b.eq BM64
  movz.x x0 0x7c00 0
  movk.x x0 0x9b00 1 ; MUL 64-bit = MADD Xd, Xn, Xm, XZR, C6.2.293 / C6.2.275
  cmp.x.i x9 4
  b.ne BM1
  movz.x x10 0x8000 1
  eor.x x0 x0 x10
  bl BO_W3
  b BO_RET
:BM1
  bl BO_W3
  bl BO_FIX
  b BO_RET
:BM64
  bl ALLOC
  str.x x0 sp 8
  movz.x x0 0x7c00 0
  movk.x x0 0x9b40 1 ; SMULH, C6.2.379
  ldr.x x1 sp 8
  ldr.x x2 sp 4
  ldr.x x3 sp 5
  bl E3
  movz.x x0 0x7c00 0
  movk.x x0 0x9b00 1 ; MUL, C6.2.293
  bl BO_W3
  movz.x x0 0xfc1f 0
  movk.x x0 0xeb80 1 ; CMP Xt, Xd, ASR #63 = SUBS XZR, Xt, Xd, ASR #63, C6.2.465
  movz.x x1 0 0
  ldr.x x2 sp 8
  ldr.x x3 sp 2
  bl E3
  movz.x x0 1 0
  movz.x x1 1 0
  bl TRAPC
  b BO_RET
; div, rem (unsigned): CBZ Xb -> trap 2 (C6.2.78), unless b is a number other than 0; UDIV (C6.2.490);
; rem: t = a / b; rd = a - t * b (MSUB Xd, Xt, Xb, Xa, C6.2.291)
:BO_DIV
  ldr.x x9 sp 6
  cmp.x.i x9 6
  b.hs E_SUBSET
  ldr.x x10 sp 3
  ldr.x x0 x10 3
  cmp.x.i x0 3
  b.ne BD1
  ldr.x x0 x10 4
  cbnz.x x0 BD2
:BD1
  ldr.x x0 sp 5
  movz.x x9 0xb400 1 ; CBZ Xb, C6.2.78
  orr.x x0 x0 x9
  movz.x x1 2 0
  bl TRAPSITE
:BD2
  ldr.x x9 sp 1
  cmp.x.i x9 37
  b.eq BD_REM
  movz.x x0 0x0800 0
  movk.x x0 0x9ac0 1 ; UDIV 64-bit, C6.2.490
  bl BO_W3
  b BO_RET
:BD_REM
  bl ALLOC
  str.x x0 sp 8
  movz.x x0 0x0800 0
  movk.x x0 0x9ac0 1 ; UDIV, C6.2.490
  ldr.x x1 sp 8
  ldr.x x2 sp 4
  ldr.x x3 sp 5
  bl E3
  movz.x x0 0x8000 0
  movk.x x0 0x9b00 1 ; MSUB 64-bit, C6.2.291 (Ra at bit 10)
  ldr.x x1 sp 4
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  ldr.x x1 sp 2
  ldr.x x2 sp 8
  ldr.x x3 sp 5
  bl E3
  b BO_RET
; and, or, xor: canonical in, canonical out
:BO_LOG
  movz.x x0 0x8a00 1 ; AND (shifted register) 64-bit, C6.2.15
  ldr.x x9 sp 1
  cmp.x.i x9 39
  b.lo BL1
  movz.x x0 0xaa00 1 ; ORR (shifted register) 64-bit, C6.2.302
  b.eq BL1
  movz.x x0 0xca00 1 ; EOR (shifted register) 64-bit, C6.2.156
:BL1
  bl BO_W3
  b BO_RET
; shl (unsigned): by a number s: LSL #s = UBFM Xd, Xn, #(64-s) mod 64, #(63-s) (C6.2.487), then wrap;
; by a register: CMP Xb, #w; B.HS trap 7; LSLV (C6.2.271; 32-bit for u32)
:BO_SHL
  ldr.x x9 sp 6
  cmp.x.i x9 6
  b.hs E_SUBSET
  ldr.x x10 sp 7
  cbnz.x x10 BS_IL
  bl BO_SCHK
  movz.x x0 0x2000 0
  movk.x x0 0x9ac0 1 ; LSLV 64-bit, C6.2.271
  ldr.x x9 sp 6
  cmp.x.i x9 4
  b.ne BS1
  movz.x x10 0x8000 1
  eor.x x0 x0 x10
  bl BO_W3
  b BO_RET
:BS1
  bl BO_W3
  bl BO_FIX
  b BO_RET
:BS_IL
  ldr.x x1 sp 5
  movz.x x2 64 0
  sub.x x2 x2 x1
  ubfm.x x2 x2 0 5
  movz.x x3 63 0
  sub.x x3 x3 x1
  movz.x x0 0xd340 1 ; UBFM 64-bit, C6.2.487
  ubfm.x x2 x2 48 47
  orr.x x0 x0 x2
  ubfm.x x3 x3 54 53
  orr.x x0 x0 x3
  ldr.x x1 sp 4
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 2
  orr.x x0 x0 x1
  bl EMIT
  bl BO_FIX
  b BO_RET
; shr: unsigned LSR, signed ASR. By a number s: UBFM/SBFM Xd, Xn, #s, #63 (C6.2.487, C6.2.355);
; by a register: the amount check, then LSRV (C6.2.274) / ASRV (C6.2.21)
:BO_SHR
  ldr.x x10 sp 7
  cbnz.x x10 BR_I
  bl BO_SCHK
  movz.x x0 0x2400 0
  movk.x x0 0x9ac0 1 ; LSRV 64-bit, C6.2.274
  ldr.x x9 sp 6
  cmp.x.i x9 6
  b.lo BR1
  movz.x x0 0x2800 0
  movk.x x0 0x9ac0 1 ; ASRV 64-bit, C6.2.21
:BR1
  bl BO_W3
  b BO_RET
:BR_I
  movz.x x0 0xfc00 0
  movk.x x0 0xd340 1 ; UBFM Xd, Xn, #s, #63 = LSR, C6.2.487
  ldr.x x9 sp 6
  cmp.x.i x9 6
  b.lo BR2
  movz.x x0 0xfc00 0
  movk.x x0 0x9340 1 ; SBFM Xd, Xn, #s, #63 = ASR, C6.2.355
:BR2
  ldr.x x1 sp 5
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  ldr.x x1 sp 4
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 2
  orr.x x0 x0 x1
  bl EMIT
  b BO_RET
; comparisons: CMP Xa, #imm (C6.2.96) or CMP Xa, Xb (C6.2.97); the result is the flags and a condition
:BO_CMP
  ldr.x x10 sp 7
  cbnz.x x10 BC_I
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP (shifted register) = SUBS XZR, Xa, Xb, C6.2.97
  ldr.x x1 sp 5
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  b BC_E
:BC_I
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP (immediate) = SUBS XZR, Xa, #imm, C6.2.96
  ldr.x x1 sp 5
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
:BC_E
  ldr.x x1 sp 4
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x9 sp 1
  sub.x.i x9 x9 43
  add.x x9 x9 x9
  ldr.x x10 sp 6
  cmp.x.i x10 6
  b.lo BC1
  add.x.i x9 x9 1
:BC1
  adr x10 CONDS
  ldrb.r w1 x10 x9
  movz.x x0 4 0
  movz.x x2 1 0
  b BO_RET2
:BO_RET
  movz.x x0 1 0
  ldr.x x1 sp 2
  ldr.x x2 sp 6
:BO_RET2
  ldr.x.post x30 sp 80
  ret
; BO_W3: x0 fixed part; emit it | rd | ra << 5 | (imm12 << 10, or rb << 16), fields from the BINOP frame
:BO_W3
  ldr.x x1 sp 2
  orr.x x0 x0 x1
  ldr.x x1 sp 4
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 5
  ldr.x x2 sp 7
  cbnz.x x2 BW_I
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  b EMIT
:BW_I
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  b EMIT
; BO_FIX: after a 64-bit op on rd: u8/u16/u32 wrap (CANON), i8/i16/i32 overflow check (trap 1)
:BO_FIX
  ldr.x x1 sp 6
  ldr.x x0 sp 2
  cmp.x.i x1 6
  b.lo CANON
  cmp.x.i x1 9
  b.eq BF_X
  movz.x x2 1 0
  b CHKEXT
:BF_X
  ret
; BO_SCHK: shift amount in a register must be below the width: CMP Xb, #w (C6.2.96); B.HS trap 7
:BO_SCHK
  ldr.x x1 sp 6
  adr x9 TWID
  ldrb.r w1 x9 x1
  ldr.x x2 sp 5
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP Xb, #w, C6.2.96
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  ubfm.x x2 x2 59 58
  orr.x x0 x0 x2
  str.x.pre x30 sp -16
  bl EMIT
  movz.x x0 2 0
  movz.x x1 7 0
  bl TRAPC
  ldr.x.post x30 sp 16
  ret
; ======================================================================================================
; COND: read a condition (a bool value); emit a branch taken when it is false; x0 = that branch's address
:COND
  str.x.pre x30 sp -16
  movz.x x0 1 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x2 1
  b.ne E_TYPE
  cmp.x.i x0 4
  b.ne CO1
  movz.x x9 1 0
  eor.x x1 x1 x9
  movz.x x0 0x5400 1 ; B.cond with the opposite condition, C6.2.34
  orr.x x0 x0 x1
  b CO2
:CO1
  bl MATREG
  movz.x x9 0xb400 1 ; CBZ Xr, C6.2.78
  orr.x x0 x0 x9
:CO2
  mov.x x10 x20
  bl EMIT
  mov.x x0 x10
  ldr.x.post x30 sp 16
  ret
; MATREG: x0 kind, x1 number/register/condition, x2 type -> x0 a register holding the value
:MATREG
  cmp.x.i x0 1
  b.ne MR1
  mov.x x0 x1
  ret
:MR1
  str.x.pre x30 sp -32
  str.x x0 sp 1
  str.x x1 sp 2
  str.x x2 sp 3
  bl ALLOC
  mov.x x3 x0
  ldr.x x0 sp 1
  ldr.x x1 sp 2
  ldr.x x2 sp 3
  str.x x3 sp 1
  bl FORCE
  ldr.x x0 sp 1
  ldr.x.post x30 sp 32
  ret
; FORCE: x0 kind, x1 number/register/condition, x2 type, x3 register: put the value in that register
:FORCE
  str.x.pre x30 sp -16
  cmp.x.i x0 1
  b.eq FO_R
  cmp.x.i x0 3
  b.eq FO_C
  cmp.x.i x0 4
  b.ne E_BARE
  mov.x x0 x3
  bl CSET
  b FO_X
:FO_R
  cmp.x x1 x3
  b.eq FO_X
  movz.x x0 0x03e0 0
  movk.x x0 0xaa00 1 ; MOV Xd, Xm = ORR Xd, XZR, Xm, C6.2.282
  orr.x x0 x0 x3
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  b FO_X
:FO_C
  mov.x x0 x3
  bl MATCONST
:FO_X
  ldr.x.post x30 sp 16
  ret
; CSET Xd, cond = CSINC Xd, XZR, XZR, opposite cond (C6.2.141). x0 register, x1 condition
:CSET
  movz.x x9 0x07e0 0
  movk.x x9 0x9a9f 1 ; CSINC 64-bit, Rm = Rn = 31, C6.2.141
  orr.x x9 x9 x0
  movz.x x0 1 0
  eor.x x1 x1 x0
  ubfm.x x1 x1 52 51
  orr.x x0 x9 x1
  b EMIT
; MATCONST: x0 register, x1 64-bit value: MOVZ (C6.2.285) or, for a negative value, MOVN (C6.2.284) with
; the first halfword that differs from the fill, then MOVK (C6.2.283) for each other such halfword
:MATCONST
  str.x.pre x30 sp -16
  mov.x x3 x0
  mov.x x4 x1
  movz.x x5 0 0
  movz.x x6 0 0
  movz.x x7 0xd280 1 ; MOVZ 64-bit, C6.2.285
  movz.x x8 0 0
  cmp.x.i x4 0
  b.ge MC2
  movz.x x6 0xffff 0
  movz.x x7 0x9280 1 ; MOVN 64-bit, C6.2.284
:MC2
  ubfm.x x2 x5 60 59
  lsrv.x x1 x4 x2
  ubfm.x x1 x1 0 15
  cmp.x x1 x6
  b.eq MC4
  movz.x x0 0xf280 1 ; MOVK 64-bit, C6.2.283
  cbnz.x x8 MC3
  eor.x x1 x1 x6
  mov.x x0 x7
  movz.x x8 1 0
:MC3
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ubfm.x x2 x5 43 42
  orr.x x0 x0 x2
  orr.x x0 x0 x3
  bl EMIT
:MC4
  add.x.i x5 x5 1
  cmp.x.i x5 4
  b.lo MC2
  cbnz.x x8 MC5
  orr.x x0 x7 x3
  bl EMIT
:MC5
  ldr.x.post x30 sp 16
  ret
; CANON: x0 register, x1 type: u8/u16/u32 UBFM Xr, Xr, #0, #w-1 (C6.2.487); i8/i16/i32 SBFM (C6.2.355)
:CANON
  cmp.x.i x1 2
  b.lo CN_X
  cmp.x.i x1 5
  b.lo CN_U
  cmp.x.i x1 6
  b.lo CN_X
  cmp.x.i x1 9
  b.lo CN_S
:CN_X
  ret
:CN_U
  movz.x x9 0xd340 1 ; UBFM 64-bit, C6.2.487
  b CN_GO
:CN_S
  movz.x x9 0x9340 1 ; SBFM 64-bit, C6.2.355
:CN_GO
  adr x10 TWID
  ldrb.r w10 x10 x1
  sub.x.i x10 x10 1
  ubfm.x x10 x10 54 53
  orr.x x9 x9 x10
  orr.x x9 x9 x0
  ubfm.x x0 x0 59 58
  orr.x x0 x9 x0
  b EMIT
; CHKEXT: x0 register, x1 type (i8 i16 i32), x2 trap code: CMP Xr, Wr, SXTB/SXTH/SXTW (SUBS extended,
; option 100/101/110, C6.2.463), then B.NE trap
:CHKEXT
  sub.x.i x3 x1 2
  ubfm.x x3 x3 51 50
  movz.x x9 0x001f 0
  movk.x x9 0xeb20 1 ; SUBS (extended register) 64-bit, Rd 31 = CMP, C6.2.463
  orr.x x3 x3 x9
  ubfm.x x9 x0 48 47
  orr.x x3 x3 x9
  ubfm.x x9 x0 59 58
  orr.x x0 x3 x9
  str.x.pre x30 sp -16
  bl EMIT
  movz.x x0 1 0
  mov.x x1 x2
  bl TRAPC
  ldr.x.post x30 sp 16
  ret
; RANGE: x1 number, x2 type: error 8 unless the number is a value of that type
:RANGE
  adr x9 TWID
  ldrb.r w9 x9 x2
  cmp.x.i x9 64
  b.eq RG_64
  movz.x x10 1 0
  lslv.x x10 x10 x9
  cmp.x.i x2 6
  b.hs RG_S
  cmp.x x1 x10
  b.hs E_RANGE
  ret
:RG_S
  ubfm.x x10 x10 1 63
  add.x x11 x1 x10
  add.x x10 x10 x10
  cmp.x x11 x10
  b.hs E_RANGE
  ret
:RG_64
  cmp.x.i x2 9
  b.eq RG_OK
  cmp.x.i x1 0
  b.lt E_RANGE
:RG_OK
  ret
; ======================================================================================================
; TOK: next word. x0 kind (0 end of line, 1 word, 2 number, 3 end of input), x1 packed word (first 8
; characters, first in the low byte) or number, x2 length. Clobbers x0..x7.
:TOK
  ldrb w0 x19 0
  cmp.w.i w0 32
  b.eq TK_SKIP
  cmp.w.i w0 9
  b.eq TK_SKIP
  cmp.w.i w0 13
  b.eq TK_SKIP
  cbz.w w0 TK_EOF
  cmp.w.i w0 10
  b.ne TK_WORD
  add.x.i x19 x19 1
  add.x.i x22 x22 1
  movz.x x0 0 0
  ret
:TK_SKIP
  add.x.i x19 x19 1
  b TOK
:TK_EOF
  movz.x x0 3 0
  ret
:TK_WORD
  movz.x x1 0 0
  movz.x x2 0 0
  movz.x x3 0 0
:TK_CH
  ldrb w0 x19 0
  cmp.w.i w0 32
  b.ls TK_END
  cmp.x.i x2 8
  b.hs TK_LONG
  lslv.x x4 x0 x3
  orr.x x1 x1 x4
  add.x.i x3 x3 8
:TK_LONG
  add.x.i x2 x2 1
  add.x.i x19 x19 1
  b TK_CH
:TK_END
  ubfm.x x4 x1 0 7
  cmp.x.i x4 45
  b.eq TK_NUM
  sub.x.i x4 x4 48
  cmp.x.i x4 9
  b.ls TK_NUM
  movz.x x0 1 0
  ret
; a number: [-]decimal, at most 19 digits, inside the signed 64-bit range
:TK_NUM
  sub.x x5 x19 x2
  movz.x x6 0 0
  ldrb w0 x5 0
  cmp.w.i w0 45
  b.ne TK_N1
  movz.x x6 1 0
  add.x.i x5 x5 1
:TK_N1
  sub.x x0 x19 x5
  cbz.x x0 E_NUM
  cmp.x.i x0 19
  b.hi E_NUM
  movz.x x1 0 0
  movz.x x7 10 0
:TK_N2
  cmp.x x5 x19
  b.hs TK_N3
  ldrb w0 x5 0
  sub.x.i x0 x0 48
  cmp.x.i x0 9
  b.hi E_NUM
  mul.x x1 x1 x7
  add.x x1 x1 x0
  add.x.i x5 x5 1
  b TK_N2
:TK_N3
  movz.x x7 0x8000 3
  cbnz.x x6 TK_NEG
  cmp.x x1 x7
  b.hs E_RANGE
  movz.x x0 2 0
  ret
:TK_NEG
  cmp.x x1 x7
  b.hi E_RANGE
  sub.x x1 xzr x1
  movz.x x0 2 0
  ret
; KW: x1 packed word -> x0 keyword code (0 none). Clobbers x9..x11.
:KW
  adr x9 KWTAB
:KW1
  ldr.w w0 x9 2
  cbz.w w0 KW2
  ldr.x x10 x9 0
  cmp.x x10 x1
  b.eq KW2
  add.x.i x9 x9 16
  b KW1
:KW2
  ret
; TYPEWORD: read a type name -> x0 type 1..9
:TYPEWORD
  str.x.pre x30 sp -16
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_SYNTAX
  bl KW
  cmp.x.i x0 76
  b.eq E_SUBSET
  sub.x.i x9 x0 1
  cmp.x.i x9 8
  b.hi E_SYNTAX
  ldr.x.post x30 sp 16
  ret
; EOL: the line must end here
:EOL
  str.x.pre x30 sp -16
  bl TOK
  cbz.x x0 EOL1
  cmp.x.i x0 3
  b.ne E_EXTRA
:EOL1
  ldr.x.post x30 sp 16
  ret
; FINDNAME: x1 packed name -> x0 its entry (0 none). Clobbers x9..x11.
:FINDNAME
  movz.x x9 0x70 1
  movz.x x10 0 0
:FD1
  cmp.x x10 x23
  b.hs FD0
  ldr.x x11 x9 0
  cmp.x x11 x1
  b.eq FDF
  add.x.i x9 x9 16
  add.x.i x10 x10 1
  b FD1
:FD0
  movz.x x0 0 0
  ret
:FDF
  mov.x x0 x9
  ret
; ADDNAME: x1 packed name, x2 register, x3 type. Clobbers x9, x10.
:ADDNAME
  cmp.x.i x23 64
  b.hs E_FULL
  movz.x x9 0x70 1
  ubfm.x x10 x23 60 59
  add.x x9 x9 x10
  str.x x1 x9 0
  str.w w2 x9 2
  str.w w3 x9 3
  add.x.i x23 x23 1
  ret
; PUSHB: x0 branch to patch, x1 kind (1 if, 2 else, 3 while, 4 for), x2 head, x3 loop register,
; x4 its type, x5 names and x6 pool slots to restore at end
:PUSHB
  movz.x x9 0x72 1
  cmp.x x25 x9
  b.hs E_FULL
  str.x x1 x25 0
  str.x x0 x25 1
  str.x x2 x25 2
  str.x x3 x25 3
  str.x x4 x25 4
  str.x x5 x25 5
  str.x x6 x25 6
  add.x.i x25 x25 64
  ret
; ALLOC: -> x0 the next free register of the pool. Clobbers x9.
:ALLOC
  add.x x0 x29 x24
  adr x9 POOLEND
  cmp.x x0 x9
  b.hs E_REGS
  ldrb w0 x0 0
  add.x.i x24 x24 1
  ret
; DESTREG: x0 destination register, or 255 for a new temporary
:DESTREG
  cmp.x.i x0 255
  b.eq ALLOC
  ret
; EMIT: x0 word -> output. Clobbers x9.
:EMIT
  movz.x x9 0x70 1
  cmp.x x20 x9
  b.hs E_FULL
  str.w.post w0 x20 4
  ret
; E3: emit x0 | x1 | x2 << 5 | x3 << 16. Clobbers x0..x3, x9.
:E3
  orr.x x0 x0 x1
  ubfm.x x2 x2 59 58
  orr.x x0 x0 x2
  ubfm.x x3 x3 48 47
  orr.x x0 x0 x3
  b EMIT
; TRAPC: x0 condition, x1 trap code: B.cond (C6.2.34) to the trap stub, distance filled at the end
:TRAPC
  movz.x x9 0x5400 1
  orr.x x0 x0 x9
; TRAPSITE: x0 branch word, x1 trap code: note the place, emit the word
:TRAPSITE
  movz.x x9 0x73 1
  cmp.x x26 x9
  b.hs E_FULL
  str.w w20 x26 0
  str.w w1 x26 1
  add.x.i x26 x26 8
  b EMIT
; PATCH19: x0 address of a B.cond/CBZ/CBNZ, x1 target: the word distance into bits 5..23
:PATCH19
  sub.x x2 x1 x0
  ubfm.x x2 x2 2 20
  ubfm.x x2 x2 59 58
  ldr.w w1 x0 0
  orr.x x1 x1 x2
  str.w w1 x0 0
  ret
; PATCH26: x0 address of a B, x1 target: the word distance into bits 0..25
:PATCH26
  sub.x x2 x1 x0
  ubfm.x x2 x2 2 27
  ldr.w w1 x0 0
  orr.x x1 x1 x2
  str.w w1 x0 0
  ret
; BACK: x0 earlier address: emit B to it (C6.2.35)
:BACK
  sub.x x2 x0 x20
  ubfm.x x2 x2 2 27
  movz.x x0 0x1400 1 ; B, C6.2.35
  orr.x x0 x0 x2
  b EMIT
; ======================================================================================================
; errors
:E_READ
  movz.x x0 1 0
  b FAIL
:E_WRITE
  movz.x x0 2 0
  b FAIL
:E_UNDEF
  movz.x x0 3 0
  b FAIL
:E_FEW
  movz.x x0 4 0
  b FAIL
:E_EXTRA
  movz.x x0 5 0
  b FAIL
:E_NUM
  movz.x x0 6 0
  b FAIL
:E_TYPE
  movz.x x0 7 0
  b FAIL
:E_RANGE
  movz.x x0 8 0
  b FAIL
:E_SUBSET
  movz.x x0 9 0
  b FAIL
:E_TWICE
  movz.x x0 10 0
  b FAIL
:E_BARE
  movz.x x0 11 0
  b FAIL
:E_REGS
  movz.x x0 12 0
  b FAIL
:E_LONG
  movz.x x0 13 0
  b FAIL
:E_FULL
  movz.x x0 14 0
  b FAIL
:E_BLOCK
  movz.x x0 15 0
  b FAIL
:E_SYNTAX
  movz.x x0 16 0
; FAIL: x0 code: "om3: line N: error E" and a newline on stderr (fd 2), exit E
:FAIL
  mov.x x12 x0
  movz.x x9 0x73 1
  adr x10 MSG1
  bl COPYZ
  mov.x x0 x22
  bl PUTDEC
  adr x10 MSG2
  bl COPYZ
  mov.x x0 x12
  bl PUTDEC
  movz.x x0 10 0
  strb w0 x9 0
  add.x.i x9 x9 1
  movz.x x0 2 0
  movz.x x1 0x73 1
  sub.x x2 x9 x1
  movz.x x8 64 0
  svc 0
  mov.x x0 x12
  movz.x x8 93 0
  svc 0
; COPYZ: copy the NUL-ended text at x10 to x9 (x9 moves on)
:COPYZ
  ldrb w11 x10 0
  cbz.w w11 CZ1
  strb w11 x9 0
  add.x.i x9 x9 1
  add.x.i x10 x10 1
  b COPYZ
:CZ1
  ret
; PUTDEC: x0 number: its decimal digits at x9 (x9 moves on)
:PUTDEC
  movz.x x1 10 0
  add.x.i x2 x9 40
  mov.x x3 x2
:PD1
  udiv.x x4 x0 x1
  msub.x x5 x4 x1 x0
  add.x.i x5 x5 48
  sub.x.i x3 x3 1
  strb w5 x3 0
  mov.x x0 x4
  cbnz.x x0 PD1
:PD2
  ldrb w5 x3 0
  strb w5 x9 0
  add.x.i x9 x9 1
  add.x.i x3 x3 1
  cmp.x x3 x2
  b.lo PD2
  ret
