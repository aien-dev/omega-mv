
work/obj/sieve.O2.o:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <pass>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d2909022 	mov	x2, #0x8481                	// #33921
   8:	f2a003c2 	movk	x2, #0x1e, lsl #16
   c:	910003fd 	mov	x29, sp
  10:	f9000bf3 	str	x19, [sp, #16]
  14:	90000013 	adrp	x19, 0 <pass>
  18:	91000263 	add	x3, x19, #0x0
  1c:	52800001 	mov	w1, #0x0                   	// #0
  20:	aa0303e0 	mov	x0, x3
  24:	94000000 	bl	0 <memset>
  28:	aa0003e3 	mov	x3, x0
  2c:	d2909002 	mov	x2, #0x8480                	// #33920
  30:	d2800041 	mov	x1, #0x2                   	// #2
  34:	d2800080 	mov	x0, #0x4                   	// #4
  38:	52800024 	mov	w4, #0x1                   	// #1
  3c:	f2a003c2 	movk	x2, #0x1e, lsl #16
  40:	14000005 	b	54 <pass+0x54>
  44:	91000421 	add	x1, x1, #0x1
  48:	9b017c20 	mul	x0, x1, x1
  4c:	eb02001f 	cmp	x0, x2
  50:	540001a8 	b.hi	84 <pass+0x84>  // b.pmore
  54:	38616865 	ldrb	w5, [x3, x1]
  58:	35ffff65 	cbnz	w5, 44 <pass+0x44>
  5c:	d503201f 	nop
  60:	38206864 	strb	w4, [x3, x0]
  64:	8b010000 	add	x0, x0, x1
  68:	eb02001f 	cmp	x0, x2
  6c:	54fffec8 	b.hi	44 <pass+0x44>  // b.pmore
  70:	38206864 	strb	w4, [x3, x0]
  74:	8b010000 	add	x0, x0, x1
  78:	eb02001f 	cmp	x0, x2
  7c:	54ffff29 	b.ls	60 <pass+0x60>  // b.plast
  80:	17fffff1 	b	44 <pass+0x44>
  84:	91000263 	add	x3, x19, #0x0
  88:	90000001 	adrp	x1, 0 <pass>
  8c:	9147a063 	add	x3, x3, #0x1e8, lsl #12
  90:	91000021 	add	x1, x1, #0x0
  94:	91120463 	add	x3, x3, #0x481
  98:	d2800000 	mov	x0, #0x0                   	// #0
  9c:	d503201f 	nop
  a0:	38401422 	ldrb	w2, [x1], #1
  a4:	7100005f 	cmp	w2, #0x0
  a8:	9a801400 	cinc	x0, x0, eq	// eq = none
  ac:	eb01007f 	cmp	x3, x1
  b0:	54ffff81 	b.ne	a0 <pass+0xa0>  // b.any
  b4:	f9400bf3 	ldr	x19, [sp, #16]
  b8:	a8c27bfd 	ldp	x29, x30, [sp], #32
  bc:	d65f03c0 	ret

Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	910003fd 	mov	x29, sp
   8:	a90153f3 	stp	x19, x20, [sp, #16]
   c:	d2800653 	mov	x19, #0x32                  	// #50
  10:	d2800014 	mov	x20, #0x0                   	// #0
  14:	d503201f 	nop
  18:	94000000 	bl	0 <main>
  1c:	8b000294 	add	x20, x20, x0
  20:	f1000673 	subs	x19, x19, #0x1
  24:	54ffffa1 	b.ne	18 <main+0x18>  // b.any
  28:	90000013 	adrp	x19, 0 <main>
  2c:	91000273 	add	x19, x19, #0x0
  30:	aa0003e2 	mov	x2, x0
  34:	aa1303e1 	mov	x1, x19
  38:	52800040 	mov	w0, #0x2                   	// #2
  3c:	94000000 	bl	0 <__printf_chk>
  40:	aa1403e2 	mov	x2, x20
  44:	aa1303e1 	mov	x1, x19
  48:	52800040 	mov	w0, #0x2                   	// #2
  4c:	94000000 	bl	0 <__printf_chk>
  50:	a94153f3 	ldp	x19, x20, [sp, #16]
  54:	52800000 	mov	w0, #0x0                   	// #0
  58:	a8c27bfd 	ldp	x29, x30, [sp], #32
  5c:	d65f03c0 	ret
