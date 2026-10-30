  b0:	14000094 	b	0x300
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
 200:	d2884803 	mov	x3, #0x4240                	// #16960
 204:	f2a001e3 	movk	x3, #0xf, lsl #16
 208:	eb03005f 	cmp	x2, x3
 20c:	54000382 	b.cs	0x27c  // b.hs, b.nlast
 210:	91000443 	add	x3, x2, #0x1
 214:	d2800004 	mov	x4, #0x0                   	// #0
 218:	d2800005 	mov	x5, #0x0                   	// #0
 21c:	f100047f 	cmp	x3, #0x1
 220:	54000220 	b.eq	0x264  // b.none
 224:	d2884806 	mov	x6, #0x4240                	// #16960
 228:	f2a001e6 	movk	x6, #0xf, lsl #16
 22c:	eb0600bf 	cmp	x5, x6
 230:	54000602 	b.cs	0x2f0  // b.hs, b.nlast
 234:	910004a5 	add	x5, x5, #0x1
 238:	d2800026 	mov	x6, #0x1                   	// #1
 23c:	8a060068 	and	x8, x3, x6
 240:	f100011f 	cmp	x8, #0x0
 244:	54000061 	b.ne	0x250  // b.any
 248:	d341fc63 	lsr	x3, x3, #1
 24c:	14000004 	b	0x25c
 250:	d2800066 	mov	x6, #0x3                   	// #3
 254:	9b067c68 	mul	x8, x3, x6
 258:	91000503 	add	x3, x8, #0x1
 25c:	91000484 	add	x4, x4, #0x1
 260:	17ffffef 	b	0x21c
 264:	8b040000 	add	x0, x0, x4
 268:	eb01009f 	cmp	x4, x1
 26c:	54000049 	b.ls	0x274  // b.plast
 270:	aa0403e1 	mov	x1, x4
 274:	91000442 	add	x2, x2, #0x1
 278:	17ffffe2 	b	0x200
 27c:	d10083ff 	sub	sp, sp, #0x20
 280:	eb3c63ff 	cmp	sp, x28
 284:	540003a3 	b.cc	0x2f8  // b.lo, b.ul, b.last
 288:	f90003e0 	str	x0, [sp]
 28c:	f90007e1 	str	x1, [sp, #8]
 290:	f9000bfe 	str	x30, [sp, #16]
 294:	f94003e0 	ldr	x0, [sp]
 298:	97ffff87 	bl	0xb4
 29c:	f94003e0 	ldr	x0, [sp]
 2a0:	f94007e1 	ldr	x1, [sp, #8]
 2a4:	f9400bfe 	ldr	x30, [sp, #16]
 2a8:	910083ff 	add	sp, sp, #0x20
 2ac:	d10083ff 	sub	sp, sp, #0x20
 2b0:	eb3c63ff 	cmp	sp, x28
 2b4:	54000223 	b.cc	0x2f8  // b.lo, b.ul, b.last
 2b8:	f90003e0 	str	x0, [sp]
 2bc:	f90007e1 	str	x1, [sp, #8]
 2c0:	f9000bfe 	str	x30, [sp, #16]
 2c4:	f94007e0 	ldr	x0, [sp, #8]
 2c8:	97ffff7b 	bl	0xb4
 2cc:	f94003e0 	ldr	x0, [sp]
 2d0:	f94007e1 	ldr	x1, [sp, #8]
 2d4:	f9400bfe 	ldr	x30, [sp, #16]
 2d8:	910083ff 	add	sp, sp, #0x20
 2dc:	d2800000 	mov	x0, #0x0                   	// #0
 2e0:	d65f03c0 	ret
 2e4:	91019020 	add	x0, x1, #0x64
 2e8:	d2800ba8 	mov	x8, #0x5d                  	// #93
 2ec:	d4000001 	svc	#0x0
 2f0:	d2800081 	mov	x1, #0x4                   	// #4
 2f4:	17fffffc 	b	0x2e4
 2f8:	d28000c1 	mov	x1, #0x6                   	// #6
 2fc:	17fffffa 	b	0x2e4
 300:	d2a2001c 	mov	x28, #0x10000000            	// #268435456
 304:	d2a21010 	mov	x16, #0x10800000            	// #276824064
 308:	9100021f 	mov	sp, x16
 30c:	97ffffba 	bl	0x1f4
 310:	d2800ba8 	mov	x8, #0x5d                  	// #93
 314:	d4000001 	svc	#0x0
