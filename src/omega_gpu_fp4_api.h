#ifndef OMEGA_GPU_FP4_API_H
#define OMEGA_GPU_FP4_API_H
/*
 * omega_gpu_fp4_api: native (no CUDA) FP4 expert-weight GEMV for the OWN-30B lane (cut L3, 2026-10-13).
 * New file. Nothing in the FP8 API, the session or the existing codegen changes. NOT wired into any server.
 *
 * ======================= FORMAT "OFP4v1" (NVFP4-style), the exact bit layout =======================
 *
 *   A weight matrix W is n rows by k columns (row-major, k = input columns). k must be a multiple of 256.
 *
 *   element code   4 bits  c = (s << 3) | (e1 << 2) | (e0 << 1) | m        (e2m1, bit 3 is the sign)
 *                  value(c) = (-1)^s * T[c & 7],   T = { 0, 0.5, 1, 1.5, 2, 3, 4, 6 }   ("-0" decodes to 0)
 *   nibble order   element c of a row lives in byte c / 2 of the row; the LOW nibble holds the EVEN column.
 *                  A row is k / 2 bytes. A block of 16 columns is 8 bytes = two little-endian u32 words;
 *                  column 16 b + i is nibble i of word i / 8 (i % 8) of block b.
 *   block scale    one byte per 16 consecutive columns of one row, row-major [n][k / 16]. The byte is an
 *                  UNSIGNED e4m3 value: bit 7 must be 0 and the NaN code 0x7F is refused. value = e4m3(byte)
 *                  (bias 7, 3 mantissa bits, subnormal m * 2^-9; 0x7E = 448 is the largest, 0x00 is zero).
 *   tensor scale   one f32 per tensor (here: per expert projection matrix, e.g. the 1536 x 2048 gate|up of
 *                  one expert), finite and > 0.
 *   dequantised    W[r][c] = value(code[r][c]) * e4m3(scale[r][c / 16]) * tensor_scale
 *   quantiser      (omega_fp4_quantize) tensor_scale = amax / (6 * 448); per block the ideal scale is
 *                  amax_blk / (6 * tensor_scale), rounded to e4m3, then the best of its two neighbouring e4m3
 *                  codes and itself by squared error; each element goes to the nearest grid point, ties to the
 *                  even code. Round to nearest everywhere; nothing is clipped except by the e4m3 scale cap.
 *   bytes per 16 weights: 8 (codes) + 1 (scale) = 9, i.e. 4.5 bits per weight (FP8 e4m3 + block-128 scale: 8.001).
 *
 * ======================= kernel numerics (bit-exact contract, host emulation = omega_fp4_gemv_emulate) =====
 *
 *   x' = x * 2^64 (host, exact), st'' = tensor_scale * 2^120 (host, exact; refused if not finite).
 *   code -> f32 bits  ((c & 7) << 22) | ((c & 8) << 28)   = value(c) * 2^-126 exactly (code 1 is an f32 subnormal)
 *   scale byte -> f32 bits  (byte & 0x7F) << 20            = e4m3(byte) * 2^-120 exactly
 *   per 16-column block b of a row:  dot = FFMA chain over i = 0..15 (dot = fma(f_i, x'_i, dot), dot0 = 0)
 *                                    s1  = fmul(bits(scale), st'')   (= e4m3 * tensor_scale, rounded once)
 *                                    acc = fma(dot, s1, acc)
 *   lane l of a row of LPA lanes (LPA = 16 or 32) owns blocks b = l, l + LPA, l + 2 LPA, ... in that order;
 *   the row total is the SHFL.DOWN tree over offsets LPA/2 .. 1 (acc[l] += acc[l + off]); y = lane0 * 2^62.
 *
 * ======================= C signatures =======================
 *   int omega_gpu_fp4_tensor_upload(uint32_t experts, uint32_t n, uint32_t k, const uint8_t *w4,
 *                                   const uint8_t *scales, const float *tensor_scale, OmegaGpuFp4Tensor **out);
 *       w4: experts * n * k / 2 bytes, scales: experts * n * k / 16 bytes, tensor_scale: experts floats.
 *   int omega_gpu_fp4_gemv_grouped_f32(t, groups, expert_ids, x, x_rows, y, info)   as the FP8 grouped call.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omega_blackwell_codegen_fp4.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OMEGA_GPU_FP4_OK = 0,
    OMEGA_GPU_FP4_BAD_ARGS = -1,
    OMEGA_GPU_FP4_TOO_LARGE = -2,
    OMEGA_GPU_FP4_CODEGEN_FAIL = -3,
    OMEGA_GPU_FP4_CHIP_FAIL = -4,
    OMEGA_GPU_FP4_BAD_SCALE = -5 /* a scale byte with bit 7 set or the NaN code 0x7F, or a bad tensor scale */
};

#define OMEGA_GPU_FP4_MAX_GROUPS 64u
#define OMEGA_GPU_FP4_GROUPED_CTAS 6u

