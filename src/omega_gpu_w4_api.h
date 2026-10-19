#ifndef OMEGA_GPU_W4_API_H
#define OMEGA_GPU_W4_API_H
/*
 * omega_gpu_w4_api: native (no CUDA) 4-bit OCP MXFP4 GEMV (e2m1 weights, one E8M0 scale per 32 along k) for the
 * Qwen3.8-27B decode graph (qwen38/w4-gemv, 2026-10-19). Kernel and storage contract: omega_blackwell_codegen_w4.h.
 *
 *   int omega_gpu_w4_tensor_upload(uint32_t n, uint32_t k, const uint8_t *w4, const uint8_t *e8, OmegaGpuW4Tensor **out);
 *     w4: n * k / 2 bytes, e8: n * k / 32 bytes (the header's layout). Requires n >= 1, k % 1024 == 0 (k % 512 for an
 *     explicit 8-byte lane shape), k <= 32768. Refuses a scale byte above OMEGA_W4_E_MAX (BAD_ARGS, nothing uploaded).
 *   int omega_gpu_w4_gemv_f32(const OmegaGpuW4Tensor *t, const float *x, float *y, OmegaGpuW4Info *info);
 *     y[r] = sum_c e2m1(w[r][c]) * 2^(E[r][c/32] - 127) * x[c], f32 accumulate, one launch, poison-checked.
 *   int omega_gpu_w4_gemv_record(OmegaGpuCmdList *cl, const OmegaGpuW4Tensor *t, uint64_t tab_va, uint64_t x_va, uint64_t y_va);
 *     appended to a command list (caller holds the session lock): tab_va holds { w_va u64, s_va u64 } (entry below),
 *     x_va the k floats x' = x * 2^64, y_va n floats. Warm the kernel (one gemv_f32 or omega_gpu_w4_prepare) first.
 *
 * Host converter (the f7 MXFP4 rule, sc qwen38_omega::w4): from a checkpoint FP8 projection ([n][k] e4m3 with
 * [n/128][k/128] bf16 scale_inv) to MXFP4: per 32 weights v = e4m3 * scale, X = 2^(floor(log2 max|v|) - 2),
 * q = e2m1(|v| / X) round to nearest, ties to the even code, saturating at 6, sign kept; E = log2 X + 127.
 * An all-zero group gets E = 127 and zero nibbles. Rows [row0, row0 + rows) only, so callers may split the work.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omega_blackwell_codegen_w4.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OMEGA_GPU_W4_OK = 0,
    OMEGA_GPU_W4_BAD_ARGS = -1,
    OMEGA_GPU_W4_TOO_LARGE = -2,
    OMEGA_GPU_W4_CODEGEN_FAIL = -3,
    OMEGA_GPU_W4_CHIP_FAIL = -4
};

#define OMEGA_GPU_W4_CTAS 384u /* CTA budget of one launch, split evenly (the FP8 big-k rule) */

typedef struct {
    uint32_t n, k;
    uint32_t lane_bytes, rows_per_warp, warps_per_cta, coal, sched;
    uint32_t grid_x, steps;
    uint64_t weight_bytes, scale_bytes;
    uint64_t elapsed_ns, call_ns;
    bool kernel_cache_hit;
} OmegaGpuW4Info;

typedef struct OmegaGpuW4Tensor OmegaGpuW4Tensor;
struct OmegaGpuCmdList;

int omega_gpu_w4_tensor_upload(uint32_t n, uint32_t k, const uint8_t *w4, const uint8_t *e8, OmegaGpuW4Tensor **out);
void omega_gpu_w4_tensor_free(OmegaGpuW4Tensor *t);
void omega_gpu_w4_tensor_shape(const OmegaGpuW4Tensor *t, uint32_t *n, uint32_t *k);
size_t omega_gpu_w4_tensor_bytes(const OmegaGpuW4Tensor *t); /* device bytes: weights + scales */
int omega_gpu_w4_tensor_entry(const OmegaGpuW4Tensor *t, uint64_t *w_va, uint64_t *s_va);
int omega_gpu_w4_gemv_f32(const OmegaGpuW4Tensor *t, const float *x, float *y, OmegaGpuW4Info *info);
int omega_gpu_w4_gemv_record(struct OmegaGpuCmdList *cl, const OmegaGpuW4Tensor *t, uint64_t tab_va, uint64_t x_va, uint64_t y_va);
int omega_gpu_w4_prepare(uint32_t k, uint32_t n); /* build the kernel the automatic choice makes for (k, n) now */

/* shape knobs (tests and sweeps): 0 = automatic. Each clears the kernel cache. */
void omega_gpu_w4_set_shape(uint32_t lane_bytes, uint32_t rows, uint32_t warps);
void omega_gpu_w4_set_cta_budget(uint32_t ctas);
void omega_gpu_w4_set_sched(int sched); /* -1 automatic */
void omega_gpu_w4_set_coal(int coal);   /* -1 automatic */
void omega_gpu_w4_test_set_mutant(int mutant);
void omega_gpu_w4_spec_for(uint32_t k, uint32_t n, OmegaW4GemvSpec *spec); /* host only */
const char *omega_gpu_w4_last_error(void);

typedef struct {
    uint64_t weights, groups, zero_groups, saturated; /* saturated: |v| / X above 6 (never by the X rule; counted as a check) */
    double sq_err, sq_ref;                            /* sum (q X - v)^2 and sum v^2 (relative RMS error) */
} OmegaW4Stats;

int omega_gpu_w4_from_fp8(uint32_t n, uint32_t k, const uint8_t *w8, const uint16_t *scale_bf16, uint32_t row0, uint32_t rows,
                          uint8_t *w4, uint8_t *e8, OmegaW4Stats *stats);
/* host oracle: y[r] in double from the packed tensor */
void omega_w4_gemv_oracle(const uint8_t *w4, const uint8_t *e8, uint32_t n, uint32_t k, const float *x, double *y);
/* round |a| (a >= 0, finite) to the e2m1 magnitude code 0..7: nearest, ties to the even code, saturating at 6 */
uint8_t omega_w4_e2m1_rne(double a);

#ifdef __cplusplus
}
#endif
#endif
