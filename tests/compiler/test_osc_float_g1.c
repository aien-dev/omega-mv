/*
 * test_osc_float_g1.c -- gate G1 of the OSC-4 float slice (docs/osc/OSC-4-FLOATS.md).
 *
 * For every operation of the slice at every float type (f16, bf16, f8e4m3, f32,
 * f64): + - * / min max, fma, neg abs sqrt, the six comparisons, every float to
 * float conversion, int <-> float conversions, and array load / store, it
 *   1. compiles a one-operation OSC unit (twice: the code must be identical),
 *   2. decodes every emitted word with our decoder and re-encodes it (must give
 *      the same word), counting the float data-processing words,
 *   3. runs N random operand triples (default 10,000, argv[1]) on native code and
 *      on the reference interpreter, and compares both, bit for bit, with the C
 *      reference in float_slice_ref.c (compiled -O0 -ffp-contract=off, gcc output
 *      used as an oracle only).
 * Then the strict section: `strict fn` traps OSC_TRAP_FLOAT (15) when a float
 * operation produces NaN or infinity, the same unit without `strict` propagates.
 * Output: one "g1 ..." line per case, "g1 strict: ..." and OSC4_FLOAT_G1_PASS / _FAIL.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "float_slice_ref.h"
#include "osc_a64.h"
#include "osc_cg.h"
#include "osc_front.h"
#include "osc_interp.h"
#include "osc_native.h"
#include "osc_rt.h"

static int fails;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fails++;                                      \
            if (fails <= 40) {                            \
                printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf(__VA_ARGS__);                      \
                printf("\n");                             \
            }                                             \
        }                                                 \
    } while (0)

static OscUnit *U;
static OscRt *RI, *RN;
static uint64_t rs = 0x6A1F10A7C0FFEEull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static const char *const TN[] = {"f16", "bf16", "f8e4m3", "f32", "f64"};
static const char *const TU[] = {"u16", "u16", "u8", "u32", "u64"};
static const unsigned TM[] = {10, 7, 3, 23, 52};   /* fraction bits */
static const unsigned TW[] = {16, 16, 8, 32, 64};  /* total bits */

/* a float operand of type t: specials, raw bits, values near 1 (so operations
 * interact and round), and values whose low fraction bits make an exact tie for a
 * narrower format (conversions and narrowing round exactly halfway) */
static uint64_t gen_float(int t)
{
    unsigned m = TM[t], w = TW[t], e = w - 1 - m;
    uint64_t sign = 1ull << (w - 1), mmask = (1ull << m) - 1, emax = (1ull << e) - 1, r = rnd();
    uint64_t s = (r & 1) ? sign : 0;
    switch ((r >> 1) % 8) {
    case 0: { /* specials */
        uint64_t sp[] = {0, 1, mmask, 1ull << m, (emax - 1) << m | mmask, (emax >> 1) << m,
                         emax << m, emax << m | 1, emax << m | (1ull << (m - 1)), emax << m | mmask};
        uint64_t v = sp[(r >> 8) % 10];
        return s | v;
    }
    case 1: case 2: case 3: return rnd() & ((sign << 1) - 1);
    case 4: case 5: case 6: { /* exponent within +-(m + 4) of the bias, low half of the fraction sometimes zero */
        int span = (int)m + 4;
        int64_t ex = (int64_t)(emax >> 1) + (int64_t)((r >> 8) % (uint64_t)(2 * span + 1)) - span;
        if (ex < 1) ex = 1;
        if (ex > (int64_t)emax - 1) ex = (int64_t)emax - 1;
        uint64_t f = rnd() & mmask;
        if ((r >> 20) & 1) f &= ~(mmask >> (m / 2));
        return s | (uint64_t)ex << m | f;
    }
    default: { /* a tie for p fraction bits: bit (m-p-1) set, all below clear */
        static const unsigned ps[] = {3, 7, 10, 23};
        unsigned p = ps[(r >> 8) % 4];
        uint64_t f = rnd() & mmask;
        if (p < m) f = (f & ~((1ull << (m - p)) - 1)) | (1ull << (m - p - 1));
        uint64_t ex = 1 + (r >> 16) % (emax - 1);
        return s | ex << m | f;
    }
    }
}

