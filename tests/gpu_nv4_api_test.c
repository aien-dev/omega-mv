/* gpu_nv4_api_test.c: the grouped NVFP4 GEMV (omega_gpu_nv4_api.h, flashnext/port).
 *
 *   ./gpu_nv4_api_test --host-only   e4m3 kernel bits for every code, e2m1 bits, codegen budgets at the Flash-Next shapes
 *   ./gpu_nv4_api_test --sim         the kernel through tests/bw_warp_sim.h: one-hot codes (incl. the tail lanes), parity over
 *                                    shapes, groups and knobs, mutants 1..7 red
 *   ./gpu_nv4_api_test --chip [--reps N] [--out f.json] [--fixture DIR]
 *                                    chip parity at the Flash-Next expert shapes (11 groups), one-hot codes, mutants red,
 *                                    upload refusals, timing (GB/s of the bytes the launch streams, against 273);
 *                                    --fixture: real NVFP4 expert tensors and the host reference's outputs
 *                                    (sc flashnext_check nv4-fixture) must agree
 *   --shape ROWS WARPS, --sched S, --ctas N: knobs for sweeps
 */
#include "omega_gpu_nv4_api.h"
#include "omega_blackwell_codegen_nv4.h"
#include "omega_blackwell_codegen_w4.h"
#include "omega_blackwell_codegen_moe.h"
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOL 1e-5 /* relative to the row's sum |w| |x| bound */

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

typedef struct { uint32_t n, k; uint8_t *w, *s; float *g; } T4;
/* random nibbles; e4m3 scales from a band of normal codes that steps between adjacent groups; per-row globals */
static void t4_make(T4 *t, uint32_t n, uint32_t k, uint32_t seed) {
    t->n = n; t->k = k;
    t->w = malloc((size_t)n * k / 2u); t->s = malloc((size_t)n * k / 16u); t->g = malloc((size_t)n * 4u);
    for (size_t i = 0; i < (size_t)n * k / 2u; i++) t->w[i] = (uint8_t)(lcg(&seed) >> 24);
    for (size_t i = 0; i < (size_t)n * k / 16u; i++) t->s[i] = (uint8_t)(0x28u + (i * 5u + (lcg(&seed) >> 29)) % 40u); /* 2^-2 .. ~2^3 */
    for (uint32_t r = 0; r < n; r++) t->g[r] = ldexpf(1.0f + (float)(r % 7u) / 8.0f, -9 + (int)(r % 3u));
}
static void t4_free(T4 *t) { free(t->w); free(t->s); free(t->g); }
/* the block a slab entry holds, host side: nibbles, scales, row scales * 2^62 */
static uint8_t *t4_block(const T4 *t) {
    const size_t nb = (size_t)t->n * t->k / 2u, sb = (size_t)t->n * t->k / 16u;
    uint8_t *b = aligned_alloc(256, (omega_nv4_block_bytes(t->n, t->k) + 255u) & ~(size_t)255u);
    memcpy(b, t->w, nb); memcpy(b + nb, t->s, sb);
    float *rs = (float *)(b + nb + sb);
    for (uint32_t r = 0; r < t->n; r++) rs[r] = ldexpf(t->g[r], OMEGA_NV4_ROWSCALE_FOLD);
    return b;
}

static double parity(const T4 *t, const float *x, const float *y, size_t *bad) {
    double *ref = malloc(sizeof(double) * t->n), worst = 0.0;
    omega_nv4_gemv_oracle(t->w, t->s, t->g, t->n, t->k, x, ref);
    *bad = 0;
    for (uint32_t r = 0; r < t->n; r++) {
        double bound = 0.0;
        const uint8_t *row = t->w + (size_t)r * (t->k / 2u);
        for (uint32_t c = 0; c < t->k; c++)
            bound += fabs((double)omega_w4_e2m1_ref(OMEGA_W4_NIBBLE(row, c)) * x[c] * omega_nv4_e4m3_ref(t->s[(size_t)r * (t->k / 16u) + c / 16u])) * t->g[r];
        const double rel = fabs((double)y[r] - ref[r]) / (bound > 0 ? bound : 1.0);
        if (!(rel <= TOL)) (*bad)++;
        if (rel > worst || rel != rel) worst = rel != rel ? INFINITY : rel;
    }
    free(ref);
    return worst;
}

