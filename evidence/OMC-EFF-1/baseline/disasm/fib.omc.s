  b0:	14000091 	b	0x2f4
  b4:	d2800141 	mov	x1, #0xa                   	// #10
  b8:	d28003e2 	mov	x2, #0x1f                  	// #31
  bc:	d2a21003 	mov	x3, #0x10800000            	// #276824064
  c0:	38226861 	strb	w1, [x3, x2]
  c4:	d28003e1 	mov	x1, #0x1f                  	// #31
  c8:	aa0003e2 	mov	x2, x0
  cc:	d2800003 	mov	x3, #0x0                   	// #0
  d0:	f100245f 	cmp	x2, #0x9
  d4:	54000289 	b.ls	0x124  // b.plast
  d8:	d2884804 	mov	x4, #0x4240                	// #16960
  dc:	f2a001e4 	movk	x4, #0xf, lsl #16
  e0:	eb04007f 	cmp	x3, x4
  e4:	540007c2 	b.cs	0x1dc  // b.hs, b.nlast
  e8:	91000463 	add	x3, x3, #0x1
  ec:	d1000421 	sub	x1, x1, #0x1
  f0:	d2800144 	mov	x4, #0xa                   	// #10
  f4:	9ac40846 	udiv	x6, x2, x4
  f8:	9b0488c5 	msub	x5, x6, x4, x2
  fc:	9100c0a8 	add	x8, x5, #0x30
 100:	d348fd09 	lsr	x9, x8, #8
 104:	b5000709 	cbnz	x9, 0x1e4
 108:	f100803f 	cmp	x1, #0x20
 10c:	54000642 	b.cs	0x1d4  // b.hs, b.nlast
 110:	d2a2100a 	mov	x10, #0x10800000            	// #276824064
 114:	38216948 	strb	w8, [x10, x1]
 118:	d2800144 	mov	x4, #0xa                   	// #10
 11c:	9ac40842 	udiv	x2, x2, x4
 120:	17ffffec 	b	0xd0
 124:	d1000421 	sub	x1, x1, #0x1
 128:	9100c043 	add	x3, x2, #0x30
 12c:	d348fc64 	lsr	x4, x3, #8
 130:	b50005a4 	cbnz	x4, 0x1e4
 134:	f100803f 	cmp	x1, #0x20
 138:	540004e2 	b.cs	0x1d4  // b.hs, b.nlast
 13c:	d2a21005 	mov	x5, #0x10800000            	// #276824064
 140:	382168a3 	strb	w3, [x5, x1]
 144:	d2800404 	mov	x4, #0x20                  	// #32
 148:	cb010085 	sub	x5, x4, x1
 14c:	d2800406 	mov	x6, #0x20                  	// #32
 150:	eb0600bf 	cmp	x5, x6
 154:	54000408 	b.hi	0x1d4  // b.pmore
 158:	cb0500c6 	sub	x6, x6, x5
 15c:	eb06003f 	cmp	x1, x6
 160:	540003a8 	b.hi	0x1d4  // b.pmore
 164:	d10103ff 	sub	sp, sp, #0x40
 168:	eb3c63ff 	cmp	sp, x28
 16c:	54000403 	b.cc	0x1ec  // b.lo, b.ul, b.last
 170:	f90003e0 	str	x0, [sp]
 174:	f90007e1 	str	x1, [sp, #8]
 178:	f9000be2 	str	x2, [sp, #16]
 17c:	f9000fe3 	str	x3, [sp, #24]
 180:	f90013e4 	str	x4, [sp, #32]
 184:	f90017e5 	str	x5, [sp, #40]
 188:	f9001be6 	str	x6, [sp, #48]
 18c:	f9001ffe 	str	x30, [sp, #56]
 190:	d2800020 	mov	x0, #0x1                   	// #1
 194:	d2a21001 	mov	x1, #0x10800000            	// #276824064
 198:	f94007f0 	ldr	x16, [sp, #8]
 19c:	8b100021 	add	x1, x1, x16
 1a0:	f94017e2 	ldr	x2, [sp, #40]
 1a4:	d2800808 	mov	x8, #0x40                  	// #64
 1a8:	d4000001 	svc	#0x0
 1ac:	aa0003e3 	mov	x3, x0
 1b0:	f94003e0 	ldr	x0, [sp]
 1b4:	f94007e1 	ldr	x1, [sp, #8]
 1b8:	f9400be2 	ldr	x2, [sp, #16]
 1bc:	f9401ffe 	ldr	x30, [sp, #56]
 1c0:	910103ff 	add	sp, sp, #0x40
 1c4:	d65f03c0 	ret
 1c8:	91019020 	add	x0, x1, #0x64
 1cc:	d2800ba8 	mov	x8, #0x5d                  	// #93
 1d0:	d4000001 	svc	#0x0
 1d4:	d2800061 	mov	x1, #0x3                   	// #3
 1d8:	17fffffc 	b	0x1c8
 1dc:	d2800081 	mov	x1, #0x4                   	// #4
 1e0:	17fffffa 	b	0x1c8
 1e4:	d28000a1 	mov	x1, #0x5                   	// #5
 1e8:	17fffff8 	b	0x1c8
 1ec:	d28000c1 	mov	x1, #0x6                   	// #6
 1f0:	17fffff6 	b	0x1c8
 1f4:	f100081f 	cmp	x0, #0x2
 1f8:	54000042 	b.cs	0x200  // b.hs, b.nlast
 1fc:	d65f03c0 	ret
 200:	d1000402 	sub	x2, x0, #0x1
 204:	d10083ff 	sub	sp, sp, #0x20
 208:	eb3c63ff 	cmp	sp, x28
 20c:	54000403 	b.cc	0x28c  // b.lo, b.ul, b.last
 210:	f90003e0 	str	x0, [sp]
 214:	f90007e1 	str	x1, [sp, #8]
 218:	f9000be2 	str	x2, [sp, #16]
 21c:	f9000ffe 	str	x30, [sp, #24]
 220:	f9400be0 	ldr	x0, [sp, #16]
 224:	97fffff4 	bl	0x1f4
 228:	aa0003e1 	mov	x1, x0
 22c:	f94003e0 	ldr	x0, [sp]
 230:	f9400ffe 	ldr	x30, [sp, #24]
 234:	910083ff 	add	sp, sp, #0x20
 238:	d1000803 	sub	x3, x0, #0x2
 23c:	d100c3ff 	sub	sp, sp, #0x30
 240:	eb3c63ff 	cmp	sp, x28
 244:	54000243 	b.cc	0x28c  // b.lo, b.ul, b.last
 248:	f90003e0 	str	x0, [sp]
 24c:	f90007e1 	str	x1, [sp, #8]
 250:	f9000be2 	str	x2, [sp, #16]
 254:	f9000fe3 	str	x3, [sp, #24]
 258:	f90013fe 	str	x30, [sp, #32]
 25c:	f9400fe0 	ldr	x0, [sp, #24]
 260:	97ffffe5 	bl	0x1f4
 264:	aa0003e2 	mov	x2, x0
 268:	f94003e0 	ldr	x0, [sp]
 26c:	f94007e1 	ldr	x1, [sp, #8]
 270:	f94013fe 	ldr	x30, [sp, #32]
 274:	9100c3ff 	add	sp, sp, #0x30
 278:	8b020020 	add	x0, x1, x2
 27c:	d65f03c0 	ret
 280:	91019020 	add	x0, x1, #0x64
 284:	d2800ba8 	mov	x8, #0x5d                  	// #93
 288:	d4000001 	svc	#0x0
 28c:	d28000c1 	mov	x1, #0x6                   	// #6
 290:	17fffffc 	b	0x280
 294:	d10043ff 	sub	sp, sp, #0x10
 298:	eb3c63ff 	cmp	sp, x28
 29c:	54000283 	b.cc	0x2ec  // b.lo, b.ul, b.last
 2a0:	f90003fe 	str	x30, [sp]
 2a4:	d28004c0 	mov	x0, #0x26                  	// #38
 2a8:	97ffffd3 	bl	0x1f4
 2ac:	f94003fe 	ldr	x30, [sp]
 2b0:	910043ff 	add	sp, sp, #0x10
 2b4:	d10043ff 	sub	sp, sp, #0x10
 2b8:	eb3c63ff 	cmp	sp, x28
 2bc:	54000183 	b.cc	0x2ec  // b.lo, b.ul, b.last
 2c0:	f90003e0 	str	x0, [sp]
 2c4:	f90007fe 	str	x30, [sp, #8]
 2c8:	f94003e0 	ldr	x0, [sp]
 2cc:	97ffff7a 	bl	0xb4
 2d0:	f94007fe 	ldr	x30, [sp, #8]
 2d4:	910043ff 	add	sp, sp, #0x10
 2d8:	d2800000 	mov	x0, #0x0                   	// #0
 2dc:	d65f03c0 	ret
 2e0:	91019020 	add	x0, x1, #0x64
 2e4:	d2800ba8 	mov	x8, #0x5d                  	// #93
 2e8:	d4000001 	svc	#0x0
 2ec:	d28000c1 	mov	x1, #0x6                   	// #6
 2f0:	17fffffc 	b	0x2e0
 2f4:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 2f8:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 2fc:	9100021f 	mov	sp, x16
 300:	97ffffe5 	bl	0x294
 304:	d2800ba8 	mov	x8, #0x5d                  	// #93
 308:	d4000001 	svc	#0x0
