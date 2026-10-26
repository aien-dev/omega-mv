  b0:	140000dc 	b	0x420
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
 1f4:	d2800000 	mov	x0, #0x0                   	// #0
 1f8:	d2909021 	mov	x1, #0x8481                	// #33921
 1fc:	f2a003c1 	movk	x1, #0x1e, lsl #16
 200:	eb01001f 	cmp	x0, x1
 204:	54000162 	b.cs	0x230  // b.hs, b.nlast
 208:	d2800001 	mov	x1, #0x0                   	// #0
 20c:	d2909022 	mov	x2, #0x8481                	// #33921
 210:	f2a003c2 	movk	x2, #0x1e, lsl #16
 214:	eb02001f 	cmp	x0, x2
 218:	54000942 	b.cs	0x340  // b.hs, b.nlast
 21c:	d2800403 	mov	x3, #0x20                  	// #32
 220:	f2a21003 	movk	x3, #0x1080, lsl #16
 224:	38206861 	strb	w1, [x3, x0]
 228:	91000400 	add	x0, x0, #0x1
 22c:	17fffff3 	b	0x1f8
 230:	d2800040 	mov	x0, #0x2                   	// #2
 234:	d2800001 	mov	x1, #0x0                   	// #0
 238:	9b007c02 	mul	x2, x0, x0
 23c:	d2909003 	mov	x3, #0x8480                	// #33920
 240:	f2a003c3 	movk	x3, #0x1e, lsl #16
 244:	eb03005f 	cmp	x2, x3
 248:	540004c8 	b.hi	0x2e0  // b.pmore
 24c:	d2884804 	mov	x4, #0x4240                	// #16960
 250:	f2a001e4 	movk	x4, #0xf, lsl #16
 254:	eb04003f 	cmp	x1, x4
 258:	54000782 	b.cs	0x348  // b.hs, b.nlast
 25c:	91000421 	add	x1, x1, #0x1
 260:	d2909022 	mov	x2, #0x8481                	// #33921
 264:	f2a003c2 	movk	x2, #0x1e, lsl #16
 268:	eb02001f 	cmp	x0, x2
 26c:	540006a2 	b.cs	0x340  // b.hs, b.nlast
 270:	d2800403 	mov	x3, #0x20                  	// #32
 274:	f2a21003 	movk	x3, #0x1080, lsl #16
 278:	38606864 	ldrb	w4, [x3, x0]
 27c:	f100009f 	cmp	x4, #0x0
 280:	540002c1 	b.ne	0x2d8  // b.any
 284:	9b007c02 	mul	x2, x0, x0
 288:	d2800003 	mov	x3, #0x0                   	// #0
 28c:	d2909004 	mov	x4, #0x8480                	// #33920
 290:	f2a003c4 	movk	x4, #0x1e, lsl #16
 294:	eb04005f 	cmp	x2, x4
 298:	54000208 	b.hi	0x2d8  // b.pmore
 29c:	d2884805 	mov	x5, #0x4240                	// #16960
 2a0:	f2a001e5 	movk	x5, #0xf, lsl #16
 2a4:	eb05007f 	cmp	x3, x5
 2a8:	54000502 	b.cs	0x348  // b.hs, b.nlast
 2ac:	91000463 	add	x3, x3, #0x1
 2b0:	d2800024 	mov	x4, #0x1                   	// #1
 2b4:	d2909025 	mov	x5, #0x8481                	// #33921
 2b8:	f2a003c5 	movk	x5, #0x1e, lsl #16
 2bc:	eb05005f 	cmp	x2, x5
 2c0:	54000402 	b.cs	0x340  // b.hs, b.nlast
 2c4:	d2800406 	mov	x6, #0x20                  	// #32
 2c8:	f2a21006 	movk	x6, #0x1080, lsl #16
 2cc:	382268c4 	strb	w4, [x6, x2]
 2d0:	8b000042 	add	x2, x2, x0
 2d4:	17ffffee 	b	0x28c
 2d8:	91000400 	add	x0, x0, #0x1
 2dc:	17ffffd7 	b	0x238
 2e0:	d2800001 	mov	x1, #0x0                   	// #0
 2e4:	d2800002 	mov	x2, #0x0                   	// #0
 2e8:	d2908fe3 	mov	x3, #0x847f                	// #33919
 2ec:	f2a003c3 	movk	x3, #0x1e, lsl #16
 2f0:	eb03005f 	cmp	x2, x3
 2f4:	540001c2 	b.cs	0x32c  // b.hs, b.nlast
 2f8:	91000843 	add	x3, x2, #0x2
 2fc:	d2909024 	mov	x4, #0x8481                	// #33921
 300:	f2a003c4 	movk	x4, #0x1e, lsl #16
 304:	eb04007f 	cmp	x3, x4
 308:	540001c2 	b.cs	0x340  // b.hs, b.nlast
 30c:	d2800405 	mov	x5, #0x20                  	// #32
 310:	f2a21005 	movk	x5, #0x1080, lsl #16
 314:	386368a6 	ldrb	w6, [x5, x3]
 318:	f10000df 	cmp	x6, #0x0
 31c:	54000041 	b.ne	0x324  // b.any
 320:	91000421 	add	x1, x1, #0x1
 324:	91000442 	add	x2, x2, #0x1
 328:	17fffff0 	b	0x2e8
 32c:	aa0103e0 	mov	x0, x1
 330:	d65f03c0 	ret
 334:	91019020 	add	x0, x1, #0x64
 338:	d2800ba8 	mov	x8, #0x5d                  	// #93
 33c:	d4000001 	svc	#0x0
 340:	d2800061 	mov	x1, #0x3                   	// #3
 344:	17fffffc 	b	0x334
 348:	d2800081 	mov	x1, #0x4                   	// #4
 34c:	17fffffa 	b	0x334
 350:	d2800000 	mov	x0, #0x0                   	// #0
 354:	d2800001 	mov	x1, #0x0                   	// #0
 358:	d2800002 	mov	x2, #0x0                   	// #0
 35c:	f100c85f 	cmp	x2, #0x32
 360:	54000222 	b.cs	0x3a4  // b.hs, b.nlast
 364:	d10083ff 	sub	sp, sp, #0x20
 368:	eb3c63ff 	cmp	sp, x28
 36c:	54000563 	b.cc	0x418  // b.lo, b.ul, b.last
 370:	f90003e0 	str	x0, [sp]
 374:	f90007e1 	str	x1, [sp, #8]
 378:	f9000be2 	str	x2, [sp, #16]
 37c:	f9000ffe 	str	x30, [sp, #24]
 380:	97ffff9d 	bl	0x1f4
 384:	aa0003e1 	mov	x1, x0
 388:	f94003e0 	ldr	x0, [sp]
 38c:	f9400be2 	ldr	x2, [sp, #16]
 390:	f9400ffe 	ldr	x30, [sp, #24]
 394:	910083ff 	add	sp, sp, #0x20
 398:	8b010000 	add	x0, x0, x1
 39c:	91000442 	add	x2, x2, #0x1
 3a0:	17ffffef 	b	0x35c
 3a4:	d10083ff 	sub	sp, sp, #0x20
 3a8:	eb3c63ff 	cmp	sp, x28
 3ac:	54000363 	b.cc	0x418  // b.lo, b.ul, b.last
 3b0:	f90003e0 	str	x0, [sp]
 3b4:	f90007e1 	str	x1, [sp, #8]
 3b8:	f9000bfe 	str	x30, [sp, #16]
 3bc:	f94007e0 	ldr	x0, [sp, #8]
 3c0:	97ffff3d 	bl	0xb4
 3c4:	f94003e0 	ldr	x0, [sp]
 3c8:	f94007e1 	ldr	x1, [sp, #8]
 3cc:	f9400bfe 	ldr	x30, [sp, #16]
 3d0:	910083ff 	add	sp, sp, #0x20
 3d4:	d10083ff 	sub	sp, sp, #0x20
 3d8:	eb3c63ff 	cmp	sp, x28
 3dc:	540001e3 	b.cc	0x418  // b.lo, b.ul, b.last
 3e0:	f90003e0 	str	x0, [sp]
 3e4:	f90007e1 	str	x1, [sp, #8]
 3e8:	f9000bfe 	str	x30, [sp, #16]
 3ec:	f94003e0 	ldr	x0, [sp]
 3f0:	97ffff31 	bl	0xb4
 3f4:	f94003e0 	ldr	x0, [sp]
 3f8:	f94007e1 	ldr	x1, [sp, #8]
 3fc:	f9400bfe 	ldr	x30, [sp, #16]
 400:	910083ff 	add	sp, sp, #0x20
 404:	d2800000 	mov	x0, #0x0                   	// #0
 408:	d65f03c0 	ret
 40c:	91019020 	add	x0, x1, #0x64
 410:	d2800ba8 	mov	x8, #0x5d                  	// #93
 414:	d4000001 	svc	#0x0
 418:	d28000c1 	mov	x1, #0x6                   	// #6
 41c:	17fffffc 	b	0x40c
 420:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 424:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 428:	9100021f 	mov	sp, x16
 42c:	97ffffc9 	bl	0x350
 430:	d2800ba8 	mov	x8, #0x5d                  	// #93
 434:	d4000001 	svc	#0x0
