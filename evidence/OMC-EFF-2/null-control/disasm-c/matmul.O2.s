
/tmp/omc-eff-2-aa/work/obj/matmul.O2.o:     file format elf64-littleaarch64


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
  20:	910023f6 	add	x22, sp, #0x8
  24:	aa1603e1 	mov	x1, x22
  28:	f9002bf7 	str	x23, [sp, #80]
  2c:	f9400043 	ldr	x3, [x2]
  30:	f9000fe3 	str	x3, [sp, #24]
  34:	d2800003 	mov	x3, #0x0                   	// #0
  38:	94000000 	bl	0 <clock_gettime>
			38: R_AARCH64_CALL26	clock_gettime
  3c:	a940d3f5 	ldp	x21, x20, [sp, #8]
  40:	d2994000 	mov	x0, #0xca00                	// #51712
  44:	f2a77340 	movk	x0, #0x3b9a, lsl #16
  48:	9b007eb5 	mul	x21, x21, x0
  4c:	d289d8ac 	mov	x12, #0x4ec5                	// #20165
  50:	d291746b 	mov	x11, #0x8ba3                	// #35747
  54:	90000009 	adrp	x9, 0 <main>
			54: R_AARCH64_ADR_PREL_PG_HI21	.bss+0x15f900
  58:	9000000d 	adrp	x13, 0 <main>
			58: R_AARCH64_ADR_PREL_PG_HI21	.bss+0xafc80
  5c:	91000129 	add	x9, x9, #0x0
			5c: R_AARCH64_ADD_ABS_LO12_NC	.bss+0x15f900
  60:	910001ad 	add	x13, x13, #0x0
			60: R_AARCH64_ADD_ABS_LO12_NC	.bss+0xafc80
  64:	f2b89d8c 	movk	x12, #0xc4ec, lsl #16
  68:	f2b745cb 	movk	x11, #0xba2e, lsl #16
  6c:	f2dd89cc 	movk	x12, #0xec4e, lsl #32
  70:	f2d45d0b 	movk	x11, #0xa2e8, lsl #32
  74:	d28bf210 	mov	x16, #0x5f90                	// #24464
  78:	aa0903ea 	mov	x10, x9
  7c:	aa0d03e8 	mov	x8, x13
  80:	d2802587 	mov	x7, #0x12c                 	// #300
  84:	d280000f 	mov	x15, #0x0                   	// #0
  88:	d280000e 	mov	x14, #0x0                   	// #0
  8c:	f2e9d88c 	movk	x12, #0x4ec4, lsl #48
  90:	f2e5d16b 	movk	x11, #0x2e8b, lsl #48
  94:	f2a00030 	movk	x16, #0x1, lsl #16
  98:	d104b0e2 	sub	x2, x7, #0x12c
  9c:	aa0e03e6 	mov	x6, x14
  a0:	9bcb7c45 	umulh	x5, x2, x11
  a4:	9bcc7cc4 	umulh	x4, x6, x12
  a8:	927ff8a0 	and	x0, x5, #0xfffffffffffffffe
  ac:	d341fca5 	lsr	x5, x5, #1
  b0:	d342fc84 	lsr	x4, x4, #2
  b4:	8b050000 	add	x0, x0, x5
  b8:	8b040483 	add	x3, x4, x4, lsl #1
  bc:	d37ef400 	lsl	x0, x0, #2
  c0:	cb050000 	sub	x0, x0, x5
  c4:	8b030883 	add	x3, x4, x3, lsl #2
  c8:	cb000040 	sub	x0, x2, x0
  cc:	cb0300c3 	sub	x3, x6, x3
  d0:	f8227900 	str	x0, [x8, x2, lsl #3]
  d4:	f8227943 	str	x3, [x10, x2, lsl #3]
  d8:	91000442 	add	x2, x2, #0x1
  dc:	910008c6 	add	x6, x6, #0x2
  e0:	eb07005f 	cmp	x2, x7
  e4:	54fffde1 	b.ne	a0 <main+0xa0>  // b.any
  e8:	9104b1ef 	add	x15, x15, #0x12c
  ec:	910005ce 	add	x14, x14, #0x1
  f0:	9125214a 	add	x10, x10, #0x948
  f4:	91252108 	add	x8, x8, #0x948
  f8:	91000c47 	add	x7, x2, #0x3
  fc:	eb1001ff 	cmp	x15, x16
 100:	54fffcc1 	b.ne	98 <main+0x98>  // b.any
 104:	9142bdad 	add	x13, x13, #0xaf, lsl #12
 108:	929f8fea 	mov	x10, #0xffffffffffff0380    	// #-64640
 10c:	913201ad 	add	x13, x13, #0xc80
 110:	d28bf210 	mov	x16, #0x5f90                	// #24464
 114:	912581ab 	add	x11, x13, #0x960
 118:	d280000f 	mov	x15, #0x0                   	// #0
 11c:	90000011 	adrp	x17, 0 <main>
			11c: R_AARCH64_ADR_PREL_PG_HI21	.bss
 120:	f2bffeaa 	movk	x10, #0xfff5, lsl #16
 124:	f2a00030 	movk	x16, #0x1, lsl #16
 128:	d37df1e8 	lsl	x8, x15, #3
 12c:	9100022e 	add	x14, x17, #0x0
			12c: R_AARCH64_ADD_ABS_LO12_NC	.bss
 130:	8b09010c 	add	x12, x8, x9
 134:	aa0d03e6 	mov	x6, x13
 138:	8b0801c8 	add	x8, x14, x8
 13c:	d2800007 	mov	x7, #0x0                   	// #0
 140:	8b0a00c0 	add	x0, x6, x10
 144:	aa0c03e3 	mov	x3, x12
 148:	d2800002 	mov	x2, #0x0                   	// #0
 14c:	d503201f 	nop
 150:	f9400004 	ldr	x4, [x0]
 154:	91258000 	add	x0, x0, #0x960
 158:	f8408465 	ldr	x5, [x3], #8
 15c:	9b0408a2 	madd	x2, x5, x4, x2
 160:	eb0000df 	cmp	x6, x0
 164:	54ffff61 	b.ne	150 <main+0x150>  // b.any
 168:	910020c6 	add	x6, x6, #0x8
 16c:	f8277902 	str	x2, [x8, x7, lsl #3]
 170:	910004e7 	add	x7, x7, #0x1
 174:	eb06017f 	cmp	x11, x6
 178:	54fffe41 	b.ne	140 <main+0x140>  // b.any
 17c:	9104b1ef 	add	x15, x15, #0x12c
 180:	eb1001ff 	cmp	x15, x16
 184:	54fffd21 	b.ne	128 <main+0x128>  // b.any
 188:	d28bf205 	mov	x5, #0x5f90                	// #24464
 18c:	912581c3 	add	x3, x14, #0x960
 190:	d2800004 	mov	x4, #0x0                   	// #0
 194:	d2800017 	mov	x23, #0x0                   	// #0
 198:	d2800013 	mov	x19, #0x0                   	// #0
 19c:	f2a00025 	movk	x5, #0x1, lsl #16
 1a0:	d1258060 	sub	x0, x3, #0x960
 1a4:	d503201f 	nop
 1a8:	f8408402 	ldr	x2, [x0], #8
 1ac:	8b020273 	add	x19, x19, x2
 1b0:	eb00007f 	cmp	x3, x0
 1b4:	54ffffa1 	b.ne	1a8 <main+0x1a8>  // b.any
 1b8:	f94001c0 	ldr	x0, [x14]
 1bc:	9104b084 	add	x4, x4, #0x12c
 1c0:	91258063 	add	x3, x3, #0x960
 1c4:	9125a1ce 	add	x14, x14, #0x968
 1c8:	8b0002f7 	add	x23, x23, x0
 1cc:	eb05009f 	cmp	x4, x5
 1d0:	54fffe81 	b.ne	1a0 <main+0x1a0>  // b.any
 1d4:	52800020 	mov	w0, #0x1                   	// #1
 1d8:	aa1603e1 	mov	x1, x22
 1dc:	94000000 	bl	0 <clock_gettime>
			1dc: R_AARCH64_CALL26	clock_gettime
 1e0:	90000016 	adrp	x22, 0 <main>
			1e0: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
 1e4:	a94093e1 	ldp	x1, x4, [sp, #8]
 1e8:	d2994002 	mov	x2, #0xca00                	// #51712
 1ec:	90000000 	adrp	x0, 0 <stderr>
			1ec: R_AARCH64_ADR_GOT_PAGE	stderr
 1f0:	f9400000 	ldr	x0, [x0]
			1f0: R_AARCH64_LD64_GOT_LO12_NC	stderr
 1f4:	f2a77342 	movk	x2, #0x3b9a, lsl #16
 1f8:	910002d6 	add	x22, x22, #0x0
			1f8: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
 1fc:	9b027c21 	mul	x1, x1, x2
 200:	cb150084 	sub	x4, x4, x21
 204:	f9400000 	ldr	x0, [x0]
 208:	cb140023 	sub	x3, x1, x20
 20c:	8b030083 	add	x3, x4, x3
 210:	aa1603e2 	mov	x2, x22
 214:	52800041 	mov	w1, #0x2                   	// #2
 218:	94000000 	bl	0 <__fprintf_chk>
			218: R_AARCH64_CALL26	__fprintf_chk
 21c:	aa1303e2 	mov	x2, x19
 220:	aa1603e1 	mov	x1, x22
 224:	52800040 	mov	w0, #0x2                   	// #2
 228:	94000000 	bl	0 <__printf_chk>
			228: R_AARCH64_CALL26	__printf_chk
 22c:	aa1703e2 	mov	x2, x23
 230:	aa1603e1 	mov	x1, x22
 234:	52800040 	mov	w0, #0x2                   	// #2
 238:	94000000 	bl	0 <__printf_chk>
			238: R_AARCH64_CALL26	__printf_chk
 23c:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			23c: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
 240:	f9400000 	ldr	x0, [x0]
			240: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
 244:	f9400fe2 	ldr	x2, [sp, #24]
 248:	f9400001 	ldr	x1, [x0]
 24c:	eb010042 	subs	x2, x2, x1
 250:	d2800001 	mov	x1, #0x0                   	// #0
 254:	54000101 	b.ne	274 <main+0x274>  // b.any
 258:	a9427bfd 	ldp	x29, x30, [sp, #32]
 25c:	52800000 	mov	w0, #0x0                   	// #0
 260:	a94353f3 	ldp	x19, x20, [sp, #48]
 264:	a9445bf5 	ldp	x21, x22, [sp, #64]
 268:	f9402bf7 	ldr	x23, [sp, #80]
 26c:	910183ff 	add	sp, sp, #0x60
 270:	d65f03c0 	ret
 274:	94000000 	bl	0 <__stack_chk_fail>
			274: R_AARCH64_CALL26	__stack_chk_fail
