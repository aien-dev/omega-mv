  b0:	14000105 	b	0x4c4
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
 1f8:	f104b01f 	cmp	x0, #0x12c
 1fc:	54000502 	b.cs	0x29c  // b.hs, b.nlast
 200:	d2800001 	mov	x1, #0x0                   	// #0
 204:	f104b03f 	cmp	x1, #0x12c
 208:	54000462 	b.cs	0x294  // b.hs, b.nlast
 20c:	d2802582 	mov	x2, #0x12c                 	// #300
 210:	9b027c03 	mul	x3, x0, x2
 214:	8b010064 	add	x4, x3, x1
 218:	d2800045 	mov	x5, #0x2                   	// #2
 21c:	9b057c26 	mul	x6, x1, x5
 220:	8b060008 	add	x8, x0, x6
 224:	d28001a9 	mov	x9, #0xd                   	// #13
 228:	9ac9090b 	udiv	x11, x8, x9
 22c:	9b09a16a 	msub	x10, x11, x9, x8
 230:	d28bf20c 	mov	x12, #0x5f90                	// #24464
 234:	f2a0002c 	movk	x12, #0x1, lsl #16
 238:	eb0c009f 	cmp	x4, x12
 23c:	54000382 	b.cs	0x2ac  // b.hs, b.nlast
 240:	d280040d 	mov	x13, #0x20                  	// #32
 244:	f2a2100d 	movk	x13, #0x1080, lsl #16
 248:	f82479aa 	str	x10, [x13, x4, lsl #3]
 24c:	d2802582 	mov	x2, #0x12c                 	// #300
 250:	9b027c03 	mul	x3, x0, x2
 254:	8b010064 	add	x4, x3, x1
 258:	d2800065 	mov	x5, #0x3                   	// #3
 25c:	9b057c06 	mul	x6, x0, x5
 260:	8b0100c8 	add	x8, x6, x1
 264:	d2800169 	mov	x9, #0xb                   	// #11
 268:	9ac9090b 	udiv	x11, x8, x9
 26c:	9b09a16a 	msub	x10, x11, x9, x8
 270:	d28bf20c 	mov	x12, #0x5f90                	// #24464
 274:	f2a0002c 	movk	x12, #0x1, lsl #16
 278:	eb0c009f 	cmp	x4, x12
 27c:	54000182 	b.cs	0x2ac  // b.hs, b.nlast
 280:	d29f940d 	mov	x13, #0xfca0                	// #64672
 284:	f2a2114d 	movk	x13, #0x108a, lsl #16
 288:	f82479aa 	str	x10, [x13, x4, lsl #3]
 28c:	91000421 	add	x1, x1, #0x1
 290:	17ffffdd 	b	0x204
 294:	91000400 	add	x0, x0, #0x1
 298:	17ffffd8 	b	0x1f8
 29c:	d65f03c0 	ret
 2a0:	91019020 	add	x0, x1, #0x64
 2a4:	d2800ba8 	mov	x8, #0x5d                  	// #93
 2a8:	d4000001 	svc	#0x0
 2ac:	d2800061 	mov	x1, #0x3                   	// #3
 2b0:	17fffffc 	b	0x2a0
 2b4:	d2800000 	mov	x0, #0x0                   	// #0
 2b8:	f104b01f 	cmp	x0, #0x12c
 2bc:	54000562 	b.cs	0x368  // b.hs, b.nlast
 2c0:	d2802582 	mov	x2, #0x12c                 	// #300
 2c4:	9b027c01 	mul	x1, x0, x2
 2c8:	d2800002 	mov	x2, #0x0                   	// #0
 2cc:	f104b05f 	cmp	x2, #0x12c
 2d0:	54000482 	b.cs	0x360  // b.hs, b.nlast
 2d4:	d2800003 	mov	x3, #0x0                   	// #0
 2d8:	aa0203e4 	mov	x4, x2
 2dc:	d2800005 	mov	x5, #0x0                   	// #0
 2e0:	f104b0bf 	cmp	x5, #0x12c
 2e4:	540002a2 	b.cs	0x338  // b.hs, b.nlast
 2e8:	8b050026 	add	x6, x1, x5
 2ec:	d28bf208 	mov	x8, #0x5f90                	// #24464
 2f0:	f2a00028 	movk	x8, #0x1, lsl #16
 2f4:	eb0800df 	cmp	x6, x8
 2f8:	54000402 	b.cs	0x378  // b.hs, b.nlast
 2fc:	d2800409 	mov	x9, #0x20                  	// #32
 300:	f2a21009 	movk	x9, #0x1080, lsl #16
 304:	f866792a 	ldr	x10, [x9, x6, lsl #3]
 308:	d28bf20b 	mov	x11, #0x5f90                	// #24464
 30c:	f2a0002b 	movk	x11, #0x1, lsl #16
 310:	eb0b009f 	cmp	x4, x11
 314:	54000322 	b.cs	0x378  // b.hs, b.nlast
 318:	d29f940c 	mov	x12, #0xfca0                	// #64672
 31c:	f2a2114c 	movk	x12, #0x108a, lsl #16
 320:	f864798d 	ldr	x13, [x12, x4, lsl #3]
 324:	9b0d7d4e 	mul	x14, x10, x13
 328:	8b0e0063 	add	x3, x3, x14
 32c:	9104b084 	add	x4, x4, #0x12c
 330:	910004a5 	add	x5, x5, #0x1
 334:	17ffffeb 	b	0x2e0
 338:	8b020025 	add	x5, x1, x2
 33c:	d28bf206 	mov	x6, #0x5f90                	// #24464
 340:	f2a00026 	movk	x6, #0x1, lsl #16
 344:	eb0600bf 	cmp	x5, x6
 348:	54000182 	b.cs	0x378  // b.hs, b.nlast
 34c:	d29f2408 	mov	x8, #0xf920                	// #63776
 350:	f2a212a8 	movk	x8, #0x1095, lsl #16
 354:	f8257903 	str	x3, [x8, x5, lsl #3]
 358:	91000442 	add	x2, x2, #0x1
 35c:	17ffffdc 	b	0x2cc
 360:	91000400 	add	x0, x0, #0x1
 364:	17ffffd5 	b	0x2b8
 368:	d65f03c0 	ret
 36c:	91019020 	add	x0, x1, #0x64
 370:	d2800ba8 	mov	x8, #0x5d                  	// #93
 374:	d4000001 	svc	#0x0
 378:	d2800061 	mov	x1, #0x3                   	// #3
 37c:	17fffffc 	b	0x36c
 380:	d10043ff 	sub	sp, sp, #0x10
 384:	eb3c63ff 	cmp	sp, x28
 388:	540009a3 	b.cc	0x4bc  // b.lo, b.ul, b.last
 38c:	f90003fe 	str	x30, [sp]
 390:	97ffff99 	bl	0x1f4
 394:	f94003fe 	ldr	x30, [sp]
 398:	910043ff 	add	sp, sp, #0x10
 39c:	d10043ff 	sub	sp, sp, #0x10
 3a0:	eb3c63ff 	cmp	sp, x28
 3a4:	540008c3 	b.cc	0x4bc  // b.lo, b.ul, b.last
 3a8:	f90003fe 	str	x30, [sp]
 3ac:	97ffffc2 	bl	0x2b4
 3b0:	f94003fe 	ldr	x30, [sp]
 3b4:	910043ff 	add	sp, sp, #0x10
 3b8:	d2800000 	mov	x0, #0x0                   	// #0
 3bc:	d2800001 	mov	x1, #0x0                   	// #0
 3c0:	d2800002 	mov	x2, #0x0                   	// #0
 3c4:	f104b05f 	cmp	x2, #0x12c
 3c8:	540003c2 	b.cs	0x440  // b.hs, b.nlast
 3cc:	d2800003 	mov	x3, #0x0                   	// #0
 3d0:	f104b07f 	cmp	x3, #0x12c
 3d4:	540001c2 	b.cs	0x40c  // b.hs, b.nlast
 3d8:	d2802584 	mov	x4, #0x12c                 	// #300
 3dc:	9b047c45 	mul	x5, x2, x4
 3e0:	8b0300a6 	add	x6, x5, x3
 3e4:	d28bf208 	mov	x8, #0x5f90                	// #24464
 3e8:	f2a00028 	movk	x8, #0x1, lsl #16
 3ec:	eb0800df 	cmp	x6, x8
 3f0:	54000622 	b.cs	0x4b4  // b.hs, b.nlast
 3f4:	d29f2409 	mov	x9, #0xf920                	// #63776
 3f8:	f2a212a9 	movk	x9, #0x1095, lsl #16
 3fc:	f866792a 	ldr	x10, [x9, x6, lsl #3]
 400:	8b0a0000 	add	x0, x0, x10
 404:	91000463 	add	x3, x3, #0x1
 408:	17fffff2 	b	0x3d0
 40c:	d28025a3 	mov	x3, #0x12d                 	// #301
 410:	9b037c44 	mul	x4, x2, x3
 414:	91000085 	add	x5, x4, #0x0
 418:	d28bf206 	mov	x6, #0x5f90                	// #24464
 41c:	f2a00026 	movk	x6, #0x1, lsl #16
 420:	eb0600bf 	cmp	x5, x6
 424:	54000482 	b.cs	0x4b4  // b.hs, b.nlast
 428:	d29f2408 	mov	x8, #0xf920                	// #63776
 42c:	f2a212a8 	movk	x8, #0x1095, lsl #16
 430:	f8657909 	ldr	x9, [x8, x5, lsl #3]
 434:	8b090021 	add	x1, x1, x9
 438:	91000442 	add	x2, x2, #0x1
 43c:	17ffffe2 	b	0x3c4
 440:	d10083ff 	sub	sp, sp, #0x20
 444:	eb3c63ff 	cmp	sp, x28
 448:	540003a3 	b.cc	0x4bc  // b.lo, b.ul, b.last
 44c:	f90003e0 	str	x0, [sp]
 450:	f90007e1 	str	x1, [sp, #8]
 454:	f9000bfe 	str	x30, [sp, #16]
 458:	f94003e0 	ldr	x0, [sp]
 45c:	97ffff16 	bl	0xb4
 460:	f94003e0 	ldr	x0, [sp]
 464:	f94007e1 	ldr	x1, [sp, #8]
 468:	f9400bfe 	ldr	x30, [sp, #16]
 46c:	910083ff 	add	sp, sp, #0x20
 470:	d10083ff 	sub	sp, sp, #0x20
 474:	eb3c63ff 	cmp	sp, x28
 478:	54000223 	b.cc	0x4bc  // b.lo, b.ul, b.last
 47c:	f90003e0 	str	x0, [sp]
 480:	f90007e1 	str	x1, [sp, #8]
 484:	f9000bfe 	str	x30, [sp, #16]
 488:	f94007e0 	ldr	x0, [sp, #8]
 48c:	97ffff0a 	bl	0xb4
 490:	f94003e0 	ldr	x0, [sp]
 494:	f94007e1 	ldr	x1, [sp, #8]
 498:	f9400bfe 	ldr	x30, [sp, #16]
 49c:	910083ff 	add	sp, sp, #0x20
 4a0:	d2800000 	mov	x0, #0x0                   	// #0
 4a4:	d65f03c0 	ret
 4a8:	91019020 	add	x0, x1, #0x64
 4ac:	d2800ba8 	mov	x8, #0x5d                  	// #93
 4b0:	d4000001 	svc	#0x0
 4b4:	d2800061 	mov	x1, #0x3                   	// #3
 4b8:	17fffffc 	b	0x4a8
 4bc:	d28000c1 	mov	x1, #0x6                   	// #6
 4c0:	17fffffa 	b	0x4a8
 4c4:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 4c8:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 4cc:	9100021f 	mov	sp, x16
 4d0:	97ffffac 	bl	0x380
 4d4:	d2800ba8 	mov	x8, #0x5d                  	// #93
 4d8:	d4000001 	svc	#0x0
