#!/bin/sh
# enccases.sh -- the rung-2 encoder comparison cases, one per line: "<C encoder form and args>|<asm2 line>".
# Every form in src/aarch64_encoder.h, with registers and immediates at their edges. Units: the C
# encoder takes load/store offsets in bytes; asm2's table takes the manual's imm12 (offset / access size),
# so the asm2 line holds offset/size. movz/movk: C takes the shift (0,16,32,48), asm2 the manual's hw.
R="0 1 7 15 30 31"
rx() { [ "$1" = 31 ] && echo xzr || echo "x$1"; }
rw() { [ "$1" = 31 ] && echo wzr || echo "w$1"; }
rs() { [ "$1" = 31 ] && echo sp || echo "x$1"; }
for f in add sub mul and orr eor subs; do for sf in 1 0; do for d in $R; do for n in $R; do for m in $R; do
  if [ $sf = 1 ]; then echo "${f}_reg 1 $d $n $m|$f.x $(rx $d) $(rx $n) $(rx $m)"; else echo "${f}_reg 0 $d $n $m|$f.w $(rw $d) $(rw $n) $(rw $m)"; fi
done; done; done; done; done
for sf in 1 0; do for d in $R; do for m in $R; do
  if [ $sf = 1 ]; then echo "mov_reg 1 $d $m|mov.x $(rx $d) $(rx $m)"; else echo "mov_reg 0 $d $m|mov.w $(rw $d) $(rw $m)"; fi
done; done; done
for f in movz movk; do for d in $R; do for v in 0 1 4660 65535; do
  for s in 0 16 32 48; do echo "$f 1 $d $v $s|$f.x $(rx $d) $v $((s / 16))"; done
  for s in 0 16; do echo "$f 0 $d $v $s|$f.w $(rw $d) $v $((s / 16))"; done
done; done; done
echo "ret|ret"
for v in 0 1 -1 2 -2 12345 -12345 33554431 -33554432; do echo "b $v|b.raw $v"; done
for c in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do for v in 0 1 -1 262143 -262144 1000 -1000; do echo "b_cond $c $v|b.c.raw $c $v"; done; done
for f in cbz cbnz; do for t in $R; do for v in 0 1 -1 262143 -262144 777; do
  echo "$f 1 $t $v|$f.x.raw $(rx $t) $v"; echo "$f 0 $t $v|$f.w.raw $(rw $t) $v"; done; done; done
for f in ldr str; do for t in $R; do for n in $R; do for k in 0 1 8 4095; do
  echo "${f}_uoff 1 $t $n $((k * 8))|$f.x $(rx $t) $(rs $n) $k"; echo "${f}_uoff 0 $t $n $((k * 4))|$f.w $(rw $t) $(rs $n) $k"; done; done; done; done
for f in ldrb strb; do for t in $R; do for n in $R; do for k in 0 1 100 4095; do echo "${f}_uoff $t $n $k|$f $(rw $t) $(rs $n) $k"; done; done; done; done
for t in $R; do for n in $R; do for v in -256 -1 0 1 255; do
  echo "ldr_post $t $n $v|ldr.w.post $(rw $t) $(rs $n) $v"; echo "str_post $t $n $v|str.w.post $(rw $t) $(rs $n) $v"
  echo "ldr_x_post $t $n $v|ldr.x.post $(rx $t) $(rs $n) $v"; echo "str_x_post $t $n $v|str.x.post $(rx $t) $(rs $n) $v"
done; done; done
for d in $R; do for n in $R; do for v in 0 1 4095; do
  echo "subs_imm 1 $d $n $v|subs.x.i $(rx $d) $(rx $n) $v"; echo "subs_imm 0 $d $n $v|subs.w.i $(rw $d) $(rw $n) $v"; done; done; done
