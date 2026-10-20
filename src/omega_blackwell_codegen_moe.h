#ifndef OMEGA_BLACKWELL_CODEGEN_MOE_H
#define OMEGA_BLACKWELL_CODEGEN_MOE_H
/*
 * omega_blackwell_codegen_moe: FP8 (e4m3) GEMV kernel family for the OWN-30B MoE lane
 * (Cut 6 + Cut 5, 2026-10-13). New file: nothing in omega_blackwell_codegen.{c,h} changes.
 *
 * One kernel: y[g][r] = sum_k W_g[r][k] * x[g or 0][k] * S_g[r/128][k/128], m = 1 (decode), for
 * G groups (experts) in one launch. Weights are e4m3 bytes in the checkpoint's row-major
 * [n][k] order (n output rows, k input columns), scales are the checkpoint's 128 x 128
 * `weight_scale_inv` grid, stored on the device as f32 (times 2^OMEGA_FP8_SCALE_EXP, see below).
 *
 * Numeric scheme (hand-checked bit arithmetic, proven exhaustively in tests/gpu_fp8_api_test.c):
 *   f32 bits ((b & 0x7F) << 20) | ((b & 0x80) << 24) equal e4m3(b) * 2^-120 exactly, for normals,
 *   subnormals and both zeros (bf16 field exponent 0 with the 3 mantissa bits shifted reproduces
 *   2^-6 * 0.M). The e4m3 NaN codes 0x7F / 0xFF decode wrong (a finite number): the upload refuses
 *   them. The 2^120 is folded on the host: x' = x * 2^OMEGA_FP8_X_EXP and S' = S * 2^OMEGA_FP8_SCALE_EXP
 *   (64 + 56 = 120), chosen so that every product w' * x' is a normal f32 for any practical x.
 *   The kernel accumulates in f32: per lane, a dot of lane_bytes weights (FFMA chain), then
 *   acc_r = fma(dot, S', acc_r) once per 128-block, then a warp reduction (SHFL.DOWN) per row.
 *
 * Thread mapping: a CTA has `warps` warps (threads_x = 32 * warps, threads_y = 1); warp w of CTA
 * (cx, cy) handles group cy and row groups rg = cx * warps + w, rg + stride, ... < RG (RG = n / rows,
 * stride = grid_x * warps, both launch arguments). A row group is `rows` consecutive rows, inside
 * one 128-row scale block (rows divides 128). Per K step a warp reads 32 * lane_bytes contiguous
 * bytes of each of its rows (lane L: bytes [L * lane_bytes, +lane_bytes)), the same lane_bytes
 * floats of x', and one scale word per lane (the block of its chunk; 128 / lane_bytes lanes share
 * it). Step count KS = k / (32 * lane_bytes) is baked into the kernel; k must be a multiple of
 * 32 * lane_bytes and of 128.
 *
 * Constant bank 0 argument words (cbank + 0x380, as the other APIs):
 *   0x380 tab   u64  group table: entry g at tab + 16 g = { w_va u64, s_va u64 }
 *   0x388 x     u64  x' rows (f32, k each; row g at x + g * x_stride)
 *   0x390 y     u64  y rows (f32, n each; row g at y + g * y_stride)
 *   0x398 RG    u32  row groups per group (n / rows)
 *   0x39c x_stride u32 bytes between x rows (0: one x row shared by every group)
 *   0x3a0 y_stride u32 bytes between y rows
 *   0x3a4 stride u32 row groups per warp iteration (grid_x * warps)
 * Grid: grid_x CTAs per group, grid_y = groups. The kernel does not depend on n, RG, the strides
 * or the grid, so one kernel per (k, lane_bytes, rows, warps, mutant) serves every n and G.
 *
 * Only lane 0 of a warp stores (R words per row group) inside a BSSY/BSYNC region; the warp is
 * whole again for the next row group's SHFLs.
 *
 * mutant (tests only, never production): 1 drops the block scale (acc += dot), 2 never advances the
 * scale pointer (every K step uses the step-0 block: wrong whenever KS >= 2), 3 reads the group
 * table entry of group cy ^ 1 (swapped bases). Each must be caught by the parity test.
 *
 * Instruction encodings: every IR op used here already exists in omega_blackwell_codegen.h; no new
 * encodings were added. ISETP_GE (immediate) and IADD3 carry fixed control words in the encoder,
 * so this generator uses ISETP_GE_U32 (register) and IMAD for adds instead.
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_FP8_X_EXP 64      /* x' = x * 2^64 (host, exact) */
#define OMEGA_FP8_SCALE_EXP 56  /* S' = S * 2^56 (host, exact for every finite bf16 scale below 2^72) */
#define OMEGA_FP8_BLOCK 128u    /* scale block (rows and columns) */
#define OMEGA_FP8_MAX_K 16384u
#define OMEGA_FP8_MAX_GROUPS 64u /* group table entries per launch buffer (8 experts today) */

