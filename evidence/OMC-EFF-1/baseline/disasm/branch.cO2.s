
work/obj/branch.O2.o:     file format elf64-littleaarch64


Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d2884805 	mov	x5, #0x4240                	// #16960
   8:	d2800024 	mov	x4, #0x1                   	// #1
   c:	910003fd 	mov	x29, sp
  10:	d2800002 	mov	x2, #0x0                   	// #0
  14:	f2a001e5 	movk	x5, #0xf, lsl #16
  18:	a90153f3 	stp	x19, x20, [sp, #16]
  1c:	d2800013 	mov	x19, #0x0                   	// #0
  20:	91000484 	add	x4, x4, #0x1
  24:	aa0403e0 	mov	x0, x4
  28:	d2800001 	mov	x1, #0x0                   	// #0
  2c:	d503201f 	nop
  30:	f240001f 	tst	x0, #0x1
  34:	d341fc03 	lsr	x3, x0, #1
  38:	8b000400 	add	x0, x0, x0, lsl #1
  3c:	91000421 	add	x1, x1, #0x1
  40:	9a800460 	csinc	x0, x3, x0, eq	// eq = none
  44:	f100041f 	cmp	x0, #0x1
  48:	54ffff41 	b.ne	30 <main+0x30>  // b.any
  4c:	eb01027f 	cmp	x19, x1
  50:	8b010042 	add	x2, x2, x1
  54:	9a812273 	csel	x19, x19, x1, cs	// cs = hs, nlast
  58:	eb05009f 	cmp	x4, x5
  5c:	54fffe21 	b.ne	20 <main+0x20>  // b.any
  60:	90000001 	adrp	x1, 0 <main>
  64:	91000034 	add	x20, x1, #0x0
  68:	aa1403e1 	mov	x1, x20
  6c:	52800040 	mov	w0, #0x2                   	// #2
  70:	94000000 	bl	0 <__printf_chk>
  74:	aa1303e2 	mov	x2, x19
  78:	aa1403e1 	mov	x1, x20
  7c:	52800040 	mov	w0, #0x2                   	// #2
  80:	94000000 	bl	0 <__printf_chk>
  84:	a94153f3 	ldp	x19, x20, [sp, #16]
  88:	52800000 	mov	w0, #0x0                   	// #0
  8c:	a8c27bfd 	ldp	x29, x30, [sp], #32
  90:	d65f03c0 	ret
