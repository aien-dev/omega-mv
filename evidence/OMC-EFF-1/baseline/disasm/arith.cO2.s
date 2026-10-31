
work/obj/arith.O2.o:     file format elf64-littleaarch64


Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d2938d27 	mov	x7, #0x9c69                	// #40041
   8:	f2b396a7 	movk	x7, #0x9cb5, lsl #16
   c:	910003fd 	mov	x29, sp
  10:	f2c8a887 	movk	x7, #0x4544, lsl #32
  14:	d2884866 	mov	x6, #0x4243                	// #16963
  18:	d28b4005 	mov	x5, #0x5a00                	// #23040
  1c:	d2800001 	mov	x1, #0x0                   	// #0
  20:	d2800002 	mov	x2, #0x0                   	// #0
  24:	f2e18de7 	movk	x7, #0xc6f, lsl #48
  28:	f2a001e6 	movk	x6, #0xf, lsl #16
  2c:	f2a04c45 	movk	x5, #0x262, lsl #16
  30:	a90153f3 	stp	x19, x20, [sp, #16]
  34:	d2800013 	mov	x19, #0x0                   	// #0
  38:	9b017c23 	mul	x3, x1, x1
  3c:	d37be840 	lsl	x0, x2, #5
  40:	cb020002 	sub	x2, x0, x2
  44:	ca410c63 	eor	x3, x3, x1, lsr #3
  48:	8b010042 	add	x2, x2, x1
  4c:	91000421 	add	x1, x1, #0x1
  50:	9bc77c64 	umulh	x4, x3, x7
  54:	cb040060 	sub	x0, x3, x4
  58:	8b400480 	add	x0, x4, x0, lsr #1
  5c:	d353fc00 	lsr	x0, x0, #19
  60:	9b068c00 	msub	x0, x0, x6, x3
  64:	8b000273 	add	x19, x19, x0
  68:	eb05003f 	cmp	x1, x5
  6c:	54fffe61 	b.ne	38 <main+0x38>  // b.any
  70:	90000014 	adrp	x20, 0 <main>
  74:	91000294 	add	x20, x20, #0x0
  78:	aa1403e1 	mov	x1, x20
  7c:	52800040 	mov	w0, #0x2                   	// #2
  80:	94000000 	bl	0 <__printf_chk>
  84:	aa1303e2 	mov	x2, x19
  88:	aa1403e1 	mov	x1, x20
  8c:	52800040 	mov	w0, #0x2                   	// #2
  90:	94000000 	bl	0 <__printf_chk>
  94:	a94153f3 	ldp	x19, x20, [sp, #16]
  98:	52800000 	mov	w0, #0x0                   	// #0
  9c:	a8c27bfd 	ldp	x29, x30, [sp], #32
  a0:	d65f03c0 	ret
