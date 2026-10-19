/* gpu_w4_api_test.c: the MXFP4 GEMV (omega_gpu_w4_api.h, qwen38/w4-gemv).
 *
 *   ./gpu_w4_api_test --host-only   e2m1 bits for all 16 codes, rounding, converter, codegen budgets for every shape
 *   ./gpu_w4_api_test --sim         the kernel through tests/bw_warp_sim.h: one-hot codes, parity, mutants 1..6 red
 *   ./gpu_w4_api_test --chip [--reps N] [--out f.json] [--fp8]
 *                                   chip parity at every Qwen3.8-27B shape, one-hot codes (subnormals 1 and 9),
 *                                   mutants red, then timing: weight GB/s against 273; --fp8 also times the FP8 kernel
 *                                   on the same shapes in the same process (same method, same session)
 *   --shape LB ROWS WARPS, --sched S, --coal C, --ctas N: kernel knobs for sweeps
 */
#include "omega_gpu_w4_api.h"
#include "omega_gpu_fp8_api.h"
#include "omega_blackwell_codegen_w4.h"
#include "omega_blackwell_codegen_moe.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOL 1e-5 /* relative to the row's |w| . |x| bound */

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

typedef struct { uint32_t n, k; uint8_t *w, *e; } T4;
/* random nibbles; scale bytes step by 3 between adjacent groups (so a wrong-byte or stuck-scale read changes y) */
static void t4_make(T4 *t, uint32_t n, uint32_t k, uint32_t seed) {
    t->n = n; t->k = k;
    t->w = malloc((size_t)n * k / 2u); t->e = malloc((size_t)n * k / 32u);
    for (size_t i = 0; i < (size_t)n * k / 2u; i++) t->w[i] = (uint8_t)(lcg(&seed) >> 24);
    for (size_t i = 0; i < (size_t)n * k / 32u; i++) t->e[i] = (uint8_t)(112u + (i * 3u + (lcg(&seed) >> 30)) % 24u);
}
static void t4_free(T4 *t) { free(t->w); free(t->e); }

/* worst |y - oracle| / (sum |q| |x| s) over rows; bad = rows past TOL */
static double parity(const T4 *t, const float *x, const float *y, size_t *bad) {
    double *ref = malloc(sizeof(double) * t->n), worst = 0.0;
    omega_w4_gemv_oracle(t->w, t->e, t->n, t->k, x, ref);
    *bad = 0;
    for (uint32_t r = 0; r < t->n; r++) {
        double bound = 0.0;
        const uint8_t *row = t->w + (size_t)r * (t->k / 2u);
        for (uint32_t c = 0; c < t->k; c++)
            bound += fabs((double)omega_w4_e2m1_ref(OMEGA_W4_NIBBLE(row, c)) * x[c]) * ldexp(1.0, (int)t->e[(size_t)r * (t->k / 32u) + c / 32u] - 127);
        const double rel = fabs((double)y[r] - ref[r]) / (bound > 0 ? bound : 1.0);
        if (!(rel <= TOL)) (*bad)++;
        if (rel > worst || rel != rel) worst = rel != rel ? INFINITY : rel;
    }
    free(ref);
    return worst;
}

static OmegaW4GemvSpec base_spec(uint32_t k, uint32_t lb, uint32_t rows, uint32_t warps, uint32_t sched, uint32_t coal) {
    OmegaW4GemvSpec s; memset(&s, 0, sizeof s);
    s.k = k; s.lane_bytes = lb; s.rows = rows; s.warps = warps; s.sched = sched; s.coal = coal && warps * rows <= 32u;
    return s;
}

