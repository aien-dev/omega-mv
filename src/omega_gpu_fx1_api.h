#ifndef OMEGA_GPU_FX1_API_H
#define OMEGA_GPU_FX1_API_H
/*
 * omega_gpu_fx1_api: FP8 e4m3 weights stored LOSSLESSLY coded (FX1) and the GEMV that reads them (ledger L274,
 * docs/adr/0002-mathematics-before-compute.md). The code and the kernel contract are in
 * omega_blackwell_codegen_fx1.h. y equals omega_gpu_fp8_gemv_f32 on the same tensor bit for bit (same decoded bytes,
 * same FFMA order); the kernel reads 6.8 to 6.9 bits per weight instead of 8 (tools/wpt_fxcodec.c, whole checkpoint).
 *
 * Host side (no device needed): omega_fx1_encode builds the four planes from row-major e4m3 bytes;
 * omega_fx1_decode_check decodes them the way a kernel lane does and compares every byte.
 * Device side: upload once (read-only afterwards), then GEMV calls on the shared session (omega_gpu_session.h).
 * Return codes: the OMEGA_GPU_FP8_* codes of omega_gpu_fp8_api.h.
 */
#include <stddef.h>
#include <stdint.h>
#include "omega_gpu_fp8_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t n, k;
    uint8_t *nib;        /* n * k / 2 bytes */
    uint32_t *exp;       /* exp_words 32-bit words: per-row regions + OMEGA_FX1_TAIL_WORD_ROWS * 32 zero words */
    size_t exp_words;
    uint32_t *rows;      /* n region offsets (words) */
    uint32_t lut[256];   /* (e << 23) | length, indexed by the next 8 stream bits */
    uint8_t len[16];     /* code length per exponent value (0 = unused) */
    uint64_t coded_bytes; /* nib + exp + rows + lut: what the kernel reads, the scales excluded (as for raw FP8) */
} OmegaFx1Host;

int omega_fx1_encode(uint32_t n, uint32_t k, const uint8_t *w, OmegaFx1Host *out);
/* 0 when every byte decodes back to w, else -1 (and *first_bad the first wrong byte index when non-NULL). */
int omega_fx1_decode_check(const OmegaFx1Host *h, const uint8_t *w, size_t *first_bad);
void omega_fx1_host_free(OmegaFx1Host *h);

typedef struct OmegaGpuFx1Tensor OmegaGpuFx1Tensor;

typedef struct {
    uint32_t n, k, rows_per_warp, warps_per_cta, grid_x, insns, gprs;
    uint64_t coded_bytes, raw_bytes;
    uint64_t elapsed_ns;          /* chip time of the launch (session marker) */
    int kernel_cache_hit;
} OmegaGpuFx1Info;

/* scale: the checkpoint's bf16 weight_scale_inv grid [n/128][k/128]. Buffers are GPU-cached when
 * omega_gpu_fp8_weight_cache() is on (read-only after upload: spark-hardware-manual section 4 rule 3), as the raw path. */
int omega_gpu_fx1_tensor_upload(const OmegaFx1Host *h, const uint16_t *scale, OmegaGpuFx1Tensor **out);
void omega_gpu_fx1_tensor_free(OmegaGpuFx1Tensor *t);
/* rows 1 or 2, warps 1..32, ctas 0 = automatic (one CTA per `warps` row groups, at most OMEGA_GPU_FP8_MAX_CTAS). */
void omega_gpu_fx1_set_shape(uint32_t rows, uint32_t warps, uint32_t ctas);
void omega_gpu_fx1_test_set_mutant(int m);
int omega_gpu_fx1_gemv_f32(const OmegaGpuFx1Tensor *t, const float *x, float *y, OmegaGpuFx1Info *info);

#ifdef __cplusplus
}
#endif
#endif