static OmegaNv4GemvSpec base_spec(uint32_t k, uint32_t rows, uint32_t warps, uint32_t sched) {
    OmegaNv4GemvSpec s; memset(&s, 0, sizeof s);
    s.k = k; s.rows = rows; s.warps = warps; s.sched = sched;
    return s;
}

static void host_only(void) {
    /* 1. e4m3 kernel bits * 2^120 == the e4m3 value for every non-NaN code (normals and subnormals) */
    size_t bad = 0;
    for (unsigned b = 0; b < 256; b++) {
        if ((b & 0x7Fu) == 0x7Fu) continue;
        uint32_t bits = omega_nv4_e4m3_kernel_bits((uint8_t)b); float f; memcpy(&f, &bits, 4);
        if ((double)f * ldexp(1.0, 120) != omega_nv4_e4m3_ref((uint8_t)b) || (signbit(f) != 0) != ((b & 0x80u) != 0)) bad++;
    }
    CHECK(bad == 0, "e4m3 kernel bits: %zu of 254 codes differ", bad);
    CHECK(omega_nv4_e4m3_ref(0x38) == 1.0 && omega_nv4_e4m3_ref(0x01) == ldexp(1.0, -9) && omega_nv4_e4m3_ref(0x7E) == 448.0, "e4m3 reference: 1.0, 2^-9, 448");
    /* 2. 2^120 constant and the e2m1 bits are the W4 kernel's */
    { uint32_t b = 0x7B800000u; float f; memcpy(&f, &b, 4); CHECK((double)f == ldexp(1.0, 120), "2^120 bits"); }
    /* 3. codegen at every Flash-Next shape and the knobs */
    static const uint32_t ks[] = { 2560, 640, 1024, 576, 64, 6144 };
    for (size_t i = 0; i < sizeof ks / sizeof ks[0]; i++)
        for (uint32_t rows = 1; rows <= 8; rows *= 2)
            for (uint32_t sched = 0; sched <= 1; sched++) {
                OmegaNv4GemvSpec s = base_spec(ks[i], rows, 8, sched);
                uint32_t insns = 0, gprs = 0;
                const int rc = omega_nv4_gemv_prog_stats(&s, &insns, &gprs);
                if (rc == 0 && (rows == 2 || i == 0)) printf("  codegen k %u rows %u sched %u: %u insns, %u GPRs\n", ks[i], rows, sched, insns, gprs);
                if (rows == 8u && ks[i] % OMEGA_NV4_STEP) { CHECK(rc != 0 && !omega_nv4_gemv_spec_ok(&s), "k %u rows 8 with a tail refused by the spec (no Flash-Next launch uses it)", ks[i]); continue; }
                CHECK(rc == 0 && 32u * 8u * omega_bw_gpr_declared(gprs) <= 65536u, "codegen k %u rows %u sched %u builds and fits 8 warps", ks[i], rows, sched);
            }
    OmegaNv4GemvSpec s = base_spec(2600, 2, 8, 1); CHECK(!omega_nv4_gemv_spec_ok(&s), "k 2600 refused");
    s = base_spec(2560, 3, 8, 1); CHECK(!omega_nv4_gemv_spec_ok(&s), "rows 3 refused");
    s = base_spec(2560, 2, 8, 1); s.mutant = 7; CHECK(!omega_nv4_gemv_spec_ok(&s), "mutant 7 needs a tail");
    CHECK(omega_nv4_block_bytes(1280, 2560) == 1280u * 1280u + 1280u * 160u + 1280u * 4u, "block bytes gate_up");
}

