
work/obj/matmul.O2.o:     file format elf64-littleaarch64


Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d289d8ab 	mov	x11, #0x4ec5                	// #20165
   8:	d2917469 	mov	x9, #0x8ba3                	// #35747
   c:	9000000a 	adrp	x10, 0 <main>
  10:	9000000c 	adrp	x12, 0 <main>
  14:	9100014a 	add	x10, x10, #0x0
  18:	9100018c 	add	x12, x12, #0x0
  1c:	f2b89d8b 	movk	x11, #0xc4ec, lsl #16
  20:	f2b745c9 	movk	x9, #0xba2e, lsl #16
  24:	910003fd 	mov	x29, sp
  28:	f2dd89cb 	movk	x11, #0xec4e, lsl #32
  2c:	f2d45d09 	movk	x9, #0xa2e8, lsl #32
  30:	d28bf20f 	mov	x15, #0x5f90                	// #24464
  34:	aa0a03e8 	mov	x8, x10
  38:	aa0c03e7 	mov	x7, x12
  3c:	d2802586 	mov	x6, #0x12c                 	// #300
  40:	d280000e 	mov	x14, #0x0                   	// #0
  44:	d280000d 	mov	x13, #0x0                   	// #0
  48:	f2e9d88b 	movk	x11, #0x4ec4, lsl #48
  4c:	f2e5d169 	movk	x9, #0x2e8b, lsl #48
  50:	f2a0002f 	movk	x15, #0x1, lsl #16
  54:	a90153f3 	stp	x19, x20, [sp, #16]
  58:	d104b0c1 	sub	x1, x6, #0x12c
  5c:	aa0d03e5 	mov	x5, x13
  60:	9bc97c24 	umulh	x4, x1, x9
  64:	9bcb7ca3 	umulh	x3, x5, x11
  68:	927ff880 	and	x0, x4, #0xfffffffffffffffe
  6c:	d341fc84 	lsr	x4, x4, #1
  70:	d342fc63 	lsr	x3, x3, #2
  74:	8b040000 	add	x0, x0, x4
  78:	8b030462 	add	x2, x3, x3, lsl #1
  7c:	d37ef400 	lsl	x0, x0, #2
  80:	cb040000 	sub	x0, x0, x4
  84:	8b020862 	add	x2, x3, x2, lsl #2
  88:	cb000020 	sub	x0, x1, x0
  8c:	cb0200a2 	sub	x2, x5, x2
  90:	f82178e0 	str	x0, [x7, x1, lsl #3]
  94:	f8217902 	str	x2, [x8, x1, lsl #3]
  98:	91000421 	add	x1, x1, #0x1
  9c:	910008a5 	add	x5, x5, #0x2
  a0:	eb06003f 	cmp	x1, x6
  a4:	54fffde1 	b.ne	60 <main+0x60>  // b.any
  a8:	9104b1ce 	add	x14, x14, #0x12c
  ac:	910005ad 	add	x13, x13, #0x1
  b0:	91252108 	add	x8, x8, #0x948
  b4:	912520e7 	add	x7, x7, #0x948
  b8:	91000c26 	add	x6, x1, #0x3
  bc:	eb0f01df 	cmp	x14, x15
  c0:	54fffcc1 	b.ne	58 <main+0x58>  // b.any
  c4:	9142bd8c 	add	x12, x12, #0xaf, lsl #12
  c8:	929f8fe8 	mov	x8, #0xffffffffffff0380    	// #-64640
  cc:	9132018c 	add	x12, x12, #0xc80
  d0:	d28bf20f 	mov	x15, #0x5f90                	// #24464
  d4:	91258189 	add	x9, x12, #0x960
  d8:	d280000e 	mov	x14, #0x0                   	// #0
  dc:	90000010 	adrp	x16, 0 <main>
  e0:	f2bffea8 	movk	x8, #0xfff5, lsl #16
  e4:	f2a0002f 	movk	x15, #0x1, lsl #16
  e8:	d37df1c7 	lsl	x7, x14, #3
  ec:	9100020d 	add	x13, x16, #0x0
  f0:	8b0a00eb 	add	x11, x7, x10
  f4:	aa0c03e5 	mov	x5, x12
  f8:	8b0701a7 	add	x7, x13, x7
  fc:	d2800006 	mov	x6, #0x0                   	// #0
 100:	8b0800a0 	add	x0, x5, x8
 104:	aa0b03e2 	mov	x2, x11
 108:	d2800001 	mov	x1, #0x0                   	// #0
 10c:	d503201f 	nop
 110:	f9400003 	ldr	x3, [x0]
 114:	91258000 	add	x0, x0, #0x960
 118:	f8408444 	ldr	x4, [x2], #8
 11c:	9b030481 	madd	x1, x4, x3, x1
 120:	eb05001f 	cmp	x0, x5
 124:	54ffff61 	b.ne	110 <main+0x110>  // b.any
 128:	91002005 	add	x5, x0, #0x8
 12c:	f82678e1 	str	x1, [x7, x6, lsl #3]
 130:	910004c6 	add	x6, x6, #0x1
 134:	eb0900bf 	cmp	x5, x9
 138:	54fffe41 	b.ne	100 <main+0x100>  // b.any
 13c:	9104b1ce 	add	x14, x14, #0x12c
 140:	eb0f01df 	cmp	x14, x15
 144:	54fffd21 	b.ne	e8 <main+0xe8>  // b.any
 148:	d28bf205 	mov	x5, #0x5f90                	// #24464
 14c:	912581a3 	add	x3, x13, #0x960
 150:	d2800004 	mov	x4, #0x0                   	// #0
 154:	d2800013 	mov	x19, #0x0                   	// #0
 158:	d2800002 	mov	x2, #0x0                   	// #0
 15c:	f2a00025 	movk	x5, #0x1, lsl #16
 160:	d1258060 	sub	x0, x3, #0x960
 164:	d503201f 	nop
 168:	f8408401 	ldr	x1, [x0], #8
 16c:	8b010042 	add	x2, x2, x1
 170:	eb00007f 	cmp	x3, x0
 174:	54ffffa1 	b.ne	168 <main+0x168>  // b.any
 178:	f94001a0 	ldr	x0, [x13]
 17c:	9104b084 	add	x4, x4, #0x12c
 180:	91258063 	add	x3, x3, #0x960
 184:	9125a1ad 	add	x13, x13, #0x968
 188:	8b000273 	add	x19, x19, x0
 18c:	eb05009f 	cmp	x4, x5
 190:	54fffe81 	b.ne	160 <main+0x160>  // b.any
 194:	90000001 	adrp	x1, 0 <main>
 198:	91000034 	add	x20, x1, #0x0
 19c:	aa1403e1 	mov	x1, x20
 1a0:	52800040 	mov	w0, #0x2                   	// #2
 1a4:	94000000 	bl	0 <__printf_chk>
 1a8:	aa1303e2 	mov	x2, x19
 1ac:	aa1403e1 	mov	x1, x20
 1b0:	52800040 	mov	w0, #0x2                   	// #2
 1b4:	94000000 	bl	0 <__printf_chk>
 1b8:	a94153f3 	ldp	x19, x20, [sp, #16]
 1bc:	52800000 	mov	w0, #0x0                   	// #0
 1c0:	a8c27bfd 	ldp	x29, x30, [sp], #32
 1c4:	d65f03c0 	ret
