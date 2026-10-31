# m1.spec -- KernelFlow milestone 1: two kernels (vecadd, the 35B RES ADD) for sm_121 and sm_80,
# host-only. Run from the omega-mv root:  build/kflow/kflow seed/kflow/m1.spec STATE
# Oracles: nvdisasm and cuobjdump are read-only checkers; asm2 is the seeded encoder; kemu and kpack
# are the seeded host emulator and cubin packer from this directory.
tool asm2 seed/rung2/asm2.bin
tool nvdisasm /usr/local/cuda/bin/nvdisasm
tool cuobjdump /usr/local/cuda/bin/cuobjdump
tool kemu build/kflow/kemu
tool kpack build/kflow/kpack

# vecadd, sm_121: referee = the chip-proven bytes gate6 checks
job vecadd 121 seed/sm121/sm121.tbl seed/sm121/vecadd.s2 seed/sm121/vecadd.bin vadd 10 p8 p8 p8 4
expect encode VALID-UNPROVEN
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect gpu NOT_RUN
expect cloud NOT_RUN
expect job VALID-UNPROVEN

# vecadd, sm_80: referee = Mesa NAK's encoding (seed/sm80/vectors/vecadd.nak.bin)
job vecadd80 80 seed/sm80/sm80.tbl seed/sm80/vecadd.s2 seed/sm80/vectors/vecadd.nak.bin vadd 10 p8 p8 p8 4
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect job VALID-UNPROVEN

# RES ADD (35B decode residual add), sm_121: referee = the C encoder's bytes from CudaArch r6
job resadd 121 seed/sm121/sm121.tbl seed/kflow/sm121/resadd.s2 seed/kflow/sm121/resadd.c.bin radd 32 p8 p8 p8 p8 p8 4 4
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect job VALID-UNPROVEN

# RES ADD, sm_80: no NAK referee exists for it; verify checks every ';=' intent text against nvdisasm
job resadd80 80 seed/sm80/sm80.tbl seed/kflow/sm80/resadd.s2 - radd 32 p8 p8 p8 p8 p8 4 4
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect job VALID-UNPROVEN

# negative control 1: one wrong bit planted in the sm_121 LDG table base must stop the flow at verify
tsub ldg.2 0x0c1e1900 0x0c1e1980
job vecaddbad 121 seed/sm121/sm121.tbl seed/sm121/vecadd.s2 seed/sm121/vecadd.bin vadd 10 p8 p8 p8 4
expect encode VALID-UNPROVEN
expect verify INVALID
expect package NOT_RUN
expect emulate NOT_RUN
expect job INVALID

# negative control 2: the same on the sm_80 table
tsub ldg.2 0x080e0000 0x080e0080
job vecbad80 80 seed/sm80/sm80.tbl seed/sm80/vecadd.s2 seed/sm80/vectors/vecadd.nak.bin vadd 10 p8 p8 p8 4
expect verify INVALID
expect package NOT_RUN
expect job INVALID

# negative control 3: with no referee and no intent texts, a wrong source register (R21 -> R22 in one
# FADD) decodes fine, so verify is INCOMPLETE and the host emulator must catch it
nointent
lsub 21 22
job radbad80 80 seed/sm80/sm80.tbl seed/kflow/sm80/resadd.s2 - radd 32 p8 p8 p8 p8 p8 4 4
expect verify INCOMPLETE
expect package VALID-UNPROVEN
expect emulate INVALID
expect job INVALID
