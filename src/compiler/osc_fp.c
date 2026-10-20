/*
 * osc_fp.c -- exact software rounding for the OSC-4 float slice formats.
 * See osc_fp.h. Integer arithmetic for every rounding; C float / double only
 * where a value is exact (f16 sums and products in double) or where the double
 * result is rounded again to a format at most 11 bits wide, which is innocuous
 * (Figueroa 1995: p' >= 2p + 2 for + - * / sqrt; 53 >= 24).
 */
#include "osc_fp.h"

#include <math.h>
#include <string.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

typedef struct {
    unsigned m;      /* fraction bits */
    unsigned w;      /* total bits */
    int bias;
    unsigned emaxf;  /* largest exponent field of a finite value */
    bool satfinite;  /* f8e4m3: overflow -> largest finite, no infinity */
} Fmt;

static Fmt fmt_of(OscScalar s) {
    switch (s) {
    case OSC_T_F16: return (Fmt){10, 16, 15, 30, false};
    case OSC_T_BF16: return (Fmt){7, 16, 127, 254, false};
    case OSC_T_F8E4M3: return (Fmt){3, 8, 7, 15, true};
    case OSC_T_F32: return (Fmt){23, 32, 127, 254, false};
    default: return (Fmt){52, 64, 1023, 2046, false};
    }
}
static uint64_t sign_bit(const Fmt *f) { return 1ULL << (f->w - 1); }
static uint64_t max_finite(const Fmt *f) { /* f8e4m3: S.1111.110 */
    return f->satfinite ? 0x7Eu : ((uint64_t)f->emaxf << f->m) | ((1ULL << f->m) - 1);
}
static uint64_t inf_bits(const Fmt *f) { return f->satfinite ? max_finite(f) : (uint64_t)(f->emaxf + 1) << f->m; }

static int msb128(u128 x) {
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    return hi ? 127 - __builtin_clzll(hi) : 63 - __builtin_clzll(lo);
}

uint64_t osc_fp_round(OscScalar to, bool neg, u128 mag, int e2) {
    Fmt f = fmt_of(to);
    uint64_t sg = neg ? sign_bit(&f) : 0;
    if (mag == 0) return sg;
    int E = msb128(mag) + e2;                 /* value in [2^E, 2^(E+1)) */
    int emin = 1 - f.bias;
    int emax = (int)f.emaxf - f.bias;
    if (E > emax) return sg | inf_bits(&f);   /* overflow before rounding */
    int q = (E < emin ? emin : E) - (int)f.m; /* quantum 2^q */
    int sh = q - e2;
    u128 n;
    if (sh <= 0) {
        n = mag << -sh;                       /* exact: E <= emax keeps n < 2^(m+1) */
    } else if (sh >= 129) {
        n = 0;                                /* below half a quantum */
    } else {
        u128 half = (u128)1 << (sh - 1);
        n = sh == 128 ? 0 : mag >> sh;
        u128 rem = sh == 128 ? mag : mag & (((u128)1 << sh) - 1);
        if (rem > half || (rem == half && (n & 1))) n++;
    }
    uint64_t bits;
    if (E < emin) {
        bits = (uint64_t)n;                   /* subnormal; n == 2^m is the smallest normal */
    } else {
        if (n == (u128)1 << (f.m + 1)) { n >>= 1; E++; }
        if (E > emax) return sg | inf_bits(&f);
        bits = ((uint64_t)(E + f.bias) << f.m) | ((uint64_t)n - (1ULL << f.m));
    }
    if (f.satfinite && bits > max_finite(&f)) bits = max_finite(&f);
    return sg | bits;
}