static void host_only(void) {
    /* 1. e2m1 kernel bits == value * 2^-126, all 16 codes (normals, the subnormal 0.5, both zeros) */
    for (unsigned c = 0; c < 16; c++) {
        uint32_t b = omega_w4_e2m1_kernel_bits((uint8_t)c); float f; memcpy(&f, &b, 4);
        const double want = (double)omega_w4_e2m1_ref((uint8_t)c) * ldexp(1.0, -126);
        CHECK((double)f == want && (signbit(f) != 0) == ((c & 8u) != 0), "e2m1 code %u: kernel bits %08x = %g, want %g", c, b, (double)f, want);
    }
    /* 2. S' bits (E + 62) << 23 == 2^(E - 65) for every allowed E; product scale 2^-62 * 2^(E-65) = 2^(E-127) */
    for (unsigned E = 0; E <= OMEGA_W4_E_MAX; E++) {
        uint32_t b = (E + OMEGA_W4_SCALE_BIAS) << 23; float f; memcpy(&f, &b, 4);
        CHECK((double)f == ldexp(1.0, (int)E - 65), "S' for E %u", E);
    }
    /* 3. rounding: nearest, ties to the even code, saturating */
    static const struct { double a; uint8_t c; } rn[] = { { 0.0, 0 }, { 0.24, 0 }, { 0.25, 0 }, { 0.26, 1 }, { 0.75, 2 }, { 1.25, 2 },
        { 1.75, 4 }, { 2.5, 4 }, { 2.51, 5 }, { 3.5, 6 }, { 5.0, 6 }, { 5.01, 7 }, { 6.0, 7 }, { 7.9, 7 } };
    for (size_t i = 0; i < sizeof rn / sizeof rn[0]; i++) CHECK(omega_w4_e2m1_rne(rn[i].a) == rn[i].c, "e2m1 rne(%g) = %u, want %u", rn[i].a, omega_w4_e2m1_rne(rn[i].a), rn[i].c);
    /* 4. converter: a block built from exact MXFP4 values round-trips exactly; the group exponent is floor(log2 amax) - 2 */
    {
        const uint32_t n = 128, k = 256;
        uint8_t *w8 = calloc((size_t)n * k, 1), *w4 = calloc((size_t)n * k / 2u, 1), *e8 = calloc((size_t)n * k / 32u, 1);
        uint16_t sc[2] = { 0x3F80u, 0x3F80u }; /* scale 1.0 */
        uint16_t *s = calloc((n / 128u) * (k / 128u), 2); for (uint32_t i = 0; i < (n / 128u) * (k / 128u); i++) s[i] = sc[0];
        uint32_t seed = 7;
        /* e4m3 codes of 6, 4, 3, 2, 1.5, 1, 0.5, 0 times 2^-2 (codes with exponent >= 1: exact) */
        static const uint8_t codes[] = { 0x3C, 0x38, 0x34, 0x30, 0x2C, 0x28, 0x20, 0x00 };
        for (uint32_t r = 0; r < n; r++)
            for (uint32_t g4 = 0; g4 < k / 32u; g4++) {
                for (uint32_t c = 0; c < 32; c++) w8[(size_t)r * k + g4 * 32 + c] = codes[(lcg(&seed) >> 29)] | ((lcg(&seed) >> 31) ? 0x80u : 0);
                w8[(size_t)r * k + g4 * 32] = 0x3C; /* amax 1.5 = 6 * 2^-2 in every group */
            }
        OmegaW4Stats st;
        int rc = omega_gpu_w4_from_fp8(n, k, w8, s, 0, n, w4, e8, &st);
        CHECK(rc == 0, "converter rc %d", rc);
        size_t diff = 0;
        for (uint32_t r = 0; r < n && rc == 0; r++)
            for (uint32_t c = 0; c < k; c++) {
                const double v = (double)omega_fp8_e4m3_ref(w8[(size_t)r * k + c]);
                const double back = (double)omega_w4_e2m1_ref(OMEGA_W4_NIBBLE(w4 + (size_t)r * (k / 2u), c)) * ldexp(1.0, (int)e8[(size_t)r * (k / 32u) + c / 32u] - 127);
                if (back != v) diff++;
            }
        CHECK(diff == 0 && st.sq_err == 0.0, "converter: exact MXFP4 block round-trips (%zu differ, sq_err %g)", diff, st.sq_err);
        CHECK(e8[0] == 125u, "group exponent floor(log2 1.5) - 2 = -2 -> E 125 (got %u)", e8[0]);
        /* nibble order: weight 0 low nibble of byte 0, weight 1 high nibble */
        w8[0] = 0x3C; w8[1] = 0x20 | 0x80; /* 1.5 (q 6, nibble 7) then -0.125 (q 0.5, nibble 9) */
        omega_gpu_w4_from_fp8(n, k, w8, s, 0, 1, w4, e8, NULL);
        CHECK(w4[0] == 0x97u, "nibble order: byte 0 = %02x, want 97 (weight 0 low nibble)", w4[0]);
        CHECK(omega_gpu_w4_from_fp8(n, 100, w8, s, 0, 1, w4, e8, NULL) == OMEGA_GPU_W4_BAD_ARGS, "k not a multiple of 128 refused");
        free(w8); free(w4); free(e8); free(s);
    }
    /* 5. codegen: every production shape builds within the instruction and register budgets */
    static const uint32_t ks[] = { 5120, 6144, 17408 };
    for (size_t i = 0; i < 3; i++)
        for (uint32_t lb = 8; lb <= 16; lb += 8)
            for (uint32_t rows = 1; rows <= 8; rows *= 2)
                for (uint32_t sched = 0; sched <= 1; sched++)
                    for (uint32_t coal = 0; coal <= 1; coal++) {
                        OmegaW4GemvSpec s = base_spec(ks[i], lb, rows, 16, sched, coal);
                        uint32_t insns = 0, gprs = 0;
                        const int rc = omega_w4_gemv_prog_stats(&s, &insns, &gprs);
                        const int fits = rc == 0 && 32u * 16u * omega_bw_gpr_declared(gprs) <= 65536u;
                        if (rc == 0) printf("  codegen k %u lb %u rows %u sched %u coal %u: %u insns, %u GPRs%s\n", ks[i], lb, rows, sched, s.coal, insns, gprs, fits ? "" : " (too many registers at 16 warps)");
                        if (lb * rows <= 32u) CHECK(rc == 0, "codegen k %u lb %u rows %u sched %u coal %u builds", ks[i], lb, rows, sched, coal);
                    }
    /* 6. refusals */
    OmegaW4GemvSpec s = base_spec(5000, 16, 1, 1, 0, 0); CHECK(omega_w4_gemv_steps(&s) == 0, "k 5000 refused");
    s = base_spec(5120, 12, 1, 1, 0, 0); CHECK(omega_w4_gemv_steps(&s) == 0, "lane bytes 12 refused");
    s = base_spec(5120, 16, 1, 1, 0, 0); s.mutant = 6; CHECK(omega_w4_gemv_steps(&s) == 0, "mutant 6 needs coal");
}

