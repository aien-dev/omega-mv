
/tmp/omc-eff-3-aa/work/obj/branch.O2.o:     file format elf64-littleaarch64


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
  1c:	d2800014 	mov	x20, #0x0                   	// #0
  20:	aa1403f3 	mov	x19, x20
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
  48:	d2884805 	mov	x5, #0x4240                	// #16960
  4c:	d2800024 	mov	x4, #0x1                   	// #1
  50:	f2a001e5 	movk	x5, #0xf, lsl #16
  54:	d503201f 	nop
  58:	91000484 	add	x4, x4, #0x1
  5c:	d2800002 	mov	x2, #0x0                   	// #0
  60:	aa0403e0 	mov	x0, x4
  64:	d503201f 	nop
  68:	f240001f 	tst	x0, #0x1
  6c:	d341fc03 	lsr	x3, x0, #1
  70:	8b000400 	add	x0, x0, x0, lsl #1
  74:	91000442 	add	x2, x2, #0x1
  78:	9a800460 	csinc	x0, x3, x0, eq	// eq = none
  7c:	f100041f 	cmp	x0, #0x1
  80:	54ffff41 	b.ne	68 <main+0x68>  // b.any
  84:	eb02027f 	cmp	x19, x2
  88:	8b020294 	add	x20, x20, x2
  8c:	9a822273 	csel	x19, x19, x2, cs	// cs = hs, nlast
  90:	eb05009f 	cmp	x4, x5
  94:	54fffe21 	b.ne	58 <main+0x58>  // b.any
  98:	aa1503e1 	mov	x1, x21
  9c:	94000000 	bl	0 <clock_gettime>
			9c: R_AARCH64_CALL26	clock_gettime
  a0:	a9408fe1 	ldp	x1, x3, [sp, #8]
  a4:	d2994002 	mov	x2, #0xca00                	// #51712
  a8:	90000000 	adrp	x0, 0 <stderr>
			a8: R_AARCH64_ADR_GOT_PAGE	stderr
  ac:	f9400000 	ldr	x0, [x0]
			ac: R_AARCH64_LD64_GOT_LO12_NC	stderr
  b0:	f2a77342 	movk	x2, #0x3b9a, lsl #16
  b4:	90000015 	adrp	x21, 0 <main>
			b4: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  b8:	910002b5 	add	x21, x21, #0x0
			b8: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  bc:	9b027c21 	mul	x1, x1, x2
  c0:	f9400000 	ldr	x0, [x0]
  c4:	9b028ed6 	msub	x22, x22, x2, x3
  c8:	cb170023 	sub	x3, x1, x23
  cc:	aa1503e2 	mov	x2, x21
  d0:	8b0302c3 	add	x3, x22, x3
  d4:	52800041 	mov	w1, #0x2                   	// #2
  d8:	94000000 	bl	0 <__fprintf_chk>
			d8: R_AARCH64_CALL26	__fprintf_chk
  dc:	aa1403e2 	mov	x2, x20
  e0:	aa1503e1 	mov	x1, x21
  e4:	52800040 	mov	w0, #0x2                   	// #2
  e8:	94000000 	bl	0 <__printf_chk>
			e8: R_AARCH64_CALL26	__printf_chk
  ec:	aa1303e2 	mov	x2, x19
  f0:	aa1503e1 	mov	x1, x21
  f4:	52800040 	mov	w0, #0x2                   	// #2
  f8:	94000000 	bl	0 <__printf_chk>
			f8: R_AARCH64_CALL26	__printf_chk
  fc:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			fc: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
 100:	f9400000 	ldr	x0, [x0]
			100: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
 104:	f9400fe2 	ldr	x2, [sp, #24]
 108:	f9400001 	ldr	x1, [x0]
 10c:	eb010042 	subs	x2, x2, x1
 110:	d2800001 	mov	x1, #0x0                   	// #0
 114:	54000101 	b.ne	134 <main+0x134>  // b.any
 118:	a9427bfd 	ldp	x29, x30, [sp, #32]
 11c:	52800000 	mov	w0, #0x0                   	// #0
 120:	a94353f3 	ldp	x19, x20, [sp, #48]
 124:	a9445bf5 	ldp	x21, x22, [sp, #64]
 128:	f9402bf7 	ldr	x23, [sp, #80]
 12c:	910183ff 	add	sp, sp, #0x60
 130:	d65f03c0 	ret
 134:	94000000 	bl	0 <__stack_chk_fail>
			134: R_AARCH64_CALL26	__stack_chk_fail
