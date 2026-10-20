/*
 * float_slice_ref.c -- the C reference for gate G1 of the OSC-4 float slice
 * (docs/osc/OSC-4-FLOATS.md). It is the specification of the rounding: every
 * function is the plain C expression of the rule, compiled by gcc with
 *   -O0 -ffp-contract=off -march=armv8.2-a+fp16
 * (mk/compiler.mk). gcc's output is a reference oracle only; nothing here is an
 * input to the compiler. Shares no code with src/compiler/osc_fp.c:
 *   f16    C _Float16 (gcc: the half-precision instructions, FMADD H for fmaf16;
 *          sqrt through sqrtf, see ref_un)
 *   bf16   widened by its definition (see widen); float -> __bf16 and double -> __bf16 are libgcc's software
 *          conversions (__truncsfbf2, __truncdfbf2), not BFCVT (no +bf16 here)
 *   f8e4m3 no C type: decoded from the OCP formula with ldexpf, encoded by a
 *          brute-force search over all 256 codes (nearest, ties to the even code,
 *          beyond 448 saturate, NaN -> S.1111.111)
 *   f32 / f64  C float / double
 * Values cross the interface as bit patterns in uint64_t, low bits used.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "float_slice_ref.h"

/* ---- bit-pattern views ---- */
static _Float16 h_of(uint64_t x) { uint16_t w = (uint16_t)x; _Float16 h; memcpy(&h, &w, 2); return h; }
static uint64_t b_h(_Float16 h) { uint16_t w; memcpy(&w, &h, 2); return w; }
static uint64_t b_bf(__bf16 h) { uint16_t w; memcpy(&w, &h, 2); return w; }
static float f_of(uint64_t x) { uint32_t w = (uint32_t)x; float f; memcpy(&f, &w, 4); return f; }
static uint64_t b_f(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
static double d_of(uint64_t x) { double d; memcpy(&d, &x, 8); return d; }
static uint64_t b_d(double d) { uint64_t w; memcpy(&w, &d, 8); return w; }

/* ---- f8e4m3 by its definition ---- */
static float e4_val[256];
static int e4_ready;
static void e4_init(void)
{
    for (int c = 0; c < 256; c++) {
        int s = c >> 7, e = (c >> 3) & 15, m = c & 7;
        float v;
        if (e == 15 && m == 7) v = NAN;
        else if (e == 0) v = ldexpf((float)m, -9);        /* m/8 * 2^-6 */
        else v = ldexpf((float)(8 + m), e - 10);           /* (1 + m/8) * 2^(e-7) */
        e4_val[c] = s ? -v : v;
    }
    e4_ready = 1;
}
static float e4_dec(uint64_t c)
{
    if (!e4_ready) e4_init();
    float v = e4_val[c & 0xFF];
    if (isnan(v)) { uint32_t w = (c & 0x80) ? 0xFFC00000u : 0x7FC00000u; memcpy(&v, &w, 4); } /* stated widening */
    return v;
}
static uint64_t e4_enc(double x)
{
    if (!e4_ready) e4_init();
    if (isnan(x)) return signbit(x) ? 0xFF : 0x7F;
    int neg = signbit(x) != 0;
    double ax = fabs(x);
    if (ax >= 448.0) return neg ? 0xFE : 0x7E;           /* satfinite (inf included) */
    int best = 0;
    double bd = INFINITY;
    for (int c = 0; c < 0x7F; c++) {                      /* the 127 non-negative finite codes */
        double d = fabs((double)e4_val[c] - ax);
        if (d < bd || (d == bd && (c & 1) == 0)) { bd = d; best = c; }
    }
    return (uint64_t)best | (neg ? 0x80u : 0u);
}

/* ---- NaN results, spelled out (DDI0487 M.d J1.4 FPProcessNaNs, FPProcessNaNs3, FPMulAdd,
 * FPMinNum / FPMaxNum): C does not say which NaN operand comes out (gcc may swap the operands
 * of + and * or the fma multiplicands), so the reference states the rule for f16, f32, f64
 * (the storage formats compute in f32). 1 and the result in *r when a NaN decides it. */
static unsigned nm_(int t) { return t == R_F16 ? 10 : t == R_F32 ? 23 : 52; }
static uint64_t nmask(int t) { return t == R_F64 ? ~0ull >> 1 : (1ull << (t == R_F16 ? 15 : 31)) - 1; }
static uint64_t ninf(int t) { return nmask(t) & ~((1ull << nm_(t)) - 1); }
static int isnan_(int t, uint64_t x) { return (x & nmask(t)) > ninf(t); }
static int issnan_(int t, uint64_t x) { return isnan_(t, x) && !(x >> (nm_(t) - 1) & 1); }
static uint64_t quiet_(int t, uint64_t x) { return x | 1ull << (nm_(t) - 1); }
static int nan2(int t, uint64_t a, uint64_t b, uint64_t *r)
{
    if (issnan_(t, a)) *r = quiet_(t, a);
    else if (issnan_(t, b)) *r = quiet_(t, b);
    else if (isnan_(t, a)) *r = a;
    else if (isnan_(t, b)) *r = b;
    else return 0;
    return 1;
}
static int nan3(int t, uint64_t a, uint64_t b, uint64_t c, uint64_t *r)
{
    if (issnan_(t, c)) *r = quiet_(t, c);
    else if (issnan_(t, a)) *r = quiet_(t, a);
    else if (issnan_(t, b)) *r = quiet_(t, b);
    else if (isnan_(t, c)) *r = c;
    else if (isnan_(t, a)) *r = a;
    else if (isnan_(t, b)) *r = b;
    else return 0;
    uint64_t ma = a & nmask(t), mb = b & nmask(t);
    if (isnan_(t, c) && !issnan_(t, c) && ((ma == ninf(t) && mb == 0) || (ma == 0 && mb == ninf(t))))
        *r = ninf(t) | 1ull << (nm_(t) - 1); /* default NaN */
    return 1;
}
/* FMINNM / FMAXNM: one quiet NaN and one number give the number */
static int nan_mm(int t, uint64_t a, uint64_t b, uint64_t *r)
{
    if (isnan_(t, a) && !issnan_(t, a) && !isnan_(t, b)) { *r = b; return 1; }
    if (isnan_(t, b) && !issnan_(t, b) && !isnan_(t, a)) { *r = a; return 1; }
    return nan2(t, a, b, r);
}

/* ---- values: a float type's bit pattern as a C number (for the storage formats, the f32 widening) ---- */
/* bf16 widens as the manual defines it, the 16 bits followed by 16 zeros (DDI0487 M.d J1.4.3.106
 * BFMulAddH: "value1 = op1 :: Zeros{16}"), so a signalling NaN stays signalling; C's (float) on a
 * __bf16 (libgcc __extendbfsf2) quietens it instead. Every non-NaN value is the same either way. */
static float widen(int t, uint64_t a) { return t == R_BF16 ? f_of((a & 0xFFFF) << 16) : e4_dec(a); }
static uint64_t narrow_f(int t, float x) { return t == R_BF16 ? b_bf((__bf16)x) : e4_enc((double)x); }

uint64_t ref_bin(int t, int op, uint64_t a, uint64_t b)
{
    /* storage formats: the f32 operation of the exact widenings, then one rounding */
    if (t == R_BF16 || t == R_F8) return narrow_f(t, f_of(ref_bin(R_F32, op, b_f(widen(t, a)), b_f(widen(t, b)))));
    uint64_t nr;
    if (op >= R_MIN ? nan_mm(t, a, b, &nr) : nan2(t, a, b, &nr)) return nr;
    switch (t) {
    case R_F16: {
        _Float16 x = h_of(a), y = h_of(b), r;
        switch (op) {
        case R_ADD: r = x + y; break;
        case R_SUB: r = x - y; break;
        case R_MUL: r = x * y; break;
        case R_DIV: r = x / y; break;
        case R_MIN: r = __builtin_fminf16(x, y); break;
        default: r = __builtin_fmaxf16(x, y); break;
        }
        return b_h(r);
    }
    case R_F32: {
        float x = f_of(a), y = f_of(b), r;
        switch (op) {
        case R_ADD: r = x + y; break;
        case R_SUB: r = x - y; break;
        case R_MUL: r = x * y; break;
        case R_DIV: r = x / y; break;
        case R_MIN: r = __builtin_fminf(x, y); break;
        default: r = __builtin_fmaxf(x, y); break;
        }
        return b_f(r);
    }
    default: { /* R_F64 */
        double x = d_of(a), y = d_of(b), r;
        switch (op) {
        case R_ADD: r = x + y; break;
        case R_SUB: r = x - y; break;
        case R_MUL: r = x * y; break;
        case R_DIV: r = x / y; break;
        case R_MIN: r = __builtin_fmin(x, y); break;
        default: r = __builtin_fmax(x, y); break;
        }
        return b_d(r);
    }
    }
}

uint64_t ref_fma(int t, uint64_t a, uint64_t b, uint64_t c)
{
    if (t == R_BF16 || t == R_F8)
        return narrow_f(t, f_of(ref_fma(R_F32, b_f(widen(t, a)), b_f(widen(t, b)), b_f(widen(t, c)))));
    uint64_t nr;
    if (nan3(t, a, b, c, &nr)) return nr;
    switch (t) {
    case R_F16: return b_h(__builtin_fmaf16(h_of(a), h_of(b), h_of(c)));
    case R_F32: return b_f(fmaf(f_of(a), f_of(b), f_of(c)));
    default: return b_d(fma(d_of(a), d_of(b), d_of(c)));
    }
}

uint64_t ref_un(int t, int op, uint64_t a)
{
    if (op == R_NEG || op == R_ABS) { /* sign-bit operations in every format */
        uint64_t sg = t == R_F8 ? 0x80 : t == R_F32 ? 0x80000000u : t == R_F64 ? 0x8000000000000000ull : 0x8000;
        return op == R_NEG ? a ^ sg : a & ~sg;
    }
    switch (t) {
    case R_F16: return b_h((_Float16)sqrtf((float)h_of(a))); /* glibc has no sqrtf16; one f32 rounding then
                                                                  one f16 rounding is correct (24 >= 2*11 + 2) */
    case R_F32: return b_f(sqrtf(f_of(a)));
    case R_F64: return b_d(sqrt(d_of(a)));
    default: return narrow_f(t, sqrtf(widen(t, a)));
    }
}

/* the six comparisons as a mask: == 1, != 2, < 4, <= 8, > 16, >= 32 */
uint64_t ref_cmp(int t, uint64_t a, uint64_t b)
{
    double x, y;
    switch (t) {
    case R_F16: x = (double)h_of(a); y = (double)h_of(b); break;
    case R_F32: x = f_of(a); y = f_of(b); break;
    case R_F64: x = d_of(a); y = d_of(b); break;
    default: x = widen(t, a); y = widen(t, b); break;
    }
    return (uint64_t)((x == y) | (x != y) << 1 | (x < y) << 2 | (x <= y) << 3 | (x > y) << 4 | (x >= y) << 5);
}

uint64_t ref_cvt(int from, int to, uint64_t a)
{
    /* the source as the narrowest C type that holds it */
    switch (from) {
    case R_F64: {
        double x = d_of(a);
        switch (to) {
        case R_F16: return b_h((_Float16)x);
        case R_BF16: return b_bf((__bf16)x);
        case R_F8: return e4_enc(x);
        default: return b_f((float)x);
        }
    }
    case R_F16: {
        _Float16 x = h_of(a);
        switch (to) {
        case R_F64: return b_d((double)x);
        case R_F32: return b_f((float)x);
        case R_BF16: return b_bf((__bf16)(float)x);
        default: return e4_enc((double)(float)x);
        }
    }
    case R_F32: {
        float x = f_of(a);
        switch (to) {
        case R_F64: return b_d((double)x);
        case R_F16: return b_h((_Float16)x);
        case R_BF16: return b_bf((__bf16)x);
        default: return e4_enc((double)x);
        }
    }
    default: { /* bf16 / f8e4m3: the exact f32 widening, then the f32 conversion */
        float x = widen(from, a);
        switch (to) {
        case R_F64: return b_d((double)x);
        case R_F32: return b_f(x);
        case R_F16: return b_h((_Float16)x);
        default: return narrow_f(to, x);
        }
    }
    }
}

uint64_t ref_itof(int to, int sgn, uint64_t a)
{
    if (to == R_F16) return b_h(sgn ? (_Float16)(int64_t)a : (_Float16)a);
    if (to == R_F32) return b_f(sgn ? (float)(int64_t)a : (float)a);
    if (to == R_F64) return b_d(sgn ? (double)(int64_t)a : (double)a);
    double v = sgn ? (double)(int64_t)a : (double)a; /* exact: the storage formats take at most 32 bits */
    return to == R_BF16 ? b_bf((__bf16)v) : e4_enc(v);
}

/* truncate toward zero, saturate to the w-bit range, NaN -> 0 (the FCVTZS / FCVTZU rule) */
uint64_t ref_ftoi(int from, int sgn, unsigned w, uint64_t a)
{
    double x;
    switch (from) {
    case R_F16: x = (double)h_of(a); break;
    case R_F32: x = f_of(a); break;
    case R_F64: x = d_of(a); break;
    default: x = widen(from, a); break;
    }
    if (isnan(x)) return 0;
    double half = ldexp(1.0, (int)w - 1);
    uint64_t mask = w == 64 ? ~0ull : (1ull << w) - 1;
    if (sgn) {
        if (x >= half) return mask >> 1;                         /* largest w-bit signed value */
        if (x <= -half) return ~(mask >> 1);                     /* smallest, sign-extended to 64 bits */
        return (uint64_t)(int64_t)trunc(x);
    }
    if (x <= 0.0) return 0;
    if (x >= 2.0 * half) return mask;
    return (uint64_t)trunc(x);
}