typedef struct {
    uint32_t experts, n, k, groups;
    uint32_t lpa, rows, warps; /* kernel shape chosen for the call */
    uint32_t grid_x, grid_y;
    uint64_t weight_bytes;     /* code bytes the launch reads (groups * n * k / 2) */
    uint64_t scale_bytes;      /* scale bytes it reads (groups * n * k / 16) */
    uint64_t elapsed_ns;
    uint64_t call_ns;
    uint32_t completion_marker;
    bool kernel_cache_hit;
} OmegaGpuFp4Info;

typedef struct OmegaGpuFp4Tensor OmegaGpuFp4Tensor;

int omega_gpu_fp4_tensor_upload(uint32_t experts, uint32_t n, uint32_t k, const uint8_t *w4, const uint8_t *scales,
                                const float *tensor_scale, OmegaGpuFp4Tensor **out);
void omega_gpu_fp4_tensor_free(OmegaGpuFp4Tensor *t);
int omega_gpu_fp4_gemv_grouped_f32(const OmegaGpuFp4Tensor *t, uint32_t groups, const uint32_t *expert_ids, const float *x,
                                   uint32_t x_rows, float *y, OmegaGpuFp4Info *info);
const char *omega_gpu_fp4_rc_name(int rc);
const char *omega_gpu_fp4_last_error(void);
void omega_gpu_fp4_cache_clear(void);

/* ---- measurement knobs (tests; each clears the kernel cache) ---- */
void omega_gpu_fp4_set_shape(uint32_t lpa, uint32_t rows, uint32_t warps); /* 0 = automatic */
void omega_gpu_fp4_set_cta_budget(uint32_t ctas);                            /* 0 = automatic */
void omega_gpu_fp4_test_set_mutant(int mutant);  /* TEST ONLY: 1 scale dropped, 2 nibble pairs swapped, 3 block 32, 4 group table swapped */
void omega_gpu_fp4_test_set_no_fold(int on);     /* TEST ONLY: upload the tensor scale without the 2^120 fold */
void omega_gpu_fp4_shape_for(uint32_t k, uint32_t groups, uint32_t *lpa, uint32_t *rows, uint32_t *warps);

/* ---- host helpers (omega_gpu_fp4_pack.c), no device ---- */
float omega_fp4_value(uint8_t code);              /* e2m1 value of a 4-bit code */
float omega_fp4_e4m3_unsigned(uint8_t byte);      /* e4m3 value of a scale byte (byte & 0x7F; 0x7F is NaN-class: returns NaN) */
uint32_t omega_fp4_code_bits(uint8_t code);       /* the kernel's f32 bits for a code (value * 2^-126) */
uint32_t omega_fp4_scale_bits(uint8_t byte);      /* the kernel's f32 bits for a scale byte (value * 2^-120) */
uint8_t omega_fp4_nearest_code(float v);          /* nearest e2m1 code for v (any magnitude, saturates at 6), ties to the even code */
uint8_t omega_fp4_e4m3_round(float v);            /* nearest unsigned e4m3 byte for v >= 0 (saturates at 0x7E), ties to even */
/* Quantise a row-major n x k float matrix. w4: n*k/2 bytes, scales: n*k/16 bytes. Returns the tensor scale. */
float omega_fp4_quantize(const float *w, uint32_t n, uint32_t k, uint8_t *w4, uint8_t *scales);
/* Dequantise to float: W = value * e4m3 * tensor_scale (the exact format value, computed in double then rounded). */
void omega_fp4_dequantize(const uint8_t *w4, const uint8_t *scales, float tensor_scale, uint32_t n, uint32_t k, float *w);
/* Check a scale array: 0 if all bytes legal (bit 7 clear, not 0x7F); else the index + 1 of the first bad byte. */
size_t omega_fp4_scan_scales(const uint8_t *scales, size_t count);
/* x' = x * 2^64 for the kernel; st'' = tensor_scale * 2^120 (returns 0 and fills *out, or -1 on overflow/non-finite). */
void omega_fp4_pack_x(const float *x, uint32_t k, float *out);
int omega_fp4_pack_tscale(const float *tensor_scale, size_t count, float *out, int mutant_no_fold);
/* The bit-exact emulation of the kernel for ONE row: y = result of the tree for that row. lpa = 16 or 32.
 * mutant 0 only. w4row: k/2 bytes, srow: k/16 bytes, xp: x' (k floats), st2: st''. */
float omega_fp4_gemv_row_emulate(const uint8_t *w4row, const uint8_t *srow, float st2, const float *xp, uint32_t k, uint32_t lpa);
/* Whole matrix. */
void omega_fp4_gemv_emulate(const uint8_t *w4, const uint8_t *scales, float st2, const float *xp, uint32_t n, uint32_t k, uint32_t lpa, float *y);
/* Double-precision oracle straight from the format: y[r] = sum_c W[r][c] * x[c]; yscale[r] = sum |terms|. */
void omega_fp4_gemv_oracle(const uint8_t *w4, const uint8_t *scales, float tensor_scale, uint32_t n, uint32_t k, const float *x,
                           double *y, double *yscale);

#ifdef __cplusplus
}
#endif
#endif