/* ---- simulator ---- */
static int sim_gemv(const OmegaNv4GemvSpec *spec, const T4 *t, uint32_t groups, const uint32_t *pick, uint32_t ntens,
                    const float *x, uint32_t x_rows, float *y, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_nv4_gemv_prog(spec, &prog) != 0) { snprintf(msg, ml, "codegen refused"); return -1; }
    const uint32_t n = t[0].n, k = t[0].k, RG = n / spec->rows;
    uint8_t *blk[8];
    for (uint32_t i = 0; i < ntens; i++) blk[i] = t4_block(&t[i]);
    float *xf = aligned_alloc(16, ((size_t)x_rows * k * 4 + 15) & ~(size_t)15);
    for (uint32_t r = 0; r < x_rows; r++) omega_fp8_pack_x_f32(x + (size_t)r * k, k, xf + (size_t)r * k);
    uint64_t *tab = aligned_alloc(16, 16u * groups);
    for (uint32_t gi = 0; gi < groups; gi++) { tab[2 * gi] = (uintptr_t)blk[pick[gi]]; tab[2 * gi + 1] = (uintptr_t)blk[pick[gi]] + (size_t)n * k / 2u; }
    float *yd = aligned_alloc(16, ((size_t)groups * n * 4 + 15) & ~(size_t)15);
    uint32_t grid_x = (RG + spec->warps - 1) / spec->warps; if (grid_x > 3) grid_x = 3;
    int err = 0;
    float *copy = malloc((size_t)groups * n * 4);
    for (int run = 0; run < 2 && !err; run++) {
        WsSim s; memset(&s, 0, sizeof s);
        s.p = &prog; s.threads = 32 * spec->warps; s.order = run == 1;
        static uint8_t smem[64]; s.smem = smem; s.smem_bytes = 0;
        const uint64_t ta = (uintptr_t)tab, xa = (uintptr_t)xf, ya = (uintptr_t)yd;
        s.cbank[OMEGA_FP8_ARG_TAB / 4] = (uint32_t)ta; s.cbank[OMEGA_FP8_ARG_TAB / 4 + 1] = (uint32_t)(ta >> 32);
        s.cbank[OMEGA_FP8_ARG_X / 4] = (uint32_t)xa; s.cbank[OMEGA_FP8_ARG_X / 4 + 1] = (uint32_t)(xa >> 32);
        s.cbank[OMEGA_FP8_ARG_Y / 4] = (uint32_t)ya; s.cbank[OMEGA_FP8_ARG_Y / 4 + 1] = (uint32_t)(ya >> 32);
        s.cbank[OMEGA_FP8_ARG_RG / 4] = RG; s.cbank[OMEGA_FP8_ARG_XSTRIDE / 4] = x_rows == 1 ? 0 : k * 4;
        s.cbank[OMEGA_FP8_ARG_YSTRIDE / 4] = n * 4; s.cbank[OMEGA_FP8_ARG_STRIDE / 4] = grid_x * spec->warps;
        s.nranges = 0;
        for (uint32_t i = 0; i < ntens; i++) { s.ranges[s.nranges].lo = (uintptr_t)blk[i]; s.ranges[s.nranges].hi = (uintptr_t)blk[i] + omega_nv4_block_bytes(n, k); s.nranges++; }
        s.ranges[s.nranges].lo = (uintptr_t)xf; s.ranges[s.nranges].hi = (uintptr_t)xf + (size_t)x_rows * k * 4; s.nranges++;
        s.ranges[s.nranges].lo = (uintptr_t)yd; s.ranges[s.nranges].hi = (uintptr_t)yd + (size_t)groups * n * 4; s.nranges++;
        s.ranges[s.nranges].lo = (uintptr_t)tab; s.ranges[s.nranges].hi = (uintptr_t)tab + 16u * groups; s.nranges++;
        for (size_t i = 0; i < (size_t)groups * n; i++) yd[i] = -999.0f;
        err = ws_run(&s, grid_x, groups);
        if (err) { snprintf(msg, ml, "run %d: %s", run, s.msg); break; }
        size_t unwritten = 0; for (size_t i = 0; i < (size_t)groups * n; i++) if (yd[i] == -999.0f) unwritten++;
        if (unwritten) { err = -1; snprintf(msg, ml, "%zu outputs never written", unwritten); break; }
        if (run == 0) memcpy(copy, yd, (size_t)groups * n * 4);
        else if (memcmp(copy, yd, (size_t)groups * n * 4)) { err = -1; snprintf(msg, ml, "run %d changed the result", run); }
    }
    if (!err) memcpy(y, yd, (size_t)groups * n * 4);
    for (uint32_t i = 0; i < ntens; i++) free(blk[i]);
    free(xf); free(tab); free(yd); free(copy);
    return err ? -1 : 0;
}