/* ---- simulator ---- */
static int sim_gemv(const OmegaW4GemvSpec *spec, const T4 *t, const float *x, float *y, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_w4_gemv_prog(spec, &prog) != 0) { snprintf(msg, ml, "codegen refused"); return -1; }
    const uint32_t n = t->n, k = t->k, RG = n / spec->rows;
    float *xf = aligned_alloc(16, ((size_t)k * 4 + 15) & ~(size_t)15);
    omega_fp8_pack_x_f32(x, k, xf);
    uint64_t *tab = aligned_alloc(16, 32);
    tab[0] = (uintptr_t)t->w; tab[1] = (uintptr_t)t->e;
    float *yd = aligned_alloc(16, ((size_t)n * 4 + 15) & ~(size_t)15);
    uint32_t grid_x = (RG + spec->warps - 1) / spec->warps; if (grid_x > 3) grid_x = 3; /* several row groups per warp */
    int err = 0;
    float *copy = malloc((size_t)n * 4);
    const int runs = spec->coal ? 4 : 2;
    for (int run = 0; run < runs && !err; run++) {
        WsSim s; memset(&s, 0, sizeof s);
        s.p = &prog; s.threads = 32 * spec->warps; s.order = run == 1; s.warp_sched = run == 2 ? 1u : run == 3 ? 0x9e3779b9u : 0u;
        static uint8_t smem[8192]; s.smem = smem; s.smem_bytes = omega_w4_gemv_shared_bytes(spec);
        const uint64_t ta = (uintptr_t)tab, xa = (uintptr_t)xf, ya = (uintptr_t)yd;
        s.cbank[OMEGA_FP8_ARG_TAB / 4] = (uint32_t)ta; s.cbank[OMEGA_FP8_ARG_TAB / 4 + 1] = (uint32_t)(ta >> 32);
        s.cbank[OMEGA_FP8_ARG_X / 4] = (uint32_t)xa; s.cbank[OMEGA_FP8_ARG_X / 4 + 1] = (uint32_t)(xa >> 32);
        s.cbank[OMEGA_FP8_ARG_Y / 4] = (uint32_t)ya; s.cbank[OMEGA_FP8_ARG_Y / 4 + 1] = (uint32_t)(ya >> 32);
        s.cbank[OMEGA_FP8_ARG_RG / 4] = RG; s.cbank[OMEGA_FP8_ARG_XSTRIDE / 4] = 0;
        s.cbank[OMEGA_FP8_ARG_YSTRIDE / 4] = n * 4; s.cbank[OMEGA_FP8_ARG_STRIDE / 4] = grid_x * spec->warps;
        s.nranges = 5;
        s.ranges[0].lo = (uintptr_t)t->w; s.ranges[0].hi = (uintptr_t)t->w + (size_t)n * k / 2u;
        s.ranges[1].lo = (uintptr_t)t->e; s.ranges[1].hi = (uintptr_t)t->e + (size_t)n * k / 32u;
        s.ranges[2].lo = (uintptr_t)xf; s.ranges[2].hi = (uintptr_t)xf + (size_t)k * 4;
        s.ranges[3].lo = (uintptr_t)yd; s.ranges[3].hi = (uintptr_t)yd + (size_t)n * 4;
        s.ranges[4].lo = (uintptr_t)tab; s.ranges[4].hi = (uintptr_t)tab + 16;
        for (uint32_t i = 0; i < n; i++) yd[i] = -999.0f;
        err = ws_run(&s, grid_x, 1);
        if (err) { snprintf(msg, ml, "run %d: %s", run, s.msg); break; }
        size_t unwritten = 0; for (uint32_t i = 0; i < n; i++) if (yd[i] == -999.0f) unwritten++;
        if (unwritten) { err = -1; snprintf(msg, ml, "%zu outputs never written", unwritten); break; }
        if (run == 0) memcpy(copy, yd, (size_t)n * 4);
        else if (memcmp(copy, yd, (size_t)n * 4)) { err = -1; snprintf(msg, ml, "run %d changed the result", run); }
    }
    if (!err) memcpy(y, yd, (size_t)n * 4);
    free(xf); free(tab); free(yd); free(copy);
    return err ? -1 : 0;
}

