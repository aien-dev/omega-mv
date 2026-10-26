
/tmp/omc-eff-2-aa/work/obj/arith.O2.o:     file format elf64-littleaarch64


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
  1c:	d2800013 	mov	x19, #0x0                   	// #0
  20:	aa1303f4 	mov	x20, x19
  24:	a9045bf5 	stp	x21, x22, [sp, #64]
  28:	910023f5 	add	x21, sp, #0x8
  2c:	aa1503e1 	mov	x1, x21
  30:	f9002bf7 	str	x23, [sp, #80]
  34:	f9400043 	ldr	x3, [x2]
  38:	f9000fe3 	str	x3, [sp, #24]
  3c:	d2800003 	mov	x3, #0x0                   	// #0
  40:	94000000 	bl	0 <clock_gettime>
			40: R_AARCH64_CALL26	clock_gettime
  44:	a940dff6 	ldp	x22, x23, [sp, #8]
  48:	d2994000 	mov	x0, #0xca00                	// #51712
  4c:	f2a77340 	movk	x0, #0x3b9a, lsl #16
  50:	9b007ed6 	mul	x22, x22, x0
  54:	d2938d27 	mov	x7, #0x9c69                	// #40041
  58:	d2884866 	mov	x6, #0x4243                	// #16963
  5c:	f2b396a7 	movk	x7, #0x9cb5, lsl #16
  60:	d28b4005 	mov	x5, #0x5a00                	// #23040
  64:	f2c8a887 	movk	x7, #0x4544, lsl #32
  68:	d2800002 	mov	x2, #0x0                   	// #0
  6c:	f2e18de7 	movk	x7, #0xc6f, lsl #48
  70:	f2a001e6 	movk	x6, #0xf, lsl #16
  74:	f2a04c45 	movk	x5, #0x262, lsl #16
  78:	9b027c43 	mul	x3, x2, x2
  7c:	d37bea60 	lsl	x0, x19, #5
  80:	cb130000 	sub	x0, x0, x19
  84:	8b020013 	add	x19, x0, x2
  88:	ca420c63 	eor	x3, x3, x2, lsr #3
  8c:	91000442 	add	x2, x2, #0x1
  90:	9bc77c64 	umulh	x4, x3, x7
  94:	cb040060 	sub	x0, x3, x4
  98:	8b400480 	add	x0, x4, x0, lsr #1
  9c:	d353fc00 	lsr	x0, x0, #19
  a0:	9b068c00 	msub	x0, x0, x6, x3
  a4:	8b000294 	add	x20, x20, x0
  a8:	eb05005f 	cmp	x2, x5
  ac:	54fffe61 	b.ne	78 <main+0x78>  // b.any
  b0:	52800020 	mov	w0, #0x1                   	// #1
  b4:	aa1503e1 	mov	x1, x21
  b8:	94000000 	bl	0 <clock_gettime>
			b8: R_AARCH64_CALL26	clock_gettime
  bc:	90000015 	adrp	x21, 0 <main>
			bc: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  c0:	a94093e1 	ldp	x1, x4, [sp, #8]
  c4:	d2994002 	mov	x2, #0xca00                	// #51712
  c8:	90000000 	adrp	x0, 0 <stderr>
			c8: R_AARCH64_ADR_GOT_PAGE	stderr
  cc:	f9400000 	ldr	x0, [x0]
			cc: R_AARCH64_LD64_GOT_LO12_NC	stderr
  d0:	f2a77342 	movk	x2, #0x3b9a, lsl #16
  d4:	910002b5 	add	x21, x21, #0x0
			d4: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  d8:	9b027c21 	mul	x1, x1, x2
  dc:	cb160084 	sub	x4, x4, x22
  e0:	f9400000 	ldr	x0, [x0]
  e4:	cb170023 	sub	x3, x1, x23
  e8:	8b030083 	add	x3, x4, x3
  ec:	aa1503e2 	mov	x2, x21
  f0:	52800041 	mov	w1, #0x2                   	// #2
  f4:	94000000 	bl	0 <__fprintf_chk>
			f4: R_AARCH64_CALL26	__fprintf_chk
  f8:	aa1303e2 	mov	x2, x19
  fc:	aa1503e1 	mov	x1, x21
 100:	52800040 	mov	w0, #0x2                   	// #2
 104:	94000000 	bl	0 <__printf_chk>
			104: R_AARCH64_CALL26	__printf_chk
 108:	aa1403e2 	mov	x2, x20
 10c:	aa1503e1 	mov	x1, x21
 110:	52800040 	mov	w0, #0x2                   	// #2
 114:	94000000 	bl	0 <__printf_chk>
			114: R_AARCH64_CALL26	__printf_chk
 118:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			118: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
 11c:	f9400000 	ldr	x0, [x0]
			11c: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
 120:	f9400fe2 	ldr	x2, [sp, #24]
 124:	f9400001 	ldr	x1, [x0]
 128:	eb010042 	subs	x2, x2, x1
 12c:	d2800001 	mov	x1, #0x0                   	// #0
 130:	54000101 	b.ne	150 <main+0x150>  // b.any
 134:	a9427bfd 	ldp	x29, x30, [sp, #32]
 138:	52800000 	mov	w0, #0x0                   	// #0
 13c:	a94353f3 	ldp	x19, x20, [sp, #48]
 140:	a9445bf5 	ldp	x21, x22, [sp, #64]
 144:	f9402bf7 	ldr	x23, [sp, #80]
 148:	910183ff 	add	sp, sp, #0x60
 14c:	d65f03c0 	ret
 150:	94000000 	bl	0 <__stack_chk_fail>
			150: R_AARCH64_CALL26	__stack_chk_fail