/* worst parity over groups: group g used tensor pick[g] and x row (x_rows == 1 ? 0 : g) */
static size_t parity_groups(const T4 *t, uint32_t groups, const uint32_t *pick, const float *x, uint32_t x_rows, const float *y, double *worst) {
    size_t bad = 0; *worst = 0;
    for (uint32_t gi = 0; gi < groups; gi++) {
        size_t b = 0; double w = parity(&t[pick[gi]], x + (size_t)(x_rows == 1 ? 0 : gi) * t[0].k, y + (size_t)gi * t[0].n, &b);
        bad += b; if (w > *worst) *worst = w;
    }
    return bad;
}

static void sim_battery(void) {
    char msg[240];
    int caught[8] = { 0 };
    /* 1. one-hot codes: row r = code r % 16 at column (r * 37) % k incl. the tail columns, scale 1, global 1 */
    for (uint32_t k = 640; k <= 2560; k += 1920) {
        T4 t; t.n = 64; t.k = k; t.w = calloc(t.n * t.k / 2u, 1); t.s = malloc(t.n * t.k / 16u); memset(t.s, 0x38, t.n * t.k / 16u);
        t.g = malloc(t.n * 4u); for (uint32_t r = 0; r < t.n; r++) t.g[r] = 1.0f;
        float *x = malloc(t.k * 4), *y = malloc(t.n * 4);
        uint32_t seed = 3; for (uint32_t c = 0; c < t.k; c++) x[c] = frand(&seed, 0.5f, 2.0f);
        for (uint32_t r = 0; r < t.n; r++) OMEGA_W4_SET_NIBBLE(t.w + (size_t)r * (t.k / 2u), (r * 37u + (r % 4u) * 500u) % t.k, r % 16u);
        OmegaNv4GemvSpec s = base_spec(t.k, 2, 2, 1);
        uint32_t pick = 0;
        int rc = sim_gemv(&s, &t, 1, &pick, 1, x, 1, y, msg, sizeof msg);
        size_t bad = 0;
        for (uint32_t r = 0; r < t.n && rc == 0; r++) if (y[r] != (float)((double)omega_w4_e2m1_ref((uint8_t)(r % 16u)) * x[(r * 37u + (r % 4u) * 500u) % t.k])) bad++;
        CHECK(rc == 0 && bad == 0, "sim one-hot k %u: %s, %zu rows wrong", k, rc ? msg : "ran", bad);
        free(x); free(y); t4_free(&t);
    }
    /* 2. parity over shapes, groups and knobs */
    static const struct { uint32_t n, k, rows, warps, sched, groups, xrows; } pts[] = {
        { 64, 2560, 2, 2, 1, 3, 1 }, { 64, 640, 2, 2, 1, 3, 3 }, { 32, 1024, 4, 2, 0, 1, 1 }, { 48, 576, 2, 4, 1, 2, 2 },
        { 16, 64, 1, 1, 0, 1, 1 }, { 64, 2560, 8, 2, 1, 1, 1 }, { 64, 640, 4, 2, 1, 2, 2 }, { 32, 6144, 2, 8, 1, 1, 1 },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        T4 t[3];
        for (uint32_t q = 0; q < 3; q++) t4_make(&t[q], pts[i].n, pts[i].k, 11u + (uint32_t)i * 7u + q);
        const uint32_t G = pts[i].groups, XR = pts[i].xrows;
        uint32_t pick[3] = { 2, 0, 1 };
        float *x = malloc((size_t)XR * pts[i].k * 4), *y = malloc((size_t)G * pts[i].n * 4);
        uint32_t seed = 5u + (uint32_t)i; for (size_t c = 0; c < (size_t)XR * pts[i].k; c++) x[c] = frand(&seed, -2.0f, 2.0f);
        OmegaNv4GemvSpec s = base_spec(pts[i].k, pts[i].rows, pts[i].warps, pts[i].sched);
        int rc = sim_gemv(&s, t, G, pick, 3, x, XR, y, msg, sizeof msg);
        double worst = INFINITY; size_t bad = rc == 0 ? parity_groups(t, G, pick, x, XR, y, &worst) : 1;
        CHECK(rc == 0 && bad == 0, "sim n %u k %u rows %u warps %u sched %u groups %u xrows %u: %s worst %.2e bad %zu", pts[i].n, pts[i].k, pts[i].rows, pts[i].warps,
              pts[i].sched, G, XR, rc ? msg : "ok", worst, bad);
        if (i == 0 || i == 1) /* mutants: each must change y past the tolerance (or fail to run) */
            for (int m = 1; m <= 7; m++) {
                OmegaNv4GemvSpec sm = s; sm.mutant = m;
                if (!omega_nv4_gemv_spec_ok(&sm)) { printf("  sim k %u mutant %d: refused by the spec (equivalent to production at this k)\n", pts[i].k, m); continue; }
                int mrc = sim_gemv(&sm, t, G, pick, 3, x, XR, y, msg, sizeof msg);
                double w2; size_t mb = mrc == 0 ? parity_groups(t, G, pick, x, XR, y, &w2) : 1;
                CHECK(mrc != 0 || mb > 0, "sim k %u mutant %d caught (rc %d, %zu bad rows)", pts[i].k, m, mrc, mb);
                if (mrc != 0 || mb > 0) caught[m] = 1;
            }
        free(x); free(y); for (uint32_t q = 0; q < 3; q++) t4_free(&t[q]);
    }
    for (int m = 1; m <= 7; m++) CHECK(caught[m], "sim mutant %d caught on at least one shape", m);
}