/* an integer operand of width w, extended to 64 bits as the cell holds it */
static uint64_t gen_int(unsigned w, int sgn)
{
    uint64_t r = rnd(), v;
    switch (r % 4) {
    case 0: v = rnd() % 4096; break;
    case 1: { unsigned k = (unsigned)((r >> 8) % w); v = (1ull << k) + (rnd() % 64) - 32; break; } /* near 2^k: ties */
    default: v = rnd(); break;
    }
    if ((r >> 40) & 1) v = (uint64_t)-(int64_t)v;
    if (w < 64) {
        v &= (1ull << w) - 1;
        if (sgn && (v >> (w - 1))) v |= ~((1ull << w) - 1);
    }
    return v;
}

static void cat(char *dst, size_t cap, const char *fmt, ...)
{
    size_t n = strlen(dst);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst + n, cap - n, fmt, ap);
    va_end(ap);
}

typedef enum { K_BIN, K_FMA, K_UN, K_CMP, K_CVT, K_ITOF, K_FTOI, K_LOAD, K_STORE } Kind;
typedef struct {
    Kind k;
    int t, op, t2;    /* t: float type (K_ITOF: destination); op: R_ADD.. / R_NEG..; t2: K_CVT destination */
    unsigned iw;      /* K_ITOF / K_FTOI integer width */
    int isg;          /* ... and signedness */
} Case;

static const char *const BIN_OP[] = {"+", "-", "*", "/", "min", "max"};
static const char *const UN_OP[] = {"neg", "abs", "sqrt"};

static void case_name(const Case *c, char *b, size_t n)
{
    switch (c->k) {
    case K_BIN: snprintf(b, n, "%s %s", BIN_OP[c->op], TN[c->t]); break;
    case K_FMA: snprintf(b, n, "fma %s", TN[c->t]); break;
    case K_UN: snprintf(b, n, "%s %s", UN_OP[c->op], TN[c->t]); break;
    case K_CMP: snprintf(b, n, "cmp %s", TN[c->t]); break;
    case K_CVT: snprintf(b, n, "cvt %s->%s", TN[c->t], TN[c->t2]); break;
    case K_ITOF: snprintf(b, n, "cvt %c%u->%s", c->isg ? 'i' : 'u', c->iw, TN[c->t]); break;
    case K_FTOI: snprintf(b, n, "cvt %s->%c%u", TN[c->t], c->isg ? 'i' : 'u', c->iw); break;
    case K_LOAD: snprintf(b, n, "load %s", TN[c->t]); break;
    default: snprintf(b, n, "store %s", TN[c->t]); break;
    }
}

static void case_src(const Case *c, char *src, size_t cap, int strict)
{
    const char *T = TN[c->t], *B = TU[c->t];
    src[0] = 0;
    if (c->k == K_ITOF) {
        cat(src, cap, "%sfn entry(a: %c%u, b: %c%u, c: %c%u) -> u64 {\n    return ((a as %s) as %s bits) as u64;\n}\n",
            strict ? "strict " : "", c->isg ? 'i' : 'u', c->iw, c->isg ? 'i' : 'u', c->iw, c->isg ? 'i' : 'u', c->iw, T, B);
        return;
    }
    cat(src, cap, "%sfn entry(a: %s, b: %s, c: %s) -> %s {\n", strict ? "strict " : "", T, T, T,
        c->k == K_FTOI && c->isg ? "i64" : "u64");
    switch (c->k) {
    case K_BIN:
        if (c->op >= R_MIN) cat(src, cap, "    return (%s(a, b) as %s bits) as u64;\n", BIN_OP[c->op], B);
        else cat(src, cap, "    return ((a %s b) as %s bits) as u64;\n", BIN_OP[c->op], B);
        break;
    case K_FMA: cat(src, cap, "    return (fma(a, b, c) as %s bits) as u64;\n", B); break;
    case K_UN:
        if (c->op == R_NEG) cat(src, cap, "    return ((-a) as %s bits) as u64;\n", B);
        else cat(src, cap, "    return (%s(a) as %s bits) as u64;\n", UN_OP[c->op], B);
        break;
    case K_CMP:
        cat(src, cap,
            "    let mut m: u64 = 0;\n"
            "    if a == b { m = m | 1; }\n    if a != b { m = m | 2; }\n    if a < b { m = m | 4; }\n"
            "    if a <= b { m = m | 8; }\n    if a > b { m = m | 16; }\n    if a >= b { m = m | 32; }\n"
            "    return m;\n");
        break;
    case K_CVT: cat(src, cap, "    return ((a as %s) as %s bits) as u64;\n", TN[c->t2], TU[c->t2]); break;
    case K_FTOI: cat(src, cap, "    return (a as %c%u) as %s;\n", c->isg ? 'i' : 'u', c->iw, c->isg ? "i64" : "u64"); break;
    case K_LOAD: case K_STORE:
        cat(src, cap, "    let m: own [%s; 3] = alloc(a);\n    m[1] = b;\n    m[2] = m[1];\n    return (m[%d] as %s bits) as u64;\n", T,
            c->k == K_LOAD ? 0 : 2, B);
        break;
    default: break;
    }
    cat(src, cap, "}\n");
}

