
/tmp/omc-eff-3-aa/work/obj/sieve.O2.o:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <pass>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d2909022 	mov	x2, #0x8481                	// #33921
   8:	f2a003c2 	movk	x2, #0x1e, lsl #16
   c:	910003fd 	mov	x29, sp
  10:	f9000bf3 	str	x19, [sp, #16]
  14:	90000013 	adrp	x19, 0 <pass>
			14: R_AARCH64_ADR_PREL_PG_HI21	.bss
  18:	91000263 	add	x3, x19, #0x0
			18: R_AARCH64_ADD_ABS_LO12_NC	.bss
  1c:	52800001 	mov	w1, #0x0                   	// #0
  20:	aa0303e0 	mov	x0, x3
  24:	94000000 	bl	0 <memset>
			24: R_AARCH64_CALL26	memset
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
			84: R_AARCH64_ADD_ABS_LO12_NC	.bss
  88:	90000001 	adrp	x1, 0 <pass>
			88: R_AARCH64_ADR_PREL_PG_HI21	.bss+0x2
  8c:	9147a063 	add	x3, x3, #0x1e8, lsl #12
  90:	91000021 	add	x1, x1, #0x0
			90: R_AARCH64_ADD_ABS_LO12_NC	.bss+0x2
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
   0:	d10183ff 	sub	sp, sp, #0x60
   4:	52800020 	mov	w0, #0x1                   	// #1
   8:	90000002 	adrp	x2, 0 <__stack_chk_guard>
			8: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
   c:	f9400042 	ldr	x2, [x2]
			c: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
  10:	a9027bfd 	stp	x29, x30, [sp, #32]
  14:	910083fd 	add	x29, sp, #0x20
  18:	a90353f3 	stp	x19, x20, [sp, #48]
  1c:	a9045bf5 	stp	x21, x22, [sp, #64]
  20:	d2800015 	mov	x21, #0x0                   	// #0
  24:	a90563f7 	stp	x23, x24, [sp, #80]
  28:	910023f8 	add	x24, sp, #0x8
  2c:	aa1803e1 	mov	x1, x24
  30:	f9400043 	ldr	x3, [x2]
  34:	f9000fe3 	str	x3, [sp, #24]
  38:	d2800003 	mov	x3, #0x0                   	// #0
  3c:	94000000 	bl	0 <clock_gettime>
			3c: R_AARCH64_CALL26	clock_gettime
  40:	a940dff6 	ldp	x22, x23, [sp, #8]
  44:	d2994000 	mov	x0, #0xca00                	// #51712
  48:	f2a77340 	movk	x0, #0x3b9a, lsl #16
  4c:	9b007ed6 	mul	x22, x22, x0
  50:	aa1503e0 	mov	x0, x21
  54:	d2800654 	mov	x20, #0x32                  	// #50
  58:	94000000 	bl	0 <main>
			58: R_AARCH64_CALL26	.text
  5c:	aa0003f3 	mov	x19, x0
  60:	f1000694 	subs	x20, x20, #0x1
  64:	8b0002b5 	add	x21, x21, x0
  68:	54ffff81 	b.ne	58 <main+0x58>  // b.any
  6c:	52800020 	mov	w0, #0x1                   	// #1
  70:	aa1803e1 	mov	x1, x24
  74:	94000000 	bl	0 <clock_gettime>
			74: R_AARCH64_CALL26	clock_gettime
  78:	90000014 	adrp	x20, 0 <main>
			78: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  7c:	a94093e1 	ldp	x1, x4, [sp, #8]
  80:	d2994002 	mov	x2, #0xca00                	// #51712
  84:	90000000 	adrp	x0, 0 <stderr>
			84: R_AARCH64_ADR_GOT_PAGE	stderr
  88:	f9400000 	ldr	x0, [x0]
			88: R_AARCH64_LD64_GOT_LO12_NC	stderr
  8c:	f2a77342 	movk	x2, #0x3b9a, lsl #16
  90:	91000294 	add	x20, x20, #0x0
			90: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  94:	9b027c21 	mul	x1, x1, x2
  98:	cb160084 	sub	x4, x4, x22
  9c:	f9400000 	ldr	x0, [x0]
  a0:	cb170023 	sub	x3, x1, x23
  a4:	8b030083 	add	x3, x4, x3
  a8:	aa1403e2 	mov	x2, x20
  ac:	52800041 	mov	w1, #0x2                   	// #2
  b0:	94000000 	bl	0 <__fprintf_chk>
			b0: R_AARCH64_CALL26	__fprintf_chk
  b4:	aa1303e2 	mov	x2, x19
  b8:	aa1403e1 	mov	x1, x20
  bc:	52800040 	mov	w0, #0x2                   	// #2
  c0:	94000000 	bl	0 <__printf_chk>
			c0: R_AARCH64_CALL26	__printf_chk
  c4:	aa1503e2 	mov	x2, x21
  c8:	aa1403e1 	mov	x1, x20
  cc:	52800040 	mov	w0, #0x2                   	// #2
  d0:	94000000 	bl	0 <__printf_chk>
			d0: R_AARCH64_CALL26	__printf_chk
  d4:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			d4: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
  d8:	f9400000 	ldr	x0, [x0]
			d8: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
  dc:	f9400fe2 	ldr	x2, [sp, #24]
  e0:	f9400001 	ldr	x1, [x0]
  e4:	eb010042 	subs	x2, x2, x1
  e8:	d2800001 	mov	x1, #0x0                   	// #0
  ec:	54000101 	b.ne	10c <main+0x10c>  // b.any
  f0:	a9427bfd 	ldp	x29, x30, [sp, #32]
  f4:	52800000 	mov	w0, #0x0                   	// #0
  f8:	a94353f3 	ldp	x19, x20, [sp, #48]
  fc:	a9445bf5 	ldp	x21, x22, [sp, #64]
 100:	a94563f7 	ldp	x23, x24, [sp, #80]
 104:	910183ff 	add	sp, sp, #0x60
 108:	d65f03c0 	ret
 10c:	94000000 	bl	0 <__stack_chk_fail>
			10c: R_AARCH64_CALL26	__stack_chk_fail
