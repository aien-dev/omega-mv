#!/bin/sh
# Hand-calculation worksheet for seed/rung2/asm2.hex1 (rung 2), my arithmetic kept for anyone to redo.
# Each helper is one field layout from Arm DDI 0487 M.d (section named). Branch distances are NOT
# computed here: branch words carry a zero distance field and an '@c' mark, and hex1 (rung 1) fills them.
# Output: hex1 language (words, ':c' labels, '@c' marks, '#' comments).
i=0
e() { printf '%08x%s  # [%d] %s\n' $(( $1 & 0xffffffff )) "${3:+@$3}" $i "$2"; i=$((i + 1)); }
d() { printf '%08x  # data: %s\n' $1 "$2"; }
r() { if [ "$1" = 31 ]; then echo "$2zr"; else echo "$2$1"; fi; }
c() { eval "printf %s \"\$L_$1\""; }                     # label name -> its one-character hex1 label
lab() { printf ':%s  # %s\n' "$(c $1)" "$1"; }
movz() { e $(( 0xD2800000 | ($3<<21) | ($2<<5) | $1 )) "movz x$1, #$2$( [ $3 != 0 ] && echo ", lsl #$(( $3 * 16 ))")  C6.2.285 MOVZ"; }
movk() { e $(( 0xF2800000 | ($3<<21) | ($2<<5) | $1 )) "movk x$1, #$2, lsl #$(( $3 * 16 ))  C6.2.283 MOVK"; }
movx() { e $(( 0xAA0003E0 | ($2<<16) | $1 )) "orr x$1, xzr, $(r $2 x)  C6.2.302 ORR (shifted register), alias MOV C6.2.282"; }
addi() { e $(( 0x91000000 | ($3<<10) | ($2<<5) | $1 )) "add x$1, x$2, #$3  C6.2.5 ADD (immediate)"; }
subi() { e $(( 0xD1000000 | ($3<<10) | ($2<<5) | $1 )) "sub x$1, x$2, #$3  C6.2.457 SUB (immediate)"; }
addx() { e $(( 0x8B000000 | ($3<<16) | ($2<<5) | $1 )) "add x$1, x$2, x$3  C6.2.6 ADD (shifted register)"; }
subx() { e $(( 0xCB000000 | ($3<<16) | ($2<<5) | $1 )) "sub x$1, $(r $2 x), x$3  C6.2.458 SUB (shifted register)"; }
andx() { e $(( 0x8A000000 | ($3<<16) | ($2<<5) | $1 )) "and x$1, x$2, x$3  C6.2.15 AND (shifted register)"; }
orrx() { e $(( 0xAA000000 | ($3<<16) | ($2<<5) | $1 )) "orr x$1, x$2, x$3  C6.2.302 ORR (shifted register)"; }
mulx() { e $(( 0x9B007C00 | ($3<<16) | ($2<<5) | $1 )) "madd x$1, x$2, x$3, xzr  C6.2.275 MADD, alias MUL C6.2.293"; }
msub() { e $(( 0x9B008000 | ($3<<16) | ($4<<10) | ($2<<5) | $1 )) "msub x$1, x$2, x$3, x$4  C6.2.291 MSUB"; }
udiv() { e $(( 0x9AC00800 | ($3<<16) | ($2<<5) | $1 )) "udiv x$1, x$2, x$3  C6.2.490 UDIV"; }
lslv() { e $(( 0x9AC02000 | ($3<<16) | ($2<<5) | $1 )) "lslv x$1, x$2, x$3  C6.2.271 LSLV"; }
lsrv() { e $(( 0x9AC02400 | ($3<<16) | ($2<<5) | $1 )) "lsrv x$1, x$2, x$3  C6.2.274 LSRV"; }
asrv() { e $(( 0x9AC02800 | ($3<<16) | ($2<<5) | $1 )) "asrv x$1, x$2, x$3  C6.2.21 ASRV"; }
cmpi() { e $(( 0xF100001F | ($2<<10) | ($1<<5) )) "subs xzr, x$1, #$2  C6.2.464 SUBS (immediate), alias CMP C6.2.96"; }
cmpw() { e $(( 0x7100001F | ($2<<10) | ($1<<5) )) "subs wzr, w$1, #$2  C6.2.464 SUBS (immediate) 32-bit, alias CMP C6.2.96"; }
subsw(){ e $(( 0x71000000 | ($3<<10) | ($2<<5) | $1 )) "subs w$1, w$2, #$3  C6.2.464 SUBS (immediate) 32-bit"; }
cmpx() { e $(( 0xEB00001F | ($2<<16) | ($1<<5) )) "subs xzr, x$1, x$2  C6.2.465 SUBS (shifted register), alias CMP C6.2.97"; }
o() { [ "$1" = 0 ] && echo "" || echo ", #$1"; }
ldrb() { e $(( 0x39400000 | ($3<<10) | ($2<<5) | $1 )) "ldrb w$1, [x$2$(o $3)]  C6.2.220 LDRB (immediate), unsigned offset"; }
strb() { e $(( 0x39000000 | ($3<<10) | ($2<<5) | $1 )) "strb $(r $1 w), [x$2$(o $3)]  C6.2.417 STRB (immediate), unsigned offset"; }
ldrx() { e $(( 0xF9400000 | ($3<<10) | ($2<<5) | $1 )) "ldr x$1, [x$2$(o $(( $3 * 8 )))]  C6.2.216 LDR (immediate) 64-bit, unsigned offset"; }
strx() { e $(( 0xF9000000 | ($3<<10) | ($2<<5) | $1 )) "str x$1, [x$2$(o $(( $3 * 8 )))]  C6.2.415 STR (immediate) 64-bit, unsigned offset"; }
strw() { e $(( 0xB9000000 | ($3<<10) | ($2<<5) | $1 )) "str w$1, [x$2$(o $(( $3 * 4 )))]  C6.2.415 STR (immediate) 32-bit, unsigned offset"; }
svc()  { e $(( 0xD4000001 )) "svc #0  C6.2.466 SVC"; }
ret()  { e $(( 0xD65F03C0 )) "ret  C6.2.338 RET (x30)"; }
bc()   { e $(( 0x54000000 | $1 )) "b.$2 @$(c $3)  C6.2.34 B.cond (cond $1, Table C1-1) -> $3" "$(c $3)"; }
cbz()  { e $(( 0xB4000000 | $1 )) "cbz x$1, @$(c $2)  C6.2.78 CBZ -> $2" "$(c $2)"; }
cbnz() { e $(( 0xB5000000 | $1 )) "cbnz x$1, @$(c $2)  C6.2.77 CBNZ -> $2" "$(c $2)"; }
adrl() { e $(( 0x10000000 | $1 )) "adr x$1, @$(c $2)  C6.2.12 ADR (word-aligned target, immlo 0) -> $2" "$(c $2)"; }
call() { e $(( 0x1000005E )) "adr x30, +2  C6.2.12 ADR: return point = after the next word"; bc 14 al $1; }
EQ=0; NE=1; HS=2; LO=3; HI=8; LE=13; LT=11; AL=14
# ---- one-character hex1 labels for each place name (any printable character but # : @) ----
L_RD=a L_RDDONE=b L_PASS=c L_LINE=d L_NL=e L_SKIPL=f L_ENDL=g L_SKIPSP=h L_SS1=i
L_TOKEN=j L_TK0=k L_TK1=l L_TK3=m L_TK4=n L_TK5=o L_TKR=p
L_NUM=q L_N1=r L_NDEC=s L_NHEX=t L_NH1=u L_NEND=v L_NRET=w
L_REG=x L_R1=y L_R2=z L_R31=A L_FINDTAB=B L_FT1=C L_FT2=D L_FT0=E L_FINDSYM=F L_FS1=G L_FS2=H L_FS0=I
L_ERR=J L_COPYZ=K L_CZR=L L_PUTDEC=M L_PD1=N L_PD2=O L_PDR=P L_MSG1=Q L_MSG2=R
L_TABDEF=S L_TD1=T L_TD2=U L_TD3=V L_TD4=W L_TD4A=X L_TD5=Y L_TD6=Z L_TD9=0
L_LABDEF=1 L_DIR=2 L_DW=3 L_DW1=4 L_STORE=5
L_INSTR=6 L_IF1=7 L_FR=8 L_FI=9 L_FU='!' L_FS='$' L_FSC='%' L_FL='&' L_FL0='.' L_FA='+' L_FINS='-' L_FNEXT='/' L_IDONE='<'
L_PASSEND='=' L_OUTP='>' L_WL='_' L_EXIT0='^'
. "$(dirname "$0")/rung2-listing.sh"
