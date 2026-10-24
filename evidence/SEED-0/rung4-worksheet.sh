#!/bin/sh
# rung4-worksheet.sh -- the arithmetic behind om4's keyword table (seed/rung4/om4.s, label KWTAB).
# A calculator only: for each keyword, its first 8 characters packed little-endian (first character in
# the low byte) as two 32-bit halves, then the code om4 gives it, as asm2 lines.
# Codes: 1..9 types, 33..50 ops, 51..58 the new values, 64.. statements, 77 = not in the subset.
kw() { # word code
  lo=0; hi=0; i=0
  for c in $(printf '%s' "$1" | od -An -tu1); do
    if [ $i -lt 4 ]; then lo=$((lo + (c << (8*i)))); else hi=$((hi + (c << (8*(i-4))))); fi
    i=$((i+1))
  done
  printf '  .w 0x%08x\n  .w 0x%08x\n  .w %d\n  .w 0\n' $lo $hi "$2"
}
set -f
for t in "bool 1" "uchar 2" "u8 2" "ushort 3" "u16 3" "uint 4" "u32 4" "ulong 5" "u64 5" \
         "byte 6" "i8 6" "short 7" "i16 7" "int 8" "i32 8" "long 9" "i64 9" \
         "add 33" "sub 34" "mul 35" "div 36" "rem 37" "and 38" "or 39" "xor 40" "shl 41" "shr 42" \
         "eq 43" "ne 44" "lt 45" "le 46" "gt 47" "ge 48" "neg 49" "not 50" \
         "get 51" "len 52" "read 53" "write 54" "true 55" "false 56" "view 57" "edit 58" \
         "fn 64" "to 65" "end 66" "if 67" "else 68" "while 69" "bound 70" "for 71" "ret 72" "= 73" \
         "( 74" ") 75" "void 76" "global 78" "text 79" "put 80" "free 81" "exit 82" "note 83" "new 84" \
         "break 77" "trap 77" "own 77"; do
  kw $t
done
printf '  .w 0\n  .w 0\n  .w 0\n  .w 0\n'
