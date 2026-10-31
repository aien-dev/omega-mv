# tmatmul16.spec -- 16x16x16 tile matmul (f16 in, f32 accumulate) on sm_121 HMMA.16816.F32, OmegaTile (L3).
# Run from the omega-mv root: build/kflow/kflow seed/kflow/specs/tmatmul16.spec STATE
# Harness tmm1: A, B random normal f16 (exponents -5..5), D compared with an f64 CPU reference within
# |d - ref| <= 1966085 * 2^-40 * sum_k |a_ik b_kj| (gamma_15 for u = 2^-23, rounded up; seed/kflow/README.md).
tool asm2 seed/rung2/asm2.bin
tool nvdisasm /usr/local/cuda/bin/nvdisasm
tool cuobjdump /usr/local/cuda/bin/cuobjdump
tool kemu build/kflow/kemu
tool kpack build/kflow/kpack

job tmm 121 seed/sm121/sm121.tbl seed/kflow/sm121/tmatmul16.s2 - tmm1 48 p8 p8 p8
expect encode VALID-UNPROVEN
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate VALID-UNPROVEN
expect gpu NOT_RUN
expect cloud NOT_RUN
expect job VALID-UNPROVEN

# negative control 1: the HMMA type field flipped in the table (f16 -> bf16, w2 bit 18): nvdisasm prints
# HMMA.16816.F32.BF16, which is not the intent text, so verify is INVALID and the job stops
tsub hmma.f16.2 0x00001800 0x00041800
job tmmbf 121 seed/sm121/sm121.tbl seed/kflow/sm121/tmatmul16.s2 - tmm1 48 p8 p8 p8
expect encode VALID-UNPROVEN
expect verify INVALID
expect package NOT_RUN
expect emulate NOT_RUN
expect job INVALID

# negative control 1b: the same flipped bit with the intent texts ignored: the decode check can no longer
# object (INCOMPLETE), so the emulator must catch it on its own
nointent
tsub hmma.f16.2 0x00001800 0x00041800
job tmmbfe 121 seed/sm121/sm121.tbl seed/kflow/sm121/tmatmul16.s2 - tmm1 48 p8 p8 p8
expect verify INCOMPLETE
expect package VALID-UNPROVEN
expect emulate INVALID
expect job INVALID

# negative control 2: the same bytes, but the harness lays B out transposed in memory (the reference stays
# A x B): the decode check passes and the emulator comparison must fail
job tmmt 121 seed/sm121/sm121.tbl seed/kflow/sm121/tmatmul16.s2 - tmmt 48 p8 p8 p8
expect verify VALID-UNPROVEN
expect package VALID-UNPROVEN
expect emulate INVALID
expect job INVALID
