
work/obj/leafcall.O2.o:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <mix.constprop.0>:
   0:	d28f3621 	mov	x1, #0x79b1                	// #31153
   4:	f2b3c6e1 	movk	x1, #0x9e37, lsl #16
   8:	9b017c01 	mul	x1, x0, x1
   c:	ca401420 	eor	x0, x1, x0, lsr #5
  10:	d65f03c0 	ret

Disassembly of section .text.startup:

0000000000000000 <main>:
   0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
   4:	d2987004 	mov	x4, #0xc380                	// #50048
   8:	d2800003 	mov	x3, #0x0                   	// #0
   c:	910003fd 	mov	x29, sp
  10:	d2800022 	mov	x2, #0x1                   	// #1
  14:	f2a03924 	movk	x4, #0x1c9, lsl #16
  18:	a90153f3 	stp	x19, x20, [sp, #16]
  1c:	d2800013 	mov	x19, #0x0                   	// #0
  20:	aa0303e0 	mov	x0, x3
  24:	91000463 	add	x3, x3, #0x1
  28:	94000000 	bl	0 <main>
  2c:	8b000042 	add	x2, x2, x0
  30:	ca020273 	eor	x19, x19, x2
  34:	eb04007f 	cmp	x3, x4
  38:	54ffff41 	b.ne	20 <main+0x20>  // b.any
  3c:	90000014 	adrp	x20, 0 <main>
  40:	91000294 	add	x20, x20, #0x0
  44:	aa1403e1 	mov	x1, x20
  48:	52800040 	mov	w0, #0x2                   	// #2
  4c:	94000000 	bl	0 <__printf_chk>
  50:	aa1303e2 	mov	x2, x19
  54:	aa1403e1 	mov	x1, x20
  58:	52800040 	mov	w0, #0x2                   	// #2
  5c:	94000000 	bl	0 <__printf_chk>
  60:	a94153f3 	ldp	x19, x20, [sp, #16]
  64:	52800000 	mov	w0, #0x0                   	// #0
  68:	a8c27bfd 	ldp	x29, x30, [sp], #32
  6c:	d65f03c0 	ret