static void sim_battery(void) {
    char msg[240];
    /* 1. one-hot codes: row r = code r % 16 at column (r * 37) % k, every other weight zero, E = 127 (scale 1) */
    {
        T4 t; t.n = 64; t.k = 1024; t.w = calloc(t.n * t.k / 2u, 1); t.e = malloc(t.n * t.k / 32u); memset(t.e, 127, t.n * t.k / 32u);
        float *x = malloc(t.k * 4), *y = malloc(t.n * 4);
        uint32_t seed = 3; for (uint32_t c = 0; c < t.k; c++) x[c] = frand(&seed, 0.5f, 2.0f);
        for (uint32_t r = 0; r < t.n; r++) OMEGA_W4_SET_NIBBLE(t.w + (size_t)r * (t.k / 2u), (r * 37u) % t.k, r % 16u);
        for (uint32_t lb = 8; lb <= 16; lb += 8) {
            OmegaW4GemvSpec s = base_spec(t.k, lb, 2, 2, 1, 1);
            int rc = sim_gemv(&s, &t, x, y, msg, sizeof msg);
            size_t bad = 0; for (uint32_t r = 0; r < t.n && rc == 0; r++) if (y[r] != (float)((double)omega_w4_e2m1_ref((uint8_t)(r % 16u)) * x[(r * 37u) % t.k])) { if (getenv("W4_DIAG") && bad < 4) printf("  row %u code %u got %g want %g\n", r, r % 16u, (double)y[r], (double)omega_w4_e2m1_ref((uint8_t)(r % 16u)) * x[(r * 37u) % t.k]); bad++; }
            CHECK(rc == 0 && bad == 0, "sim one-hot lb %u: %s, %zu rows wrong", lb, rc ? msg : "ran", bad);
        }
        free(x); free(y); t4_free(&t);
    }
    /* 2. parity over shapes and knobs */
    static const struct { uint32_t n, k, lb, rows, warps, sched, coal; } pts[] = {
        { 64, 1024, 16, 1, 1, 0, 0 }, { 64, 1024, 16, 2, 2, 1, 1 }, { 96, 2048, 16, 4, 2, 1, 1 }, { 64, 2048, 8, 8, 2, 1, 1 },
        { 128, 1024, 8, 2, 4, 0, 1 }, { 48, 3072, 16, 2, 8, 1, 0 }, { 64, 5120, 16, 2, 4, 1, 1 }, { 32, 17408, 16, 2, 2, 1, 1 },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        T4 t; t4_make(&t, pts[i].n, pts[i].k, 11u + (uint32_t)i);
        float *x = malloc(t.k * 4), *y = malloc(t.n * 4);
        uint32_t seed = 5u + (uint32_t)i; for (uint32_t c = 0; c < t.k; c++) x[c] = frand(&seed, -2.0f, 2.0f);
        OmegaW4GemvSpec s = base_spec(t.k, pts[i].lb, pts[i].rows, pts[i].warps, pts[i].sched, pts[i].coal);
        int rc = sim_gemv(&s, &t, x, y, msg, sizeof msg);
        size_t bad = 0; double worst = rc == 0 ? parity(&t, x, y, &bad) : INFINITY;
        CHECK(rc == 0 && bad == 0, "sim n %u k %u lb %u rows %u warps %u sched %u coal %u: %s worst %.2e bad %zu", t.n, t.k, s.lane_bytes, s.rows, s.warps, s.sched, s.coal, rc ? msg : "ok", worst, bad);
        if (i == 6) /* mutants on one shape: each must change y past the tolerance (or fail to run) */
            for (int m = 1; m <= 6; m++) {
                OmegaW4GemvSpec sm = s; sm.mutant = m;
                if (m == 3) continue; /* swapped group table needs two groups: the chip battery runs it */
                int mrc = sim_gemv(&sm, &t, x, y, msg, sizeof msg);
                size_t mb = 0; if (mrc == 0) parity(&t, x, y, &mb);
                CHECK(mrc != 0 || mb > 0, "sim mutant %d caught (rc %d, %zu bad rows)", m, mrc, mb);
            }
        free(x); free(y); t4_free(&t);
    }
}

