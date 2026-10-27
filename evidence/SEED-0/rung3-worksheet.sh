#!/bin/sh
# rung3-worksheet.sh -- the arithmetic behind om3's keyword table (seed/rung3/om3.s, label KWTAB).
# A calculator only: it prints, for each keyword, its first 8 characters packed little-endian
# (first character in the low byte) as two 32-bit halves, and the code om3 gives it.
# Codes: 1..9 types (bool u8 u16 u32 u64 i8 i16 i32 i64), 33..50 ops, 64.. statements, 77 = not in the subset.
kw() { # word code
  lo=0; hi=0; i=0
  for c in $(printf '%s' "$1" | od -An -tu1); do
    if [ $i -lt 4 ]; then lo=$((lo + (c << (8*i)))); else hi=$((hi + (c << (8*(i-4))))); fi
    i=$((i+1))
  done
  printf '  .w 0x%08x .w 0x%08x .w %d .w 0  ; %s\n' $lo $hi "$2" "$1"
}
set -f
for t in "bool 1" "uchar 2" "u8 2" "ushort 3" "u16 3" "uint 4" "u32 4" "ulong 5" "u64 5" \
         "byte 6" "i8 6" "short 7" "i16 7" "int 8" "i32 8" "long 9" "i64 9" \
         "add 33" "sub 34" "mul 35" "div 36" "rem 37" "and 38" "or 39" "xor 40" "shl 41" "shr 42" \
         "eq 43" "ne 44" "lt 45" "le 46" "gt 47" "ge 48" "neg 49" "not 50" \
         "fn 64" "to 65" "end 66" "if 67" "else 68" "while 69" "bound 70" "for 71" "ret 72" "= 73" \
         "( 74" ") 75" "void 76" "break 77" "trap 77" "put 77" "free 77" "get 77" "new 77" "own 77" \
         "edit 77" "view 77" "true 77" "false 77"; do
  kw $t
done
