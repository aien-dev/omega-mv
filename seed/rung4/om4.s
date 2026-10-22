; om4.s -- rung 4 of the Omega seed chain: om3 (seed/rung3/om3.s) grown into the systems subset a compiler
; needs, written in asm2's language and built by asm2:
;   cat seed/rung2/table.s2 seed/rung3/table3.s2 seed/rung4/om4.s | seed/rung2/asm2.bin > om4.bin
; Use: om4.bin < program.omegese > out. Two kinds of output (docs/one_layer/OMEGESE.md section 9 has the words):
; - function mode (no `fn main`): the functions' words only, entry = word 0, to be called under the OSC-1
;   native rules (docs/osc/OSC-1-DESIGN.md section 7): parameters in x0..x5, result in x0 in canonical
;   form, OscRt * in x7, a trap calls OscRt.trap (offset 16) with x0 = rt, x1 = code. With more than one
;   function, word 0 is a B to the last one (calls go to earlier functions, so the entry is written last).
; - program mode (a line `fn main`): a whole AArch64 Linux program (ELF file header laid out by hand, no
;   linker). Its start code sets the stack to a fixed 8 MiB pool, calls main, and exits with main's value;
;   a trap exits with status 100 + code.
;
; What om4 adds to om3: many functions, calls (earlier functions, and a function calling itself in program
; mode), void functions and ret without a value; signed div, rem and shl; for with a value as count;
; true and false; local arrays (new, on the stack, only in a function's outermost block), array parameters
; (view / edit), global arrays (global, fixed pools in the program's memory image, no heap), text literals
; (text, read-only bytes with their length), get, put, len; read and write (Linux system calls on a
; checked part of an array) and exit; note lines. Every array carries its length; every index is checked
; (trap 3) unless it is a number, which is checked when om4 reads it.
; Semantics follow the C path's interpreter (src/compiler/osc_interp.c): unsigned ops wrap to the width,
; signed add/sub/mul/neg/div/shl trap 1 out of range, div/rem trap 2 on zero, a loop bound trap 4, a
; conversion out of range trap 5, a shift amount outside 0..width-1 trap 7; om4 adds trap 3 (index or
; system call range outside the array) and trap 6 (the stack pool is used up, program mode).
;
; Code generation (as om3): every name lives in its own register for its whole scope; temporaries are
; registers freed at the end of each statement. Registers handed out: x0..x6 not taken by parameters,
; x8..x15, x17 (never x7 = rt, x16 = scratch, x18, x19..x30; x28 = stack pool floor in program mode).
; A call saves every register in use and x30 on the stack, loads the arguments into x0..x5 from that save
; area, branches with BL, and loads the registers back; so a function needs no entry or exit code beyond
; giving back the stack of its local arrays.
;
; Every machine word om4 writes is built from a fixed part read from the encoding diagram of the Arm ARM
; DDI 0487 M.d section named beside it, ORed with register and number fields.
;
; om4's own registers: x18 input end, x19 input cursor, x20 output cursor, x21 output buffer start, x22 line
; number, x23 names in scope, x24 next free pool slot, x25 block stack top, x26 trap list end, x27 return
; type (0 outside a function, 10 void), x28 pool slots held by names, x29 pool start.
; Memory: input 0x800000 (under 8 MiB), output 0x1000000 (8 MiB: 176-byte file header, 1 entry word, then
; text bytes and functions), names 0x1800000 (32 bytes: packed name 8, register 4, type 4, length 4, kind
; 4, address 8), blocks 0x1810000 (128 bytes), trap list 0x1820000 (8 bytes: site, code), message
; 0x1830000, functions 0x1840000 (128 bytes: name 8, address 8, return type 4, parameter count 4, then per
; parameter 16 bytes: type 4, length 4, kind 4), state 0x1850000 (8-byte slots: 1 globals and texts in the
; name table, 2 functions, 4 stack bytes of local arrays, 5 the path has ended, 6 program mode, 7 global
; pool bytes, 9 last function, 10 value nesting depth).
; Name kinds: 0 scalar, 1 local array, 2 view parameter, 3 edit parameter, 4 global array, 5 text.
; Errors: "om4: line N: error E" on stderr, exit E: 1 read, 2 write, 3 unknown name, 4 line ends too
; early, 5 extra words, 6 bad number, 7 type mismatch, 8 number out of range for its type, 9 not in the
; subset, 10 name defined twice, 11 number with no type, 12 out of registers, 13 word over 8 characters,
; 14 table or buffer full, 15 block structure, 16 syntax, 17 a line after ret or exit in the same block,
; 18 a function that needs a value can reach its end, 19 one array passed twice in a call (aliasing),
; 20 writing through a read-only array, 21 a number index outside the array, 22 bad text literal,
; 23 array or pool too large.
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
  .w 0x000035a8 ; p_filesz = the size of om4.bin, 13736 bytes (asm2 has no size arithmetic; gate4.sh checks it)
  .w 0x00000000
  .w 0x01460000 ; p_memsz: up to 0x1860000
  .w 0x00000000
  .w 0x00001000
  .w 0x00000000
; ---- 0x400078: entry ----
  b START
  .w 0
; ---- keyword table, 16 bytes per entry: packed word (8), code (4), 0 (4); made with
; evidence/SEED-0/rung4-worksheet.sh. Codes: 1..9 types, 33..58 values, 64.. statements, 77 not in subset.
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
  .w 0x00746567
  .w 0x00000000
  .w 51
  .w 0
  .w 0x006e656c
  .w 0x00000000
  .w 52
  .w 0
  .w 0x64616572
  .w 0x00000000
  .w 53
  .w 0
  .w 0x74697277
  .w 0x00000065
  .w 54
  .w 0
  .w 0x65757274
  .w 0x00000000
  .w 55
  .w 0
  .w 0x736c6166
  .w 0x00000065
  .w 56
  .w 0
  .w 0x77656976
  .w 0x00000000
  .w 57
  .w 0
  .w 0x74696465
  .w 0x00000000
  .w 58
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
  .w 0x626f6c67
  .w 0x00006c61
  .w 78
  .w 0
  .w 0x74786574
  .w 0x00000000
  .w 79
  .w 0
  .w 0x00747570
  .w 0x00000000
  .w 80
  .w 0
  .w 0x65657266
  .w 0x00000000
  .w 81
  .w 0
  .w 0x74697865
  .w 0x00000000
  .w 82
  .w 0
  .w 0x65746f6e
  .w 0x00000000
  .w 83
  .w 0
  .w 0x0077656e
  .w 0x00000000
  .w 84
  .w 0
  .w 0x61657262
  .w 0x0000006b
  .w 77
  .w 0
  .w 0x70617274
  .w 0x00000000
  .w 77
  .w 0
  .w 0x006e776f
  .w 0x00000000
  .w 77
  .w 0
  .w 0
  .w 0
  .w 0
  .w 0
; ---- messages ("om4: line ", ": error "), each with its length ----
:MSG1
  .w 0x3a346d6f
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
; ---- the pool index of each register x0..x17 (255: not in the pool) ----
:REGIDX
  .b 0
  .b 1
  .b 2
  .b 3
  .b 4
  .b 5
  .b 6
  .b 255
  .b 7
  .b 8
  .b 9
  .b 10
  .b 11
  .b 12
  .b 13
  .b 14
  .b 255
  .b 15
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
; ---- log2 of the element size by type code 0..9 ----
:TLOG
  .b 0
  .b 0
  .b 0
  .b 1
  .b 2
  .b 3
  .b 0
  .b 1
  .b 2
  .b 3
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
  .b 0 ; pad to a word boundary (16 + 18 + 10 + 10 + 12 bytes of tables above)
  .b 0
; ---- load by element type 0..9, register offset scaled by the element size (LSL #log2 size; S = 1
; except for bytes): uchar LDRB C6.2.221, ushort LDRH C6.2.223, uint LDR (32-bit) C6.2.218, ulong and
; long LDR (64-bit) C6.2.218, byte LDRSB (64-bit) C6.2.225, short LDRSH (64-bit) C6.2.227, int LDRSW
; C6.2.230. Fixed part: option 011 (LSL), Rm Rn Rt zero.
:LDOP
  .w 0
  .w 0
  .w 0x38606800
  .w 0x78607800
  .w 0xb8607800
  .w 0xf8607800
  .w 0x38a06800
  .w 0x78a07800
  .w 0xb8a07800
  .w 0xf8607800
; ---- store by element type: STRB C6.2.418, STRH C6.2.420, STR (32-bit, 64-bit) C6.2.416 ----
:STOP
  .w 0
  .w 0
  .w 0x38206800
  .w 0x78207800
  .w 0xb8207800
  .w 0xf8207800
  .w 0x38206800
  .w 0x78207800
  .w 0xb8207800
  .w 0xf8207800
; ---- the file header om4 writes in program mode: ELF-64 header and two PT_LOAD program headers
; (the file at 0x400000 read/execute; the stack and global pools at 0x10000000 read/write, no file
; bytes). Words 24, 26 (file size) and 40 (pool size) are filled in at the end. ----
:ELFHDR
  .w 0x464c457f
  .w 0x00010102
  .w 0x00000000
  .w 0x00000000
  .w 0x00b70002
  .w 0x00000001
  .w 0x004000b0 ; e_entry: the word after the header
  .w 0x00000000
  .w 0x00000040
  .w 0x00000000
  .w 0x00000000
  .w 0x00000000
  .w 0x00000000
  .w 0x00380040
  .w 0x00000002 ; e_phnum 2
  .w 0x00000000
  .w 0x00000001 ; PT_LOAD
  .w 0x00000005 ; PF_R | PF_X
  .w 0x00000000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x00400000
  .w 0x00000000
  .w 0x00000000 ; p_filesz (filled in)
  .w 0x00000000
  .w 0x00000000 ; p_memsz (filled in)
  .w 0x00000000
  .w 0x00001000
  .w 0x00000000
  .w 0x00000001 ; PT_LOAD
  .w 0x00000006 ; PF_R | PF_W
  .w 0x00000000
  .w 0x00000000
  .w 0x10000000 ; the stack pool from 0x10000000, the global pools from 0x10800000
  .w 0x00000000
  .w 0x10000000
  .w 0x00000000
  .w 0x00000000 ; p_filesz 0
  .w 0x00000000
  .w 0x00000000 ; p_memsz (filled in: 8 MiB stack + global pools)
  .w 0x00000000
  .w 0x00001000
  .w 0x00000000
; ======================================================================================================
; start: read all of stdin to 0x800000 (at most 8 MiB - 1 bytes); x18 = its end (no end byte is written)
:START
  movz.x x19 0x80 1
  movz.x x12 0 0
:RD
  movz.x x0 0 0
  add.x x1 x19 x12
  movz.x x2 0x80 1
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
  add.x x18 x19 x12
  movz.x x9 0x185 1
  movz.x x10 0 0
:ST0
  str.x xzr x9 0
  add.x.i x9 x9 8
  add.x.i x10 x10 1
  cmp.x.i x10 16
  b.lo ST0
; ---- program mode when some line is `fn main` (blanks, fn, blanks, main, then a blank or the end) ----
  mov.x x10 x19
:PS_LINE
  cmp.x x10 x18
  b.hs PS_DONE
  ldrb w0 x10 0
  cmp.w.i w0 32
  b.eq PS_ADV
  cmp.w.i w0 9
  b.eq PS_ADV
  cmp.w.i w0 102
  b.ne PS_SKIP
  add.x.i x12 x10 1
  cmp.x x12 x18
  b.hs PS_DONE
  ldrb w0 x12 0
  cmp.w.i w0 110
  b.ne PS_SKIP
  add.x.i x12 x12 1
  mov.x x13 x12
:PS_B
  cmp.x x12 x18
  b.hs PS_DONE
  ldrb w0 x12 0
  cmp.w.i w0 32
  b.eq PS_B1
  cmp.w.i w0 9
  b.ne PS_M
:PS_B1
  add.x.i x12 x12 1
  b PS_B
:PS_M
  cmp.x x12 x13
  b.eq PS_SKIP
  add.x.i x13 x12 4
  cmp.x x13 x18
  b.hi PS_SKIP
  ldrb w0 x12 0
  cmp.w.i w0 109
  b.ne PS_SKIP
  ldrb w0 x12 1
  cmp.w.i w0 97
  b.ne PS_SKIP
  ldrb w0 x12 2
  cmp.w.i w0 105
  b.ne PS_SKIP
  ldrb w0 x12 3
  cmp.w.i w0 110
  b.ne PS_SKIP
  cmp.x x13 x18
  b.hs PS_YES
  ldrb w0 x13 0
  cmp.w.i w0 32
  b.hi PS_SKIP
:PS_YES
  movz.x x9 0x185 1
  movz.x x0 1 0
  str.x x0 x9 6
  b PS_DONE
:PS_ADV
  add.x.i x10 x10 1
  b PS_LINE
:PS_SKIP
  cmp.x x10 x18
  b.hs PS_DONE
  ldrb w0 x10 0
  add.x.i x10 x10 1
  cmp.w.i w0 10
  b.ne PS_SKIP
  b PS_LINE
:PS_DONE
  movz.x x21 0x100 1
  add.x.i x20 x21 180
  movz.x x22 1 0
  movz.x x23 0 0
  movz.x x24 0 0
  movz.x x25 0x181 1
  movz.x x26 0x182 1
  movz.x x27 0 0
  movz.x x28 0 0
  adr x29 POOLC
; ---- one statement per line ----
:STMT
  mov.x x24 x28
  bl TOK
  cbz.x x0 STMT
  cmp.x.i x0 3
  b.eq S_EOF
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi S_LONG
  mov.x x12 x1
  bl KW
  cmp.x.i x0 83
  b.eq S_NOTE
  cmp.x.i x0 64
  b.eq S_FN
  cbz.x x27 S_TOP
  cmp.x.i x0 66
  b.eq S_END
  cmp.x.i x0 68
  b.eq S_ELSE
  movz.x x9 0x185 1
  ldr.x x10 x9 5
  cbnz.x x10 E_DEAD
  cbz.x x0 S_NAME
  cmp.x.i x0 67
  b.eq S_IF
  cmp.x.i x0 69
  b.eq S_WHILE
  cmp.x.i x0 71
  b.eq S_FOR
  cmp.x.i x0 72
  b.eq S_RET
  cmp.x.i x0 80
  b.eq S_PUT
  cmp.x.i x0 81
  b.eq E_SUBSET ; free: no heap and nothing to give back (decision 24), so not a word
  cmp.x.i x0 82
  b.eq S_EXIT
  cmp.x.i x0 77
  b.eq E_SUBSET
  cmp.x.i x0 78
  b.eq E_SUBSET
  cmp.x.i x0 79
  b.eq E_SUBSET
  b E_SYNTAX
:S_TOP
  cmp.x.i x0 78
  b.eq S_GLOBAL
  cmp.x.i x0 79
  b.eq S_TEXT
  b E_SYNTAX
; ---- note ...: the rest of the line is not read ----
:S_NOTE
  cmp.x x19 x18
  b.hs STMT
  ldrb w0 x19 0
  cmp.w.i w0 10
  b.eq STMT
  add.x.i x19 x19 1
  b S_NOTE
; ---- a name at the start of a line: a call of a void function, or name [=] value ----
:S_NAME
  mov.x x1 x12
  bl FINDNAME
  cbnz.x x0 S_ASSIGN
  mov.x x1 x12
  bl FINDFN
  cbz.x x0 S_ASSIGN
  ldr.w w9 x0 4
  cmp.x.i x9 10
  b.ne E_TYPE
  movz.x x1 255 0
  bl CALL
  bl EOL
  b STMT
; ---- fn name (param type | param view T N | param edit T N)* to type | void ----
:S_FN
  cbnz.x x27 E_BLOCK
  bl TOK
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 63
  b.hi E_LONG
  mov.x x12 x1
  bl KW
  cbnz.x x0 E_SYNTAX
  mov.x x1 x12
  sub.x x3 x19 x2
  bl NEWNAME
  movz.x x9 0x185 1
  ldr.x x10 x9 2
  cmp.x.i x10 512
  b.hs E_FULL
  add.x.i x11 x10 1
  str.x x11 x9 2
  movz.x x11 0x184 1
  ubfm.x x10 x10 57 56
  add.x x11 x11 x10
  str.x x11 x9 3
  str.x x11 x9 9
  str.x x12 x11 0
  str.x x20 x11 1
  str.x xzr x11 2
  str.w w2 x11 30
  movz.x x16 0x80 1
  sub.x x16 x3 x16
  str.w w16 x11 31
  ldr.x x23 x9 1
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
  bl NEWNAME
  cmp.x.i x13 6
  b.hs E_SUBSET
  sub.x.i sp sp 48
  str.x x12 sp 0
  bl TOK
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_SYNTAX
  bl KW
  cmp.x.i x0 57
  b.eq FN_ARR
  cmp.x.i x0 58
  b.eq FN_ARR
  sub.x.i x9 x0 1
  cmp.x.i x9 8
  b.hi E_SYNTAX
  str.x x0 sp 1
  str.x xzr sp 2
  str.x xzr sp 3
  b FN_ADD
; an array parameter: view (kind 2) or edit (kind 3), element type, length (a number 1..2^24)
:FN_ARR
  sub.x.i x0 x0 55
  str.x x0 sp 3
  bl TYPEWORD
  bl ELEMTYPE
  str.x x0 sp 1
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 2
  b.ne E_SYNTAX
  bl ARRLEN
  str.x x1 sp 2
:FN_ADD
  adr x9 POOLC
  ldrb.r w2 x9 x13
  ldr.x x1 sp 0
  ldr.x x3 sp 1
  ldr.x x4 sp 2
  ldr.x x5 sp 3
  movz.x x6 0 0
  bl ADDNAME
  movz.x x9 0x185 1
  ldr.x x11 x9 3
  ubfm.x x10 x13 60 59
  add.x x11 x11 x10
  ldr.x x0 sp 1
  str.w w0 x11 6
  ldr.x x0 sp 2
  str.w w0 x11 7
  ldr.x x0 sp 3
  str.w w0 x11 8
  add.x.i sp sp 48
  add.x.i x13 x13 1
  b FN_P
:FN_TO
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_SYNTAX
  bl KW
  cmp.x.i x0 76
  b.eq FN_VOID
  sub.x.i x9 x0 1
  cmp.x.i x9 8
  b.hi E_SYNTAX
  mov.x x27 x0
  b FN_T2
:FN_VOID
  movz.x x27 10 0
:FN_T2
  movz.x x9 0x185 1
  ldr.x x11 x9 3
  str.w w27 x11 4
  str.w w13 x11 5
  str.x xzr x9 4
  str.x xzr x9 5
  adr x29 POOLC
  add.x x29 x29 x13
  movz.x x24 0 0
  movz.x x28 0 0
  movz.x x25 0x181 1
  movz.x x26 0x182 1
  bl EOL
  b STMT
; ---- name [=] value, or name = new T N v (a local array) ----
:S_ASSIGN
  sub.x.i sp sp 64
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
  ldr.w w9 x0 5
  cbnz.x x9 E_TYPE
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
  ldr.x x1 sp 0
  movz.x x2 0 0
  bl FINDFN
  cbnz.x x0 E_TWICE
  cmp.x.i x3 1
  b.ne SA2
  cmp.x.i x5 8
  b.hi SA2
  mov.x x1 x4
  bl KW
  cmp.x.i x0 84
  b.eq SA_ARR
:SA2
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
  movz.x x4 0 0
  movz.x x5 0 0
  movz.x x6 0 0
  bl ADDNAME
:SA_END
  add.x.i sp sp 64
  bl EOL
  b STMT
; x = new T N v: N elements of type T on the stack, each set to v; only in the function's outermost block,
; so the stack held by local arrays is known at every ret. SUB SP (C6.2.457); program mode: CMP SP, X28
; (SUBS extended, C6.2.463), B.LO trap 6; MOV Xb, SP (ADD immediate, C6.2.5); then the fill loop.
:SA_ARR
  movz.x x9 0x181 1
  cmp.x x25 x9
  b.ne E_SUBSET
  bl TYPEWORD
  bl ELEMTYPE
  str.x x0 sp 2
  bl TOK
  cmp.x.i x0 2
  b.ne E_SYNTAX
  bl ARRLEN
  str.x x1 sp 3
  ldr.x x2 sp 2
  adr x9 TLOG
  ldrb.r w9 x9 x2
  lslv.x x1 x1 x9
  add.x.i x1 x1 15
  ubfm.x x1 x1 4 63
  ubfm.x x1 x1 60 59
  str.x x1 sp 4
  movz.x x9 0x185 1
  ldr.x x10 x9 4
  add.x x10 x10 x1
  movz.x x11 0x40 1
  cmp.x x10 x11
  b.hi E_TOOBIG
  str.x x10 x9 4
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 1
  ldr.x x0 sp 2
  movz.x x1 255 0
  bl VAL
  ldr.x x9 sp 2
  cmp.x x2 x9
  b.ne E_TYPE
  bl MATREG
  str.x x0 sp 5
  ldr.x x1 sp 4
  bl SPSUB
  bl STKCHK
  movz.x x0 0x03e0 0
  movk.x x0 0x9100 1 ; MOV Xb, SP = ADD Xb, SP, #0, C6.2.5 / C6.2.278
  ldr.x x1 sp 1
  orr.x x0 x0 x1
  bl EMIT
  bl ALLOC
  str.x x0 sp 6
  movz.x x9 0xd280 1 ; MOVZ Xi, #0, C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  ldr.x x1 sp 3
  movz.x x9 4095 0
  cmp.x x1 x9
  b.ls SAA1
  bl ALLOC
  str.x x0 sp 7
  ldr.x x1 sp 3
  bl MATCONST
:SAA1
  str.x x20 sp 4
  ldr.x x9 sp 2
  adr x10 STOP
  ldr.w.r w0 x10 x9
  ldr.x x1 sp 5
  orr.x x0 x0 x1
  ldr.x x1 sp 1
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 6
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 0x0400 0
  movk.x x0 0x9100 1 ; ADD Xi, Xi, #1, C6.2.5
  ldr.x x1 sp 6
  orr.x x0 x0 x1
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x1 sp 3
  movz.x x9 4095 0
  cmp.x x1 x9
  b.hi SAA2
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP Xi, #N, C6.2.96
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  b SAA3
:SAA2
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xi, Xn, C6.2.97
  ldr.x x1 sp 7
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
:SAA3
  ldr.x x1 sp 6
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x0 sp 4
  sub.x x2 x0 x20
  ubfm.x x2 x2 2 20
  ubfm.x x2 x2 59 58
  movz.x x0 0x0003 0
  movk.x x0 0x5400 1 ; B.LO back to the store, C6.2.34
  orr.x x0 x0 x2
  bl EMIT
  ldr.x x1 sp 0
  ldr.x x2 sp 1
  ldr.x x3 sp 2
  ldr.x x4 sp 3
  movz.x x5 1 0
  movz.x x6 0 0
  bl ADDNAME
  b SA_END
; ---- ret [value]: the value into x0, give back the local arrays' stack, RET ----
:S_RET
  cmp.x.i x27 10
  b.eq SR_V
  mov.x x0 x27
  movz.x x1 0 0
  bl VAL
  cmp.x x2 x27
  b.ne E_TYPE
  movz.x x3 0 0
  bl FORCE
  bl EPI
  bl DEAD1
  bl EOL
  b STMT
:SR_V
  bl EOL
  bl EPI
  bl DEAD1
  b STMT
; ---- exit value (int): Linux exit (system call 93, C6.2.466 SVC); the path ends ----
:S_EXIT
  movz.x x0 8 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x2 8
  b.ne E_TYPE
  movz.x x3 0 0
  bl FORCE
  movz.x x0 0x0ba8 0
  movk.x x0 0xd280 1 ; MOVZ X8, #93, C6.2.285
  bl EMIT
  movz.x x0 0x0001 0
  movk.x x0 0xd400 1 ; SVC #0, C6.2.466
  bl EMIT
  bl DEAD1
  bl EOL
  b STMT
; ---- put A i v: index, then value, then the checked store ----
:S_PUT
  sub.x.i sp sp 64
  bl ARRNAME
  str.x x0 sp 0
  ldr.w w9 x0 5
  cmp.x.i x9 2
  b.eq E_RDONLY
  cmp.x.i x9 5
  b.eq E_RDONLY
  movz.x x0 5 0
  movz.x x1 255 0
  bl VAL
  str.x x0 sp 1
  str.x x1 sp 2
  str.x x2 sp 3
  ldr.x x9 sp 0
  ldr.w w0 x9 3
  movz.x x1 255 0
  bl VAL
  ldr.x x9 sp 0
  ldr.w w9 x9 3
  cmp.x x2 x9
  b.ne E_TYPE
  bl MATREG
  str.x x0 sp 4
  ldr.x x0 sp 0
  ldr.x x1 sp 1
  ldr.x x2 sp 2
  ldr.x x3 sp 3
  bl ELEMADDR
  ldr.x x9 sp 0
  ldr.w w9 x9 3
  adr x10 STOP
  ldr.w.r w2 x10 x9
  ldr.x x3 sp 4
  orr.x x2 x2 x3
  ubfm.x x0 x0 59 58
  orr.x x2 x2 x0
  ubfm.x x1 x1 48 47
  orr.x x0 x2 x1
  bl EMIT
  add.x.i sp sp 64
  bl EOL
  b STMT
; ---- global NAME T N: a fixed pool of N elements, zero at start, in the read/write part of the program
; image after the 8 MiB stack pool (address 0x10800000 + pool bytes so far); at most 1 GiB of pools ----
:S_GLOBAL
  bl TOPNAME
  bl TYPEWORD
  bl ELEMTYPE
  mov.x x13 x0
  bl TOK
  cmp.x.i x0 2
  b.ne E_SYNTAX
  bl ARRLEN
  mov.x x14 x1
  adr x9 TLOG
  ldrb.r w9 x9 x13
  lslv.x x1 x1 x9
  add.x.i x1 x1 15
  ubfm.x x1 x1 4 63
  ubfm.x x1 x1 60 59
  movz.x x9 0x185 1
  ldr.x x10 x9 7
  movz.x x6 0x1080 1
  add.x x6 x6 x10
  add.x x10 x10 x1
  movz.x x11 0x4000 1
  cmp.x x10 x11
  b.hi E_TOOBIG
  str.x x10 x9 7
  mov.x x1 x12
  movz.x x2 0 0
  mov.x x3 x13
  mov.x x4 x14
  movz.x x5 4 0
  bl ADDNAME
  movz.x x9 0x185 1
  str.x x23 x9 1
  bl EOL
  b STMT
; ---- text NAME "...": read-only bytes in the program image (escapes \n \t \\ \"), with their length ----
:S_TEXT
  bl TOPNAME
  bl TOK
  cmp.x.i x0 4
  b.ne E_SYNTAX
  sub.x x6 x20 x21
  movz.x x9 0x40 1
  add.x x6 x6 x9
  add.x x2 x1 x2
  movz.x x14 0 0
:TX1
  cmp.x x1 x2
  b.hs TX4
  ldrb w0 x1 0
  add.x.i x1 x1 1
  cmp.w.i w0 92
  b.ne TX3
  ldrb w0 x1 0
  add.x.i x1 x1 1
  cmp.w.i w0 110
  b.ne TXa
  movz.x x0 10 0
  b TX3
:TXa
  cmp.w.i w0 116
  b.ne TXb
  movz.x x0 9 0
  b TX3
:TXb
  cmp.w.i w0 92
  b.eq TX3
  cmp.w.i w0 34
  b.ne E_TEXT
:TX3
  bl EMITB
  add.x.i x14 x14 1
  b TX1
:TX4
  ubfm.x x9 x20 0 1
  cbz.x x9 TX5
  movz.x x0 0 0
  bl EMITB
  b TX4
:TX5
  mov.x x1 x12
  movz.x x2 0 0
  movz.x x3 2 0
  mov.x x4 x14
  movz.x x5 5 0
  bl ADDNAME
  movz.x x9 0x185 1
  str.x x23 x9 1
  bl EOL
  b STMT
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
  movz.x x9 0x181 1
  cmp.x x25 x9
  b.eq E_BLOCK
  sub.x.i x10 x25 128
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
  movz.x x9 0x185 1
  ldr.x x11 x9 5
  str.x x11 x10 7
  str.x xzr x9 5
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
; ---- for i T K: i = 0; head: not i < K -> exit; body; latch i = add i 1; B head. K a number, or a value
; of type T: a name is read at every pass, another value once before the loop; then the hidden counter
; guard of a while with no bound (trap 4 after 1000000 passes) ----
:S_FOR
  sub.x.i sp sp 80
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
  bl NEWNAME
  bl TYPEWORD
  str.x x0 sp 3
  cmp.x.i x0 1
  b.eq E_TYPE
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 2
  b.ne SF_VAL
  ldr.x x2 sp 3
  movz.x x9 0x185 1
  ldr.x x3 x9 11
  bl RANGE
  str.x x1 sp 4
:SF_NUM
  bl EOL
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 5
  mov.x x2 x0
  ldr.x x1 sp 2
  ldr.x x3 sp 3
  movz.x x4 0 0
  movz.x x5 0 0
  movz.x x6 0 0
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
  mov.x x1 x13
  b SF_CMPR
; a value count
:SF_VAL
  mov.x x3 x0
  mov.x x4 x1
  mov.x x5 x2
  ldr.x x0 sp 3
  movz.x x1 255 0
  bl VALTOK
  cmp.x.i x0 3
  b.eq SF_TYPED
  ldr.x x9 sp 3
  cmp.x x2 x9
  b.ne E_TYPE
  bl MATREG
  str.x x0 sp 7
  bl EOL
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 5
  mov.x x2 x0
  ldr.x x1 sp 2
  ldr.x x3 sp 3
  movz.x x4 0 0
  movz.x x5 0 0
  movz.x x6 0 0
  bl ADDNAME
  ldr.x x0 sp 5
  movz.x x9 0xd280 1 ; MOVZ Xi, #0, C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  bl ALLOC
  mov.x x28 x24
  str.x x0 sp 8
  movz.x x9 0xd280 1 ; MOVZ Xc, #0 (the hidden counter), C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  str.x x20 sp 6
  ldr.x x1 sp 7
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xi, Xn, C6.2.97
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  ldr.x x1 sp 5
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  bl SF_BR
  str.x x0 sp 9
  bl ALLOC
  mov.x x13 x0
  movz.x x1 0x4240 0
  movk.x x1 0x000f 1
  bl MATCONST
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xc, Xm (the counter against 1000000), C6.2.97
  ldr.x x1 sp 8
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ubfm.x x1 x13 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 2 0
  movz.x x1 4 0
  bl TRAPC
  movz.x x0 0x0400 0
  movk.x x0 0x9100 1 ; ADD Xc, Xc, #1, C6.2.5
  ldr.x x1 sp 8
  orr.x x0 x0 x1
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x0 sp 9
  b SF_PUSH
:SF_TYPED
  ldr.x x9 sp 3
  cmp.x x2 x9
  b.ne E_TYPE
  str.x x1 sp 4
  b SF_NUM
:SF_CMPR
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xi, Xk, C6.2.97
  ldr.x x9 sp 5
  ubfm.x x9 x9 59 58
  orr.x x0 x0 x9
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
:SF_B
  bl SF_BR
:SF_PUSH
  movz.x x1 4 0
  ldr.x x2 sp 6
  ldr.x x3 sp 5
  ldr.x x4 sp 3
  ldr.x x5 sp 0
  ldr.x x6 sp 1
  bl PUSHB
  add.x.i sp sp 80
  b STMT
; SF_BR: B.HS (unsigned) or B.GE (signed) to the loop exit, C6.2.34 (distance filled at end); x0 = its place
; (the loop type is the caller frame slot 3, at sp + 40 once this routine has pushed 16 bytes)
:SF_BR
  str.x.pre x30 sp -16
  mov.x x13 x20
  movz.x x0 0x5400 1 ; B.cond, C6.2.34
  ldr.x x9 sp 5
  movz.x x1 2 0
  cmp.x.i x9 6
  b.lo SFB1
  movz.x x1 10 0
:SFB1
  orr.x x0 x0 x1
  bl EMIT
  mov.x x0 x13
  ldr.x.post x30 sp 16
  ret
; ---- end: close the innermost block, or the function ----
:S_END
  movz.x x9 0x181 1
  cmp.x x25 x9
  b.eq FINISH
  sub.x.i x25 x25 128
  movz.x x9 0x185 1
  ldr.x x0 x25 0
  cmp.x.i x0 2
  b.eq SE_D
  str.x xzr x9 5
  b SE_D2
:SE_D
  ldr.x x10 x9 5
  ldr.x x11 x25 7
  and.x x10 x10 x11
  str.x x10 x9 5
:SE_D2
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
; ---- end of a function: a void function that can reach its end returns; trap stubs ----
; shared tail, function mode: MOV X0, X7; LDR X16, [X7, #16]; BR X16 (OscRt.trap); program mode:
; ADD X0, X1, #100; MOVZ X8, #93; SVC #0 (exit 100 + code). Per trap code used: MOVZ X1, #code; B tail.
:FINISH
  bl EOL
  movz.x x9 0x185 1
  ldr.x x10 x9 5
  cbnz.x x10 FI_0
  cmp.x.i x27 10
  b.ne E_NORET
  bl EPI
:FI_0
  movz.x x9 0x182 1
  cmp.x x26 x9
  b.eq FI_T
  mov.x x13 x20
  movz.x x9 0x185 1
  ldr.x x10 x9 6
  cbnz.x x10 FI_PT
  movz.x x0 0x03e0 0
  movk.x x0 0xaa07 1 ; MOV X0, X7 = ORR X0, XZR, X7, C6.2.282
  bl EMIT
  movz.x x0 0x08f0 0
  movk.x x0 0xf940 1 ; LDR X16, [X7, #16], unsigned offset imm12 = 2, C6.2.216
  bl EMIT
  movz.x x0 0x0200 0
  movk.x x0 0xd61f 1 ; BR X16, C6.2.46
  bl EMIT
  b FI_C0
:FI_PT
  movz.x x0 0x9020 0
  movk.x x0 0x9101 1 ; ADD X0, X1, #100, C6.2.5
  bl EMIT
  movz.x x0 0x0ba8 0
  movk.x x0 0xd280 1 ; MOVZ X8, #93, C6.2.285
  bl EMIT
  movz.x x0 0x0001 0
  movk.x x0 0xd400 1 ; SVC #0, C6.2.466
  bl EMIT
:FI_C0
  movz.x x14 1 0
:FI_C
  movz.x x15 0 0
  movz.x x12 0x182 1
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
  movz.x x27 0 0
  movz.x x9 0x185 1
  ldr.x x23 x9 1
  str.x xzr x9 3
  b STMT
; ---- end of input: write the output ----
:S_EOF
  cbnz.x x27 E_BLOCK
  movz.x x9 0x185 1
  ldr.x x10 x9 2
  cbz.x x10 E_SYNTAX
  ldr.x x11 x9 6
  cbnz.x x11 EOF_PROG
  ldr.x x11 x9 1
  cbnz.x x11 E_SUBSET
  add.x.i x12 x21 180
  cmp.x.i x10 1
  b.eq WRITEOUT
  ldr.x x0 x9 9
  ldr.x x0 x0 1
  add.x.i x12 x21 176
  sub.x x2 x0 x12
  ubfm.x x2 x2 2 27
  movz.x x3 0x1400 1 ; B to the last function, C6.2.35
  orr.x x3 x3 x2
  str.w w3 x12 0
  b WRITEOUT
; program mode: start code = MOVZ X28, #0x1000, LSL 16 (stack pool floor); MOVZ X16, #0x1080, LSL 16;
; MOV SP, X16 (ADD immediate, C6.2.5); BL main (C6.2.43); [MOVZ X0, #0 for a void main]; MOVZ X8, #93;
; SVC #0. The entry word after the header is a B to it.
:EOF_PROG
  movz.x x2 4 0
  movz.x x1 0x616d 0
  movk.x x1 0x6e69 1
  bl FINDFN
  cbz.x x0 E_SYNTAX
  ldr.w w9 x0 5
  cbnz.x x9 E_SUBSET
  mov.x x14 x0
  add.x.i x12 x21 176
  sub.x x2 x20 x12
  ubfm.x x2 x2 2 27
  movz.x x3 0x1400 1 ; B, C6.2.35
  orr.x x3 x3 x2
  str.w w3 x12 0
  movz.x x0 0x001c 0
  movk.x x0 0xd2a2 1 ; MOVZ X28, #0x1000, LSL #16, C6.2.285
  bl EMIT
  movz.x x0 0x1010 0
  movk.x x0 0xd2a2 1 ; MOVZ X16, #0x1080, LSL #16, C6.2.285
  bl EMIT
  movz.x x0 0x021f 0
  movk.x x0 0x9100 1 ; MOV SP, X16 = ADD SP, X16, #0, C6.2.5
  bl EMIT
  ldr.x x0 x14 1
  sub.x x2 x0 x20
  ubfm.x x2 x2 2 27
  movz.x x0 0x9400 1 ; BL main, C6.2.43
  orr.x x0 x0 x2
  bl EMIT
  ldr.w w9 x14 4
  cmp.x.i x9 10
  b.ne EP1
  movz.x x0 0xd280 1 ; MOVZ X0, #0, C6.2.285
  bl EMIT
:EP1
  movz.x x0 0x0ba8 0
  movk.x x0 0xd280 1 ; MOVZ X8, #93, C6.2.285
  bl EMIT
  movz.x x0 0x0001 0
  movk.x x0 0xd400 1 ; SVC #0, C6.2.466
  bl EMIT
  adr x10 ELFHDR
  mov.x x11 x21
  movz.x x12 0 0
:EP2
  ldr.w.post w0 x10 4
  str.w.post w0 x11 4
  add.x.i x12 x12 1
  cmp.x.i x12 44
  b.lo EP2
  sub.x x0 x20 x21
  str.w w0 x21 24
  str.w w0 x21 26
  movz.x x9 0x185 1
  ldr.x x0 x9 7
  movz.x x1 0x80 1
  add.x x0 x0 x1
  str.w w0 x21 40
  mov.x x12 x21
:WRITEOUT
  sub.x x2 x20 x12
  cbz.x x2 EXIT0
  movz.x x0 1 0
  mov.x x1 x12
  movz.x x8 64 0
  svc 0
  cmp.x.i x0 0
  b.le E_WRITE
  add.x x12 x12 x0
  b WRITEOUT
:EXIT0
  movz.x x0 0 0
  movz.x x8 93 0
  svc 0
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
  str.x.pre x30 sp -128
  str.x x0 sp 1
  str.x x1 sp 2
  movz.x x9 0x185 1 ; nesting depth (state slot 10): at most 1024 values inside one another, else error 14
  ldr.x x10 x9 10
  add.x.i x10 x10 1
  str.x x10 x9 10
  cmp.x.i x10 1024
  b.hi E_FULL
  cbz.x x3 E_FEW
  cmp.x.i x3 3
  b.eq E_FEW
  cmp.x.i x3 4
  b.eq E_SYNTAX
  cmp.x.i x3 2
  b.ne VT_W
  mov.x x1 x4
  movz.x x9 0x185 1 ; a bare number: x2 = 1 when written as 2^63 or more (RANGE needs it later)
  ldr.x x2 x9 11
  movz.x x0 2 0
  ldr.x x9 sp 1
  cbz.x x9 VT_RET
  mov.x x3 x2
  mov.x x2 x9
  bl RANGE
  movz.x x0 3 0
  b VT_RET
:VT_W
  cmp.x.i x5 8
  b.hi VT_LONG
  mov.x x1 x4
  bl KW
  cbnz.x x0 VT_K
  str.x x1 sp 4
  bl FINDNAME
  cbz.x x0 VT_FN
  ldr.w w9 x0 5
  cbnz.x x9 E_TYPE
  ldr.w w1 x0 2
  ldr.w w2 x0 3
  movz.x x0 1 0
  b VT_RET
; a call of a function that gives a value
:VT_FN
  ldr.x x1 sp 4
  movz.x x2 0 0
  bl FINDFN
  cbz.x x0 E_UNDEF
:VT_FN2
  ldr.w w9 x0 4
  cmp.x.i x9 10
  b.eq E_TYPE
  ldr.x x1 sp 2
  bl CALL
  b VT_RET
:VT_K
  sub.x.i x9 x0 1
  cmp.x.i x9 8
  b.ls VT_TYPE
  cmp.x.i x0 74
  b.eq VT_PAREN
  cmp.x.i x0 77
  b.eq E_SUBSET
  str.x x0 sp 3
  cmp.x.i x0 51
  b.eq VT_GET
  cmp.x.i x0 52
  b.eq VT_LEN
  cmp.x.i x0 53
  b.eq VT_SYS
  cmp.x.i x0 54
  b.eq VT_SYS
  cmp.x.i x0 55
  b.eq VT_TRUE
  cmp.x.i x0 56
  b.eq VT_FALSE
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
  ldr.x x3 sp 6
  bl RANGE
  str.x x2 sp 6
  movz.x x0 3 0
  str.x x0 sp 4
:VB3
  cmp.x.i x11 2
  b.ne VB4
  ldr.x x1 sp 8
  ldr.x x2 sp 6
  ldr.x x3 sp 9
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
  movz.x x9 0x185 1
  ldr.x x10 x9 10
  sub.x.i x10 x10 1
  str.x x10 x9 10
  ldr.x.post x30 sp 128
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
  movz.x x9 0x185 1 ; x3 = the number was written as 2^63 or more
  ldr.x x3 x9 11
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
  b.hs BO_SDIV
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
  cmp.x.i x9 9
  b.eq BS_64
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
; RANGE: x1 number, x2 type, x3 1 when the number was written as 2^63 or more (TK_NUM, state slot 11):
; error 8 unless the number is a value of that type
:RANGE
  adr x9 TWID
  ldrb.r w9 x9 x2
  cmp.x.i x9 64
  b.eq RG_64
  cbnz.x x3 E_RANGE
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
  b.eq RG_L
  cbnz.x x3 RG_OK
  cmp.x.i x1 0
  b.lt E_RANGE
:RG_OK
  ret
:RG_L
  cbnz.x x3 E_RANGE
  ret
; ======================================================================================================
; TOK: next word. x0 kind (0 end of line, 1 word, 2 number, 3 end of input), x1 packed word (first 8
; characters, first in the low byte) or number, x2 length. Clobbers x0..x7.
:TOK
  cmp.x x19 x18
  b.hs TK_EOF
  ldrb w0 x19 0
  cmp.w.i w0 32
  b.eq TK_SKIP
  cmp.w.i w0 9
  b.eq TK_SKIP
  cmp.w.i w0 13
  b.eq TK_SKIP
  cmp.w.i w0 10
  b.eq TK_NL
  cmp.w.i w0 34
  b.eq TK_TEXT
  cmp.w.i w0 32
  b.lo E_SYNTAX
  b TK_WORD
; a text literal: kind 4, x1 its first byte, x2 its length in the source (escapes not yet read); it ends
; at the next " that is not after a \, on the same line, and is followed by a blank, a line end or the end
:TK_TEXT
  add.x.i x19 x19 1
  mov.x x1 x19
:TT1
  cmp.x x19 x18
  b.hs E_TEXT
  ldrb w0 x19 0
  cmp.w.i w0 10
  b.eq E_TEXT
  cmp.w.i w0 34
  b.eq TT_END
  add.x.i x19 x19 1
  cmp.w.i w0 92
  b.ne TT1
  cmp.x x19 x18
  b.hs E_TEXT
  ldrb w0 x19 0
  cmp.w.i w0 10
  b.eq E_TEXT
  add.x.i x19 x19 1
  b TT1
:TT_END
  sub.x x2 x19 x1
  add.x.i x19 x19 1
  movz.x x0 4 0
  cmp.x x19 x18
  b.hs TT_OK
  ldrb w3 x19 0
  cmp.w.i w3 32
  b.hi E_TEXT
:TT_OK
  ret
:TK_NL
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
  cmp.x x19 x18
  b.hs TK_END
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
; a number: [-]decimal, at most 20 digits, a value of some whole-number type: -2^63 .. 2^64 - 1, error 8
; outside. State slot 11 = 1 when it is 2^63 or more (only ulong holds such a number), else 0.
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
  cmp.x.i x0 20
  b.hi E_NUM
  movz.x x1 0 0
  movz.x x7 10 0
  movz.x x3 0x9999 0 ; x3 = 0x1999999999999999 = (2^64 - 1) / 10 rounded down: x1 * 10 + digit fits
  movk.x x3 0x9999 1 ; unless x1 > x3, or x1 = x3 and digit > 5
  movk.x x3 0x9999 2
  movk.x x3 0x1999 3
:TK_N2
  cmp.x x5 x19
  b.hs TK_N3
  ldrb w0 x5 0
  sub.x.i x0 x0 48
  cmp.x.i x0 9
  b.hi E_NUM
  cmp.x x1 x3
  b.hi E_RANGE
  b.lo TK_N2A
  cmp.x.i x0 5
  b.hi E_RANGE
:TK_N2A
  mul.x x1 x1 x7
  add.x x1 x1 x0
  add.x.i x5 x5 1
  b TK_N2
:TK_N3
  movz.x x7 0x8000 3
  movz.x x4 0x185 1
  str.x xzr x4 11
  cbnz.x x6 TK_NEG
  cmp.x x1 x7
  b.lo TK_N4
  movz.x x3 1 0
  str.x x3 x4 11
:TK_N4
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
  movz.x x9 0x180 1
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
  movz.x x9 0x183 1
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
  b FAIL
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
; div, rem (signed): CBZ Xb -> trap 2 unless b is a number other than 0; SDIV (C6.2.357); i64 div also
; traps 1 for MIN / -1: CMN Xb, #1 (ADDS immediate, C6.2.10); B.NE over the next two (C6.2.34);
; NEGS XZR, Xa (SUBS shifted register, C6.2.465) sets V only for MIN; B.VS trap 1. i8/i16/i32: the
; quotient is exact in 64 bits, then the sign-extension check (trap 1). rem: t = a / b; rd = a - t * b
; (MSUB, C6.2.291), always in range (MIN rem -1 = 0).
:BO_SDIV
  ldr.x x10 sp 3
  ldr.x x0 x10 3
  cmp.x.i x0 3
  b.ne SD1
  ldr.x x0 x10 4
  cbnz.x x0 SD2
:SD1
  ldr.x x0 sp 5
  movz.x x9 0xb400 1 ; CBZ Xb, C6.2.78
  orr.x x0 x0 x9
  movz.x x1 2 0
  bl TRAPSITE
:SD2
  ldr.x x9 sp 1
  cmp.x.i x9 37
  b.eq SD_REM
  ldr.x x9 sp 6
  cmp.x.i x9 9
  b.ne SD4
  ldr.x x10 sp 3
  ldr.x x0 x10 3
  cmp.x.i x0 3
  b.ne SD3
  ldr.x x0 x10 4
  cmn.x.i x0 1
  b.ne SD4
:SD3
  movz.x x0 0x041f 0
  movk.x x0 0xb100 1 ; CMN Xb, #1 = ADDS XZR, Xb, #1, C6.2.10
  ldr.x x1 sp 5
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 0x0061 0
  movk.x x0 0x5400 1 ; B.NE +3 words, C6.2.34
  bl EMIT
  movz.x x0 0x03ff 0
  movk.x x0 0xeb00 1 ; NEGS XZR, Xa = SUBS XZR, XZR, Xa, C6.2.465
  ldr.x x1 sp 4
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 6 0
  movz.x x1 1 0
  bl TRAPC
:SD4
  movz.x x0 0x0c00 0
  movk.x x0 0x9ac0 1 ; SDIV 64-bit, C6.2.357
  bl BO_W3
  ldr.x x9 sp 6
  cmp.x.i x9 9
  b.eq BO_RET
  bl BO_FIX
  b BO_RET
:SD_REM
  bl ALLOC
  str.x x0 sp 8
  movz.x x0 0x0c00 0
  movk.x x0 0x9ac0 1 ; SDIV, C6.2.357
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
; shl (i64): t = a << s (LSLV C6.2.271, or UBFM C6.2.487 for a number); X16 = t >> s arithmetic (ASRV
; C6.2.21, or SBFM C6.2.355); CMP X16, Xa (C6.2.97); B.NE trap 1 (a bit left the value); MOV Xd, Xt.
; (i8/i16/i32 shift in 64 bits exactly and take the sign-extension check of BO_FIX, as unsigned types
; take their wrap.)
:BS_64
  bl ALLOC
  str.x x0 sp 8
  ldr.x x10 sp 7
  cbnz.x x10 BS64_I
  bl BO_SCHK
  movz.x x0 0x2000 0
  movk.x x0 0x9ac0 1 ; LSLV Xt, Xa, Xb, C6.2.271
  ldr.x x1 sp 8
  ldr.x x2 sp 4
  ldr.x x3 sp 5
  bl E3
  movz.x x0 0x2810 0
  movk.x x0 0x9ac0 1 ; ASRV X16, Xt, Xb, C6.2.21
  movz.x x1 0 0
  ldr.x x2 sp 8
  ldr.x x3 sp 5
  bl E3
  b BS64_C
:BS64_I
  ldr.x x1 sp 5
  movz.x x2 64 0
  sub.x x2 x2 x1
  ubfm.x x2 x2 0 5
  movz.x x3 63 0
  sub.x x3 x3 x1
  movz.x x0 0xd340 1 ; UBFM Xt, Xa, #(64-s) mod 64, #63-s = LSL, C6.2.487
  ubfm.x x2 x2 48 47
  orr.x x0 x0 x2
  ubfm.x x3 x3 54 53
  orr.x x0 x0 x3
  ldr.x x1 sp 4
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 8
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 0xfc10 0
  movk.x x0 0x9340 1 ; SBFM X16, Xt, #s, #63 = ASR, C6.2.355
  ldr.x x1 sp 5
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  ldr.x x1 sp 8
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
:BS64_C
  movz.x x0 0x021f 0
  movk.x x0 0xeb00 1 ; CMP X16, Xa = SUBS XZR, X16, Xa, C6.2.97
  ldr.x x1 sp 4
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 1 0
  movz.x x1 1 0
  bl TRAPC
  movz.x x0 0x03e0 0
  movk.x x0 0xaa00 1 ; MOV Xd, Xt = ORR Xd, XZR, Xt, C6.2.282
  ldr.x x1 sp 2
  orr.x x0 x0 x1
  ldr.x x1 sp 8
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  b BO_RET
; ======================================================================================================
; values new in om4 (inside VALTOK's frame: 0 x30, 1 expected, 2 dest, 3 op, 4.. free)
; true, false: bool numbers
:VT_TRUE
  movz.x x0 3 0
  movz.x x1 1 0
  movz.x x2 1 0
  b VT_RET
:VT_FALSE
  movz.x x0 3 0
  movz.x x1 0 0
  movz.x x2 1 0
  b VT_RET
; len A: the length of A, a ulong number
:VT_LEN
  bl ARRNAME
  ldr.w w1 x0 4
  movz.x x0 3 0
  movz.x x2 5 0
  b VT_RET
; get A i: the checked load (the load of the element type, register offset, LDOP table)
:VT_GET
  bl ARRNAME
  str.x x0 sp 4
  movz.x x0 5 0
  movz.x x1 255 0
  bl VAL
  mov.x x3 x2
  mov.x x2 x1
  mov.x x1 x0
  ldr.x x0 sp 4
  bl ELEMADDR
  str.x x0 sp 5
  str.x x1 sp 6
  ldr.x x0 sp 2
  bl DESTREG
  str.x x0 sp 7
  ldr.x x9 sp 4
  ldr.w w9 x9 3
  adr x10 LDOP
  ldr.w.r w0 x10 x9
  ldr.x x1 sp 7
  orr.x x0 x0 x1
  ldr.x x1 sp 5
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 6
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 1 0
  ldr.x x1 sp 7
  ldr.x x9 sp 4
  ldr.w w2 x9 3
  b VT_RET
; read fd A off n / write fd A off n: a long (bytes moved, or minus the Linux error number). fd int, off
; and n ulong; n <= len A and off <= len A - n, else trap 3 (CMP C6.2.97, B.HI C6.2.34, SUB C6.2.458).
; Then as a call: save the registers in use, X0 = fd, X1 = A + off (ADD through X16, C6.2.6), X2 = n,
; X8 = 63 (read) or 64 (write), SVC #0 (C6.2.466), the result from X0, the registers back.
; Frame: 4/5 fd, 6 A, 7/8 off, 9/10 n, 11 t, 12 saved count, 13 save bytes, 14 pool slot before.
:VT_SYS
  str.x x24 sp 14
  movz.x x0 8 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x2 8
  b.ne E_TYPE
  str.x x0 sp 4
  str.x x1 sp 5
  bl ARRNAME
  str.x x0 sp 6
  ldr.x x9 sp 3
  cmp.x.i x9 53
  b.ne VS1
  ldr.w w9 x0 5
  cmp.x.i x9 2
  b.eq E_RDONLY
  cmp.x.i x9 5
  b.eq E_RDONLY
:VS1
  movz.x x0 5 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x2 5
  b.ne E_TYPE
  bl MATREG
  str.x x0 sp 8
  movz.x x0 5 0
  movz.x x1 255 0
  bl VAL
  cmp.x.i x2 5
  b.ne E_TYPE
  bl MATREG
  str.x x0 sp 10
  bl ALLOC
  str.x x0 sp 11
  ldr.x x9 sp 6
  ldr.w w1 x9 4
  bl MATCONST
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xn, Xt, C6.2.97
  ldr.x x1 sp 10
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 11
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 8 0
  movz.x x1 3 0
  bl TRAPC
  movz.x x0 0 0
  movk.x x0 0xcb00 1 ; SUB Xt, Xt, Xn, C6.2.458
  ldr.x x1 sp 11
  ldr.x x2 sp 11
  ldr.x x3 sp 10
  bl E3
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xoff, Xt, C6.2.97
  ldr.x x1 sp 8
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  ldr.x x1 sp 11
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 8 0
  movz.x x1 3 0
  bl TRAPC
  bl SAVE
  str.x x0 sp 12
  str.x x1 sp 13
  movz.x x0 0 0
  ldr.x x1 sp 4
  ldr.x x2 sp 5
  bl LOADARG
  movz.x x0 1 0
  ldr.x x9 sp 6
  bl BASEARG
  movz.x x0 16 0
  movz.x x1 1 0
  ldr.x x2 sp 8
  bl LOADARG
  movz.x x0 0x0021 0
  movk.x x0 0x8b10 1 ; ADD X1, X1, X16, C6.2.6
  bl EMIT
  movz.x x0 2 0
  movz.x x1 1 0
  ldr.x x2 sp 10
  bl LOADARG
  ldr.x x9 sp 3
  add.x.i x9 x9 10
  ubfm.x x9 x9 59 58
  movz.x x0 0x0008 0
  movk.x x0 0xd280 1 ; MOVZ X8, #63 or #64, C6.2.285
  orr.x x0 x0 x9
  bl EMIT
  movz.x x0 0x0001 0
  movk.x x0 0xd400 1 ; SVC #0, C6.2.466
  bl EMIT
  ldr.x x0 sp 2
  ldr.x x1 sp 14
  ldr.x x2 sp 12
  ldr.x x3 sp 13
  movz.x x4 1 0
  bl RESTORE
  mov.x x1 x0
  movz.x x0 1 0
  movz.x x2 9 0
  b VT_RET
; ======================================================================================================
; CALL: x0 function entry, x1 destination (255 none). Reads the arguments, then: SAVE; each argument into
; X0..X5 from the save area (LOADARG); BL (C6.2.43); RESTORE. Out: x0 1, x1 result register, x2 type.
; Frame: 0 x30, 1 entry, 2 dest, 3 pool slot before, 4 i, 5 parameter count, 6.. 3 per argument (kind,
; register/number, name entry of an array), 24 saved count, 25 save bytes.
:CALL
  sub.x.i sp sp 224
  str.x x30 sp 0
  str.x x0 sp 1
  str.x x1 sp 2
  str.x x24 sp 3
  str.x xzr sp 4
  ldr.w w9 x0 5
  str.x x9 sp 5
  movz.x x9 0x185 1
  ldr.x x10 x9 6
  cbnz.x x10 CA_ARG
  ldr.x x10 x9 3
  cmp.x x10 x0
  b.eq E_SUBSET
:CA_ARG
  ldr.x x9 sp 4
  ldr.x x10 sp 5
  cmp.x x9 x10
  b.hs CA_DONE
  ldr.x x11 sp 1
  ubfm.x x12 x9 60 59
  add.x x11 x11 x12
  ldr.w w12 x11 8
  cbnz.x x12 CA_ARR
  ldr.w w0 x11 6
  movz.x x1 255 0
  bl VAL
  cmp.x.i x0 4
  b.ne CA1
  bl MATREG
  mov.x x1 x0
  movz.x x0 1 0
  movz.x x2 1 0
:CA1
  ldr.x x9 sp 4
  ldr.x x11 sp 1
  ubfm.x x12 x9 60 59
  add.x x11 x11 x12
  ldr.w w12 x11 6
  cmp.x x2 x12
  b.ne E_TYPE
  movz.x x3 0 0
  b CA_PUT
:CA_ARR
  bl ARRARG
  mov.x x3 x2
  ldr.x x9 sp 4
  movz.x x10 0 0
:CA2
  cmp.x x10 x9
  b.hs CA_PUT
  movz.x x11 3 0
  mul.x x11 x11 x10
  add.x.i x11 x11 8
  ldr.x.r x12 sp x11
  cmp.x x12 x3
  b.eq E_ALIAS
  add.x.i x10 x10 1
  b CA2
:CA_PUT
  ldr.x x9 sp 4
  movz.x x11 3 0
  mul.x x11 x11 x9
  add.x.i x11 x11 6
  str.x.r x0 sp x11
  add.x.i x11 x11 1
  str.x.r x1 sp x11
  add.x.i x11 x11 1
  str.x.r x3 sp x11
  add.x.i x9 x9 1
  str.x x9 sp 4
  b CA_ARG
:CA_DONE
  bl SAVE
  str.x x0 sp 24
  str.x x1 sp 25
  movz.x x13 0 0
:CA3
  ldr.x x10 sp 5
  cmp.x x13 x10
  b.hs CA4
  movz.x x11 3 0
  mul.x x11 x11 x13
  add.x.i x11 x11 6
  ldr.x.r x1 sp x11
  add.x.i x11 x11 1
  ldr.x.r x2 sp x11
  mov.x x0 x13
  bl LOADARG
  add.x.i x13 x13 1
  b CA3
:CA4
  ldr.x x9 sp 1
  ldr.x x0 x9 1
  sub.x x2 x0 x20
  ubfm.x x2 x2 2 27
  movz.x x0 0x9400 1 ; BL, C6.2.43
  orr.x x0 x0 x2
  bl EMIT
  ldr.x x9 sp 1
  ldr.w w9 x9 4
  movz.x x4 1 0
  cmp.x.i x9 10
  b.ne CA5
  movz.x x4 0 0
:CA5
  ldr.x x0 sp 2
  ldr.x x1 sp 3
  ldr.x x2 sp 24
  ldr.x x3 sp 25
  bl RESTORE
  mov.x x1 x0
  movz.x x0 1 0
  ldr.x x9 sp 1
  ldr.w w2 x9 4
  ldr.x x30 sp 0
  add.x.i sp sp 224
  ret
; ARRARG: an array argument for the parameter at x11 (type +24, length +28, kind +32): [view | edit] NAME.
; Its type and length must equal the parameter's; an edit parameter needs a writable array (local or edit,
; error 20); a global is never passed (error 19: only so is every pair of live references disjoint).
; Out: x0 5 (register x1) or 6 (address x1, a text), x2 the name entry.
:ARRARG
  str.x.pre x30 sp -32
  str.x x11 sp 1
  str.x xzr sp 2
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  str.x x1 sp 3
  bl KW
  cbz.x x0 AA1
  cmp.x.i x0 57
  b.lo E_SYNTAX
  cmp.x.i x0 58
  b.hi E_SYNTAX
  str.x x0 sp 2
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  str.x x1 sp 3
:AA1
  ldr.x x1 sp 3
  bl FINDNAME
  cbz.x x0 E_UNDEF
  ldr.w w9 x0 5
  cbz.x x9 E_TYPE
  cmp.x.i x9 4
  b.eq E_ALIAS
  ldr.x x11 sp 1
  ldr.w w10 x11 8
  cmp.x.i x10 3
  b.ne AA2
  ldr.x x12 sp 2
  cmp.x.i x12 57
  b.eq E_RDONLY ; view A to an edit parameter: passing a view as edit is error 20 (OMEGESE.md 9.4)
  cmp.x.i x9 1
  b.eq AA3
  cmp.x.i x9 3
  b.ne E_RDONLY
  b AA3
:AA2
  ldr.x x12 sp 2
  cmp.x.i x12 58
  b.eq E_TYPE
:AA3
  ldr.w w12 x0 3
  ldr.w w13 x11 6
  cmp.x x12 x13
  b.ne E_TYPE
  ldr.w w12 x0 4
  ldr.w w13 x11 7
  cmp.x x12 x13
  b.ne E_TYPE
  mov.x x2 x0
  cmp.x.i x9 5
  b.eq AA4
  ldr.w w1 x0 2
  movz.x x0 5 0
  b AA5
:AA4
  ldr.x x1 x0 3
  movz.x x0 6 0
:AA5
  ldr.x.post x30 sp 32
  ret
; SAVE: n = registers in use (parameters + pool slots), A = 8 (n + 1) rounded up to 16: SUB SP, SP, #A
; (C6.2.457); program mode: CMP SP, X28 (C6.2.463), B.LO trap 6; STR Xr, [SP, #8j] for each (C6.2.415,
; unsigned offset); STR X30, [SP, #8n]. Out: x0 n, x1 A.
:SAVE
  str.x.pre x30 sp -32
  adr x9 POOLC
  sub.x x10 x29 x9
  add.x x10 x10 x24
  str.x x10 sp 1
  add.x.i x11 x10 2
  ubfm.x x11 x11 1 63
  ubfm.x x11 x11 60 59
  str.x x11 sp 2
  mov.x x1 x11
  bl SPSUB
  bl STKCHK
  movz.x x13 0 0
:SV1
  ldr.x x10 sp 1
  cmp.x x13 x10
  b.hs SV2
  adr x9 POOLC
  ldrb.r w0 x9 x13
  movz.x x9 0x03e0 0
  movk.x x9 0xf900 1 ; STR Xr, [SP, #8j], C6.2.415
  orr.x x0 x0 x9
  ubfm.x x1 x13 54 53
  orr.x x0 x0 x1
  bl EMIT
  add.x.i x13 x13 1
  b SV1
:SV2
  movz.x x0 0x03fe 0
  movk.x x0 0xf900 1 ; STR X30, [SP, #8n], C6.2.415
  ubfm.x x1 x13 54 53
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x0 sp 1
  ldr.x x1 sp 2
  ldr.x.post x30 sp 32
  ret
; LOADARG: x0 target register, x1 kind (1 or 5: register x2, read back from its save slot: LDR Xt, [SP,
; #8 slot], C6.2.216; 3 or 6: the number or address x2, MATCONST)
:LOADARG
  cmp.x.i x1 3
  b.eq MATCONST_X
  cmp.x.i x1 6
  b.eq MATCONST_X
  adr x9 REGIDX
  ldrb.r w9 x9 x2
  movz.x x10 0x03e0 0
  movk.x x10 0xf940 1 ; LDR Xt, [SP, #8j], C6.2.216
  orr.x x0 x0 x10
  ubfm.x x9 x9 54 53
  orr.x x0 x0 x9
  b EMIT
:MATCONST_X
  mov.x x1 x2
  b MATCONST
; BASEARG: x0 target register, x9 an array's name entry: its register from the save area, or its address
:BASEARG
  ldr.w w10 x9 5
  cmp.x.i x10 4
  b.hs BA_ADDR
  movz.x x1 1 0
  ldr.w w2 x9 2
  b LOADARG
:BA_ADDR
  ldr.x x1 x9 3
  b MATCONST
; RESTORE: x0 destination (255: a new temporary at pool slot x1), x1 pool slot before the arguments, x2
; saved count, x3 save bytes, x4 1 if there is a result in X0. MOV Xd, X0 (C6.2.282); LDR each register
; in use before the arguments except Xd (C6.2.216); LDR X30; ADD SP, SP, #A (C6.2.5). Out: x0 Xd.
:RESTORE
  str.x.pre x30 sp -64
  str.x x2 sp 2
  str.x x3 sp 3
  mov.x x24 x1
  str.x x1 sp 4
  movz.x x9 255 0
  str.x x9 sp 1
  cbz.x x4 RS2
  cmp.x.i x0 255
  b.ne RS1
  bl ALLOC
:RS1
  str.x x0 sp 1
  cbz.x x0 RS2
  movz.x x9 0x03e0 0
  movk.x x9 0xaa00 1 ; MOV Xd, X0 = ORR Xd, XZR, X0, C6.2.282
  orr.x x0 x0 x9
  bl EMIT
:RS2
  movz.x x13 0 0
  adr x9 POOLC
  sub.x x14 x29 x9
  ldr.x x9 sp 4
  add.x x14 x14 x9
:RS3
  cmp.x x13 x14
  b.hs RS4
  adr x9 POOLC
  ldrb.r w0 x9 x13
  ldr.x x9 sp 1
  cmp.x x0 x9
  b.eq RS5
  movz.x x9 0x03e0 0
  movk.x x9 0xf940 1 ; LDR Xr, [SP, #8j], C6.2.216
  orr.x x0 x0 x9
  ubfm.x x1 x13 54 53
  orr.x x0 x0 x1
  bl EMIT
:RS5
  add.x.i x13 x13 1
  b RS3
:RS4
  movz.x x0 0x03fe 0
  movk.x x0 0xf940 1 ; LDR X30, [SP, #8n], C6.2.216
  ldr.x x1 sp 2
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  bl EMIT
  ldr.x x1 sp 3
  bl SPADD
  ldr.x x0 sp 1
  ldr.x.post x30 sp 64
  ret
; ======================================================================================================
; ELEMADDR: x0 array name entry, index x1 kind, x2 number or register, x3 type. A number index must be
; inside the array (error 21) and needs no check; a register index: CMP Xi, #len (C6.2.96) or CMP Xi, Xt
; (C6.2.97), B.HS trap 3 (unsigned, so a negative signed index traps too). Out: x0 base register (the
; array's register, or a new one holding its address), x1 index register.
:ELEMADDR
  str.x.pre x30 sp -48
  str.x x0 sp 1
  cmp.x.i x3 1
  b.eq E_TYPE
  cmp.x.i x1 3
  b.ne EA_R
  ldr.w w9 x0 4
  cmp.x x2 x9
  b.hs E_BOUNDS
  str.x x2 sp 2
  bl ALLOC
  str.x x0 sp 3
  ldr.x x1 sp 2
  bl MATCONST
  b EA_B
:EA_R
  str.x x2 sp 3
  ldr.w w1 x0 4
  movz.x x9 4095 0
  cmp.x x1 x9
  b.hi EA_BIG
  movz.x x0 0x001f 0
  movk.x x0 0xf100 1 ; CMP Xi, #len, C6.2.96
  ubfm.x x1 x1 54 53
  orr.x x0 x0 x1
  b EA_C
:EA_BIG
  bl ALLOC
  str.x x0 sp 4
  bl MATCONST
  movz.x x0 0x001f 0
  movk.x x0 0xeb00 1 ; CMP Xi, Xt, C6.2.97
  ldr.x x1 sp 4
  ubfm.x x1 x1 48 47
  orr.x x0 x0 x1
:EA_C
  ldr.x x1 sp 3
  ubfm.x x1 x1 59 58
  orr.x x0 x0 x1
  bl EMIT
  movz.x x0 2 0
  movz.x x1 3 0
  bl TRAPC
:EA_B
  ldr.x x9 sp 1
  ldr.w w10 x9 5
  cmp.x.i x10 4
  b.hs EA_A
  ldr.w w0 x9 2
  b EA_X
:EA_A
  bl ALLOC
  str.x x0 sp 4
  ldr.x x9 sp 1
  ldr.x x1 x9 3
  bl MATCONST
  ldr.x x0 sp 4
:EA_X
  ldr.x x1 sp 3
  ldr.x.post x30 sp 48
  ret
; ARRNAME: read an array name -> x0 its entry
:ARRNAME
  str.x.pre x30 sp -16
  bl TOK
  cbz.x x0 E_FEW
  cmp.x.i x0 3
  b.eq E_FEW
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  bl FINDNAME
  cbz.x x0 E_UNDEF
  ldr.w w9 x0 5
  cbz.x x9 E_TYPE
  ldr.x.post x30 sp 16
  ret
; TOPNAME: read a new name for a global or a text -> x12
:TOPNAME
  str.x.pre x30 sp -16
  bl TOK
  cmp.x.i x0 1
  b.ne E_SYNTAX
  cmp.x.i x2 8
  b.hi E_LONG
  mov.x x12 x1
  bl KW
  cbnz.x x0 E_SYNTAX
  mov.x x1 x12
  bl NEWNAME
  ldr.x.post x30 sp 16
  ret
; FINDNAME: x1 packed name -> x0 its entry (0 none). Clobbers x9..x11.
:FINDNAME
  movz.x x9 0x180 1
  movz.x x10 0 0
:FD1
  cmp.x x10 x23
  b.hs FD0
  ldr.x x11 x9 0
  cmp.x x11 x1
  b.eq FDF
  add.x.i x9 x9 32
  add.x.i x10 x10 1
  b FD1
:FD0
  movz.x x0 0 0
  ret
:FDF
  mov.x x0 x9
  ret
; ADDNAME: x1 packed name, x2 register, x3 type, x4 length, x5 kind, x6 address. Clobbers x9, x10.
:ADDNAME
  cmp.x.i x23 1024
  b.hs E_FULL
  movz.x x9 0x180 1
  ubfm.x x10 x23 59 58
  add.x x9 x9 x10
  str.x x1 x9 0
  str.w w2 x9 2
  str.w w3 x9 3
  str.w w4 x9 4
  str.w w5 x9 5
  str.x x6 x9 3
  add.x.i x23 x23 1
  ret
; PUSHB: x0 branch to patch, x1 kind (1 if, 2 else, 3 while, 4 for), x2 head, x3 loop register,
; x4 its type, x5 names and x6 pool slots to restore at end; slot 7 = the then-part ended (for else)
:PUSHB
  movz.x x9 0x182 1
  cmp.x x25 x9
  b.hs E_FULL
  str.x x1 x25 0
  str.x x0 x25 1
  str.x x2 x25 2
  str.x x3 x25 3
  str.x x4 x25 4
  str.x x5 x25 5
  str.x x6 x25 6
  str.x xzr x25 7
  add.x.i x25 x25 128
  ret
; ELEMTYPE: x0 a type that an array may hold (uchar .. long, not bool)
:ELEMTYPE
  cmp.x.i x0 2
  b.lo E_SUBSET
  ret
; ARRLEN: x1 an array length: 1 .. 16777216 (error 8 below, 23 above; a number written as 2^63 or more,
; state slot 11, is above)
:ARRLEN
  movz.x x9 0x185 1
  ldr.x x9 x9 11
  cbnz.x x9 E_TOOBIG
  cmp.x.i x1 1
  b.lt E_RANGE
  movz.x x9 0x100 1
  cmp.x x1 x9
  b.hi E_TOOBIG
  ret
; EPI: ADD SP, SP, #stack of the local arrays (if any), then RET (C6.2.338)
:EPI
  str.x.pre x30 sp -16
  movz.x x9 0x185 1
  ldr.x x1 x9 4
  bl SPADD
  movz.x x0 0x03c0 0
  movk.x x0 0xd65f 1 ; RET (X30), C6.2.338
  bl EMIT
  ldr.x.post x30 sp 16
  ret
; SPADD / SPSUB: x1 bytes (a multiple of 16, below 2^24): ADD (C6.2.5) / SUB (C6.2.457) SP, SP, #low 12
; bits, then #high 12 bits LSL 12 (sh = 1, bit 22); nothing for a zero part
:SPADD
  movz.x x2 0x9100 1
  b SPOP
:SPSUB
  movz.x x2 0xd100 1
:SPOP
  str.x.pre x30 sp -32
  str.x x1 sp 1
  str.x x2 sp 2
  ubfm.x x3 x1 0 11
  cbz.x x3 SPO1
  movz.x x9 0x03ff 0
  orr.x x0 x2 x9
  ubfm.x x3 x3 54 53
  orr.x x0 x0 x3
  bl EMIT
:SPO1
  ldr.x x1 sp 1
  ubfm.x x3 x1 12 23
  cbz.x x3 SPO2
  ldr.x x2 sp 2
  movz.x x9 0x03ff 0
  movk.x x9 0x0040 1
  orr.x x0 x2 x9
  ubfm.x x3 x3 54 53
  orr.x x0 x0 x3
  bl EMIT
:SPO2
  ldr.x.post x30 sp 32
  ret
; STKCHK: program mode only: CMP SP, X28 (SUBS extended register, UXTX, Rn = SP, C6.2.463); B.LO trap 6
:STKCHK
  movz.x x9 0x185 1
  ldr.x x10 x9 6
  cbz.x x10 SK1
  str.x.pre x30 sp -16
  movz.x x0 0x63ff 0
  movk.x x0 0xeb3c 1 ; CMP SP, X28, C6.2.463
  bl EMIT
  movz.x x0 3 0
  movz.x x1 6 0
  bl TRAPC
  ldr.x.post x30 sp 16
:SK1
  ret
; DEAD1: the path has ended (ret, exit)
:DEAD1
  movz.x x9 0x185 1
  movz.x x10 1 0
  str.x x10 x9 5
  ret
; EMITB: x0 one byte -> output. Clobbers x9.
:EMITB
  movz.x x9 0x180 1
  cmp.x x20 x9
  b.hs E_FULL
  strb w0 x20 0
  add.x.i x20 x20 1
  ret
; ======================================================================================================
; errors new in om4, and FAIL
:E_DEAD
  movz.x x0 17 0
  b FAIL
:E_NORET
  movz.x x0 18 0
  b FAIL
:E_ALIAS
  movz.x x0 19 0
  b FAIL
:E_RDONLY
  movz.x x0 20 0
  b FAIL
:E_BOUNDS
  movz.x x0 21 0
  b FAIL
:E_TEXT
  movz.x x0 22 0
  b FAIL
:E_TOOBIG
  movz.x x0 23 0
  b FAIL
; FAIL: x0 code: "om4: line N: error E" and a newline on stderr (fd 2), exit E
:FAIL
  mov.x x12 x0
  movz.x x9 0x183 1
  adr x10 MSG1
  movz.x x11 10 0
  bl COPYN
  mov.x x0 x22
  bl PUTDEC
  adr x10 MSG2
  movz.x x11 8 0
  bl COPYN
  mov.x x0 x12
  bl PUTDEC
  movz.x x0 10 0
  strb w0 x9 0
  add.x.i x9 x9 1
  movz.x x0 2 0
  movz.x x1 0x183 1
  sub.x x2 x9 x1
  movz.x x8 64 0
  svc 0
  mov.x x0 x12
  movz.x x8 93 0
  svc 0
; COPYN: copy x11 bytes at x10 to x9 (x9 moves on)
:COPYN
  cbz.x x11 CN1
  ldrb w13 x10 0
  strb w13 x9 0
  add.x.i x9 x9 1
  add.x.i x10 x10 1
  sub.x.i x11 x11 1
  b COPYN
:CN1
  ret
; ======================================================================================================
; NEWNAME: x1 packed name, x2 its length, x3 its first byte: the name must not be in use (a name in scope,
; when 8 characters or fewer, or a function: error 10). Keeps x1..x8, x12..x14. Clobbers x0, x9..x11, x15..x17.
:NEWNAME
  str.x.pre x30 sp -16
  str.x x1 sp 1
  cmp.x.i x2 8
  b.hi NN1
  bl FINDNAME
  cbnz.x x0 E_TWICE
:NN1
  ldr.x x1 sp 1
  bl FINDFN
  cbnz.x x0 E_TWICE
  ldr.x x1 sp 1
  ldr.x.post x30 sp 16
  ret
; FINDFN: x1 packed name (first 8 characters), x2 length (any value up to 8 for a short name), x3 first
; byte (read only for a name over 8 characters) -> x0 its function entry (0 none). A function entry keeps
; its name's length at +120 and the name's place in the input at +124. Clobbers x9..x11, x15..x17.
:FINDFN
  movz.x x9 0x185 1
  ldr.x x10 x9 2
  movz.x x9 0x184 1
:FF1
  cbz.x x10 FD0
  ldr.x x11 x9 0
  cmp.x x11 x1
  b.ne FF2
  ldr.w w11 x9 30
  cmp.x.i x2 8
  b.hi FF_LONG
  cmp.x.i x11 8
  b.ls FDF
  b FF2
:FF_LONG
  cmp.x x11 x2
  b.ne FF2
  ldr.w w15 x9 31
  movz.x x16 0x80 1
  add.x x15 x15 x16
  movz.x x16 8 0
:FF3
  cmp.x x16 x2
  b.hs FDF
  ldrb.r w17 x15 x16
  ldrb.r w11 x3 x16
  cmp.x x17 x11
  b.ne FF2
  add.x.i x16 x16 1
  b FF3
:FF2
  add.x.i x9 x9 128
  sub.x.i x10 x10 1
  b FF1
; S_LONG: a word over 8 characters at the start of a line: only a call of a void function
:S_LONG
  cbz.x x27 E_LONG
  movz.x x9 0x185 1
  ldr.x x10 x9 5
  cbnz.x x10 E_DEAD
  sub.x x3 x19 x2
  bl FINDFN
  cbz.x x0 E_LONG
  ldr.w w9 x0 4
  cmp.x.i x9 10
  b.ne E_TYPE
  movz.x x1 255 0
  bl CALL
  bl EOL
  b STMT
; VT_LONG: a word over 8 characters as a value: only a call of a function that gives a value
:VT_LONG
  mov.x x1 x4
  mov.x x2 x5
  sub.x x3 x19 x5
  bl FINDFN
  cbz.x x0 E_LONG
  b VT_FN2