/* ---- chip ---- */
typedef struct { uint32_t n, k; const char *name; } ChipPoint;
static const ChipPoint g_pts[] = {
    { 16384, 5120, "gdn mix 16384x5120" }, { 14336, 5120, "attn mix 14336x5120" }, { 5120, 6144, "o_proj 5120x6144" },
    { 34816, 5120, "gate_up 34816x5120" }, { 5120, 17408, "down 5120x17408" },
};

static double median_ns(uint64_t *v, uint32_t n) { qsort(v, n, sizeof *v, cmp_u64); return (double)v[n / 2]; }

static int chip_run(uint32_t reps, const char *out_path, int fp8) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_W4_GEMV\",\"points\":[");
    int first = 1;
    /* one-hot codes on the chip, incl. the subnormal codes 1 and 9 (no .FTZ) */
    {
        T4 t; t.n = 64; t.k = 1024; t.w = calloc(t.n * t.k / 2u, 1); t.e = malloc(t.n * t.k / 32u); memset(t.e, 127, t.n * t.k / 32u);
        float *x = malloc(t.k * 4), *y = malloc(t.n * 4);
        uint32_t seed = 3; for (uint32_t c = 0; c < t.k; c++) x[c] = frand(&seed, 0.5f, 2.0f);
        for (uint32_t r = 0; r < t.n; r++) OMEGA_W4_SET_NIBBLE(t.w + (size_t)r * (t.k / 2u), (r * 37u) % t.k, r % 16u);
        OmegaGpuW4Tensor *dt = NULL;
        int rc = omega_gpu_w4_tensor_upload(t.n, t.k, t.w, t.e, &dt);
        if (rc == 0) rc = omega_gpu_w4_gemv_f32(dt, x, y, NULL);
        size_t bad = 0; for (uint32_t r = 0; r < t.n && rc == 0; r++) if (y[r] != (float)((double)omega_w4_e2m1_ref((uint8_t)(r % 16u)) * x[(r * 37u) % t.k])) bad++;
        CHECK(rc == 0 && bad == 0, "chip one-hot all 16 codes (subnormals 1, 9): rc %d (%s), %zu rows wrong", rc, rc ? omega_gpu_w4_last_error() : "", bad);
        uint8_t big[64]; memset(big, 200, sizeof big); OmegaGpuW4Tensor *t2 = NULL;
        CHECK(omega_gpu_w4_tensor_upload(1, 2048, t.w, big, &t2) == OMEGA_GPU_W4_BAD_ARGS && !t2, "upload refuses a scale byte above 192");
        omega_gpu_w4_tensor_free(dt); free(x); free(y); t4_free(&t);
    }
    for (size_t i = 0; i < sizeof g_pts / sizeof g_pts[0]; i++) {
        const ChipPoint *p = &g_pts[i];
        T4 t; t4_make(&t, p->n, p->k, 101u + (uint32_t)i);
        float *x = malloc(p->k * 4), *y = malloc(p->n * 4);
        uint32_t seed = 9u + (uint32_t)i; for (uint32_t c = 0; c < p->k; c++) x[c] = frand(&seed, -2.0f, 2.0f);
        OmegaGpuW4Tensor *dt = NULL;
        int rc = omega_gpu_w4_tensor_upload(p->n, p->k, t.w, t.e, &dt);
        OmegaGpuW4Info info;
        if (rc == 0) rc = omega_gpu_w4_gemv_f32(dt, x, y, &info);
        size_t bad = 0; double worst = rc == 0 ? parity(&t, x, y, &bad) : INFINITY;
        CHECK(rc == 0 && bad == 0, "chip %s: rc %d (%s) worst %.2e bad %zu", p->name, rc, rc ? omega_gpu_w4_last_error() : "", worst, bad);
        if (i == 0) /* mutants on the chip: each must be red */
            for (int m = 1; m <= 6; m++) {
                if (m == 3 || m == 6) continue; /* 3: one group per launch here; 6 is a slot race the chip need not show: the simulator's warp orders catch both */
                omega_gpu_w4_test_set_mutant(m);
                int mrc = omega_gpu_w4_gemv_f32(dt, x, y, NULL);
                size_t mb = 0; if (mrc == 0) parity(&t, x, y, &mb);
                CHECK(mrc != 0 || mb > 0, "chip mutant %d caught (rc %d, %zu bad rows)", m, mrc, mb);
                omega_gpu_w4_test_set_mutant(0);
            }
        /* timing */
        uint64_t *ns = calloc(reps, sizeof *ns);
        for (uint32_t r = 0; r < reps && rc == 0; r++) { OmegaGpuW4Info in; rc = omega_gpu_w4_gemv_f32(dt, x, y, &in); ns[r] = in.elapsed_ns; }
        const double med = rc == 0 ? median_ns(ns, reps) : 0, bytes = (double)p->n * p->k / 2.0 + (double)p->n * p->k / 32.0;
        printf("  w4 %-22s lb %u rows %u warps %u coal %u sched %u grid %u: median %.1f us, %.1f GB/s (%.3f of 273)\n", p->name, info.lane_bytes,
               info.rows_per_warp, info.warps_per_cta, info.coal, info.sched, info.grid_x, med / 1e3, bytes / med, bytes / med / 273.0);
        double f8med = 0, f8bytes = (double)p->n * p->k;
        if (fp8 && rc == 0) { /* the FP8 kernel on the same shape, same session and method (random e4m3, scale 2^-8) */
            uint8_t *w8 = malloc((size_t)p->n * p->k); for (size_t q = 0; q < (size_t)p->n * p->k; q++) { uint8_t b = (uint8_t)(lcg(&seed) >> 24); if ((b & 0x7F) == 0x7F) b--; w8[q] = b; }
            uint16_t *s8 = malloc((size_t)(p->n / 128) * (p->k / 128) * 2); for (size_t q = 0; q < (size_t)(p->n / 128) * (p->k / 128); q++) s8[q] = 0x3B80u;
            OmegaGpuFp8Tensor *ft = NULL; int frc = omega_gpu_fp8_tensor_upload(1, p->n, p->k, w8, s8, &ft);
            for (uint32_t r = 0; r < reps && frc == 0; r++) { OmegaGpuFp8Info fi; frc = omega_gpu_fp8_gemv_f32(ft, 0, x, y, &fi); ns[r] = fi.elapsed_ns; }
            if (frc == 0) { f8med = median_ns(ns, reps); printf("  fp8 %-21s median %.1f us, %.1f GB/s (%.3f of 273)\n", p->name, f8med / 1e3, f8bytes / f8med, f8bytes / f8med / 273.0); }
            else printf("  fp8 %-21s rc %d (%s)\n", p->name, frc, omega_gpu_fp8_last_error());
            omega_gpu_fp8_tensor_free(ft); free(w8); free(s8);
        }
        if (out) fprintf(out, "%s{\"name\":\"%s\",\"n\":%u,\"k\":%u,\"lb\":%u,\"rows\":%u,\"warps\":%u,\"coal\":%u,\"sched\":%u,\"grid\":%u,\"bad\":%zu,\"worst_rel\":%.3e,\"w4_median_ns\":%.0f,\"w4_bytes\":%.0f,\"fp8_median_ns\":%.0f,\"fp8_bytes\":%.0f}",
                         first ? "" : ",", p->name, p->n, p->k, info.lane_bytes, info.rows_per_warp, info.warps_per_cta, info.coal, info.sched, info.grid_x, bad, worst, med, bytes, f8med, f8bytes);
        first = 0;
        free(ns); omega_gpu_w4_tensor_free(dt); free(x); free(y); t4_free(&t);
    }
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d}\n", g_checks, g_failed); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, chip = 0, fp8 = 0; uint32_t reps = 20; const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--sim")) sim = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--fp8")) fp8 = 1;
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--shape") && i + 3 < argc) { uint32_t a = (uint32_t)atoi(argv[i + 1]), b = (uint32_t)atoi(argv[i + 2]), c = (uint32_t)atoi(argv[i + 3]); omega_gpu_w4_set_shape(a, b, c); i += 3; }
        else if (!strcmp(argv[i], "--sched") && i + 1 < argc) omega_gpu_w4_set_sched(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--coal") && i + 1 < argc) omega_gpu_w4_set_coal(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--ctas") && i + 1 < argc) omega_gpu_w4_set_cta_budget((uint32_t)atoi(argv[++i]));
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (!host && !sim && !chip) host = 1;
    if (reps < 1) reps = 1;
    if (host) host_only();
    if (sim) sim_battery();
    if (chip) chip_run(reps, out, fp8);
    printf("GPU_W4_API_TEST %s%s%s: %d checks, %d failed -> %s\n", host ? "host " : "", sim ? "sim " : "", chip ? "chip" : "", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
