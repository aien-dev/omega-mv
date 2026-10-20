/*
 * float_g2_ref.c -- the C reference for gate G2 (docs/osc/OSC-4-FLOATS.md): the
 * kernels of tests/compiler/float_g2.osc written as plain C, one C operation per
 * OSC operation in the same order, compiled -O0 -ffp-contract=off (mk/compiler.mk).
 * f16 is C _Float16; bf16 is computed by the stated rule (f32 operation, one rounding
 * to __bf16 after each operation); f8e4m3 has no C type and uses the G1 reference
 * operations (float_slice_ref.c).
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "float_g2_ref.h"
#include "float_slice_ref.h"

static float fb(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
static uint64_t bf(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }

static void fill(float *x, uint64_t seed)
{
    uint64_t s = seed;
    for (int i = 0; i < 64; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        x[i] = (float)((int64_t)(s >> 40) - 8388608) * 0.00000095367431640625f;
    }
}

uint64_t g2_dot_f32(uint64_t seed, uint64_t k)
{
    float a[64], b[64], s = 0.0f;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) s = s + a[i] * b[i];
    return bf(s);
}

uint64_t g2_dot_fma_f32(uint64_t seed, uint64_t k)
{
    float a[64], b[64], s = 0.0f;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) s = fmaf(a[i], b[i], s);
    return bf(s);
}

uint64_t g2_dot_f64(uint64_t seed, uint64_t k)
{
    float a[64], b[64];
    double x[64], y[64], s = 0.0;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) { x[i] = (double)a[i] / 3.0; y[i] = (double)b[i]; }
    for (int i = 0; i < 64; i++) s = s + x[i] * y[i];
    uint64_t w;
    memcpy(&w, &s, 8);
    return w;
}

uint64_t g2_dot_f16(uint64_t seed, uint64_t k)
{
    float a[64], b[64];
    _Float16 x[64], y[64], s = 0.0f16;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) { x[i] = (_Float16)a[i]; y[i] = (_Float16)b[i]; }
    for (int i = 0; i < 64; i++) {
        _Float16 p = x[i] * y[i]; /* rounded to f16 here, whatever the excess-precision mode */
        s = s + p;
    }
    uint16_t w;
    memcpy(&w, &s, 2);
    return w;
}

uint64_t g2_dot_bf16(uint64_t seed, uint64_t k)
{
    float a[64], b[64];
    __bf16 x[64], y[64], s = (__bf16)0.0f;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) { x[i] = (__bf16)a[i]; y[i] = (__bf16)b[i]; }
    for (int i = 0; i < 64; i++) {
        __bf16 p = (__bf16)((float)x[i] * (float)y[i]);
        s = (__bf16)((float)s + (float)p);
    }
    uint16_t w;
    memcpy(&w, &s, 2);
    return w;
}

uint64_t g2_dot_f8e4m3(uint64_t seed, uint64_t k)
{
    float a[64], b[64];
    uint64_t x[64], y[64], s = 0;
    fill(a, seed);
    fill(b, seed + k + 1);
    for (int i = 0; i < 64; i++) {
        x[i] = ref_cvt(R_F32, R_F8, bf(a[i] * 0.25f));
        y[i] = ref_cvt(R_F32, R_F8, bf(b[i] * 0.25f));
    }
    for (int i = 0; i < 64; i++) s = ref_bin(R_F8, R_ADD, s, ref_bin(R_F8, R_MUL, x[i], y[i]));
    return s;
}

static float exp_f32(float x)
{
    float t = x * fb(0x3FB8AA3B);
    int32_t n = (int32_t)(t - 0.5f);
    float nf = (float)n;
    float r = (x - nf * 0.693145751953125f) - nf * fb(0x35BFBE8E);
    float p = fb(0x3AB60B61);
    p = p * r + fb(0x3C088889);
    p = p * r + fb(0x3D2AAAAB);
    p = p * r + fb(0x3E2AAAAB);
    p = p * r + fb(0x3F000000);
    p = p * r + 1.0f;
    p = p * r + 1.0f;
    uint32_t sc = (uint32_t)(n + 127) << 23;
    return p * fb(sc);
}

uint64_t g2_softmax_f32(uint64_t seed, uint64_t k)
{
    float x[64], e[64], m, s = 0.0f;
    fill(x, seed);
    m = x[0];
    for (int i = 1; i < 64; i++) m = fmaxf(m, x[i]);
    for (int i = 0; i < 64; i++) { e[i] = exp_f32(x[i] - m); s = s + e[i]; }
    for (int i = 0; i < 64; i++) e[i] = e[i] / s;
    return bf(e[k]);
}
