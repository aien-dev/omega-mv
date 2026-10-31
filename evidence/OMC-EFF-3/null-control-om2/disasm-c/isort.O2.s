
/tmp/omc-eff-3-aa/work/obj/isort.O2.o:     file format elf64-littleaarch64


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
  28:	a90563f7 	stp	x23, x24, [sp, #80]
  2c:	910023f8 	add	x24, sp, #0x8
  30:	aa1803e1 	mov	x1, x24
  34:	f9400043 	ldr	x3, [x2]
  38:	f9000fe3 	str	x3, [sp, #24]
  3c:	d2800003 	mov	x3, #0x0                   	// #0
  40:	94000000 	bl	0 <clock_gettime>
			40: R_AARCH64_CALL26	clock_gettime
  44:	a940dbf7 	ldp	x23, x22, [sp, #8]
  48:	d2994000 	mov	x0, #0xca00                	// #51712
  4c:	f2a77340 	movk	x0, #0x3b9a, lsl #16
  50:	d2860724 	mov	x4, #0x3039                	// #12345
  54:	aa0403e2 	mov	x2, x4
  58:	9b007ef7 	mul	x23, x23, x0
  5c:	90000015 	adrp	x21, 0 <main>
			5c: R_AARCH64_ADR_PREL_PG_HI21	.bss
  60:	d28f0866 	mov	x6, #0x7843                	// #30787
  64:	910002a3 	add	x3, x21, #0x0
			64: R_AARCH64_ADD_ABS_LO12_NC	.bss
  68:	f2ae3686 	movk	x6, #0x71b4, lsl #16
  6c:	91409c67 	add	x7, x3, #0x27, lsl #12
  70:	d289cda8 	mov	x8, #0x4e6d                	// #20077
  74:	f2cb5886 	movk	x6, #0x5ac4, lsl #32
  78:	d290d405 	mov	x5, #0x86a0                	// #34464
  7c:	910400e7 	add	x7, x7, #0x100
  80:	f2a838c8 	movk	x8, #0x41c6, lsl #16
  84:	f2e14f86 	movk	x6, #0xa7c, lsl #48
  88:	f2a00025 	movk	x5, #0x1, lsl #16
  8c:	d503201f 	nop
  90:	9b081040 	madd	x0, x2, x8, x4
  94:	92407802 	and	x2, x0, #0x7fffffff
  98:	d3457800 	ubfx	x0, x0, #5, #26
  9c:	9bc67c00 	umulh	x0, x0, x6
  a0:	d347fc00 	lsr	x0, x0, #7
  a4:	9b058800 	msub	x0, x0, x5, x2
  a8:	f8008460 	str	x0, [x3], #8
  ac:	eb0300ff 	cmp	x7, x3
  b0:	54ffff01 	b.ne	90 <main+0x90>  // b.any
  b4:	910002a2 	add	x2, x21, #0x0
			b4: R_AARCH64_ADD_ABS_LO12_NC	.bss
  b8:	d2800006 	mov	x6, #0x0                   	// #0
  bc:	d289c408 	mov	x8, #0x4e20                	// #20000
  c0:	f8667844 	ldr	x4, [x2, x6, lsl #3]
  c4:	d10004c3 	sub	x3, x6, #0x1
  c8:	aa0603e5 	mov	x5, x6
  cc:	b4000085 	cbz	x5, dc <main+0xdc>
  d0:	f8637840 	ldr	x0, [x2, x3, lsl #3]
  d4:	eb00009f 	cmp	x4, x0
  d8:	54000903 	b.cc	1f8 <main+0x1f8>  // b.lo, b.ul, b.last
  dc:	910004c6 	add	x6, x6, #0x1
  e0:	f8257844 	str	x4, [x2, x5, lsl #3]
  e4:	eb0800df 	cmp	x6, x8
  e8:	54fffec1 	b.ne	c0 <main+0xc0>  // b.any
  ec:	910002a4 	add	x4, x21, #0x0
			ec: R_AARCH64_ADD_ABS_LO12_NC	.bss
  f0:	d2800000 	mov	x0, #0x0                   	// #0
  f4:	d289c403 	mov	x3, #0x4e20                	// #20000
  f8:	f8607882 	ldr	x2, [x4, x0, lsl #3]
  fc:	91000400 	add	x0, x0, #0x1
 100:	9b004c53 	madd	x19, x2, x0, x19
 104:	eb03001f 	cmp	x0, x3
 108:	54ffff81 	b.ne	f8 <main+0xf8>  // b.any
 10c:	f94002a2 	ldr	x2, [x21]
			10c: R_AARCH64_LDST64_ABS_LO12_NC	.bss
 110:	90000000 	adrp	x0, 0 <main>
			110: R_AARCH64_ADR_PREL_PG_HI21	.bss+0x8
 114:	91000000 	add	x0, x0, #0x0
			114: R_AARCH64_ADD_ABS_LO12_NC	.bss+0x8
 118:	aa0203e3 	mov	x3, x2
 11c:	f8408402 	ldr	x2, [x0], #8
 120:	eb02007f 	cmp	x3, x2
 124:	9a949694 	cinc	x20, x20, hi	// hi = pmore
 128:	eb0000ff 	cmp	x7, x0
 12c:	54ffff61 	b.ne	118 <main+0x118>  // b.any
 130:	52800020 	mov	w0, #0x1                   	// #1
 134:	aa1803e1 	mov	x1, x24
 138:	94000000 	bl	0 <clock_gettime>
			138: R_AARCH64_CALL26	clock_gettime
 13c:	90000018 	adrp	x24, 0 <main>
			13c: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
 140:	a94093e1 	ldp	x1, x4, [sp, #8]
 144:	d2994002 	mov	x2, #0xca00                	// #51712
 148:	90000000 	adrp	x0, 0 <stderr>
			148: R_AARCH64_ADR_GOT_PAGE	stderr
 14c:	f9400000 	ldr	x0, [x0]
			14c: R_AARCH64_LD64_GOT_LO12_NC	stderr
 150:	f2a77342 	movk	x2, #0x3b9a, lsl #16
 154:	91000318 	add	x24, x24, #0x0
			154: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
 158:	9b027c21 	mul	x1, x1, x2
 15c:	cb170084 	sub	x4, x4, x23
 160:	f9400000 	ldr	x0, [x0]
 164:	cb160023 	sub	x3, x1, x22
 168:	8b030083 	add	x3, x4, x3
 16c:	aa1803e2 	mov	x2, x24
 170:	52800041 	mov	w1, #0x2                   	// #2
 174:	94000000 	bl	0 <__fprintf_chk>
			174: R_AARCH64_CALL26	__fprintf_chk
 178:	aa1303e2 	mov	x2, x19
 17c:	aa1803e1 	mov	x1, x24
 180:	52800040 	mov	w0, #0x2                   	// #2
 184:	94000000 	bl	0 <__printf_chk>
			184: R_AARCH64_CALL26	__printf_chk
 188:	aa1403e2 	mov	x2, x20
 18c:	aa1803e1 	mov	x1, x24
 190:	52800040 	mov	w0, #0x2                   	// #2
 194:	94000000 	bl	0 <__printf_chk>
			194: R_AARCH64_CALL26	__printf_chk
 198:	f94002a2 	ldr	x2, [x21]
			198: R_AARCH64_LDST64_ABS_LO12_NC	.bss
 19c:	910002b5 	add	x21, x21, #0x0
			19c: R_AARCH64_ADD_ABS_LO12_NC	.bss
 1a0:	914082b5 	add	x21, x21, #0x20, lsl #12
 1a4:	aa1803e1 	mov	x1, x24
 1a8:	52800040 	mov	w0, #0x2                   	// #2
 1ac:	94000000 	bl	0 <__printf_chk>
			1ac: R_AARCH64_CALL26	__printf_chk
 1b0:	f9787ea2 	ldr	x2, [x21, #28920]
 1b4:	aa1803e1 	mov	x1, x24
 1b8:	52800040 	mov	w0, #0x2                   	// #2
 1bc:	94000000 	bl	0 <__printf_chk>
			1bc: R_AARCH64_CALL26	__printf_chk
 1c0:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			1c0: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
 1c4:	f9400000 	ldr	x0, [x0]
			1c4: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
 1c8:	f9400fe2 	ldr	x2, [sp, #24]
 1cc:	f9400001 	ldr	x1, [x0]
 1d0:	eb010042 	subs	x2, x2, x1
 1d4:	d2800001 	mov	x1, #0x0                   	// #0
 1d8:	54000181 	b.ne	208 <main+0x208>  // b.any
 1dc:	a9427bfd 	ldp	x29, x30, [sp, #32]
 1e0:	52800000 	mov	w0, #0x0                   	// #0
 1e4:	a94353f3 	ldp	x19, x20, [sp, #48]
 1e8:	a9445bf5 	ldp	x21, x22, [sp, #64]
 1ec:	a94563f7 	ldp	x23, x24, [sp, #80]
 1f0:	910183ff 	add	sp, sp, #0x60
 1f4:	d65f03c0 	ret
 1f8:	d1000463 	sub	x3, x3, #0x1
 1fc:	f8257840 	str	x0, [x2, x5, lsl #3]
 200:	d10004a5 	sub	x5, x5, #0x1
 204:	17ffffb2 	b	cc <main+0xcc>
 208:	94000000 	bl	0 <__stack_chk_fail>
			208: R_AARCH64_CALL26	__stack_chk_fail
