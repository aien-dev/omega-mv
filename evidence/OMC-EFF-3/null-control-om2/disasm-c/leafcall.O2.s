
/tmp/omc-eff-3-aa/work/obj/leafcall.O2.o:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <mix.constprop.0>:
   0:	d28f3621 	mov	x1, #0x79b1                	// #31153
   4:	f2b3c6e1 	movk	x1, #0x9e37, lsl #16
   8:	9b017c01 	mul	x1, x0, x1
   c:	ca401420 	eor	x0, x1, x0, lsr #5
  10:	d65f03c0 	ret

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
  1c:	d2800033 	mov	x19, #0x1                   	// #1
  20:	d2800014 	mov	x20, #0x0                   	// #0
  24:	a9045bf5 	stp	x21, x22, [sp, #64]
  28:	910023f6 	add	x22, sp, #0x8
  2c:	aa1603e1 	mov	x1, x22
  30:	f9002bf7 	str	x23, [sp, #80]
  34:	f9400043 	ldr	x3, [x2]
  38:	f9000fe3 	str	x3, [sp, #24]
  3c:	d2800003 	mov	x3, #0x0                   	// #0
  40:	94000000 	bl	0 <clock_gettime>
			40: R_AARCH64_CALL26	clock_gettime
  44:	a940dff5 	ldp	x21, x23, [sp, #8]
  48:	d2994000 	mov	x0, #0xca00                	// #51712
  4c:	f2a77340 	movk	x0, #0x3b9a, lsl #16
  50:	9b007eb5 	mul	x21, x21, x0
  54:	d2987003 	mov	x3, #0xc380                	// #50048
  58:	d2800002 	mov	x2, #0x0                   	// #0
  5c:	f2a03923 	movk	x3, #0x1c9, lsl #16
  60:	aa0203e0 	mov	x0, x2
  64:	91000442 	add	x2, x2, #0x1
  68:	94000000 	bl	0 <main>
			68: R_AARCH64_CALL26	.text
  6c:	8b000273 	add	x19, x19, x0
  70:	ca130294 	eor	x20, x20, x19
  74:	eb03005f 	cmp	x2, x3
  78:	54ffff41 	b.ne	60 <main+0x60>  // b.any
  7c:	52800020 	mov	w0, #0x1                   	// #1
  80:	aa1603e1 	mov	x1, x22
  84:	94000000 	bl	0 <clock_gettime>
			84: R_AARCH64_CALL26	clock_gettime
  88:	90000016 	adrp	x22, 0 <main>
			88: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  8c:	a94093e1 	ldp	x1, x4, [sp, #8]
  90:	d2994002 	mov	x2, #0xca00                	// #51712
  94:	90000000 	adrp	x0, 0 <stderr>
			94: R_AARCH64_ADR_GOT_PAGE	stderr
  98:	f9400000 	ldr	x0, [x0]
			98: R_AARCH64_LD64_GOT_LO12_NC	stderr
  9c:	f2a77342 	movk	x2, #0x3b9a, lsl #16
  a0:	910002d6 	add	x22, x22, #0x0
			a0: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  a4:	9b027c21 	mul	x1, x1, x2
  a8:	cb150084 	sub	x4, x4, x21
  ac:	f9400000 	ldr	x0, [x0]
  b0:	cb170023 	sub	x3, x1, x23
  b4:	8b030083 	add	x3, x4, x3
  b8:	aa1603e2 	mov	x2, x22
  bc:	52800041 	mov	w1, #0x2                   	// #2
  c0:	94000000 	bl	0 <__fprintf_chk>
			c0: R_AARCH64_CALL26	__fprintf_chk
  c4:	aa1303e2 	mov	x2, x19
  c8:	aa1603e1 	mov	x1, x22
  cc:	52800040 	mov	w0, #0x2                   	// #2
  d0:	94000000 	bl	0 <__printf_chk>
			d0: R_AARCH64_CALL26	__printf_chk
  d4:	aa1403e2 	mov	x2, x20
  d8:	aa1603e1 	mov	x1, x22
  dc:	52800040 	mov	w0, #0x2                   	// #2
  e0:	94000000 	bl	0 <__printf_chk>
			e0: R_AARCH64_CALL26	__printf_chk
  e4:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			e4: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
  e8:	f9400000 	ldr	x0, [x0]
			e8: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
  ec:	f9400fe2 	ldr	x2, [sp, #24]
  f0:	f9400001 	ldr	x1, [x0]
  f4:	eb010042 	subs	x2, x2, x1
  f8:	d2800001 	mov	x1, #0x0                   	// #0
  fc:	54000101 	b.ne	11c <main+0x11c>  // b.any
 100:	a9427bfd 	ldp	x29, x30, [sp, #32]
 104:	52800000 	mov	w0, #0x0                   	// #0
 108:	a94353f3 	ldp	x19, x20, [sp, #48]
 10c:	a9445bf5 	ldp	x21, x22, [sp, #64]
 110:	f9402bf7 	ldr	x23, [sp, #80]
 114:	910183ff 	add	sp, sp, #0x60
 118:	d65f03c0 	ret
 11c:	94000000 	bl	0 <__stack_chk_fail>
			11c: R_AARCH64_CALL26	__stack_chk_fail
