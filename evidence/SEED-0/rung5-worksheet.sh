#!/bin/sh
# rung5-worksheet.sh -- the arithmetic behind the data lines of seed/atlas/atlas.s2. A calculator only.
# 1. each message as .b lines (one byte per line, NUL at the end), from the .asciz text in atlas.s;
# 2. the 64 SHA-256 round constants K from sha256_clean.s (written there as signed decimal .word)
#    as 32-bit hex .w lines;
# 3. the 64-bit literals of atlas.s's "ldr Xn, =value" as two .w halves (low first).
str() { printf '; "%s\\n"\n' "$1"; printf '%s\n' "$1" | od -An -tx1 -v | tr ' ' '\n' | grep . | sed 's/^/  .b 0x/'; echo '  .b 0'; }
str 'ATLAS: AWAKEN'; str 'ATLAS: VERIFY'; str 'ATLAS: HANDOFF'; str 'ATLAS: REFUSE'
echo '; K'
for v in 1116352408 1899447441 -1245643825 -373957723 961987163 1508970993 -1841331548 -1424204075 \
  -670586216 310598401 607225278 1426881987 1925078388 -2132889090 -1680079193 -1046744716 \
  -459576895 -272742522 264347078 604807628 770255983 1249150122 1555081692 1996064986 \
  -1740746414 -1473132947 -1341970488 -1084653625 -958395405 -710438585 113926993 338241895 \
  666307205 773529912 1294757372 1396182291 1695183700 1986661051 -2117940946 -1838011259 \
  -1564481375 -1474664885 -1035236496 -949202525 -778901479 -694614492 -200395387 275423344 \
  430227734 506948616 659060556 883997877 958139571 1322822218 1537002063 1747873779 \
  1955562222 2024104815 -2067236844 -1933114872 -1866530822 -1538233109 -1090935817 -965641998; do
  printf '  .w 0x%08x\n' $(( v & 0xffffffff ))
done
echo '; literals'
for v in 0x401FC000 0x09000000 0x401FE000 0x40000000 0x08000000 0x40200000 0x401FE100 0x401FE080 0x5048595349435330; do
  printf '  .w 0x%08x\n  .w 0x%08x ; %s\n' $(( v & 0xffffffff )) $(( (v >> 32) & 0xffffffff )) $v
done
