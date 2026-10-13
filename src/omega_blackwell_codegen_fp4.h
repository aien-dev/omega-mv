#ifndef OMEGA_BLACKWELL_CODEGEN_FP4_H
#define OMEGA_BLACKWELL_CODEGEN_FP4_H
/*
 * omega_blackwell_codegen_fp4: FP4 (e2m1, block-16 e4m3 scales, per-tensor f32 scale) decode GEMV kernel for the
 * OWN-30B MoE lane (cut L3, 2026-10-13). New file: nothing in omega_blackwell_codegen.{c,h} or the FP8 generator changes.
 * Format and the bit-exact numerics are written in omega_gpu_fp4_api.h; this header is the kernel contract.
 *
 * y[g][r] = sum_c W_g[r][c] * x[g or 0][c], m = 1, for G groups (experts) in one launch.
 *
 * Thread mapping. A CTA has `warps` warps (32 * warps threads). The CTA first stages its group's x' row (k f32) in shared
 * memory (one 32-bit word per thread per round, k % (32 * warps) == 0, BAR.SYNC), natural order, so column c is at
 * shared byte 4 c. A row of the matrix is handled by LPA lanes (16 or 32): lane lr owns the 16-column blocks
 * b = lr + LPA j, j = 0 .. S-1 (S = k / (16 LPA)), so the lanes of one step read 8 consecutive bytes each, 8 LPA bytes
 * contiguous per warp load (LDG.E.64, fully coalesced). With LPA = 16 a warp serves two rows at once (lanes 0-15 and
 * 16-31 are two independent slots; the SHFL tree stays inside a slot for lane 0 / lane 16, the only lanes that store).
 * Per iteration a slot handles `rows` consecutive rows: row = rg * RPW + slot * rows + r, RPW = (32 / LPA) * rows.
 * Each (row, step) is one LDG.E.64 of the codes and one LDG.E.64 of the scale bytes around the lane's block (8 scale
 * bytes (lr & ~7) .. +7 of the step; the lane picks word (lr >> 2) & 1 and byte lr & 3 with two IMADs and a shift).
 * All loads of an iteration are issued before the first use; x comes from shared memory (LDS.128 x4 per step, shared by
 * the `rows` rows).
 *
 * Decode (3 integer ops per element + 1 FFMA): for nibble n of word w, u = w & (0xF << p); v = u * (2^(22-p) + 2^(28-p));
 * f = v & 0x81C00000 (p = 4 n for n < 6, p = 4 (n-4) of w >> 16 for n = 6, 7). v = (nib << 22) | (nib << 28), the mask
 * keeps sign (bit 31) and e1 e0 m (bits 24..22): f = e2m1 * 2^-126 exactly; code 1 is an f32 subnormal, no FTZ.
 *
 * Constant bank 0 argument words (cbank + 0x380; the FP8 kernel's, same offsets):
 *   0x380 tab   u64  group table: entry g at tab + 32 g = { w_va u64, s_va u64, st'' f32, pad u32 }
 *   0x388 x     u64  x' rows (f32, k each; row g at x + g * x_stride)
 *   0x390 y     u64  y rows (f32, n each; row g at y + g * y_stride)
 *   0x398 RG    u32  row groups per group (n / RPW)
 *   0x39c x_stride u32 bytes between x rows (0: one x row shared by every group)
 *   0x3a0 y_stride u32 bytes between y rows
 *   0x3a4 stride u32 row groups per warp iteration (grid_x * warps)
 * Grid: grid_x CTAs per group, grid_y = groups. Shared memory per CTA: 4 k bytes.
 *
 * mutant (tests only, never production): 1 drops the block scale (acc += dot), 2 swaps the two nibbles of every byte,
 * 3 uses block size 32 (the scale byte of block b is read as block b & ~1), 4 reads the group table entry of group cy ^ 1.
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_FP4_X_EXP 64        /* x' = x * 2^64 */
#define OMEGA_FP4_T_EXP 120       /* st'' = tensor_scale * 2^120 */
#define OMEGA_FP4_Y_EXP 62        /* y = acc * 2^62: the weight bits carry 2^-126, x' carries 2^64 */
#define OMEGA_FP4_BLOCK 16u
#define OMEGA_FP4_MAX_K 8192u
#define OMEGA_FP4_TAB_STRIDE 32u

#define OMEGA_FP4_ARG_TAB 0x380u
#define OMEGA_FP4_ARG_X 0x388u
#define OMEGA_FP4_ARG_Y 0x390u
#define OMEGA_FP4_ARG_RG 0x398u
#define OMEGA_FP4_ARG_XSTRIDE 0x39cu
#define OMEGA_FP4_ARG_YSTRIDE 0x3a0u
#define OMEGA_FP4_ARG_STRIDE 0x3a4u
#define OMEGA_FP4_ARGS_WORDS 10u

typedef struct {
    uint32_t k;      /* columns: multiple of 16 * lpa, of 32 * warps, <= OMEGA_FP4_MAX_K */
    uint32_t lpa;    /* lanes per row: 16 or 32 */
    uint32_t rows;   /* rows per slot per iteration: 1, 2 or 4 */
    uint32_t warps;  /* 1 .. 32 */
    int mutant;      /* 0 production; 1 .. 4 as above */
} OmegaFp4GemvSpec;

int omega_blackwell_codegen_fp4_gemv_prog(const OmegaFp4GemvSpec *spec, BlackwellIRProgram *prog);
int omega_blackwell_codegen_fp4_gemv(const OmegaFp4GemvSpec *spec, OmegaBlackwellKernel *kernel);
uint32_t omega_fp4_gemv_steps(const OmegaFp4GemvSpec *spec);          /* S = k / (16 lpa), 0 when refused */
uint32_t omega_fp4_gemv_rows_per_warp(const OmegaFp4GemvSpec *spec);  /* RPW */
uint32_t omega_fp4_gemv_shared_bytes(const OmegaFp4GemvSpec *spec);   /* 4 k */
int omega_fp4_gemv_prog_stats(const OmegaFp4GemvSpec *spec, uint32_t *insns, uint32_t *gprs);

#ifdef __cplusplus
}
#endif
#endif