static uint64_t case_ref(const Case *c, uint64_t a, uint64_t b, uint64_t cc)
{
    switch (c->k) {
    case K_BIN: return ref_bin(c->t, c->op, a, b);
    case K_FMA: return ref_fma(c->t, a, b, cc);
    case K_UN: return ref_un(c->t, c->op, a);
    case K_CMP: return ref_cmp(c->t, a, b);
    case K_CVT: return ref_cvt(c->t, c->t2, a);
    case K_ITOF: return ref_itof(c->t, c->isg, a);
    case K_FTOI: return ref_ftoi(c->t, c->isg, c->iw, a);
    case K_LOAD: return a;
    default: return b;
    }
}

static int is_fp_word(uint32_t w)
{
    uint32_t top = (w >> 24) & 0x5F;
    return top == 0x1E || top == 0x1F || (w & 0xFFFFFC00u) == 0x7E616800u;
}

/* compile src; on success map it and return the entry, code kept in *c and *nm */
static void *build(const char *src, const char *name, OscCode *c, OscNative *nm, unsigned *words, unsigned *fpw)
{
    OscDiag d;
    char err[160];
    if (osc_compile(src, strlen(src), U, &d, NULL) != 0) {
        CHECK(0, "%s: refused %s line %u: %s\n%s", name, osc_diag_kind_name(d.kind), d.line, d.message, src);
        return NULL;
    }
    OscCode c2;
    memset(c, 0, sizeof *c);
    memset(&c2, 0, sizeof c2);
    if (osc_cg_compile(U, c, err, sizeof err) != 0) { CHECK(0, "%s: codegen refused: %s", name, err); return NULL; }
    int same = osc_cg_compile(U, &c2, err, sizeof err) == 0 && c2.len == c->len && memcmp(c2.code, c->code, c->len) == 0;
    CHECK(same, "%s: code differs between two compiles", name);
    osc_cg_free(&c2);
    *words = *fpw = 0;
    for (size_t off = 0; off < c->len; off += 4) {
        uint32_t w = (uint32_t)c->code[off] | ((uint32_t)c->code[off + 1] << 8) | ((uint32_t)c->code[off + 2] << 16) |
                     ((uint32_t)c->code[off + 3] << 24);
        OscA64Insn i1, i2;
        uint32_t re = 0;
        int ok = osc_a64_decode(w, &i1) == 0 && osc_a64_reencode(&i1, &re) == 0 && re == w && osc_a64_decode(re, &i2) == 0 &&
                 osc_a64_equal(&i1, &i2);
        CHECK(ok, "%s: word +%zu 0x%08x does not decode and re-encode", name, off, w);
        (*words)++;
        *fpw += (unsigned)is_fp_word(w);
    }
    if (osc_native_map(nm, c->code, c->len) != 0) { CHECK(0, "%s: native map failed", name); osc_cg_free(c); return NULL; }
    return osc_native_at(nm, c->entry[U->nfuncs - 1]);
}

static unsigned long tot_runs, tot_words, tot_fpw, tot_cases;