bool osc_fp_is_nan(OscScalar s, uint64_t b) {
    if (s == OSC_T_F8E4M3) return (b & 0x7F) == 0x7F;
    Fmt f = fmt_of(s);
    uint64_t mag = b & (sign_bit(&f) - 1);
    return mag > ((uint64_t)(f.emaxf + 1) << f.m);
}
bool osc_fp_is_inf(OscScalar s, uint64_t b) {
    if (s == OSC_T_F8E4M3) return false;
    Fmt f = fmt_of(s);
    return (b & (sign_bit(&f) - 1)) == ((uint64_t)(f.emaxf + 1) << f.m);
}

uint64_t osc_fp_from_f64(OscScalar to, uint64_t d) {
    Fmt f = fmt_of(to);
    bool neg = d >> 63;
    uint64_t sg = neg ? sign_bit(&f) : 0;
    uint64_t ex = (d >> 52) & 0x7FF, fr = d & 0xFFFFFFFFFFFFFull;
    if (ex == 0x7FF) {
        if (fr == 0) return sg | inf_bits(&f);
        if (to == OSC_T_F8E4M3) return sg | 0x7F;
        /* FPConvertNaN: sign, all-ones exponent, quiet bit, top (m-1) bits of frac[50:0] */
        uint64_t payload = (fr & 0x7FFFFFFFFFFFFull) >> (51 - (f.m - 1));
        return sg | ((uint64_t)(f.emaxf + 1) << f.m) | (1ULL << (f.m - 1)) | payload;
    }
    if (ex == 0) return osc_fp_round(to, neg, fr, -1074);
    return osc_fp_round(to, neg, fr | (1ULL << 52), (int)ex - 1075);
}

uint32_t osc_fp_widen32(OscScalar from, uint64_t b) {
    if (from == OSC_T_BF16) return (uint32_t)(b & 0xFFFF) << 16;
    /* F8E4M3 */
    uint32_t s = (uint32_t)(b & 0x80) << 24, em = (uint32_t)(b & 0x7F);
    if (em == 0x7F) return s | 0x7FC00000u;
    if (em >> 3) return s | ((em + (120u << 3)) << 20);       /* rebias 7 -> 127 */
    float v = (float)em * 0x1p-9f;                            /* subnormal m * 2^-9, exact */
    uint32_t w; memcpy(&w, &v, 4);
    return s | w;
}

static uint64_t f32_to_f64_bits(uint32_t w) {
    float x; memcpy(&x, &w, 4);
    volatile double d = (double)x;                            /* FCVT D, S: exact or FPConvertNaN */
    uint64_t r; memcpy(&r, (const void *)&d, 8);
    return r;
}

uint64_t osc_fp_to_f64(OscScalar from, uint64_t b) {
    switch (from) {
    case OSC_T_F64: return b;
    case OSC_T_F32: return f32_to_f64_bits((uint32_t)b);
    case OSC_T_BF16: case OSC_T_F8E4M3: return f32_to_f64_bits(osc_fp_widen32(from, b));
    default: break;
    }
    /* F16: FCVT D, H */
    uint64_t sg = (b & 0x8000) ? 1ULL << 63 : 0;
    uint64_t ex = (b >> 10) & 0x1F, fr = b & 0x3FF;
    if (ex == 0x1F) {
        if (fr == 0) return sg | 0x7FF0000000000000ull;
        return sg | 0x7FF8000000000000ull | ((fr & 0x1FF) << 42); /* frac = op[8:0]::Zeros(42) */
    }
    if (ex == 0) {
        if (fr == 0) return sg;
        double v = (double)fr * 0x1p-24;
        uint64_t r; memcpy(&r, &v, 8);
        return sg | r;
    }
    return sg | ((ex - 15 + 1023) << 52) | (fr << 42);
}

static double f16_val(uint64_t b) {
    uint64_t d = osc_fp_to_f64(OSC_T_F16, b);
    double x; memcpy(&x, &d, 8);
    return x;
}
static uint64_t f16_of_double(double x) {
    uint64_t d; memcpy(&d, &x, 8);
    return osc_fp_from_f64(OSC_T_F16, d);
}
static bool f16_snan(uint64_t b) { return osc_fp_is_nan(OSC_T_F16, b) && !(b & 0x200); }
static uint64_t f16_quiet(uint64_t b) { return (b | 0x200) & 0xFFFF; }

