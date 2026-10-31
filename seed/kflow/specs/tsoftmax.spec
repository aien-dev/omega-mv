# tsoftmax.spec -- row softmax (16 rows x 32 f32, one warp per row) on sm_121, OmegaTile (L3).
# Run from the omega-mv root: build/kflow/kflow seed/kflow/specs/tsoftmax.spec STATE
# Harness tsmx: x = multiples of 2^-17 in [-4, 4), y compared with an f64 CPU reference within relative
# 2528617 * 2^-40 (about 19.3 * 2^-23; derivation in seed/kflow/README.md).
tool asm2 seed/rung2/asm2.bin
tool nvdisasm /usr/local/cuda/bin/nvdisasm
tool cuobjdump /usr/local/cuda/bin/cuobjdump
tool kemu build/kflow/kemu
tool kpack build/kflow/kpack

job tsmx 121 seed/sm121/sm121.tbl seed/kflow/sm121/tsoftmax.s2 - tsmx 20 p8 p8
expect encode VALID-UNPROVEN
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect gpu NOT_RUN
expect cloud NOT_RUN
expect job VALID-UNPROVEN

# negative control: the butterfly made a shuffle-down (SHFL mode 3 -> 2 in the table row) decodes as
# SHFL.DOWN, not the intent text (verify INVALID); with intents ignored the emulator must catch that the
# lanes no longer combine the whole row
tsub shfl.bfly.1 0x0c001f00 0x08001f00
job tsmxdn 121 seed/sm121/sm121.tbl seed/kflow/sm121/tsoftmax.s2 - tsmx 20 p8 p8
expect verify INVALID
expect emulate NOT_RUN
expect job INVALID

# negative control 1b: the same with the intent texts ignored; the emulator must catch it alone
nointent
tsub shfl.bfly.1 0x0c001f00 0x08001f00
job tsmxdne 121 seed/sm121/sm121.tbl seed/kflow/sm121/tsoftmax.s2 - tsmx 20 p8 p8
expect verify INCOMPLETE
expect emulate INVALID
expect job INVALID
