/* omega_gpu_fp4_pack.c: host side of the OFP4v1 format (OWN-30B cut L3): quantiser, dequantiser, bit-exact kernel
 * emulation and the double oracle. See omega_gpu_fp4_api.h for the exact layout and numerics. No device. */
#pragma GCC optimize("fp-contract=off")
#include "omega_gpu_fp4_api.h"
#include <math.h>
#include <string.h>

static const float T[8] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };

static float bits_f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

float omega_fp4_value(uint8_t code) { float v = T[code & 7]; return (code & 8) ? -v : v; }
uint32_t omega_fp4_code_bits(uint8_t code) { return ((uint32_t)(code & 7) << 22) | ((uint32_t)(code & 8) << 28); }
uint32_t omega_fp4_scale_bits(uint8_t byte) { return (uint32_t)(byte & 0x7F) << 20; }

float omega_fp4_e4m3_unsigned(uint8_t byte) {
    unsigned b = byte & 0x7Fu, e = b >> 3, m = b & 7u;
    if (b == 0x7F) return NAN;
    if (e == 0) return ldexpf((float)m, -9);
    return ldexpf(1.0f + (float)m / 8.0f, (int)e - 7);
}

uint8_t omega_fp4_nearest_code(float v) {
    float a = fabsf(v);
    uint8_t c;
    if (!(a > 0.25f)) c = 0;
    else if (a < 0.75f) c = 1;
    else if (a <= 1.25f) c = 2;
    else if (a < 1.75f) c = 3;
    else if (a <= 2.5f) c = 4;
    else if (a < 3.5f) c = 5;
    else if (a <= 5.0f) c = 6;
    else c = 7;
    return (uint8_t)((v < 0.0f && c) ? (c | 8u) : c);
}

uint8_t omega_fp4_e4m3_round(float vf) {
    double v = vf;
    if (!(v > 0.0)) return 0;
    if (v < 0x1p-6) { double m = nearbyint(v * 512.0); return (uint8_t)m; } /* 0..8: 8 is the smallest normal, byte 0x08 */
    int ex; double fr = frexp(v, &ex); /* v = fr * 2^ex, fr in [0.5, 1) */
    int e = ex - 1; double mant = (fr * 2.0 - 1.0) * 8.0;
    double m = nearbyint(mant);
    if (m >= 8.0) { m = 0.0; e++; }
    if (e > 8) return 0x7E;
    unsigned byte = ((unsigned)(e + 7) << 3) | (unsigned)m;
    return (uint8_t)(byte > 0x7E ? 0x7E : byte);
}

float omega_fp4_quantize(const float *w, uint32_t n, uint32_t k, uint8_t *w4, uint8_t *scales) {
    const size_t total = (size_t)n * k;
    double amax = 0.0;
    for (size_t i = 0; i < total; i++) { double a = fabs((double)w[i]); if (a > amax) amax = a; }
    float ts = amax > 0.0 ? (float)(amax / (6.0 * 448.0)) : 1.0f;
    const double tsd = (double)ts;
    for (uint32_t r = 0; r < n; r++) {
        const float *row = w + (size_t)r * k;
        for (uint32_t b = 0; b < k / 16; b++) {
            const float *blk = row + 16 * b;
            double ba = 0.0;
            for (int i = 0; i < 16; i++) { double a = fabs((double)blk[i]); if (a > ba) ba = a; }
            uint8_t best_s = 0, best_c[16] = { 0 };
            if (ba > 0.0) {
                uint8_t s0 = omega_fp4_e4m3_round((float)(ba / (6.0 * tsd)));
                double best_sse = INFINITY;
                for (int d = -1; d <= 1; d++) {
                    int s = (int)s0 + d;
                    if (s < 0 || s > 0x7E) continue;
                    double sv = (double)omega_fp4_e4m3_unsigned((uint8_t)s) * tsd;
                    uint8_t cc[16]; double sse = 0.0;
                    for (int i = 0; i < 16; i++) {
                        uint8_t c = sv > 0.0 ? omega_fp4_nearest_code((float)((double)blk[i] / sv)) : 0;
                        cc[i] = c;
                        double rec = (double)omega_fp4_value(c) * sv, e = (double)blk[i] - rec;
                        sse += e * e;
                    }
                    if (sse < best_sse) { best_sse = sse; best_s = (uint8_t)s; memcpy(best_c, cc, 16); }
                }
            }
            scales[(size_t)r * (k / 16) + b] = best_s;
            uint8_t *dst = w4 + (size_t)r * (k / 2) + 8 * (size_t)b;
            for (int i = 0; i < 16; i += 2) dst[i / 2] = (uint8_t)(best_c[i] | (best_c[i + 1] << 4));
        }
    }
    return ts;
}