uint64_t osc_fp_f16_bin(unsigned sub, uint64_t a, uint64_t b) {
    bool an = osc_fp_is_nan(OSC_T_F16, a), bn = osc_fp_is_nan(OSC_T_F16, b);
    if (an || bn) { /* FPProcessNaNs */
        if (f16_snan(a)) return f16_quiet(a);
        if (f16_snan(b)) return f16_quiet(b);
        return an ? a : b;
    }
    volatile double x = f16_val(a), y = f16_val(b), r;
    switch (sub) {
    case OSC_FB_ADD: r = x + y; break;  /* exact in double */
    case OSC_FB_SUB: r = x - y; break;  /* exact in double */
    case OSC_FB_MUL: r = x * y; break;  /* exact in double */
    case OSC_FB_DIV: r = x / y; break;  /* rounded twice, innocuously (53 >= 2*11 + 2) */
    default: return 0;
    }
    return f16_of_double(r);
}

uint64_t osc_fp_f16_sqrt(uint64_t a) {
    if (osc_fp_is_nan(OSC_T_F16, a)) return f16_quiet(a);
    volatile double x = f16_val(a), r = sqrt(x); /* negative nonzero -> default NaN, -0 -> -0 */
    return f16_of_double(r);
}

/* f16 value as k * 2^-24 (k an integer, |k| < 2^40); finite only */
static i128 f16_k(uint64_t b) {
    uint64_t ex = (b >> 10) & 0x1F, fr = b & 0x3FF;
    i128 k = ex ? (i128)((fr | 0x400) << (ex - 1)) : (i128)fr;
    return (b & 0x8000) ? -k : k;
}

uint64_t osc_fp_f16_fma(uint64_t a, uint64_t b, uint64_t c) {
    bool an = osc_fp_is_nan(OSC_T_F16, a), bn = osc_fp_is_nan(OSC_T_F16, b), cn = osc_fp_is_nan(OSC_T_F16, c);
    bool ainf = osc_fp_is_inf(OSC_T_F16, a), binf = osc_fp_is_inf(OSC_T_F16, b), cinf = osc_fp_is_inf(OSC_T_F16, c);
    bool az = (a & 0x7FFF) == 0, bz = (b & 0x7FFF) == 0;
    if (an || bn || cn) { /* FPProcessNaNs3(addend c, op1 a, op2 b), then FPMulAdd's inf*0 rule */
        uint64_t r;
        if (f16_snan(c)) r = f16_quiet(c);
        else if (f16_snan(a)) r = f16_quiet(a);
        else if (f16_snan(b)) r = f16_quiet(b);
        else r = cn ? c : an ? a : b;
        if (cn && !f16_snan(c) && ((ainf && bz) || (az && binf))) r = 0x7E00; /* default NaN */
        return r;
    }
    if (ainf || binf || cinf) { /* no rounding happens: inf or invalid (default NaN) */
        volatile double x = f16_val(a), y = f16_val(b), z = f16_val(c), r = fma(x, y, z);
        return f16_of_double(r);
    }
    i128 p = f16_k(a) * f16_k(b);         /* exact, scale 2^-48 */
    i128 s = p + f16_k(c) * ((i128)1 << 24); /* exact, scale 2^-48 (a multiply: c may be negative) */
    if (s == 0) {
        /* exact zero: same-signed terms keep their sign, otherwise +0 (round to nearest) */
        bool ps = ((a ^ b) & 0x8000) != 0, cs = (c & 0x8000) != 0;
        bool pz = p == 0, czero = (c & 0x7FFF) == 0;
        if (pz && czero) return (ps && cs) ? 0x8000 : 0;
        return 0;
    }
    bool neg = s < 0;
    return osc_fp_round(OSC_T_F16, neg, (u128)(neg ? -s : s), -48);
}
