
work/obj/isort.O2.o:     file format elf64-littleaarch64


Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9bd7bfd 	stp	x29, x30, [sp, #-48]!
   4:	d28f0864 	mov	x4, #0x7843                	// #30787
   8:	f2ae3684 	movk	x4, #0x71b4, lsl #16
   c:	910003fd 	mov	x29, sp
  10:	a90153f3 	stp	x19, x20, [sp, #16]
  14:	90000014 	adrp	x20, 0 <main>
  18:	91000282 	add	x2, x20, #0x0
  1c:	91409c46 	add	x6, x2, #0x27, lsl #12
  20:	d2860721 	mov	x1, #0x3039                	// #12345
  24:	d289cda5 	mov	x5, #0x4e6d                	// #20077
  28:	f2cb5884 	movk	x4, #0x5ac4, lsl #32
  2c:	d290d403 	mov	x3, #0x86a0                	// #34464
  30:	910400c6 	add	x6, x6, #0x100
  34:	aa0103e7 	mov	x7, x1
  38:	f2a838c5 	movk	x5, #0x41c6, lsl #16
  3c:	f2e14f84 	movk	x4, #0xa7c, lsl #48
  40:	f2a00023 	movk	x3, #0x1, lsl #16
  44:	f90013f5 	str	x21, [sp, #32]
  48:	9b051c20 	madd	x0, x1, x5, x7
  4c:	92407801 	and	x1, x0, #0x7fffffff
  50:	d3457800 	ubfx	x0, x0, #5, #26
  54:	9bc47c00 	umulh	x0, x0, x4
  58:	d347fc00 	lsr	x0, x0, #7
  5c:	9b038400 	msub	x0, x0, x3, x1
  60:	f8008440 	str	x0, [x2], #8
  64:	eb06005f 	cmp	x2, x6
  68:	54ffff01 	b.ne	48 <main+0x48>  // b.any
  6c:	91000281 	add	x1, x20, #0x0
  70:	d2800005 	mov	x5, #0x0                   	// #0
  74:	d289c407 	mov	x7, #0x4e20                	// #20000
  78:	f8657823 	ldr	x3, [x1, x5, lsl #3]
  7c:	d10004a2 	sub	x2, x5, #0x1
  80:	aa0503e4 	mov	x4, x5
  84:	b4000084 	cbz	x4, 94 <main+0x94>
  88:	f8627820 	ldr	x0, [x1, x2, lsl #3]
  8c:	eb00007f 	cmp	x3, x0
  90:	54000643 	b.cc	158 <main+0x158>  // b.lo, b.ul, b.last
  94:	910004a5 	add	x5, x5, #0x1
  98:	f8247823 	str	x3, [x1, x4, lsl #3]
  9c:	eb0700bf 	cmp	x5, x7
  a0:	54fffec1 	b.ne	78 <main+0x78>  // b.any
  a4:	91000284 	add	x4, x20, #0x0
  a8:	d2800000 	mov	x0, #0x0                   	// #0
  ac:	d2800002 	mov	x2, #0x0                   	// #0
  b0:	d289c403 	mov	x3, #0x4e20                	// #20000
  b4:	d503201f 	nop
  b8:	f8607881 	ldr	x1, [x4, x0, lsl #3]
  bc:	91000400 	add	x0, x0, #0x1
  c0:	9b000822 	madd	x2, x1, x0, x2
  c4:	eb03001f 	cmp	x0, x3
  c8:	54ffff81 	b.ne	b8 <main+0xb8>  // b.any
  cc:	f9400281 	ldr	x1, [x20]
  d0:	90000000 	adrp	x0, 0 <main>
  d4:	91000000 	add	x0, x0, #0x0
  d8:	d2800013 	mov	x19, #0x0                   	// #0
  dc:	d503201f 	nop
  e0:	aa0103e3 	mov	x3, x1
  e4:	f8408401 	ldr	x1, [x0], #8
  e8:	eb01007f 	cmp	x3, x1
  ec:	9a939673 	cinc	x19, x19, hi	// hi = pmore
  f0:	eb0000df 	cmp	x6, x0
  f4:	54ffff61 	b.ne	e0 <main+0xe0>  // b.any
  f8:	90000015 	adrp	x21, 0 <main>
  fc:	910002b5 	add	x21, x21, #0x0
 100:	aa1503e1 	mov	x1, x21
 104:	52800040 	mov	w0, #0x2                   	// #2
 108:	94000000 	bl	0 <__printf_chk>
 10c:	aa1303e2 	mov	x2, x19
 110:	aa1503e1 	mov	x1, x21
 114:	52800040 	mov	w0, #0x2                   	// #2
 118:	94000000 	bl	0 <__printf_chk>
 11c:	f9400282 	ldr	x2, [x20]
 120:	91000294 	add	x20, x20, #0x0
 124:	91408294 	add	x20, x20, #0x20, lsl #12
 128:	aa1503e1 	mov	x1, x21
 12c:	52800040 	mov	w0, #0x2                   	// #2
 130:	94000000 	bl	0 <__printf_chk>
 134:	f9787e82 	ldr	x2, [x20, #28920]
 138:	aa1503e1 	mov	x1, x21
 13c:	52800040 	mov	w0, #0x2                   	// #2
 140:	94000000 	bl	0 <__printf_chk>
 144:	a94153f3 	ldp	x19, x20, [sp, #16]
 148:	52800000 	mov	w0, #0x0                   	// #0
 14c:	f94013f5 	ldr	x21, [sp, #32]
 150:	a8c37bfd 	ldp	x29, x30, [sp], #48
 154:	d65f03c0 	ret
 158:	d1000442 	sub	x2, x2, #0x1
 15c:	f8247820 	str	x0, [x1, x4, lsl #3]
 160:	d1000484 	sub	x4, x4, #0x1
 164:	17ffffc8 	b	84 <main+0x84>
