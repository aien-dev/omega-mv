/* omega_gpu_fp8_pack.c: host side of the FP8 e4m3 GEMV (OWN-30B Cut 6). See omega_gpu_fp8_api.h.
 *
 * The weight bytes keep the checkpoint's row-major [n][k] order on the device (a warp reads
 * 32 * lane_bytes contiguous bytes of a row per step), so "packing" is: refuse the two e4m3 NaN
 * codes, convert the bf16 scale grid to f32 times 2^OMEGA_FP8_SCALE_EXP, and stage x as f32 times
 * 2^OMEGA_FP8_X_EXP. All three are exact (powers of two; refused on overflow). */
#include "omega_gpu_fp8_api.h"
#include "omega_blackwell_codegen_moe.h"
#include "omega_blackwell_matmul.h"
#include <math.h>
#include <string.h>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

float omega_fp8_e4m3_ref(uint8_t b) {
    int exponent = (b >> 3) & 0x0f;
    float mantissa = (float)(b & 0x07);
    float sign = (b & 0x80) ? -1.0f : 1.0f;
    if (exponent == 0x0f && (b & 0x07) == 0x07) return NAN; /* 0x7f / 0xff */
    if (exponent == 0) return sign * mantissa * ldexpf(1.0f, -9);
    return sign * (1.0f + mantissa / 8.0f) * ldexpf(1.0f, exponent - 7);
}

uint32_t omega_fp8_e4m3_kernel_bits(uint8_t b) {
    return ((uint32_t)(b & 0x7fu) << 20) | ((uint32_t)(b & 0x80u) << 24);
}

int omega_fp8_e4m3_is_nan(uint8_t b) { return (b & 0x7fu) == 0x7fu; }

/* The byte-at-a-time reference (the scan before W-FL1). Tests and the checkpoint cross-check compare the fast scans
 * against it; nothing on the upload path calls it except to recount a block that holds a NaN code. */
size_t omega_fp8_pack_scan_nan_ref(const uint8_t *w, size_t count, size_t *first) {
    size_t n = 0;
    for (size_t i = 0; i < count; i++)
        if (omega_fp8_e4m3_is_nan(w[i])) { if (n == 0 && first) *first = i; n++; }
    return n;
}

/* Fast scans (W-FL1): the same count and the same first index as the reference, on any length and alignment. A
 * block with no NaN code is skipped whole; a block that holds one is recounted by the reference loop, so the verdict
 * always comes from the reference rule. The tail shorter than one block goes through the reference loop too. */
static void scan_block(const uint8_t *w, size_t at, size_t len, size_t *n, size_t *first) {
    size_t f = 0, k = omega_fp8_pack_scan_nan_ref(w + at, len, &f);
    if (k) { if (*n == 0 && first) *first = at + f; *n += k; }
}

/* u64 SWAR, 32-byte blocks: ((x & 0x7f7f..) + 0x0101..) & 0x8080.. sets bit 7 of a byte iff (byte & 0x7f) == 0x7f
 * (no carry crosses a byte: at most 0x7f + 1 = 0x80). Portable; the scan on hosts without NEON. */
size_t omega_fp8_pack_scan_nan_swar(const uint8_t *w, size_t count, size_t *first) {
    const uint64_t lo7 = 0x7f7f7f7f7f7f7f7full, one = 0x0101010101010101ull, hi = 0x8080808080808080ull;
    size_t n = 0, i = 0;
    for (; i + 32 <= count; i += 32) {
        uint64_t q[4]; memcpy(q, w + i, sizeof q);
        uint64_t t = (((q[0] & lo7) + one) | ((q[1] & lo7) + one) | ((q[2] & lo7) + one) | ((q[3] & lo7) + one)) & hi;
        if (t) scan_block(w, i, 32, &n, first);
    }
    scan_block(w, i, count - i, &n, first);
    return n;
}

/* The upload-path scan. aarch64: 64-byte NEON blocks (lane == 0xff iff (byte & 0x7f) == 0x7f). Elsewhere: SWAR. */
size_t omega_fp8_pack_scan_nan(const uint8_t *w, size_t count, size_t *first) {
#if defined(__aarch64__) && defined(__ARM_NEON)
    const uint8x16_t m = vdupq_n_u8(0x7f);
    size_t n = 0, i = 0;
    for (; i + 64 <= count; i += 64) {
        uint8x16_t a = vceqq_u8(vandq_u8(vld1q_u8(w + i), m), m);
        uint8x16_t b = vceqq_u8(vandq_u8(vld1q_u8(w + i + 16), m), m);
        uint8x16_t c = vceqq_u8(vandq_u8(vld1q_u8(w + i + 32), m), m);
        uint8x16_t d = vceqq_u8(vandq_u8(vld1q_u8(w + i + 48), m), m);
        if (vmaxvq_u8(vorrq_u8(vorrq_u8(a, b), vorrq_u8(c, d)))) scan_block(w, i, 64, &n, first);
    }
    scan_block(w, i, count - i, &n, first);
    return n;
#else
    return omega_fp8_pack_scan_nan_swar(w, count, first);
#endif
}

int omega_fp8_pack_scales(const uint16_t *bf16, size_t count, float *out, int mutant_no_fold) {
    const float fold = mutant_no_fold ? 1.0f : ldexpf(1.0f, OMEGA_FP8_SCALE_EXP);
    for (size_t i = 0; i < count; i++) {
        float s = omega_bf16_to_fp32(bf16[i]);
        if (!(fabsf(s) <= 3.0e38f)) return -1; /* NaN or inf scale */
        float f = s * fold;
        if (!(fabsf(f) <= 3.0e38f)) return -1; /* fold overflow */
        out[i] = f;
    }
    return 0;
}

void omega_fp8_pack_x_f32(const float *x, uint32_t k, float *out) {
    const float fold = ldexpf(1.0f, OMEGA_FP8_X_EXP);
    for (uint32_t i = 0; i < k; i++) out[i] = x[i] * fold;
}

void omega_fp8_pack_x_bf16(const uint16_t *x, uint32_t k, float *out) {
    const float fold = ldexpf(1.0f, OMEGA_FP8_X_EXP);
    for (uint32_t i = 0; i < k; i++) out[i] = omega_bf16_to_fp32(x[i]) * fold;
}

void omega_fp8_gemv_oracle(const uint8_t *w, const uint16_t *scale_bf16, uint32_t n, uint32_t k,
                           const float *x, double *y, double *yscale) {
    const uint32_t kb = k / OMEGA_FP8_BLOCK;
    for (uint32_t r = 0; r < n; r++) {
        double acc = 0.0, mag = 0.0;
        const uint8_t *row = w + (size_t)r * k;
        const uint16_t *srow = scale_bf16 + (size_t)(r / OMEGA_FP8_BLOCK) * kb;
        for (uint32_t b = 0; b < kb; b++) {
            double s = (double)omega_bf16_to_fp32(srow[b]);
            for (uint32_t c = b * OMEGA_FP8_BLOCK; c < (b + 1) * OMEGA_FP8_BLOCK; c++) {
                double p = (double)omega_fp8_e4m3_ref(row[c]) * (double)x[c] * s;
                acc += p; mag += fabs(p);
            }
        }
        y[r] = acc;
        if (yscale) yscale[r] = mag;
    }
}