void omega_fp4_dequantize(const uint8_t *w4, const uint8_t *scales, float ts, uint32_t n, uint32_t k, float *w) {
    for (uint32_t r = 0; r < n; r++)
        for (uint32_t c = 0; c < k; c++) {
            uint8_t byte = w4[(size_t)r * (k / 2) + c / 2];
            uint8_t code = (c & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 15);
            double s = (double)omega_fp4_e4m3_unsigned(scales[(size_t)r * (k / 16) + c / 16]) * (double)ts;
            w[(size_t)r * k + c] = (float)((double)omega_fp4_value(code) * s);
        }
}

size_t omega_fp4_scan_scales(const uint8_t *s, size_t count) {
    for (size_t i = 0; i < count; i++) if ((s[i] & 0x80u) || s[i] == 0x7F) return i + 1;
    return 0;
}

void omega_fp4_pack_x(const float *x, uint32_t k, float *out) { for (uint32_t i = 0; i < k; i++) out[i] = ldexpf(x[i], 64); }

int omega_fp4_pack_tscale(const float *ts, size_t count, float *out, int no_fold) {
    for (size_t i = 0; i < count; i++) {
        if (!(ts[i] > 0.0f) || !isfinite(ts[i])) return -1;
        float f = no_fold ? ts[i] : ldexpf(ts[i], 120);
        if (!isfinite(f)) return -1;
        out[i] = f;
    }
    return 0;
}

float omega_fp4_gemv_row_emulate(const uint8_t *w4row, const uint8_t *srow, float st2, const float *xp, uint32_t k, uint32_t lpa) {
    float acc[32];
    const uint32_t nb = k / 16, steps = nb / lpa;
    for (uint32_t l = 0; l < lpa; l++) {
        float a = 0.0f;
        for (uint32_t j = 0; j < steps; j++) {
            const uint32_t b = l + lpa * j;
            uint32_t wd[2]; memcpy(wd, w4row + 8u * b, 8);
            float dot = 0.0f;
            for (uint32_t i = 0; i < 16; i++) {
                uint32_t code = (wd[i / 8] >> (4 * (i % 8))) & 15u;
                dot = fmaf(bits_f(((code & 7u) << 22) | ((code & 8u) << 28)), xp[16 * b + i], dot);
            }
            float s1 = bits_f(omega_fp4_scale_bits(srow[b])) * st2;
            a = fmaf(dot, s1, a);
        }
        acc[l] = a;
    }
    for (uint32_t off = lpa / 2; off >= 1; off /= 2)
        for (uint32_t l = 0; l + off < lpa; l++) acc[l] = acc[l] + acc[l + off];
    return acc[0] * bits_f(0x5E800000u); /* 2^62 */
}

void omega_fp4_gemv_emulate(const uint8_t *w4, const uint8_t *scales, float st2, const float *xp, uint32_t n, uint32_t k, uint32_t lpa, float *y) {
    for (uint32_t r = 0; r < n; r++) y[r] = omega_fp4_gemv_row_emulate(w4 + (size_t)r * (k / 2), scales + (size_t)r * (k / 16), st2, xp, k, lpa);
}

void omega_fp4_gemv_oracle(const uint8_t *w4, const uint8_t *scales, float ts, uint32_t n, uint32_t k, const float *x, double *y, double *yscale) {
    for (uint32_t r = 0; r < n; r++) {
        double acc = 0.0, mag = 0.0;
        for (uint32_t c = 0; c < k; c++) {
            uint8_t byte = w4[(size_t)r * (k / 2) + c / 2];
            uint8_t code = (c & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 15);
            double wv = (double)omega_fp4_value(code) * (double)omega_fp4_e4m3_unsigned(scales[(size_t)r * (k / 16) + c / 16]) * (double)ts;
            double t = wv * (double)x[c];
            acc += t; mag += fabs(t);
        }
        y[r] = acc; yscale[r] = mag;
    }
}
