  b0:	14000125 	b	0x544
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
 1f4:	d2860720 	mov	x0, #0x3039                	// #12345
 1f8:	d2800001 	mov	x1, #0x0                   	// #0
 1fc:	d289c402 	mov	x2, #0x4e20                	// #20000
 200:	eb02003f 	cmp	x1, x2
 204:	540002a2 	b.cs	0x258  // b.hs, b.nlast
 208:	d289cda2 	mov	x2, #0x4e6d                	// #20077
 20c:	f2a838c2 	movk	x2, #0x41c6, lsl #16
 210:	9b027c03 	mul	x3, x0, x2
 214:	d2860724 	mov	x4, #0x3039                	// #12345
 218:	8b040065 	add	x5, x3, x4
 21c:	d2b00006 	mov	x6, #0x80000000            	// #2147483648
 220:	9ac608a8 	udiv	x8, x5, x6
 224:	9b069500 	msub	x0, x8, x6, x5
 228:	d290d402 	mov	x2, #0x86a0                	// #34464
 22c:	f2a00022 	movk	x2, #0x1, lsl #16
 230:	9ac20804 	udiv	x4, x0, x2
 234:	9b028083 	msub	x3, x4, x2, x0
 238:	d289c405 	mov	x5, #0x4e20                	// #20000
 23c:	eb05003f 	cmp	x1, x5
 240:	54000142 	b.cs	0x268  // b.hs, b.nlast
 244:	d2800406 	mov	x6, #0x20                  	// #32
 248:	f2a21006 	movk	x6, #0x1080, lsl #16
 24c:	f82178c3 	str	x3, [x6, x1, lsl #3]
 250:	91000421 	add	x1, x1, #0x1
 254:	17ffffea 	b	0x1fc
 258:	d65f03c0 	ret
 25c:	91019020 	add	x0, x1, #0x64
 260:	d2800ba8 	mov	x8, #0x5d                  	// #93
 264:	d4000001 	svc	#0x0
 268:	d2800061 	mov	x1, #0x3                   	// #3
 26c:	17fffffc 	b	0x25c
 270:	d2800000 	mov	x0, #0x0                   	// #0
 274:	d289c401 	mov	x1, #0x4e20                	// #20000
 278:	eb01001f 	cmp	x0, x1
 27c:	540005c2 	b.cs	0x334  // b.hs, b.nlast
 280:	d289c402 	mov	x2, #0x4e20                	// #20000
 284:	eb02001f 	cmp	x0, x2
 288:	540005e2 	b.cs	0x344  // b.hs, b.nlast
 28c:	d2800403 	mov	x3, #0x20                  	// #32
 290:	f2a21003 	movk	x3, #0x1080, lsl #16
 294:	f8607861 	ldr	x1, [x3, x0, lsl #3]
 298:	aa0003e2 	mov	x2, x0
 29c:	d2800023 	mov	x3, #0x1                   	// #1
 2a0:	d2800004 	mov	x4, #0x0                   	// #0
 2a4:	f100005f 	cmp	x2, #0x0
 2a8:	9a9f97e5 	cset	x5, hi	// hi = pmore
 2ac:	8a050066 	and	x6, x3, x5
 2b0:	b4000326 	cbz	x6, 0x314
 2b4:	d2884808 	mov	x8, #0x4240                	// #16960
 2b8:	f2a001e8 	movk	x8, #0xf, lsl #16
 2bc:	eb08009f 	cmp	x4, x8
 2c0:	54000462 	b.cs	0x34c  // b.hs, b.nlast
 2c4:	91000484 	add	x4, x4, #0x1
 2c8:	d1000446 	sub	x6, x2, #0x1
 2cc:	d289c408 	mov	x8, #0x4e20                	// #20000
 2d0:	eb0800df 	cmp	x6, x8
 2d4:	54000382 	b.cs	0x344  // b.hs, b.nlast
 2d8:	d2800409 	mov	x9, #0x20                  	// #32
 2dc:	f2a21009 	movk	x9, #0x1080, lsl #16
 2e0:	f8667925 	ldr	x5, [x9, x6, lsl #3]
 2e4:	eb0100bf 	cmp	x5, x1
 2e8:	54000129 	b.ls	0x30c  // b.plast
 2ec:	d289c406 	mov	x6, #0x4e20                	// #20000
 2f0:	eb06005f 	cmp	x2, x6
 2f4:	54000282 	b.cs	0x344  // b.hs, b.nlast
 2f8:	d2800408 	mov	x8, #0x20                  	// #32
 2fc:	f2a21008 	movk	x8, #0x1080, lsl #16
 300:	f8227905 	str	x5, [x8, x2, lsl #3]
 304:	d1000442 	sub	x2, x2, #0x1
 308:	14000002 	b	0x310
 30c:	d2800003 	mov	x3, #0x0                   	// #0
 310:	17ffffe5 	b	0x2a4
 314:	d289c404 	mov	x4, #0x4e20                	// #20000
 318:	eb04005f 	cmp	x2, x4
 31c:	54000142 	b.cs	0x344  // b.hs, b.nlast
 320:	d2800405 	mov	x5, #0x20                  	// #32
 324:	f2a21005 	movk	x5, #0x1080, lsl #16
 328:	f82278a1 	str	x1, [x5, x2, lsl #3]
 32c:	91000400 	add	x0, x0, #0x1
 330:	17ffffd1 	b	0x274
 334:	d65f03c0 	ret
 338:	91019020 	add	x0, x1, #0x64
 33c:	d2800ba8 	mov	x8, #0x5d                  	// #93
 340:	d4000001 	svc	#0x0
 344:	d2800061 	mov	x1, #0x3                   	// #3
 348:	17fffffc 	b	0x338
 34c:	d2800081 	mov	x1, #0x4                   	// #4
 350:	17fffffa 	b	0x338
 354:	d10043ff 	sub	sp, sp, #0x10
 358:	eb3c63ff 	cmp	sp, x28
 35c:	54000f03 	b.cc	0x53c  // b.lo, b.ul, b.last
 360:	f90003fe 	str	x30, [sp]
 364:	97ffffa4 	bl	0x1f4
 368:	f94003fe 	ldr	x30, [sp]
 36c:	910043ff 	add	sp, sp, #0x10
 370:	d10043ff 	sub	sp, sp, #0x10
 374:	eb3c63ff 	cmp	sp, x28
 378:	54000e23 	b.cc	0x53c  // b.lo, b.ul, b.last
 37c:	f90003fe 	str	x30, [sp]
 380:	97ffffbc 	bl	0x270
 384:	f94003fe 	ldr	x30, [sp]
 388:	910043ff 	add	sp, sp, #0x10
 38c:	d2800000 	mov	x0, #0x0                   	// #0
 390:	d2800001 	mov	x1, #0x0                   	// #0
 394:	d289c402 	mov	x2, #0x4e20                	// #20000
 398:	eb02003f 	cmp	x1, x2
 39c:	54000182 	b.cs	0x3cc  // b.hs, b.nlast
 3a0:	d289c402 	mov	x2, #0x4e20                	// #20000
 3a4:	eb02003f 	cmp	x1, x2
 3a8:	54000c62 	b.cs	0x534  // b.hs, b.nlast
 3ac:	d2800403 	mov	x3, #0x20                  	// #32
 3b0:	f2a21003 	movk	x3, #0x1080, lsl #16
 3b4:	f8617864 	ldr	x4, [x3, x1, lsl #3]
 3b8:	91000425 	add	x5, x1, #0x1
 3bc:	9b057c86 	mul	x6, x4, x5
 3c0:	8b060000 	add	x0, x0, x6
 3c4:	91000421 	add	x1, x1, #0x1
 3c8:	17fffff3 	b	0x394
 3cc:	d2800001 	mov	x1, #0x0                   	// #0
 3d0:	d2800002 	mov	x2, #0x0                   	// #0
 3d4:	d289c3e3 	mov	x3, #0x4e1f                	// #19999
 3d8:	eb03005f 	cmp	x2, x3
 3dc:	54000262 	b.cs	0x428  // b.hs, b.nlast
 3e0:	d289c403 	mov	x3, #0x4e20                	// #20000
 3e4:	eb03005f 	cmp	x2, x3
 3e8:	54000a62 	b.cs	0x534  // b.hs, b.nlast
 3ec:	d2800404 	mov	x4, #0x20                  	// #32
 3f0:	f2a21004 	movk	x4, #0x1080, lsl #16
 3f4:	f8627885 	ldr	x5, [x4, x2, lsl #3]
 3f8:	91000446 	add	x6, x2, #0x1
 3fc:	d289c408 	mov	x8, #0x4e20                	// #20000
 400:	eb0800df 	cmp	x6, x8
 404:	54000982 	b.cs	0x534  // b.hs, b.nlast
 408:	d2800409 	mov	x9, #0x20                  	// #32
 40c:	f2a21009 	movk	x9, #0x1080, lsl #16
 410:	f866792a 	ldr	x10, [x9, x6, lsl #3]
 414:	eb0a00bf 	cmp	x5, x10
 418:	54000049 	b.ls	0x420  // b.plast
 41c:	91000421 	add	x1, x1, #0x1
 420:	91000442 	add	x2, x2, #0x1
 424:	17ffffec 	b	0x3d4
 428:	d10083ff 	sub	sp, sp, #0x20
 42c:	eb3c63ff 	cmp	sp, x28
 430:	54000863 	b.cc	0x53c  // b.lo, b.ul, b.last
 434:	f90003e0 	str	x0, [sp]
 438:	f90007e1 	str	x1, [sp, #8]
 43c:	f9000bfe 	str	x30, [sp, #16]
 440:	f94003e0 	ldr	x0, [sp]
 444:	97ffff1c 	bl	0xb4
 448:	f94003e0 	ldr	x0, [sp]
 44c:	f94007e1 	ldr	x1, [sp, #8]
 450:	f9400bfe 	ldr	x30, [sp, #16]
 454:	910083ff 	add	sp, sp, #0x20
 458:	d10083ff 	sub	sp, sp, #0x20
 45c:	eb3c63ff 	cmp	sp, x28
 460:	540006e3 	b.cc	0x53c  // b.lo, b.ul, b.last
 464:	f90003e0 	str	x0, [sp]
 468:	f90007e1 	str	x1, [sp, #8]
 46c:	f9000bfe 	str	x30, [sp, #16]
 470:	f94007e0 	ldr	x0, [sp, #8]
 474:	97ffff10 	bl	0xb4
 478:	f94003e0 	ldr	x0, [sp]
 47c:	f94007e1 	ldr	x1, [sp, #8]
 480:	f9400bfe 	ldr	x30, [sp, #16]
 484:	910083ff 	add	sp, sp, #0x20
 488:	d2800002 	mov	x2, #0x0                   	// #0
 48c:	d2800403 	mov	x3, #0x20                  	// #32
 490:	f2a21003 	movk	x3, #0x1080, lsl #16
 494:	f8627864 	ldr	x4, [x3, x2, lsl #3]
 498:	d100c3ff 	sub	sp, sp, #0x30
 49c:	eb3c63ff 	cmp	sp, x28
 4a0:	540004e3 	b.cc	0x53c  // b.lo, b.ul, b.last
 4a4:	f90003e0 	str	x0, [sp]
 4a8:	f90007e1 	str	x1, [sp, #8]
 4ac:	f9000be2 	str	x2, [sp, #16]
 4b0:	f9000fe3 	str	x3, [sp, #24]
 4b4:	f90013e4 	str	x4, [sp, #32]
 4b8:	f90017fe 	str	x30, [sp, #40]
 4bc:	f94013e0 	ldr	x0, [sp, #32]
 4c0:	97fffefd 	bl	0xb4
 4c4:	f94003e0 	ldr	x0, [sp]
 4c8:	f94007e1 	ldr	x1, [sp, #8]
 4cc:	f94017fe 	ldr	x30, [sp, #40]
 4d0:	9100c3ff 	add	sp, sp, #0x30
 4d4:	d289c3e2 	mov	x2, #0x4e1f                	// #19999
 4d8:	d2800403 	mov	x3, #0x20                  	// #32
 4dc:	f2a21003 	movk	x3, #0x1080, lsl #16
 4e0:	f8627864 	ldr	x4, [x3, x2, lsl #3]
 4e4:	d100c3ff 	sub	sp, sp, #0x30
 4e8:	eb3c63ff 	cmp	sp, x28
 4ec:	54000283 	b.cc	0x53c  // b.lo, b.ul, b.last
 4f0:	f90003e0 	str	x0, [sp]
 4f4:	f90007e1 	str	x1, [sp, #8]
 4f8:	f9000be2 	str	x2, [sp, #16]
 4fc:	f9000fe3 	str	x3, [sp, #24]
 500:	f90013e4 	str	x4, [sp, #32]
 504:	f90017fe 	str	x30, [sp, #40]
 508:	f94013e0 	ldr	x0, [sp, #32]
 50c:	97fffeea 	bl	0xb4
 510:	f94003e0 	ldr	x0, [sp]
 514:	f94007e1 	ldr	x1, [sp, #8]
 518:	f94017fe 	ldr	x30, [sp, #40]
 51c:	9100c3ff 	add	sp, sp, #0x30
 520:	d2800000 	mov	x0, #0x0                   	// #0
 524:	d65f03c0 	ret
 528:	91019020 	add	x0, x1, #0x64
 52c:	d2800ba8 	mov	x8, #0x5d                  	// #93
 530:	d4000001 	svc	#0x0
 534:	d2800061 	mov	x1, #0x3                   	// #3
 538:	17fffffc 	b	0x528
 53c:	d28000c1 	mov	x1, #0x6                   	// #6
 540:	17fffffa 	b	0x528
 544:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 548:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 54c:	9100021f 	mov	sp, x16
 550:	97ffff81 	bl	0x354
 554:	d2800ba8 	mov	x8, #0x5d                  	// #93
 558:	d4000001 	svc	#0x0
