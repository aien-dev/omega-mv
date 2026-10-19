#ifndef OMEGA_GPU_NV4_API_H
#define OMEGA_GPU_NV4_API_H
/*
 * omega_gpu_nv4_api: native (no CUDA) grouped NVFP4 GEMV for the Qwen3.8-Flash-Next experts (flashnext/port, 2026-10-19).
 * Kernel and storage contract: omega_blackwell_codegen_nv4.h. A slab holds `count` NV4 blocks of one shape [n][k] back to
 * back in ONE device buffer (512 experts + the shared expert of one layer and one projection kind = one buffer; the session
 * has ~1022 buffer slots, so one buffer per expert cannot fit 48 layers).
 *
 *   int  omega_gpu_nv4_slab_alloc(uint32_t n, uint32_t k, uint32_t count, OmegaGpuNv4Slab **out);
 *   int  omega_gpu_nv4_slab_set(OmegaGpuNv4Slab *s, uint32_t idx, const uint8_t *nib, const uint8_t *e4m3, const float *row_global);
 *        nib [n][k/2] (checkpoint U8 bytes), e4m3 [n][k/16] (weight_scale), row_global [n] (weight_scale_2 of the tensor
 *        each row came from). Host copy only; refuses the e4m3 NaN codes and a non-finite or negative global.
 *   int  omega_gpu_nv4_slab_commit(OmegaGpuNv4Slab *s);   one sync of the whole slab to the device after the sets
 *   int  omega_gpu_nv4_slab_entry(const OmegaGpuNv4Slab *s, uint32_t idx, uint64_t *w_va, uint64_t *s_va);
 *   int  omega_gpu_nv4_gemv_grouped_f32(const OmegaGpuNv4Slab *s, const uint32_t *ids, uint32_t groups, const float *x,
 *                                       uint32_t x_rows, float *y, OmegaGpuNv4Info *info);
 *        y[g][n] = block ids[g] . x[g] (x_rows == groups) or . x[0] (x_rows == 1); one launch of grid_x x groups.
 *   int  omega_gpu_nv4_gemv_record(OmegaGpuCmdList *cl, uint32_t n, uint32_t k, uint32_t groups, uint64_t tab_va,
 *                                  uint64_t x_va, uint32_t x_stride_bytes, uint64_t y_va);
 *        into a command list (caller holds the session lock): tab_va holds groups x { w_va, s_va } (a router writes it on
 *        the device), x_va the x' rows (x * 2^64, row g at x_va + g * x_stride_bytes; 0 = one shared row), y_va groups x n
 *        floats. Warm the kernel first (one grouped_f32 call or omega_gpu_nv4_prepare).
 *
 * Buffers: GPU-uncached host-visible session memory (the FP8 / W4 API rule, nvos.h:1117). Output words are poisoned
 * before a launch and a surviving poison word is CHIP_FAIL.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omega_blackwell_codegen_nv4.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OMEGA_GPU_NV4_OK = 0,
    OMEGA_GPU_NV4_BAD_ARGS = -1,
    OMEGA_GPU_NV4_TOO_LARGE = -2,
    OMEGA_GPU_NV4_CODEGEN_FAIL = -3,
    OMEGA_GPU_NV4_CHIP_FAIL = -4
};

#define OMEGA_GPU_NV4_CTAS 384u
#define OMEGA_GPU_NV4_MAX_GROUPS 64u

typedef struct {
    uint32_t n, k, groups, rows_per_warp, warps_per_cta, sched, grid_x;
    uint64_t weight_bytes; /* bytes the launch streams: groups x block bytes */
    uint64_t elapsed_ns, call_ns;
    bool kernel_cache_hit;
} OmegaGpuNv4Info;

typedef struct OmegaGpuNv4Slab OmegaGpuNv4Slab;
struct OmegaGpuCmdList;

int omega_gpu_nv4_slab_alloc(uint32_t n, uint32_t k, uint32_t count, OmegaGpuNv4Slab **out);
void omega_gpu_nv4_slab_free(OmegaGpuNv4Slab *s);
int omega_gpu_nv4_slab_set(OmegaGpuNv4Slab *s, uint32_t idx, const uint8_t *nib, const uint8_t *e4m3, const float *row_global);
int omega_gpu_nv4_slab_commit(OmegaGpuNv4Slab *s);
int omega_gpu_nv4_slab_entry(const OmegaGpuNv4Slab *s, uint32_t idx, uint64_t *w_va, uint64_t *s_va);
void omega_gpu_nv4_slab_shape(const OmegaGpuNv4Slab *s, uint32_t *n, uint32_t *k, uint32_t *count);
int omega_gpu_nv4_gemv_grouped_f32(const OmegaGpuNv4Slab *s, const uint32_t *ids, uint32_t groups, const float *x, uint32_t x_rows,
                                   float *y, OmegaGpuNv4Info *info);
int omega_gpu_nv4_gemv_record(struct OmegaGpuCmdList *cl, uint32_t n, uint32_t k, uint32_t groups, uint64_t tab_va, uint64_t x_va,
                              uint32_t x_stride_bytes, uint64_t y_va);
int omega_gpu_nv4_prepare(uint32_t k, uint32_t n);

void omega_gpu_nv4_set_shape(uint32_t rows, uint32_t warps); /* 0 = automatic; clears the kernel cache */
void omega_gpu_nv4_set_cta_budget(uint32_t ctas);
void omega_gpu_nv4_set_sched(int sched);                    /* -1 automatic */
void omega_gpu_nv4_test_set_mutant(int mutant);
void omega_gpu_nv4_spec_for(uint32_t k, uint32_t n, OmegaNv4GemvSpec *spec);
const char *omega_gpu_nv4_last_error(void);

/* host oracle: y[r] in double, rowscale = row_global (no 2^62 fold) */
void omega_nv4_gemv_oracle(const uint8_t *nib, const uint8_t *e4m3, const float *row_global, uint32_t n, uint32_t k, const float *x, double *y);
double omega_nv4_e4m3_ref(uint8_t b);

#ifdef __cplusplus
}
#endif
#endif