/* ---- chip ---- */
static double median_ns(uint64_t *v, uint32_t n) { qsort(v, n, sizeof *v, cmp_u64); return (double)v[n / 2]; }

/* reads a whole file; NULL when missing */
static void *slurp(const char *dir, const char *name, size_t *len) {
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n > 0 ? (size_t)n : 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f); if (len) *len = (size_t)n; return b;
}

/* Real-tensor parity: DIR (sc flashnext_check nv4-fixture) holds per case c: c.gu.nib/.sc/.g (fused gate|up [1280][2560]),
 * c.dn.nib/.sc/.g ([2560][640]), x.f32 [G][2560], act.f32 [G][640], gu_ref.f32 [G][1280], dn_ref.f32 [G][2560], and
 * cases.txt (one expert id per line, G lines). The host reference computed gu_ref = gate|up . x and dn_ref = down . act in f32. */
static void fixture_run(const char *dir, FILE *out) {
    size_t len = 0; char *cases = slurp(dir, "cases.txt", &len);
    CHECK(cases != NULL, "fixture %s/cases.txt readable", dir);
    if (!cases) return;
    uint32_t ids[64], G = 0;
    for (char *p = strtok(cases, "\n"); p && G < 64; p = strtok(NULL, "\n")) if (*p != '#') ids[G++] = (uint32_t)atoi(p);
    float *x = slurp(dir, "x.f32", NULL), *act = slurp(dir, "act.f32", NULL), *gu_ref = slurp(dir, "gu_ref.f32", NULL), *dn_ref = slurp(dir, "dn_ref.f32", NULL);
    CHECK(x && act && gu_ref && dn_ref, "fixture vectors readable");
    if (!x || !act || !gu_ref || !dn_ref) return;
    OmegaGpuNv4Slab *gu = NULL, *dn = NULL;
    int rc = omega_gpu_nv4_slab_alloc(1280, 2560, G, &gu);
    if (rc == 0) rc = omega_gpu_nv4_slab_alloc(2560, 640, G, &dn);
    for (uint32_t i = 0; i < G && rc == 0; i++) {
        char nm[64]; const char *kinds[2] = { "gu", "dn" };
        for (int q = 0; q < 2 && rc == 0; q++) {
            snprintf(nm, sizeof nm, "%u.%s.nib", ids[i], kinds[q]); uint8_t *nib = slurp(dir, nm, NULL);
            snprintf(nm, sizeof nm, "%u.%s.sc", ids[i], kinds[q]); uint8_t *sc = slurp(dir, nm, NULL);
            snprintf(nm, sizeof nm, "%u.%s.g", ids[i], kinds[q]); float *gl = slurp(dir, nm, NULL);
            if (!nib || !sc || !gl) rc = -100; else rc = omega_gpu_nv4_slab_set(q ? dn : gu, i, nib, sc, gl);
            free(nib); free(sc); free(gl);
        }
    }
    if (rc == 0) rc = omega_gpu_nv4_slab_commit(gu);
    if (rc == 0) rc = omega_gpu_nv4_slab_commit(dn);
    uint32_t slot[64]; for (uint32_t i = 0; i < G; i++) slot[i] = i;
    float *ygu = malloc((size_t)G * 1280 * 4), *ydn = malloc((size_t)G * 2560 * 4);
    OmegaGpuNv4Info igu, idn;
    if (rc == 0) rc = omega_gpu_nv4_gemv_grouped_f32(gu, slot, G, x, G, ygu, &igu);
    if (rc == 0) rc = omega_gpu_nv4_gemv_grouped_f32(dn, slot, G, act, G, ydn, &idn);
    CHECK(rc == 0, "fixture: real-tensor launches rc %d (%s)", rc, rc ? omega_gpu_nv4_last_error() : "");
    if (rc == 0) {
        double e2 = 0, r2 = 0, maxrel = 0; /* against the host reference's f32 outputs */
        for (size_t i = 0; i < (size_t)G * 1280; i++) { double d = ygu[i] - gu_ref[i]; e2 += d * d; r2 += (double)gu_ref[i] * gu_ref[i]; }
        const double gu_rel = sqrt(e2 / r2);
        e2 = r2 = 0;
        for (size_t i = 0; i < (size_t)G * 2560; i++) { double d = ydn[i] - dn_ref[i]; e2 += d * d; r2 += (double)dn_ref[i] * dn_ref[i]; double a = fabs(dn_ref[i]); if (a > 1e-3 && fabs(d) / a > maxrel) maxrel = fabs(d) / a; }
        const double dn_rel = sqrt(e2 / r2);
        printf("  fixture %u experts: gate_up rel RMS %.3e, down rel RMS %.3e vs the host reference (f32)\n", G, gu_rel, dn_rel);
        CHECK(gu_rel < 1e-5 && dn_rel < 1e-5, "fixture parity: gate_up %.3e down %.3e (bar 1e-5)", gu_rel, dn_rel);
        if (out) fprintf(out, ",\"fixture\":{\"experts\":%u,\"gate_up_rel_rms\":%.3e,\"down_rel_rms\":%.3e}", G, gu_rel, dn_rel);
    }
    free(ygu); free(ydn); free(x); free(act); free(gu_ref); free(dn_ref); free(cases);
    omega_gpu_nv4_slab_free(gu); omega_gpu_nv4_slab_free(dn);
}