static void run_case(const Case *c, unsigned n)
{
    char name[48], src[1024];
    case_name(c, name, sizeof name);
    case_src(c, src, sizeof src, 0);
    OscCode code;
    OscNative nm;
    unsigned words, fpw;
    void *entry = build(src, name, &code, &nm, &words, &fpw);
    if (!entry) return;
    int fi = U->nfuncs - 1;
    unsigned long nmis = 0, imis = 0;
    for (unsigned it = 0; it < n; it++) {
        uint64_t args[3];
        for (int k = 0; k < 3; k++) args[k] = c->k == K_ITOF ? gen_int(c->iw, c->isg) : gen_float(c->t);
        uint64_t want = case_ref(c, args[0], args[1], args[2]), ri = 0, rn = 0;
        osc_rt_reset(RI);
        osc_rt_reset(RN);
        int t1 = osc_interp_run_prevalidated(U, fi, args, 3, RI, &ri);
        int t2 = osc_rt_call_native(RN, entry, args, 3, &rn);
        if (t2 != 0 || rn != want) {
            if (nmis++ < 3)
                CHECK(0, "%s native: args %#llx %#llx %#llx -> trap %d %#llx, reference %#llx", name, (unsigned long long)args[0],
                      (unsigned long long)args[1], (unsigned long long)args[2], t2, (unsigned long long)rn,
                      (unsigned long long)want);
        }
        if (t1 != 0 || ri != want) {
            if (imis++ < 3)
                CHECK(0, "%s interp: args %#llx %#llx %#llx -> trap %d %#llx, reference %#llx", name, (unsigned long long)args[0],
                      (unsigned long long)args[1], (unsigned long long)args[2], t1, (unsigned long long)ri,
                      (unsigned long long)want);
        }
    }
    if (nmis > 3) CHECK(0, "%s native: %lu mismatches in all", name, nmis);
    if (imis > 3) CHECK(0, "%s interp: %lu mismatches in all", name, imis);
    printf("g1 %-18s runs=%u words=%u fp_words=%u native_mismatch=%lu interp_mismatch=%lu\n", name, n, words, fpw, nmis, imis);
    tot_runs += n;
    tot_words += words;
    tot_fpw += fpw;
    tot_cases++;
    osc_native_unmap(&nm);
    osc_cg_free(&code);
}

/* strict: run a unit with and without `strict` on fixed operands, expect a trap (or not) */
static unsigned long st_trap, st_quiet;
static void strict_one(const Case *c, uint64_t a, uint64_t b, uint64_t cc, int expect_trap)
{
    char name[48], src[1024];
    case_name(c, name, sizeof name);
    for (int strict = 0; strict < 2; strict++) {
        case_src(c, src, sizeof src, strict);
        OscCode code;
        OscNative nm;
        unsigned words, fpw;
        void *entry = build(src, name, &code, &nm, &words, &fpw);
        if (!entry) return;
        uint64_t args[3] = {a, b, cc}, ri = 0, rn = 0, want = case_ref(c, a, b, cc);
        osc_rt_reset(RI);
        osc_rt_reset(RN);
        int t1 = osc_interp_run_prevalidated(U, U->nfuncs - 1, args, 3, RI, &ri);
        int t2 = osc_rt_call_native(RN, entry, args, 3, &rn);
        int et = strict && expect_trap ? OSC_TRAP_FLOAT : 0;
        CHECK(t1 == et && t2 == et, "strict=%d %s args %#llx %#llx %#llx: traps interp %d native %d, expected %d", strict, name,
              (unsigned long long)a, (unsigned long long)b, (unsigned long long)cc, t1, t2, et);
        if (et == 0) CHECK(ri == want && rn == want, "strict=%d %s: result interp %#llx native %#llx, reference %#llx", strict,
                           name, (unsigned long long)ri, (unsigned long long)rn, (unsigned long long)want);
        CHECK(osc_rt_same_outcome(RI, RN), "strict=%d %s: interpreter and native outcomes differ", strict, name);
        if (et) st_trap++; else st_quiet++;
        osc_native_unmap(&nm);
        osc_cg_free(&code);
    }
}

