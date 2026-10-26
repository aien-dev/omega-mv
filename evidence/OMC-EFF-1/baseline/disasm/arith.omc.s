  b0:	14000084 	b	0x2c0
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
 1f8:	d2800001 	mov	x1, #0x0                   	// #0
 1fc:	d2800002 	mov	x2, #0x0                   	// #0
 200:	d28b4003 	mov	x3, #0x5a00                	// #23040
 204:	f2a04c43 	movk	x3, #0x262, lsl #16
 208:	eb03005f 	cmp	x2, x3
 20c:	540001c2 	b.cs	0x244  // b.hs, b.nlast
 210:	d28003e3 	mov	x3, #0x1f                  	// #31
 214:	9b037c04 	mul	x4, x0, x3
 218:	8b020080 	add	x0, x4, x2
 21c:	9b027c44 	mul	x4, x2, x2
 220:	d343fc45 	lsr	x5, x2, #3
 224:	ca050083 	eor	x3, x4, x5
 228:	d2884864 	mov	x4, #0x4243                	// #16963
 22c:	f2a001e4 	movk	x4, #0xf, lsl #16
 230:	9ac40866 	udiv	x6, x3, x4
 234:	9b048cc5 	msub	x5, x6, x4, x3
 238:	8b050021 	add	x1, x1, x5
 23c:	91000442 	add	x2, x2, #0x1
 240:	17fffff0 	b	0x200
 244:	d10083ff 	sub	sp, sp, #0x20
 248:	eb3c63ff 	cmp	sp, x28
 24c:	54000363 	b.cc	0x2b8  // b.lo, b.ul, b.last
 250:	f90003e0 	str	x0, [sp]
 254:	f90007e1 	str	x1, [sp, #8]
 258:	f9000bfe 	str	x30, [sp, #16]
 25c:	f94003e0 	ldr	x0, [sp]
 260:	97ffff95 	bl	0xb4
 264:	f94003e0 	ldr	x0, [sp]
 268:	f94007e1 	ldr	x1, [sp, #8]
 26c:	f9400bfe 	ldr	x30, [sp, #16]
 270:	910083ff 	add	sp, sp, #0x20
 274:	d10083ff 	sub	sp, sp, #0x20
 278:	eb3c63ff 	cmp	sp, x28
 27c:	540001e3 	b.cc	0x2b8  // b.lo, b.ul, b.last
 280:	f90003e0 	str	x0, [sp]
 284:	f90007e1 	str	x1, [sp, #8]
 288:	f9000bfe 	str	x30, [sp, #16]
 28c:	f94007e0 	ldr	x0, [sp, #8]
 290:	97ffff89 	bl	0xb4
 294:	f94003e0 	ldr	x0, [sp]
 298:	f94007e1 	ldr	x1, [sp, #8]
 29c:	f9400bfe 	ldr	x30, [sp, #16]
 2a0:	910083ff 	add	sp, sp, #0x20
 2a4:	d2800000 	mov	x0, #0x0                   	// #0
 2a8:	d65f03c0 	ret
 2ac:	91019020 	add	x0, x1, #0x64
 2b0:	d2800ba8 	mov	x8, #0x5d                  	// #93
 2b4:	d4000001 	svc	#0x0
 2b8:	d28000c1 	mov	x1, #0x6                   	// #6
 2bc:	17fffffc 	b	0x2ac
 2c0:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 2c4:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 2c8:	9100021f 	mov	sp, x16
 2cc:	97ffffca 	bl	0x1f4
 2d0:	d2800ba8 	mov	x8, #0x5d                  	// #93
 2d4:	d4000001 	svc	#0x0