static int chip_run(uint32_t reps, const char *out_path, const char *fixture) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_NV4_GEMV\",\"points\":[");
    int first = 1;
    /* one-hot on the chip, incl. the tail lanes of k 640 */
    {
        T4 t; t.n = 64; t.k = 640; t.w = calloc(t.n * t.k / 2u, 1); t.s = malloc(t.n * t.k / 16u); memset(t.s, 0x38, t.n * t.k / 16u);
        t.g = malloc(t.n * 4u); for (uint32_t r = 0; r < t.n; r++) t.g[r] = 1.0f;
        float *x = malloc(t.k * 4), *y = malloc(t.n * 4);
        uint32_t seed = 3; for (uint32_t c = 0; c < t.k; c++) x[c] = frand(&seed, 0.5f, 2.0f);
        for (uint32_t r = 0; r < t.n; r++) OMEGA_W4_SET_NIBBLE(t.w + (size_t)r * (t.k / 2u), (r * 37u + (r % 4u) * 500u) % t.k, r % 16u);
        OmegaGpuNv4Slab *sl = NULL; uint32_t id = 0;
        int rc = omega_gpu_nv4_slab_alloc(t.n, t.k, 1, &sl);
        if (rc == 0) rc = omega_gpu_nv4_slab_set(sl, 0, t.w, t.s, t.g);
        if (rc == 0) rc = omega_gpu_nv4_slab_commit(sl);
        if (rc == 0) rc = omega_gpu_nv4_gemv_grouped_f32(sl, &id, 1, x, 1, y, NULL);
        size_t bad = 0; for (uint32_t r = 0; r < t.n && rc == 0; r++) if (y[r] != (float)((double)omega_w4_e2m1_ref((uint8_t)(r % 16u)) * x[(r * 37u + (r % 4u) * 500u) % t.k])) bad++;
        CHECK(rc == 0 && bad == 0, "chip one-hot all 16 codes, tail lanes (k 640): rc %d (%s), %zu rows wrong", rc, rc ? omega_gpu_nv4_last_error() : "", bad);
        t.s[5] = 0x7F; CHECK(omega_gpu_nv4_slab_set(sl, 0, t.w, t.s, t.g) == OMEGA_GPU_NV4_BAD_ARGS, "set refuses an e4m3 NaN scale");
        omega_gpu_nv4_slab_free(sl); free(x); free(y); t4_free(&t);
    }
    static const struct { uint32_t n, k, groups, xrows; const char *name; } P[] = {
        { 1280, 2560, 11, 1, "gate_up 11 x 1280x2560" }, { 2560, 640, 11, 11, "down 11 x 2560x640" },
        { 1280, 2560, 1, 1, "gate_up 1 x 1280x2560" }, { 2560, 640, 1, 1, "down 1 x 2560x640" },
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        const uint32_t G = P[i].groups, NT = 12; /* 12 distinct tensors in the slab, the groups pick from them */
        T4 t[12]; for (uint32_t q = 0; q < NT; q++) t4_make(&t[q], P[i].n, P[i].k, 101u + (uint32_t)i * 13u + q);
        OmegaGpuNv4Slab *sl = NULL;
        int rc = omega_gpu_nv4_slab_alloc(P[i].n, P[i].k, NT, &sl);
        for (uint32_t q = 0; q < NT && rc == 0; q++) rc = omega_gpu_nv4_slab_set(sl, q, t[q].w, t[q].s, t[q].g);
        if (rc == 0) rc = omega_gpu_nv4_slab_commit(sl);
        uint32_t ids[16]; for (uint32_t q = 0; q < G; q++) ids[q] = (q * 5u + 3u) % NT;
        float *x = malloc((size_t)P[i].xrows * P[i].k * 4), *y = malloc((size_t)G * P[i].n * 4);
        uint32_t seed = 9u + (uint32_t)i; for (size_t c = 0; c < (size_t)P[i].xrows * P[i].k; c++) x[c] = frand(&seed, -2.0f, 2.0f);
        OmegaGpuNv4Info info; memset(&info, 0, sizeof info);
        if (rc == 0) rc = omega_gpu_nv4_gemv_grouped_f32(sl, ids, G, x, P[i].xrows, y, &info);
        double worst = INFINITY; size_t bad = rc == 0 ? parity_groups(t, G, ids, x, P[i].xrows, y, &worst) : 1;
        CHECK(rc == 0 && bad == 0, "chip %s: rc %d (%s) worst %.2e bad %zu", P[i].name, rc, rc ? omega_gpu_nv4_last_error() : "", worst, bad);
        if (i <= 1)
            for (int m = 1; m <= 7; m++) {
                OmegaNv4GemvSpec sm = base_spec(P[i].k, 2, 8, 1); sm.mutant = m;
                if (!omega_nv4_gemv_spec_ok(&sm)) continue;
                omega_gpu_nv4_test_set_mutant(m);
                int mrc = omega_gpu_nv4_gemv_grouped_f32(sl, ids, G, x, P[i].xrows, y, NULL);
                double w2; size_t mb = mrc == 0 ? parity_groups(t, G, ids, x, P[i].xrows, y, &w2) : 1;
                CHECK(mrc != 0 || mb > 0, "chip %s mutant %d caught (rc %d, %zu bad rows)", P[i].name, m, mrc, mb);
                omega_gpu_nv4_test_set_mutant(0);
            }
        uint64_t *ns = calloc(reps, sizeof *ns);
        for (uint32_t r = 0; r < reps && rc == 0; r++) { OmegaGpuNv4Info in; rc = omega_gpu_nv4_gemv_grouped_f32(sl, ids, G, x, P[i].xrows, y, &in); ns[r] = in.elapsed_ns; }
        const double med = rc == 0 ? median_ns(ns, reps) : 0, bytes = (double)info.weight_bytes;
        printf("  nv4 %-24s rows %u warps %u sched %u grid %u x %u: median %.1f us, %.1f GB/s (%.3f of 273)\n", P[i].name, info.rows_per_warp, info.warps_per_cta,
               info.sched, info.grid_x, G, med / 1e3, bytes / med, bytes / med / 273.0);
        if (out) fprintf(out, "%s{\"name\":\"%s\",\"n\":%u,\"k\":%u,\"groups\":%u,\"rows\":%u,\"warps\":%u,\"sched\":%u,\"grid_x\":%u,\"bad\":%zu,\"worst_rel\":%.3e,\"median_ns\":%.0f,\"bytes\":%.0f,\"reps\":%u}",
                         first ? "" : ",", P[i].name, P[i].n, P[i].k, G, info.rows_per_warp, info.warps_per_cta, info.sched, info.grid_x, bad, worst, med, bytes, reps);
        first = 0;
        free(ns); omega_gpu_nv4_slab_free(sl); free(x); free(y); for (uint32_t q = 0; q < NT; q++) t4_free(&t[q]);
    }
    if (out) fprintf(out, "]");
    if (fixture) fixture_run(fixture, out);
    if (out) { fprintf(out, ",\"checks\":%d,\"failed\":%d}\n", g_checks, g_failed); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, chip = 0; uint32_t reps = 20; const char *out = NULL, *fixture = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--sim")) sim = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
        else if (!strcmp(argv[i], "--shape") && i + 2 < argc) { omega_gpu_nv4_set_shape((uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2])); i += 2; }
        else if (!strcmp(argv[i], "--sched") && i + 1 < argc) omega_gpu_nv4_set_sched(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--ctas") && i + 1 < argc) omega_gpu_nv4_set_cta_budget((uint32_t)atoi(argv[++i]));
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (!host && !sim && !chip) host = 1;
    if (reps < 1) reps = 1;
    if (host) host_only();
    if (sim) sim_battery();
    if (chip) chip_run(reps, out, fixture);
    printf("GPU_NV4_API_TEST %s%s%s: %d checks, %d failed -> %s\n", host ? "host " : "", sim ? "sim " : "", chip ? "chip" : "", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
