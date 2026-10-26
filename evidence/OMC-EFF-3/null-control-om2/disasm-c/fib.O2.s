
/tmp/omc-eff-3-aa/work/obj/fib.O2.o:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <fib>:
   0:	a9b17bfd 	stp	x29, x30, [sp, #-240]!
   4:	910003fd 	mov	x29, sp
   8:	a90153f3 	stp	x19, x20, [sp, #16]
   c:	aa0003f4 	mov	x20, x0
  10:	f100041f 	cmp	x0, #0x1
  14:	54001ea9 	b.ls	3e8 <fib+0x3e8>  // b.plast
  18:	d1000400 	sub	x0, x0, #0x1
  1c:	a90363f7 	stp	x23, x24, [sp, #48]
  20:	927ff817 	and	x23, x0, #0xfffffffffffffffe
  24:	cb170297 	sub	x23, x20, x23
  28:	a9025bf5 	stp	x21, x22, [sp, #32]
  2c:	aa0003f3 	mov	x19, x0
  30:	a9046bf9 	stp	x25, x26, [sp, #64]
  34:	d2800016 	mov	x22, #0x0                   	// #0
  38:	aa1603fa 	mov	x26, x22
  3c:	eb17029f 	cmp	x20, x23
  40:	54001cc0 	b.eq	3d8 <fib+0x3d8>  // b.none
  44:	d1000a94 	sub	x20, x20, #0x2
  48:	a90573fb 	stp	x27, x28, [sp, #80]
  4c:	927ffa95 	and	x21, x20, #0xfffffffffffffffe
  50:	cb150015 	sub	x21, x0, x21
  54:	d280001b 	mov	x27, #0x0                   	// #0
  58:	aa1503f9 	mov	x25, x21
  5c:	d1000678 	sub	x24, x19, #0x1
  60:	eb19027f 	cmp	x19, x25
  64:	54001a80 	b.eq	3b4 <fib+0x3b4>  // b.none
  68:	d1000a73 	sub	x19, x19, #0x2
  6c:	aa1903e3 	mov	x3, x25
  70:	927ffa75 	and	x21, x19, #0xfffffffffffffffe
  74:	aa1303f9 	mov	x25, x19
  78:	cb150315 	sub	x21, x24, x21
  7c:	aa1403e4 	mov	x4, x20
  80:	aa1503f3 	mov	x19, x21
  84:	aa1703f5 	mov	x21, x23
  88:	d2800016 	mov	x22, #0x0                   	// #0
  8c:	d100071c 	sub	x28, x24, #0x1
  90:	eb13031f 	cmp	x24, x19
  94:	540017a0 	b.eq	388 <fib+0x388>  // b.none
  98:	d1000b18 	sub	x24, x24, #0x2
  9c:	aa1903e2 	mov	x2, x25
  a0:	927ffb05 	and	x5, x24, #0xfffffffffffffffe
  a4:	aa1303e6 	mov	x6, x19
  a8:	cb050385 	sub	x5, x28, x5
  ac:	d2800017 	mov	x23, #0x0                   	// #0
  b0:	aa0503f9 	mov	x25, x5
  b4:	aa1503e5 	mov	x5, x21
  b8:	d1000794 	sub	x20, x28, #0x1
  bc:	eb19039f 	cmp	x28, x25
  c0:	540015a0 	b.eq	374 <fib+0x374>  // b.none
  c4:	d1000b9c 	sub	x28, x28, #0x2
  c8:	d2800007 	mov	x7, #0x0                   	// #0
  cc:	927ffb95 	and	x21, x28, #0xfffffffffffffffe
  d0:	aa1803e1 	mov	x1, x24
  d4:	cb150295 	sub	x21, x20, x21
  d8:	aa0703f8 	mov	x24, x7
  dc:	aa1903e8 	mov	x8, x25
  e0:	aa1503e7 	mov	x7, x21
  e4:	aa1603f9 	mov	x25, x22
  e8:	aa0203f5 	mov	x21, x2
  ec:	aa1c03e2 	mov	x2, x28
  f0:	aa1703fc 	mov	x28, x23
  f4:	d1000693 	sub	x19, x20, #0x1
  f8:	eb07029f 	cmp	x20, x7
  fc:	540012a0 	b.eq	350 <fib+0x350>  // b.none
 100:	d1000a94 	sub	x20, x20, #0x2
 104:	aa1c03ea 	mov	x10, x28
 108:	927ffa89 	and	x9, x20, #0xfffffffffffffffe
 10c:	aa1403ed 	mov	x13, x20
 110:	d2800017 	mov	x23, #0x0                   	// #0
 114:	cb090269 	sub	x9, x19, x9
 118:	aa1903fc 	mov	x28, x25
 11c:	aa1803eb 	mov	x11, x24
 120:	aa1503ec 	mov	x12, x21
 124:	aa1703f4 	mov	x20, x23
 128:	aa1303f6 	mov	x22, x19
 12c:	d10006d9 	sub	x25, x22, #0x1
 130:	eb0902df 	cmp	x22, x9
 134:	54000f00 	b.eq	314 <fib+0x314>  // b.none
 138:	d1000ed5 	sub	x21, x22, #0x3
 13c:	d1000ad6 	sub	x22, x22, #0x2
 140:	927ffad8 	and	x24, x22, #0xfffffffffffffffe
 144:	aa1503ee 	mov	x14, x21
 148:	cb1802b8 	sub	x24, x21, x24
 14c:	d2800017 	mov	x23, #0x0                   	// #0
 150:	aa1803ef 	mov	x15, x24
 154:	d1000738 	sub	x24, x25, #0x1
 158:	eb0e01ff 	cmp	x15, x14
 15c:	54000ee0 	b.eq	338 <fib+0x338>  // b.none
 160:	927ff9d2 	and	x18, x14, #0xfffffffffffffffe
 164:	aa0a03f3 	mov	x19, x10
 168:	cb120312 	sub	x18, x24, x18
 16c:	d2800000 	mov	x0, #0x0                   	// #0
 170:	a9060be1 	stp	x1, x2, [sp, #96]
 174:	d1000802 	sub	x2, x0, #0x2
 178:	d1000701 	sub	x1, x24, #0x1
 17c:	eb12031f 	cmp	x24, x18
 180:	54000e00 	b.eq	340 <fib+0x340>  // b.none
 184:	aa1603e0 	mov	x0, x22
 188:	aa1403f6 	mov	x22, x20
 18c:	aa0003ea 	mov	x10, x0
 190:	aa1303f4 	mov	x20, x19
 194:	aa0103f3 	mov	x19, x1
 198:	d2800015 	mov	x21, #0x0                   	// #0
 19c:	d1000660 	sub	x0, x19, #0x1
 1a0:	d1000a73 	sub	x19, x19, #0x2
 1a4:	a90707eb 	stp	x11, x1, [sp, #112]
 1a8:	a90837ec 	stp	x12, x13, [sp, #128]
 1ac:	a90917ea 	stp	x10, x5, [sp, #144]
 1b0:	a90a1be3 	stp	x3, x6, [sp, #160]
 1b4:	a90b1fe8 	stp	x8, x7, [sp, #176]
 1b8:	a90c3fe9 	stp	x9, x15, [sp, #192]
 1bc:	a90d4bee 	stp	x14, x18, [sp, #208]
 1c0:	a90e13e2 	stp	x2, x4, [sp, #224]
 1c4:	97ffff8f 	bl	0 <fib>
 1c8:	f100067f 	cmp	x19, #0x1
 1cc:	a94707eb 	ldp	x11, x1, [sp, #112]
 1d0:	8b0002b5 	add	x21, x21, x0
 1d4:	a94837ec 	ldp	x12, x13, [sp, #128]
 1d8:	a94917ea 	ldp	x10, x5, [sp, #144]
 1dc:	a94a1be3 	ldp	x3, x6, [sp, #160]
 1e0:	a94b1fe8 	ldp	x8, x7, [sp, #176]
 1e4:	a94c3fe9 	ldp	x9, x15, [sp, #192]
 1e8:	a94d4bee 	ldp	x14, x18, [sp, #208]
 1ec:	a94e13e2 	ldp	x2, x4, [sp, #224]
 1f0:	54fffd68 	b.hi	19c <fib+0x19c>  // b.pmore
 1f4:	d1000f00 	sub	x0, x24, #0x3
 1f8:	d1000b18 	sub	x24, x24, #0x2
 1fc:	aa1403f3 	mov	x19, x20
 200:	aa1603f4 	mov	x20, x22
 204:	cb4007e0 	neg	x0, x0, lsr #1
 208:	aa0a03f6 	mov	x22, x10
 20c:	8b000420 	add	x0, x1, x0, lsl #1
 210:	8b020000 	add	x0, x0, x2
 214:	8b0002a0 	add	x0, x21, x0
 218:	f100071f 	cmp	x24, #0x1
 21c:	54fffac1 	b.ne	174 <fib+0x174>  // b.any
 220:	a9460be1 	ldp	x1, x2, [sp, #96]
 224:	aa1303ea 	mov	x10, x19
 228:	91000400 	add	x0, x0, #0x1
 22c:	d1000b39 	sub	x25, x25, #0x2
 230:	8b0002f7 	add	x23, x23, x0
 234:	d10009ce 	sub	x14, x14, #0x2
 238:	f100073f 	cmp	x25, #0x1
 23c:	54fff8c1 	b.ne	154 <fib+0x154>  // b.any
 240:	910006e0 	add	x0, x23, #0x1
 244:	8b000294 	add	x20, x20, x0
 248:	f10006df 	cmp	x22, #0x1
 24c:	54fff708 	b.hi	12c <fib+0x12c>  // b.pmore
 250:	aa1403f7 	mov	x23, x20
 254:	aa1c03f9 	mov	x25, x28
 258:	aa0b03f8 	mov	x24, x11
 25c:	aa0a03fc 	mov	x28, x10
 260:	aa0c03f5 	mov	x21, x12
 264:	aa0d03f4 	mov	x20, x13
 268:	8b1602f3 	add	x19, x23, x22
 26c:	8b130318 	add	x24, x24, x19
 270:	f100069f 	cmp	x20, #0x1
 274:	54fff408 	b.hi	f4 <fib+0xf4>  // b.pmore
 278:	aa1803e7 	mov	x7, x24
 27c:	aa1903f6 	mov	x22, x25
 280:	aa1c03f7 	mov	x23, x28
 284:	aa0103f8 	mov	x24, x1
 288:	aa0203fc 	mov	x28, x2
 28c:	aa0803f9 	mov	x25, x8
 290:	aa1503e2 	mov	x2, x21
 294:	8b1400f4 	add	x20, x7, x20
 298:	8b1402f7 	add	x23, x23, x20
 29c:	f100079f 	cmp	x28, #0x1
 2a0:	54fff0c8 	b.hi	b8 <fib+0xb8>  // b.pmore
 2a4:	aa0203f9 	mov	x25, x2
 2a8:	aa0503f5 	mov	x21, x5
 2ac:	aa0603f3 	mov	x19, x6
 2b0:	8b1c02e0 	add	x0, x23, x28
 2b4:	8b0002d6 	add	x22, x22, x0
 2b8:	f100071f 	cmp	x24, #0x1
 2bc:	54ffee88 	b.hi	8c <fib+0x8c>  // b.pmore
 2c0:	aa1903f3 	mov	x19, x25
 2c4:	8b1802d8 	add	x24, x22, x24
 2c8:	aa1503f7 	mov	x23, x21
 2cc:	aa0303f9 	mov	x25, x3
 2d0:	aa0403f4 	mov	x20, x4
 2d4:	8b18037b 	add	x27, x27, x24
 2d8:	f100067f 	cmp	x19, #0x1
 2dc:	54ffec08 	b.hi	5c <fib+0x5c>  // b.pmore
 2e0:	8b130373 	add	x19, x27, x19
 2e4:	8b13035a 	add	x26, x26, x19
 2e8:	f100069f 	cmp	x20, #0x1
 2ec:	540006c8 	b.hi	3c4 <fib+0x3c4>  // b.pmore
 2f0:	8b1a0294 	add	x20, x20, x26
 2f4:	a9425bf5 	ldp	x21, x22, [sp, #32]
 2f8:	aa1403e0 	mov	x0, x20
 2fc:	a94153f3 	ldp	x19, x20, [sp, #16]
 300:	a94363f7 	ldp	x23, x24, [sp, #48]
 304:	a9446bf9 	ldp	x25, x26, [sp, #64]
 308:	a94573fb 	ldp	x27, x28, [sp, #80]
 30c:	a8cf7bfd 	ldp	x29, x30, [sp], #240
 310:	d65f03c0 	ret
 314:	aa1403f7 	mov	x23, x20
 318:	aa1903f6 	mov	x22, x25
 31c:	aa0b03f8 	mov	x24, x11
 320:	aa1c03f9 	mov	x25, x28
 324:	aa0c03f5 	mov	x21, x12
 328:	aa0a03fc 	mov	x28, x10
 32c:	aa0d03f4 	mov	x20, x13
 330:	8b1702d3 	add	x19, x22, x23
 334:	17ffffce 	b	26c <fib+0x26c>
 338:	8b170300 	add	x0, x24, x23
 33c:	17ffffc2 	b	244 <fib+0x244>
 340:	8b000020 	add	x0, x1, x0
 344:	aa1303ea 	mov	x10, x19
 348:	a9460be1 	ldp	x1, x2, [sp, #96]
 34c:	17ffffb8 	b	22c <fib+0x22c>
 350:	aa1803e7 	mov	x7, x24
 354:	aa1903f6 	mov	x22, x25
 358:	aa1c03f7 	mov	x23, x28
 35c:	aa0103f8 	mov	x24, x1
 360:	aa0203fc 	mov	x28, x2
 364:	aa0803f9 	mov	x25, x8
 368:	aa1503e2 	mov	x2, x21
 36c:	8b070274 	add	x20, x19, x7
 370:	17ffffca 	b	298 <fib+0x298>
 374:	aa0203f9 	mov	x25, x2
 378:	aa0503f5 	mov	x21, x5
 37c:	aa0603f3 	mov	x19, x6
 380:	8b170280 	add	x0, x20, x23
 384:	17ffffcc 	b	2b4 <fib+0x2b4>
 388:	aa1903f3 	mov	x19, x25
 38c:	8b160398 	add	x24, x28, x22
 390:	aa1503f7 	mov	x23, x21
 394:	aa0303f9 	mov	x25, x3
 398:	aa0403f4 	mov	x20, x4
 39c:	8b18037b 	add	x27, x27, x24
 3a0:	f100067f 	cmp	x19, #0x1
 3a4:	54fff9e9 	b.ls	2e0 <fib+0x2e0>  // b.plast
 3a8:	d1000678 	sub	x24, x19, #0x1
 3ac:	eb19027f 	cmp	x19, x25
 3b0:	54ffe5c1 	b.ne	68 <fib+0x68>  // b.any
 3b4:	8b1b0313 	add	x19, x24, x27
 3b8:	8b13035a 	add	x26, x26, x19
 3bc:	f100069f 	cmp	x20, #0x1
 3c0:	54fff989 	b.ls	2f0 <fib+0x2f0>  // b.plast
 3c4:	d1000680 	sub	x0, x20, #0x1
 3c8:	a94573fb 	ldp	x27, x28, [sp, #80]
 3cc:	aa0003f3 	mov	x19, x0
 3d0:	eb17029f 	cmp	x20, x23
 3d4:	54ffe381 	b.ne	44 <fib+0x44>  // b.any
 3d8:	a9425bf5 	ldp	x21, x22, [sp, #32]
 3dc:	8b1a0014 	add	x20, x0, x26
 3e0:	a94363f7 	ldp	x23, x24, [sp, #48]
 3e4:	a9446bf9 	ldp	x25, x26, [sp, #64]
 3e8:	aa1403e0 	mov	x0, x20
 3ec:	a94153f3 	ldp	x19, x20, [sp, #16]
 3f0:	a8cf7bfd 	ldp	x29, x30, [sp], #240
 3f4:	d65f03c0 	ret

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
  1c:	910023f3 	add	x19, sp, #0x8
  20:	aa1303e1 	mov	x1, x19
  24:	a9045bf5 	stp	x21, x22, [sp, #64]
  28:	d2994016 	mov	x22, #0xca00                	// #51712
  2c:	f2a77356 	movk	x22, #0x3b9a, lsl #16
  30:	f9002bf7 	str	x23, [sp, #80]
  34:	f9400043 	ldr	x3, [x2]
  38:	f9000fe3 	str	x3, [sp, #24]
  3c:	d2800003 	mov	x3, #0x0                   	// #0
  40:	94000000 	bl	0 <clock_gettime>
			40: R_AARCH64_CALL26	clock_gettime
  44:	a940dff5 	ldp	x21, x23, [sp, #8]
  48:	d28004c0 	mov	x0, #0x26                  	// #38
  4c:	9b167eb5 	mul	x21, x21, x22
  50:	94000000 	bl	0 <main>
			50: R_AARCH64_CALL26	.text
  54:	aa0003f4 	mov	x20, x0
  58:	52800020 	mov	w0, #0x1                   	// #1
  5c:	aa1303e1 	mov	x1, x19
  60:	94000000 	bl	0 <clock_gettime>
			60: R_AARCH64_CALL26	clock_gettime
  64:	90000013 	adrp	x19, 0 <main>
			64: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  68:	a94097e3 	ldp	x3, x5, [sp, #8]
  6c:	91000273 	add	x19, x19, #0x0
			6c: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  70:	90000000 	adrp	x0, 0 <stderr>
			70: R_AARCH64_ADR_GOT_PAGE	stderr
  74:	f9400000 	ldr	x0, [x0]
			74: R_AARCH64_LD64_GOT_LO12_NC	stderr
  78:	aa1303e2 	mov	x2, x19
  7c:	52800041 	mov	w1, #0x2                   	// #2
  80:	9b167c64 	mul	x4, x3, x22
  84:	cb1500a5 	sub	x5, x5, x21
  88:	f9400000 	ldr	x0, [x0]
  8c:	cb170083 	sub	x3, x4, x23
  90:	8b0300a3 	add	x3, x5, x3
  94:	94000000 	bl	0 <__fprintf_chk>
			94: R_AARCH64_CALL26	__fprintf_chk
  98:	aa1403e2 	mov	x2, x20
  9c:	aa1303e1 	mov	x1, x19
  a0:	52800040 	mov	w0, #0x2                   	// #2
  a4:	94000000 	bl	0 <__printf_chk>
			a4: R_AARCH64_CALL26	__printf_chk
  a8:	90000000 	adrp	x0, 0 <__stack_chk_guard>
			a8: R_AARCH64_ADR_GOT_PAGE	__stack_chk_guard
  ac:	f9400000 	ldr	x0, [x0]
			ac: R_AARCH64_LD64_GOT_LO12_NC	__stack_chk_guard
  b0:	f9400fe2 	ldr	x2, [sp, #24]
  b4:	f9400001 	ldr	x1, [x0]
  b8:	eb010042 	subs	x2, x2, x1
  bc:	d2800001 	mov	x1, #0x0                   	// #0
  c0:	54000101 	b.ne	e0 <main+0xe0>  // b.any
  c4:	a9427bfd 	ldp	x29, x30, [sp, #32]
  c8:	52800000 	mov	w0, #0x0                   	// #0
  cc:	a94353f3 	ldp	x19, x20, [sp, #48]
  d0:	a9445bf5 	ldp	x21, x22, [sp, #64]
  d4:	f9402bf7 	ldr	x23, [sp, #80]
  d8:	910183ff 	add	sp, sp, #0x60
  dc:	d65f03c0 	ret
  e0:	94000000 	bl	0 <__stack_chk_fail>
			e0: R_AARCH64_CALL26	__stack_chk_fail