/* argument word offsets */
#define OMEGA_FP8_ARG_TAB 0x380u
#define OMEGA_FP8_ARG_X 0x388u
#define OMEGA_FP8_ARG_Y 0x390u
#define OMEGA_FP8_ARG_RG 0x398u
#define OMEGA_FP8_ARG_XSTRIDE 0x39cu
#define OMEGA_FP8_ARG_YSTRIDE 0x3a0u
#define OMEGA_FP8_ARG_STRIDE 0x3a4u
#define OMEGA_FP8_ARG_EPI_SCALE 0x3a8u /* f32, the SWIGLU epilogue only (L137) */
#define OMEGA_FP8_ARGS_WORDS 11u

typedef struct {
    uint32_t k;          /* input columns: multiple of 128 and of 32 * lane_bytes, <= OMEGA_FP8_MAX_K */
    uint32_t lane_bytes; /* 8 or 16 weight bytes per lane per step (LDG.E.64 / LDG.E.128); k must be a multiple of 256 */
    uint32_t rows;       /* rows per warp: 1, 2, 4 or 8 */
    uint32_t warps;      /* warps per CTA: 1 .. 32 (the host simulator runs up to 8) */
    uint32_t depth;      /* 0: one K step per loop iteration, x read from global memory each step.
                          * D >= 1: "deep" kernel: the CTA stages its group's x' row in shared memory once
                          * (k * 4 bytes, BAR.SYNC), then every loop iteration issues the weight and scale
                          * loads of D consecutive K steps before using any of them (D x more bytes in
                          * flight per warp); x comes from shared memory (LDS.128). Needs KS % D == 0,
                          * rows * D * lane_bytes <= 128, k % (32 * warps) == 0 and k <= 512 * warps (staging rounds). */
    int mutant;          /* 0 production; 1, 2, 3 as above */
    uint32_t epi;        /* epilogue (L137 launch fusion), 0 = store y. See OMEGA_FP8_EPI_*. */
    uint32_t pair;       /* OMEGA_FP8_EPI_SWIGLU: rows between a gate row and its up row (n / 2); 0 otherwise */
} OmegaFp8GemvSpec;

/* Epilogues (L137, fewer launches per decode token). The per-row arithmetic (dot chain, block scale, warp reduction)
 * is unchanged in every epilogue, so each row value is bit-identical to the plain kernel's; only what lane 0 does with
 * it differs.
 *   OMEGA_FP8_EPI_STORE   y[g][r] = acc (the plain kernel).
 *   OMEGA_FP8_EPI_SWIGLU  rows = 2, pair = P: row group rg holds gate row rg and up row rg + P (RG = P); lane 0 stores
 *                         act[g][rg] = silu(gate) * up * scale with the decode SWIGLU kernel's own instruction sequence
 *                         (omega_blackwell_codegen_dec.c gen_swiglu: FMUL -log2e, MUFU.EX2, FADD 1, MUFU.RCP, FMUL g,
 *                         FMUL up, FMUL scale). y_stride is then the act row stride (P floats). Replaces gate_up + SWIGLU.
 *   OMEGA_FP8_EPI_RESADD  lane 0 stores y[g][r] = y[g][r] + acc (one FADD, the decode RES ADD kernel's): y is the
 *                         residual x. Replaces o_proj + RES ADD. */
#define OMEGA_FP8_EPI_STORE 0u
#define OMEGA_FP8_EPI_SWIGLU 1u
#define OMEGA_FP8_EPI_RESADD 2u

/* 0 on success (prog solved and register-allocated), -1 on a refused spec or an allocator failure. */
int omega_blackwell_codegen_fp8_gemv_prog(const OmegaFp8GemvSpec *spec, BlackwellIRProgram *prog);

/* Hosted build: allocate and encode (omega_blackwell_kernel_free releases). */
int omega_blackwell_codegen_fp8_gemv(const OmegaFp8GemvSpec *spec, OmegaBlackwellKernel *kernel);

/* K steps the kernel runs: k / (32 * lane_bytes). 0 when the spec is refused. */
uint32_t omega_fp8_gemv_steps(const OmegaFp8GemvSpec *spec);

/* Shared memory bytes the launch must declare: k * 4 for a deep kernel, 0 otherwise. */
uint32_t omega_fp8_gemv_shared_bytes(const OmegaFp8GemvSpec *spec);

/* The IR instruction count and peak GPR count of the solved program (for budgets in tests). */
int omega_fp8_gemv_prog_stats(const OmegaFp8GemvSpec *spec, uint32_t *insns, uint32_t *gprs);

#ifdef __cplusplus
}
#endif
#endif
