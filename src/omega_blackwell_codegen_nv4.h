#ifndef OMEGA_BLACKWELL_CODEGEN_NV4_H
#define OMEGA_BLACKWELL_CODEGEN_NV4_H
/*
 * omega_blackwell_codegen_nv4: grouped 4-bit GEMV for NVIDIA NVFP4 weights (e2m1 nibbles, one e4m3 scale per 16 along k,
 * one f32 scale per tensor), for the Qwen3.8-Flash-Next experts (flashnext/port, 2026-10-19). New file; the MXFP4 kernel
 * (omega_blackwell_codegen_w4.c) is untouched and this one keeps its nibble layout, thread mapping, group table and
 * argument words (docs/w4-tile-format.md). Why NVFP4 and not MXFP4: sc evidence/flashnext-m1-20261019T1649Z measured the
 * NVIDIA checkpoint's experts at 0.081 weight relative RMS against bf16, the best MXFP4 we can make at 0.113.
 *
 *   y[g][r] = rs[r] * sum_c e2m1(w[r][c]) * e4m3(s[r][c / 16]) * x[g][c]      (g = ctaid.y, the group of the launch)
 *
 * Storage of one tensor [n][k] (one contiguous run, the "NV4 block"; a slab of experts is these blocks back to back):
 *   nibbles   [n][k/2] bytes at w_va, OMEGA_W4_NIBBLE order (low nibble = even column), the checkpoint's U8 bytes as stored
 *   scales    [n][k/16] e4m3 bytes at s_va (the checkpoint's weight_scale)
 *   rowscale  [n] f32 at s_va + n * k / 16: the tensor's weight_scale_2 times 2^62 per row (rows of a fused gate|up tensor
 *             carry their own tensor's value; 2^62 undoes the x' = x * 2^64 fold, see below)
 * The group table entry is { w_va u64, s_va u64 } (the FP8 / W4 table shape); n comes from the launch (row groups * rows).
 *
 * Numerics. The nibble decode is the W4 kernel's: f32 bits ((q & 7) << 22) | ((q & 8) << 28) = e2m1(q) * 2^-126, against
 * x' = x * 2^64, so a lane's 16-weight dot is sum e2m1 * x * 2^-62. The e4m3 byte b decodes exactly to f32 bits
 * ((b & 0x7f) << 20) | ((b & 0x80) << 24) = e4m3(b) * 2^-120 (normals and subnormals alike; no FTZ), then one FMUL by
 * 2^120. acc = sum_steps fma(dot, s, acc) = sum e2m1 * e4m3 * x * 2^-62, and the stored y = acc * rowscale.
 *
 * Mapping: lane_bytes 8 (16 weights = one scale group per lane step); a warp step covers 512 columns; KS = k / 512 full
 * steps, then a tail of TL = (k % 512) / 16 lanes. Tail lanes past TL clamp every address to lane TL - 1 and multiply
 * their scale by 0, so no padding is needed in x' or the weights (Flash-Next down_proj: k 640 = 1 step + 8 tail lanes).
 * Scale words: lane L reads the 32-bit word holding byte L of its row's step (4 lanes share a word) and extracts byte L & 3.
 * Rows of a row group: `rows` consecutive rows per warp per pass, lane 0 stores (no coalesced variant).
 *
 * Constraints: k % 64 == 0 (scale rows 4-byte aligned), 64 <= k <= 32768; rows 8 only without a tail (k % 512 == 0; 8 rows
 * with a tail exceed the register file). Flash-Next uses rows 2 everywhere: gate|up [1280][2560] (5 steps, no tail) and
 * down [2560][640] (1 step + 8 tail lanes), routed and shared alike. Upload refuses the e4m3 NaN codes 0x7f / 0xff.
 *
 * mutant (tests only): 1 drops the e4m3 scale, 2 never advances the scale pointer, 3 reads group cy ^ 1's table entry,
 * (refused when k has fewer than 2 full steps: equivalent to production there), 4 extracts byte (b + 1) & 3 of the scale word, 5 pairs each weight with the other nibble's x, 6 drops the row scale,
 * 7 leaves the clamped tail lanes unmasked (they add lane TL - 1's product again).
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_NV4_GROUP 16u
#define OMEGA_NV4_MAX_K 32768u
#define OMEGA_NV4_STEP 512u       /* columns per warp step */
#define OMEGA_NV4_ROWSCALE_FOLD 62 /* rowscale = weight_scale_2 * 2^62 */

typedef struct {
    uint32_t k;      /* multiple of 64, 64 .. 32768 */
    uint32_t rows;   /* 1, 2, 4 or 8 */
    uint32_t warps;  /* 1 .. 32 */
    uint32_t sched;  /* 0: every fixed-latency op stalls 6; 1: dependency-driven stalls (the W4 pass) */
    int mutant;      /* 0 production; 1..7 as above */
} OmegaNv4GemvSpec;

int omega_blackwell_codegen_nv4_gemv_prog(const OmegaNv4GemvSpec *spec, BlackwellIRProgram *prog);
int omega_nv4_gemv_spec_ok(const OmegaNv4GemvSpec *spec);
int omega_nv4_gemv_prog_stats(const OmegaNv4GemvSpec *spec, uint32_t *insns, uint32_t *gprs);
/* bytes of one NV4 block [n][k]: nibbles, scales, row scales */
uint64_t omega_nv4_block_bytes(uint32_t n, uint32_t k);
/* e4m3 byte -> the kernel's f32 bits (value * 2^-120) */
uint32_t omega_nv4_e4m3_kernel_bits(uint8_t b);

#ifdef __cplusplus
}
#endif
#endif
