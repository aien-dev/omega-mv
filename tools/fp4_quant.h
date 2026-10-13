/* fp4_quant.h: experimental OFP4v1 quantiser variants for the L3b accuracy work (host only, header-only, no allocations).
 * Same store format as src/omega_gpu_fp4_pack.c (e2m1 codes, one unsigned e4m3 scale per 16 columns, one f32 per matrix);
 * only HOW the codes and scales are chosen differs. Needs omega_gpu_fp4_api.h for the grid helpers. */
#ifndef FP4_QUANT_H
#define FP4_QUANT_H
#include "omega_gpu_fp4_api.h"
#include <math.h>
#include <string.h>

/* orthonormal Walsh-Hadamard transform in place, blocks of blk (power of two). H is symmetric, H*H = I. */
static void fq_wht(float *x, int n, int blk) {
    if (blk <= 1) return;
    for (int b = 0; b < n; b += blk) {
        float *p = x + b;
        for (int h = 1; h < blk; h <<= 1) for (int i = 0; i < blk; i += h << 1) for (int j = i; j < i + h; j++) { float a = p[j], c = p[j + h]; p[j] = a + c; p[j + h] = a - c; }
        float sc = 1.0f / sqrtf((float)blk); for (int i = 0; i < blk; i++) p[i] *= sc;
    }
}

/* squared error of coding blk[0..15] with scale value sv (already times the tensor scale); fills cc */
static double fq_block_sse(const float *blk, double sv, uint8_t *cc) {
    double sse = 0.0;
    for (int i = 0; i < 16; i++) {
        uint8_t c = sv > 0.0 ? omega_fp4_nearest_code((float)((double)blk[i] / sv)) : 0;
        cc[i] = c; double e = (double)blk[i] - (double)omega_fp4_value(c) * sv; sse += e * e;
    }
    return sse;
}

/* Variant 1: error-minimising block scale. The candidates are the e4m3 codes s0+lo .. s0+hi around the max-abs scale s0
 * (lo = -1, hi = 1 is the shipped quantiser). Every candidate is scored by the real squared error; the best wins. */
static float fq_quantize(const float *w, uint32_t n, uint32_t k, uint8_t *w4, uint8_t *scales, int lo, int hi) {
    const size_t total = (size_t)n * k; double amax = 0.0;
    for (size_t i = 0; i < total; i++) { double a = fabs((double)w[i]); if (a > amax) amax = a; }
    float ts = amax > 0.0 ? (float)(amax / (6.0 * 448.0)) : 1.0f; const double tsd = (double)ts;
    for (uint32_t r = 0; r < n; r++) for (uint32_t b = 0; b < k / 16; b++) {
        const float *blk = w + (size_t)r * k + 16 * b; double ba = 0.0;
        for (int i = 0; i < 16; i++) { double a = fabs((double)blk[i]); if (a > ba) ba = a; }
        uint8_t best_s = 0, best_c[16] = { 0 };
        if (ba > 0.0) {
            int s0 = omega_fp4_e4m3_round((float)(ba / (6.0 * tsd))); double best = INFINITY;
            for (int d = lo; d <= hi; d++) {
                int s = s0 + d; if (s < 1 || s > 0x7E) continue;
                uint8_t cc[16]; double sse = fq_block_sse(blk, (double)omega_fp4_e4m3_unsigned((uint8_t)s) * tsd, cc);
                if (sse < best) { best = sse; best_s = (uint8_t)s; memcpy(best_c, cc, 16); }
            }
        }
        scales[(size_t)r * (k / 16) + b] = best_s;
        uint8_t *dst = w4 + (size_t)r * (k / 2) + 8 * (size_t)b;
        for (int i = 0; i < 16; i += 2) dst[i / 2] = (uint8_t)(best_c[i] | (best_c[i + 1] << 4));
    }
    return ts;
}
#endif
