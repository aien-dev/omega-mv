// Verification oracle only, not a kernel bootstrap or driver.
.include "omega_leaf.S"
.set span,omega_kernel_unit
.text
.p2align 3
.incbin "divide.bin",0,8
.global ratio
.type ratio,%function
ratio:
.incbin "divide.bin",8
.size ratio,.-ratio
.p2align 3
.incbin "scrub.bin",0,8
.global scrub
.type scrub,%function
scrub:
.incbin "scrub.bin",8
.size scrub,.-scrub
.macro span_clear
 cbnz x2,fail8
 cbnz x3,fail8
 cbnz x4,fail8
 cbnz x5,fail8
 cbnz x6,fail8
.endm
.global _start
.type _start,%function
_start:
 mov x18,#18
 mov x19,#19
 mov x20,#20
 mov x21,#21
 mov x22,#22
 mov x23,#23
 mov x24,#24
 mov x25,#25
 mov x26,#26
 mov x27,#27
 mov x28,#28
 mov x29,sp
 // Six scalar registers, successful range including end boundary.
 mov x0,#16
 mov x1,#12
 mov x2,#4
 mov x3,#2
 mov x4,#2
 mov x5,#7
 bl span
 cmp x0,#7
 b.ne fail1
 cbnz x1,fail1
 span_clear
 // Stale generation must become checked status 9, no result.
 mov x0,#16
 mov x1,#12
 mov x2,#4
 mov x3,#1
 mov x4,#2
 mov x5,#7
 bl span
 cbnz x0,fail2
 cmp x1,#9
 b.ne fail2
 span_clear
 // Start outside extent must refuse before unsigned subtraction.
 mov x0,#16
 mov x1,#17
 mov x2,#0
 mov x3,#2
 mov x4,#2
 mov x5,#7
 bl span
 cbnz x0,fail3
 cmp x1,#3
 b.ne fail3
 span_clear
 // Wrapped start+count must not be admitted.
 mov x0,#16
 mov x1,#12
 mov x2,#-1
 mov x3,#2
 mov x4,#2
 mov x5,#7
 bl span
 cbnz x0,fail4
 cmp x1,#3
 b.ne fail4
 span_clear
 // Checked runtime divide-by-zero normal return.
 mov x0,#9
 mov x1,#0
 bl ratio
 cbnz x0,fail5
 cmp x1,#2
 b.ne fail5
 // A second normal call must work after a checked failure.
 mov x0,#9
 mov x1,#3
 bl ratio
 cmp x0,#3
 b.ne fail6
 cbnz x1,fail6
 // All allocated volatile pool registers must be scrubbed.
 bl scrub
 cmp x0,#16
 b.ne fail8
 cbnz x1,fail8
 span_clear
 cbnz x8,fail8
 cbnz x9,fail8
 cbnz x10,fail8
 cbnz x11,fail8
 cbnz x12,fail8
 cbnz x13,fail8
 cbnz x14,fail8
 cbnz x15,fail8
 cbnz x17,fail8
 // Check fixed x18, callee-saved GPRs and stack on every previous exit.
 cmp x18,#18
 b.ne fail7
 cmp x19,#19
 b.ne fail7
 cmp x20,#20
 b.ne fail7
 cmp x21,#21
 b.ne fail7
 cmp x22,#22
 b.ne fail7
 cmp x23,#23
 b.ne fail7
 cmp x24,#24
 b.ne fail7
 cmp x25,#25
 b.ne fail7
 cmp x26,#26
 b.ne fail7
 cmp x27,#27
 b.ne fail7
 cmp x28,#28
 b.ne fail7
 mov x9,sp
 cmp x29,x9
 b.ne fail7
 mov x0,#0
 b exit
fail1: mov x0,#1
 b exit
fail2: mov x0,#2
 b exit
fail3: mov x0,#3
 b exit
fail4: mov x0,#4
 b exit
fail5: mov x0,#5
 b exit
fail6: mov x0,#6
 b exit
fail7: mov x0,#7
 b exit
fail8: mov x0,#8
exit:
 mov x8,#93
 svc #0
.size _start,.-_start
.section .note.GNU-stack,"",%progbits