static void strict_section(void)
{
    /* per type: 1.0, the largest finite value, a quiet NaN; f8e4m3 saturates, so it has no overflow case */
    static const uint64_t one[] = {0x3C00, 0x3F80, 0x38, 0x3F800000u, 0x3FF0000000000000ull};
    static const uint64_t big[] = {0x7BFF, 0x7F7F, 0x7E, 0x7F7FFFFFu, 0x7FEFFFFFFFFFFFFFull};
    static const uint64_t qnan[] = {0x7E00, 0x7FC0, 0x7F, 0x7FC00000u, 0x7FF8000000000000ull};
    for (int t = 0; t < 5; t++) {
        Case add = {K_BIN, t, R_ADD, 0, 0, 0}, sq = {K_UN, t, R_SQRT, 0, 0, 0}, fm = {K_FMA, t, 0, 0, 0, 0};
        Case ld = {K_LOAD, t, 0, 0, 0, 0}, ng = {K_UN, t, R_NEG, 0, 0, 0}, cv = {K_CVT, R_F64, 0, t, 0, 0};
        uint64_t neg_one = one[t] | 1ull << (TW[t] - 1);
        strict_one(&add, one[t], one[t], 0, 0);                    /* finite: no trap either way */
        strict_one(&add, qnan[t], one[t], 0, 1);                   /* NaN in, NaN out */
        if (t != R_F8) strict_one(&add, big[t], big[t], 0, 1);     /* overflow to infinity */
        else strict_one(&add, big[t], big[t], 0, 0);               /* f8e4m3 saturates: finite */
        strict_one(&sq, neg_one, 0, 0, 1);                         /* sqrt(-1): invalid, default NaN */
        strict_one(&fm, one[t], one[t], qnan[t], 1);
        strict_one(&ld, qnan[t], one[t], 0, 0);                    /* loads, stores and parameters are not checked */
        strict_one(&ng, qnan[t], 0, 0, 1);                         /* NEG produces a NaN: checked */
        if (t != R_F64) strict_one(&cv, 0x7FF8000000000000ull, 0, 0, 1); /* f64 NaN converted */
    }
    printf("g1 strict: trapping=%lu propagating=%lu\n", st_trap, st_quiet);
}

int main(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 10000;
    U = malloc(sizeof *U);
    RI = malloc(sizeof *RI);
    RN = malloc(sizeof *RN);
    if (!U || !RI || !RN) { printf("OSC4_FLOAT_G1_FAIL out of memory\n"); return 1; }
    osc_rt_init(RI);
    osc_rt_init(RN);
    printf("g1: seed %#llx, %u operand triples per case\n", (unsigned long long)rs, n);
    static const struct { unsigned w; int s; } INTS[] = {{32, 1}, {32, 0}, {64, 1}, {64, 0}, {8, 1}, {16, 0}, {16, 1}, {8, 0}};
    for (int t = 0; t < 5; t++) {
        for (int op = R_ADD; op <= R_MAX; op++) run_case(&(Case){K_BIN, t, op, 0, 0, 0}, n);
        run_case(&(Case){K_FMA, t, 0, 0, 0, 0}, n);
        for (int op = R_NEG; op <= R_SQRT; op++) run_case(&(Case){K_UN, t, op, 0, 0, 0}, n);
        run_case(&(Case){K_CMP, t, 0, 0, 0, 0}, n);
        for (int t2 = 0; t2 < 5; t2++)
            if (t2 != t) run_case(&(Case){K_CVT, t, 0, t2, 0, 0}, n);
        for (int i = 0; i < 8; i++) {
            /* bf16 / f8e4m3 take integers of at most 32 bits (the checker refuses wider: double rounding) */
            if (!(INTS[i].w == 64 && (t == R_BF16 || t == R_F8))) run_case(&(Case){K_ITOF, t, 0, 0, INTS[i].w, INTS[i].s}, n);
            run_case(&(Case){K_FTOI, t, 0, 0, INTS[i].w, INTS[i].s}, n);
        }
        run_case(&(Case){K_LOAD, t, 0, 0, 0, 0}, n);
        run_case(&(Case){K_STORE, t, 0, 0, 0, 0}, n);
    }
    strict_section();
    printf("g1: cases=%lu runs=%lu (each on native and interpreter) words=%lu fp_words=%lu failures=%d\n", tot_cases, tot_runs,
           tot_words, tot_fpw, fails);
    printf(fails ? "OSC4_FLOAT_G1_FAIL\n" : "OSC4_FLOAT_G1_PASS\n");
    return fails ? 1 : 0;
}
