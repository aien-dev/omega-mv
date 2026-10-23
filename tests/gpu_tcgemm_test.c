/* gpu_tcgemm_test.c (OWN-30B T3, 2026-10-13): tensor-core grouped FP8 GEMM (omega_blackwell_codegen_tc.h).
 *   --host-only  program statistics and register budgets for the production shapes; the C emulation (exact HMMA model,
 *                tests/bw_hmma_model.h) against a double oracle; permutation and decode checks
 *   --sim        the generated kernel in the warp simulator (both fragment orders) bit-identical to the C emulation;
 *                mutants 1-4 caught (a simulator error counts as a FAIL, never as caught)
 *   --chip       (under flock /tmp/aien-gb10.lock) through omega_gpu_tcgemm_api: bit-identity to the emulation, second call
 *                identical, mutants caught on the chip, timing medians (TFLOPS, GB/s) at large M
 * Verdict line last: "GPU_TCGEMM: PASS|FAIL". */
#include "omega_blackwell_codegen_tc.h"
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_tcgemm_api.h"
#include <inttypes.h>
#include "bw_warp_sim_tc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static const HmmaModel GB10 = { 16, 2, 0, 2, 0 };
static uint64_t rng = 0x243f6a8885a308d3ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static float frnd(void) { return ((float)(rnd() & 0xffffff) / 16777216.0f) * 2.0f - 1.0f; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* a problem: E experts of n x k e4m3, M sorted rows (expert ids non-decreasing), X source rows */
typedef struct {
    uint32_t E, n, k, M, xr;
    uint8_t *w;          /* E * n * k */
    uint16_t *sbf;       /* E * (n/128) * (k/128) bf16 scale_inv */
    float *sf;           /* folded f32 * 2^56 */
    float *x;            /* xr * k */
    uint16_t *xp;        /* xr * k stored (permuted bf16 * 2^64) */
    uint32_t *eid, *src; /* M */
} Prob;

static uint8_t rand_e4m3(void) { for (;;) { uint8_t c = (uint8_t)rnd(); if ((c & 0x7f) != 0x7f) return c; } }
static uint16_t bf16_trunc(float f) { uint32_t u; memcpy(&u, &f, 4); return (uint16_t)(u >> 16); }

static void prob_make(Prob *p, uint32_t E, uint32_t n, uint32_t k, uint32_t M, uint32_t xr, int skewed) {
    memset(p, 0, sizeof *p);
    p->E = E; p->n = n; p->k = k; p->M = M; p->xr = xr;
    size_t wn = (size_t)E * n * k, sn = (size_t)E * (n / 128) * (k / 128);
    p->w = malloc(wn); p->sbf = malloc(sn * 2); p->sf = malloc(sn * 4); p->x = malloc((size_t)xr * k * 4); p->xp = malloc((size_t)xr * k * 2);
    p->eid = malloc(M * 4); p->src = malloc(M * 4);
    for (size_t i = 0; i < wn; i++) p->w[i] = rand_e4m3();
    for (size_t i = 0; i < sn; i++) p->sbf[i] = bf16_trunc(ldexpf(1.0f + (float)(rnd() & 127) / 128.0f, -(int)(rnd() % 12) - 4));
    if (omega_fp8_pack_scales(p->sbf, sn, p->sf, 0) != 0) { printf("scale pack failed\n"); exit(2); }
    for (size_t i = 0; i < (size_t)xr * k; i++) p->x[i] = frnd() * (rnd() % 16 == 0 ? 8.0f : 1.0f);
    for (uint32_t r = 0; r < xr; r++) omega_tcgemm_permute_x(p->x + (size_t)r * k, k, p->xp + (size_t)r * k);
    /* expert ids: sorted; skewed puts many rows on a few experts (multi-tile segments) */
    for (uint32_t i = 0; i < M; i++) { uint32_t e = skewed ? (uint32_t)((uint64_t)i * i / ((uint64_t)M * M / E + 1)) : (uint32_t)((uint64_t)i * E / M); if (e >= E) e = E - 1; p->eid[i] = e; p->src[i] = rnd() % xr; }
}
static void prob_free(Prob *p) { free(p->w); free(p->sbf); free(p->sf); free(p->x); free(p->xp); free(p->eid); free(p->src); }

/* stored index of element p in a permuted row: group (0,2,1,3) */
static uint32_t stored_ix(uint32_t p) { static const uint32_t inv[4] = { 0, 2, 1, 3 }; return (p & ~3u) | inv[p & 3u]; }

/* C emulation: exactly the kernel's arithmetic (8 HMMA steps per 128 block from a zero temporary, then fma with S') */
static void emulate(const Prob *p, float *y) {
    const uint32_t n = p->n, k = p->k, KB = k / 128;
    for (uint32_t i = 0; i < p->M; i++) {
        const uint16_t *xs = p->xp + (size_t)p->src[i] * k;
        for (uint32_t c = 0; c < n; c++) {
            const uint8_t *wr = p->w + ((size_t)p->eid[i] * n + c) * k;
            const float *sr = p->sf + ((size_t)p->eid[i] * (n / 128) + c / 128) * KB;
            float acc = 0.0f;
            for (uint32_t kb = 0; kb < KB; kb++) {
                float tmp = 0.0f;
                for (uint32_t ch = 0; ch < 2; ch++) for (uint32_t s = 0; s < 4; s++) {
                    float a[16], b[16]; int m = 0;
                    for (uint32_t t = 0; t < 4; t++) for (uint32_t j = 0; j < 4; j++) {
                        uint32_t e = kb * 128 + ch * 64 + 16 * t + 4 * s + j;
                        a[m] = u2f((uint32_t)xs[stored_ix(e)] << 16); b[m] = u2f((uint32_t)omega_tcgemm_w_bf16(wr[e]) << 16); m++;
                    }
                    tmp = hm_dot16(&GB10, a, b, tmp);
                }
                acc = fmaf(tmp, sr[kb], acc);
            }
            y[(size_t)i * n + c] = acc;
        }
    }
}

/* tiles: split every expert segment into runs of at most bm rows; rowidx padded by bm entries */
typedef struct { uint64_t w_va, s_va; uint32_t row0, cnt, pad[2]; } Tile;
static uint32_t make_tiles(const Prob *p, uint32_t bm, Tile *t, const uint8_t *wbase, const float *sbase) {
    uint32_t nt = 0;
    for (uint32_t i = 0; i < p->M;) {
        uint32_t j = i; while (j < p->M && p->eid[j] == p->eid[i]) j++;
        for (uint32_t r = i; r < j; r += bm) {
            memset(&t[nt], 0, sizeof t[nt]);
            t[nt].w_va = (uint64_t)(uintptr_t)(wbase + (size_t)p->eid[i] * p->n * p->k);
            t[nt].s_va = (uint64_t)(uintptr_t)(sbase + (size_t)p->eid[i] * (p->n / 128) * (p->k / 128));
            t[nt].row0 = r; t[nt].cnt = j - r < bm ? j - r : bm; nt++;
        }
        i = j;
    }
    return nt;
}

static int host_only(void) {
    int fail = 0;
    /* decode: every non-NaN code, the bf16 bits are the value * 2^-120 exactly */
    for (unsigned c = 0; c < 256; c++) {
        if ((c & 0x7f) == 0x7f) continue;
        unsigned e = (c >> 3) & 15, m = c & 7; double v = e ? ldexp(1.0 + m / 8.0, (int)e - 7) : ldexp(m / 8.0, -6); if (c & 0x80) v = -v;
        if ((double)u2f((uint32_t)omega_tcgemm_w_bf16((uint8_t)c) << 16) != ldexp(v, -120)) { printf("decode code %02x wrong\n", c); fail++; }
    }
    { char pm[200]; int pr = omega_tc_patch_selftest(pm, sizeof pm); printf("placeholder patch self-test: %s (%s)\n", pr ? "FAIL" : "ok", pm); if (pr) fail++; }
    /* production shapes: instructions and registers (declared = peak + 2 rounded to 8 <= 128; 32 * warps * declared <= 65536) */
    static const OmegaTcGemmSpec shapes[] = {
        { 2048, 4, 2, 8, 0, 0 }, { 768, 4, 2, 8, 0, 0 }, { 4096, 4, 2, 8, 0, 0 }, { 2048, 4, 1, 8, 0, 0 }, { 2048, 2, 2, 4, 0, 0 }, { 2048, 4, 2, 4, 0, 0 }, { 768, 4, 2, 4, 0, 0 },
        { 5120, 4, 2, 8, 0, 0 }, { 17408, 4, 2, 8, 0, 0 }, { 32768, 4, 2, 8, 0, 0 }, /* Qwen3.8-27B widths and the new cap (omega#541) */
    };
    static BlackwellIRProgram prog;
    for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        int rc = omega_blackwell_codegen_tcgemm_prog(&shapes[i], &prog);
        uint32_t peak = rc == 0 ? prog.regalloc.peak_gpr_usage : 0, decl = omega_bw_gpr_declared(peak);
        int ok = rc == 0 && decl <= 128u && 32u * shapes[i].warps * decl <= 65536u;
        printf("shape k %u rt %u ct %u warps %u: rc %d, %zu instructions, peak %u, declared %u, shared %u, span %u: %s\n", shapes[i].k, shapes[i].rt, shapes[i].ct, shapes[i].warps,
               rc, rc == 0 ? prog.count : 0, peak, decl, omega_tcgemm_shared_bytes(&shapes[i]), omega_tcgemm_span(&shapes[i]), ok ? "ok" : "REFUSED");
        if (i < 2 && !ok) fail++;
    }
    /* emulation vs double oracle (x as stored bf16, true e4m3 and bf16 scale): relative to the sum of |terms| */
    Prob p; prob_make(&p, 3, 256, 512, 40, 17, 0);
    float *y = malloc((size_t)p.M * p.n * 4); emulate(&p, y);
    double worst = 0;
    for (uint32_t i = 0; i < p.M; i++) for (uint32_t c = 0; c < p.n; c++) {
        double s = 0, mag = 0;
        for (uint32_t e = 0; e < p.k; e++) {
            double wv = (double)u2f((uint32_t)omega_tcgemm_w_bf16(p.w[((size_t)p.eid[i] * p.n + c) * p.k + e]) << 16) * ldexp(1.0, 120);
            double xv = (double)u2f((uint32_t)p.xp[(size_t)p.src[i] * p.k + stored_ix(e)] << 16) * ldexp(1.0, -64);
            double sv = (double)u2f((uint32_t)p.sbf[((size_t)p.eid[i] * (p.n / 128) + c / 128) * (p.k / 128) + e / 128] << 16);
            s += wv * xv * sv; mag += fabs(wv * xv * sv);
        }
        double rel = fabs(y[(size_t)i * p.n + c] - s) / (mag + 1e-30); if (rel > worst) worst = rel;
    }
    printf("emulation vs double oracle: worst |err| / sum|terms| = %.3g (%s, bound 1e-6)\n", worst, worst < 1e-6 ? "ok" : "FAIL");
    if (worst >= 1e-6) fail++;
    free(y); prob_free(&p);
    printf("host-only: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

/* run the kernel in the simulator on p; returns 0 and fills y, or -1 with msg */
static int sim_run(const Prob *p, const OmegaTcGemmSpec *sp, int order, float *y, char *msg, size_t msgn) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_tcgemm_prog(sp, &prog) != 0) { snprintf(msg, msgn, "codegen refused"); return -1; }
    const uint32_t bm = 16u * sp->rt, span = omega_tcgemm_span(sp);
    if (p->n % span) { snprintf(msg, msgn, "n %% span"); return -1; }
    Tile *tiles = calloc(p->M + 1, sizeof *tiles);
    uint32_t nt = make_tiles(p, bm, tiles, p->w, p->sf);
    uint32_t *ridx = malloc((p->M + bm) * 4); for (uint32_t i = 0; i < p->M + bm; i++) ridx[i] = i < p->M ? p->src[i] : 0;
    for (size_t i = 0; i < (size_t)p->M * p->n; i++) y[i] = -999.0f;
    uint8_t *smem = malloc(omega_tcgemm_shared_bytes(sp));
    static WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32u * sp->warps; s.smem = smem; s.smem_bytes = omega_tcgemm_shared_bytes(sp); s.order = order;
    float *xf = NULL; /* xf32 kernels read x * 2^64 as f32 rows in natural order */
    if (sp->xf32) { xf = malloc((size_t)p->xr * p->k * 4); for (size_t i = 0; i < (size_t)p->xr * p->k; i++) xf[i] = p->x[i] * 18446744073709551616.0f; }
    const uintptr_t xva = sp->xf32 ? (uintptr_t)xf : (uintptr_t)p->xp;
    uint64_t a[5] = { (uintptr_t)tiles, (uintptr_t)ridx, xva, (uintptr_t)y, p->n };
    for (int i = 0; i < 4; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)a[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(a[i] >> 32); }
    s.cbank[0x3a0 / 4] = p->n;
    s.ranges[0].lo = (uintptr_t)tiles; s.ranges[0].hi = (uintptr_t)(tiles + nt);
    s.ranges[1].lo = (uintptr_t)ridx; s.ranges[1].hi = (uintptr_t)(ridx + p->M + bm);
    s.ranges[2].lo = xva; s.ranges[2].hi = xva + (size_t)p->xr * p->k * (sp->xf32 ? 4u : 2u);
    s.ranges[3].lo = (uintptr_t)y; s.ranges[3].hi = (uintptr_t)(y + (size_t)p->M * p->n);
    s.ranges[4].lo = (uintptr_t)p->w; s.ranges[4].hi = (uintptr_t)(p->w + (size_t)p->E * p->n * p->k);
    s.ranges[5].lo = (uintptr_t)p->sf; s.ranges[5].hi = (uintptr_t)(p->sf + (size_t)p->E * (p->n / 128) * (p->k / 128));
    s.nranges = 6;
    int err = ws_run(&s, p->n / span, nt);
    if (err) snprintf(msg, msgn, "simulator %s", s.msg);
    free(tiles); free(ridx); free(smem); free(xf);
    return err ? -1 : 0;
}

static int sim_tests(void) {
    int fail = 0;
    static const struct { uint32_t E, n, k, M, xr; int skew; OmegaTcGemmSpec sp; } pts[] = {
        { 3, 256, 256, 40, 13, 0, { 256, 4, 2, 8, 0, 0 } },
        { 2, 128, 384, 70, 9, 1, { 384, 4, 2, 8, 0, 0 } },   /* three blocks, a multi-tile segment */
        { 4, 128, 128, 37, 11, 0, { 128, 2, 2, 4, 0, 0 } },
        { 2, 256, 256, 20, 7, 0, { 256, 4, 1, 8, 0, 0 } },
        { 2, 128, 256, 33, 5, 1, { 256, 1, 4, 4, 0, 0 } },
        { 1, 512, 256, 64, 64, 0, { 256, 4, 2, 8, 0, 0 } },  /* dense: one expert, four CTAs across n */
        { 1, 128, 17408, 64, 64, 0, { 17408, 4, 2, 8, 0, 0 } }, /* Qwen3.8-27B down width (omega#541) */
        /* cascade/tree-verify-gemm: f32 activations converted in the staging (xf32), decode-sized row counts */
        { 1, 256, 384, 1, 1, 0, { 384, 1, 4, 4, 0, 1 } },
        { 1, 256, 256, 32, 32, 0, { 256, 2, 2, 8, 0, 1 } },
        { 1, 256, 256, 5, 5, 0, { 256, 2, 2, 4, 0, 1 } },
        { 1, 512, 256, 64, 64, 0, { 256, 2, 2, 8, 0, 1 } }, /* 64 rows: two row tiles */
        { 1, 256, 256, 3, 3, 0, { 256, 1, 4, 2, 0, 0 } },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        Prob p; prob_make(&p, pts[i].E, pts[i].n, pts[i].k, pts[i].M, pts[i].xr, pts[i].skew);
        float *ye = malloc((size_t)p.M * p.n * 4), *ys = malloc((size_t)p.M * p.n * 4), *y0 = malloc((size_t)p.M * p.n * 4);
        emulate(&p, ye);
        for (int order = 0; order < 2; order++) {
            char msg[240] = ""; int rc = sim_run(&p, &pts[i].sp, order, ys, msg, sizeof msg);
            size_t bad = 0, poison = 0;
            if (rc == 0) for (size_t q = 0; q < (size_t)p.M * p.n; q++) { if (ys[q] == -999.0f) poison++; if (memcmp(&ys[q], &ye[q], 4)) bad++; }
            if (order == 0 && rc == 0) memcpy(y0, ys, (size_t)p.M * p.n * 4);
            int same = order == 0 || (rc == 0 && memcmp(y0, ys, (size_t)p.M * p.n * 4) == 0);
            printf("sim E %u n %u k %u M %u rt %u ct %u warps %u order %d: %s %s%zu of %zu differ from the emulation, %zu unwritten%s\n", p.E, p.n, p.k, p.M, pts[i].sp.rt, pts[i].sp.ct, pts[i].sp.warps, order,
                   rc ? "ERROR" : (bad || poison || !same) ? "MISMATCH" : "ok", msg, bad, (size_t)p.M * p.n, poison, bad || poison ? "" : " (bit-identical)");
            if (rc || bad || poison || !same) fail++;
        }
        free(ye); free(ys); free(y0); prob_free(&p);
    }
    /* mutants: each must run cleanly in the simulator and differ from the emulation */
    for (int m = 1; m <= 5; m++) {
        Prob p; prob_make(&p, 2, 128, 384, 50, 9, 1);
        float *ye = malloc((size_t)p.M * p.n * 4), *ys = malloc((size_t)p.M * p.n * 4);
        emulate(&p, ye);
        OmegaTcGemmSpec sp = { 384, m == 5 ? 2u : 4u, 2, 8, m, m == 5 ? 1u : 0u };
        char msg[240] = ""; int rc = sim_run(&p, &sp, 0, ys, msg, sizeof msg);
        size_t bad = 0; if (rc == 0) for (size_t q = 0; q < (size_t)p.M * p.n; q++) if (memcmp(&ys[q], &ye[q], 4)) bad++;
        printf("sim mutant %d: %s %s(%zu of %zu differ)\n", m, rc ? "ERROR (counts as FAIL)" : bad ? "caught" : "NOT CAUGHT", msg, bad, (size_t)p.M * p.n);
        if (rc || !bad) fail++;
        free(ye); free(ys); prob_free(&p);
    }
    printf("sim: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

int chip_tests(int argc, char **argv);
int tv_tests(int argc, char **argv);

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--dump") == 0) { /* raw code words for nvdisasm -b SM121 (offline check only): --dump file [k rt ct warps mutant] */
        static BlackwellIRProgram dp; static uint8_t buf[OMEGA_BW_CODE_CAP_MAX]; size_t len = 0;
        OmegaTcGemmSpec sp = { argc > 3 ? (uint32_t)atoi(argv[3]) : 2048u, argc > 4 ? (uint32_t)atoi(argv[4]) : 4u, argc > 5 ? (uint32_t)atoi(argv[5]) : 2u, argc > 6 ? (uint32_t)atoi(argv[6]) : 8u, argc > 7 ? atoi(argv[7]) : 0, argc > 8 ? (uint32_t)atoi(argv[8]) : 0u };
        if (omega_blackwell_codegen_tcgemm_prog(&sp, &dp) != 0 || omega_bw_encode_program(&dp, buf, sizeof buf, &len) != 0) return 1;
        FILE *f = fopen(argv[2], "wb"); if (!f) return 1; fwrite(buf, 1, len, f); fclose(f); printf("peak %u, %zu bytes\n", dp.regalloc.peak_gpr_usage, len); return 0;
    }
    int host = 0, sim = 0, chip = 0, tv = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--sim")) sim = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--tv")) tv = 1;
    }
    if (!host && !sim && !chip && !tv) host = sim = 1;
    int fail = 0;
    if (host) fail += host_only();
    if (sim && !fail) fail += sim_tests();
    if (chip && !fail) fail += chip_tests(argc, argv);
    if (tv && !fail) fail += tv_tests(argc, argv);
    printf("GPU_TCGEMM: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}


/* ------------------------------------------------------------- chip */
/* route `tokens` tokens to top-8 of E experts (distinct, uniform), sorted by expert; dense (E == 1): one row per token */
static void route(Prob *p, uint32_t tokens) {
    if (p->E == 1) { for (uint32_t i = 0; i < p->M; i++) { p->eid[i] = 0; p->src[i] = i; } return; }
    uint32_t *cnt = calloc(p->E, 4), *pe = malloc((size_t)tokens * 8 * 4);
    for (uint32_t t = 0; t < tokens; t++) for (uint32_t j = 0; j < 8; j++) {
        uint32_t e; int dup; do { e = rnd() % p->E; dup = 0; for (uint32_t q = 0; q < j; q++) if (pe[t * 8 + q] == e) dup = 1; } while (dup);
        pe[t * 8 + j] = e; cnt[e]++;
    }
    uint32_t *off = calloc(p->E + 1, 4); for (uint32_t e = 0; e < p->E; e++) off[e + 1] = off[e] + cnt[e];
    for (uint32_t t = 0; t < tokens; t++) for (uint32_t j = 0; j < 8; j++) { uint32_t e = pe[t * 8 + j], at = off[e]++; p->eid[at] = e; p->src[at] = t; }
    free(cnt); free(pe); free(off);
}
static void emulate_rows(const Prob *p, const uint32_t *rows, uint32_t nr, float *out) { /* emulate only the listed rows */
    Prob q = *p; q.M = 1;
    for (uint32_t i = 0; i < nr; i++) { q.eid = p->eid + rows[i]; q.src = p->src + rows[i]; emulate(&q, out + (size_t)i * p->n); }
}
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

int chip_tests(int argc, char **argv) {
    const char *shape = "gateup", *outp = NULL; uint32_t tokens = 128, reps = 9, check = 384; int mut = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shape") && i + 1 < argc) shape = argv[++i];
        else if (!strcmp(argv[i], "--tokens") && i + 1 < argc) tokens = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--check-rows") && i + 1 < argc) check = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outp = argv[++i];
        else if (!strcmp(argv[i], "--no-mutants")) mut = 0;
        else if (!strcmp(argv[i], "--tile") && i + 3 < argc) { omega_gpu_tcgemm_set_shape((uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2]), (uint32_t)atoi(argv[i + 3])); i += 3; }
    }
    uint32_t E, n, k;
    if (!strcmp(shape, "gateup")) { E = 128; n = 1536; k = 2048; } else if (!strcmp(shape, "down")) { E = 128; n = 2048; k = 768; }
    else if (!strcmp(shape, "qkv")) { E = 1; n = 5120; k = 2048; } else if (!strcmp(shape, "o")) { E = 1; n = 2048; k = 4096; }
    /* Qwen3.8-27B dense shapes (omega#541): MLP gate or up, down (k 17408), DeltaNet in_proj_qkv, out_proj / o_proj */
    else if (!strcmp(shape, "q38gate")) { E = 1; n = 17408; k = 5120; } else if (!strcmp(shape, "q38down")) { E = 1; n = 5120; k = 17408; }
    else if (!strcmp(shape, "q38qkv")) { E = 1; n = 10240; k = 5120; } else if (!strcmp(shape, "q38o")) { E = 1; n = 5120; k = 6144; }
    /* cascade/tree-verify-gemm: the decode graph's fused matrices (gate|up, GDN in_proj qkv|z, attention q|gate|k|v) */
    else if (!strcmp(shape, "q38gu")) { E = 1; n = 34816; k = 5120; } else if (!strcmp(shape, "q38mix")) { E = 1; n = 16384; k = 5120; }
    else if (!strcmp(shape, "q38amix")) { E = 1; n = 14336; k = 5120; }
    else { printf("unknown shape %s\n", shape); return 1; }
    const uint32_t M = E == 1 ? tokens : tokens * 8u;
    if (reps > 64) reps = 64;
    int fail = 0;
    Prob p; prob_make(&p, E, n, k, M, tokens, 0); route(&p, tokens);
    OmegaGpuFp8Tensor *t = NULL;
    int rc = omega_gpu_fp8_tensor_upload(E, n, k, p.w, p.sbf, &t);
    if (rc != 0) { printf("chip: upload failed %d: %s\n", rc, omega_gpu_tcgemm_last_error()); prob_free(&p); return 1; }
    float *y = malloc((size_t)M * n * 4), *y2 = malloc((size_t)M * n * 4);
    OmegaGpuTcGemmInfo info;
    rc = omega_gpu_tcgemm_f32(t, M, p.eid, p.src, p.x, tokens, y, &info);
    if (rc) { printf("chip: call failed %s: %s\n", omega_gpu_tcgemm_rc_name(rc), omega_gpu_tcgemm_last_error()); fail++; goto done; }
    printf("chip %s: %u tokens, M %u rows, %u segments, %u tiles, grid %u x %u, rt %u ct %u warps %u, declared %u registers\n", shape, tokens, M, info.segments, info.tiles, info.grid_x, info.grid_y, info.rt, info.ct, info.warps, info.gpr_declared);
    {   /* bit-identity on sampled rows: the first and last rows of the table and a stride across it */
        uint32_t nr = check < M ? check : M, *rows = calloc(nr ? nr : 1, 4); float *ye = malloc((size_t)nr * n * 4); size_t bad = 0;
        for (uint32_t i = 0; i < nr; i++) rows[i] = nr == M ? i : i < 8 ? i : i >= nr - 8 ? M - (nr - i) : (uint32_t)((uint64_t)i * M / nr);
        emulate_rows(&p, rows, nr, ye);
        for (uint32_t i = 0; i < nr; i++) for (uint32_t c = 0; c < n; c++) if (memcmp(&y[(size_t)rows[i] * n + c], &ye[(size_t)i * n + c], 4)) bad++;
        printf("chip bit-identity: %zu of %zu sampled outputs (%u rows) differ from the emulation%s\n", bad, (size_t)nr * n, nr, bad ? "" : " (bit-identical)");
        if (bad) fail++;
        free(rows); free(ye);
    }
    rc = omega_gpu_tcgemm_f32(t, M, p.eid, p.src, p.x, tokens, y2, &info);
    { int same = rc == 0 && memcmp(y, y2, (size_t)M * n * 4) == 0; printf("chip second call: %s\n", same ? "identical" : "DIFFERS"); if (!same) fail++; }
    if (mut) for (int m = 1; m <= 4; m++) {
        omega_gpu_tcgemm_test_set_mutant(m);
        rc = omega_gpu_tcgemm_f32(t, M, p.eid, p.src, p.x, tokens, y2, NULL);
        size_t b = 0; if (rc == 0) for (size_t q = 0; q < (size_t)M * n; q++) if (memcmp(&y[q], &y2[q], 4)) b++;
        printf("chip mutant %d: %s (%zu of %zu differ)%s%s\n", m, rc ? "ERROR (counts as FAIL)" : b ? "caught" : "NOT CAUGHT", b, (size_t)M * n, rc ? " " : "", rc ? omega_gpu_tcgemm_last_error() : "");
        if (rc || !b) fail++;
    }
    omega_gpu_tcgemm_test_set_mutant(0);
    {
        uint64_t ce[64], cc[64]; uint32_t done = 0;
        for (uint32_t r = 0; r < reps; r++) { if (omega_gpu_tcgemm_f32(t, M, p.eid, p.src, p.x, tokens, y2, &info) == 0) { ce[done] = info.elapsed_ns; cc[done] = info.call_ns; done++; } }
        if (done != reps) { printf("chip timing: %u of %u calls failed\n", reps - done, reps); fail++; }
        if (done) {
            qsort(ce, done, 8, cmp_u64); qsort(cc, done, 8, cmp_u64);
            double ns = (double)ce[done / 2], flops = 2.0 * M * n * k;
            printf("TCGEMM %s (medians of %u): chip %.1f us, call %.1f us; %.2f TFLOPS; weights streamed %.0f MB (%.1f GB/s), activations gathered %.0f MB, output %.0f MB; total %.1f GB/s\n", shape, done, ns / 1e3, cc[done / 2] / 1e3,
                   flops / ns / 1e3, info.streamed_bytes / 1e6, info.streamed_bytes / ns, info.act_bytes / 1e6, (double)M * n * 4 / 1e6, (info.streamed_bytes + info.act_bytes + (double)M * n * 4) / ns);
            if (outp) { FILE *f = fopen(outp, "w"); if (f) { fprintf(f, "{\"shape\":\"%s\",\"tokens\":%u,\"rows\":%u,\"tiles\":%u,\"reps\":%u,\"chip_median_ns\":%.0f,\"call_median_ns\":%" PRIu64 ",\"tflops\":%.3f,\"streamed_bytes\":%" PRIu64 ",\"act_bytes\":%" PRIu64 ",\"fail\":%d}\n",
                                                          shape, tokens, M, info.tiles, done, ns, cc[done / 2], flops / ns / 1e3, info.streamed_bytes, info.act_bytes, fail); fclose(f); } }
        }
    }
done:
    free(y); free(y2); omega_gpu_fp8_tensor_free(t); prob_free(&p);
    printf("chip: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

/* ------------------------------------------------------------- tree-verify (cascade/tree-verify-gemm) */
/* --tv: omega_gpu_tvgemm on device buffers, dense shapes, N rows from --rows (comma list, each <= 64).
 *   every N: rows checked bit for bit against the C emulation (--emu-rows of them, spread), and EVERY row of every N
 *   bit for bit against the same row of the largest N (the "same answer whatever N" property, decode N = 1 included);
 *   mutants 1-4 at the largest N must change the output; timing: median chip time of --reps launches per N. */
#include "omega_gpu_tvgemm.h"
static int parse_rows(const char *s, uint32_t *out, int cap) {
    int n = 0; while (*s && n < cap) { out[n++] = (uint32_t)strtoul(s, (char **)&s, 10); if (*s == ',') s++; else break; }
    return n;
}
int tv_tests(int argc, char **argv) {
    const char *shape = "q38gu", *rows_s = "1,2,4,8,16,32,64", *outp = NULL, *tiles_s = NULL; uint32_t reps = 15, emu = 4; int mut = 1, wcache = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shape") && i + 1 < argc) shape = argv[++i];
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows_s = argv[++i];
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--emu-rows") && i + 1 < argc) emu = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--weight-cache") && i + 1 < argc) wcache = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outp = argv[++i];
        else if (!strcmp(argv[i], "--no-mutants")) mut = 0;
        else if (!strcmp(argv[i], "--tile") && i + 3 < argc) { omega_gpu_tv_set_shape((uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2]), (uint32_t)atoi(argv[i + 3])); i += 3; }
        else if (!strcmp(argv[i], "--tiles") && i + 1 < argc) tiles_s = argv[++i];
    }
    uint32_t n, k;
    if (!strcmp(shape, "q38gu")) { n = 34816; k = 5120; } else if (!strcmp(shape, "q38gate")) { n = 17408; k = 5120; }
    else if (!strcmp(shape, "q38down")) { n = 5120; k = 17408; } else if (!strcmp(shape, "q38o")) { n = 5120; k = 6144; }
    else if (!strcmp(shape, "q38mix")) { n = 16384; k = 5120; } else if (!strcmp(shape, "q38amix")) { n = 14336; k = 5120; }
    else if (!strcmp(shape, "q38qkv")) { n = 10240; k = 5120; } else if (!strcmp(shape, "q38head")) { n = 248320; k = 5120; }
    else if (!strcmp(shape, "small")) { n = 512; k = 512; }
    else { printf("unknown shape %s\n", shape); return 1; }
    uint32_t rl[16]; int nr = parse_rows(rows_s, rl, 16), fail = 0; uint32_t nmax = 0;
    for (int i = 0; i < nr; i++) { if (rl[i] == 0 || rl[i] > OMEGA_GPU_TV_MAX_ROWS) { printf("bad row count %u\n", rl[i]); return 1; } if (rl[i] > nmax) nmax = rl[i]; }
    if (reps > 64) reps = 64;
    omega_gpu_fp8_set_weight_cache(wcache);
    Prob p; prob_make(&p, 1, n, k, nmax, nmax, 0); route(&p, nmax);
    OmegaGpuFp8Tensor *t = NULL;
    int rc = omega_gpu_fp8_tensor_upload(1, n, k, p.w, p.sbf, &t);
    if (rc != 0) { printf("tv: upload failed %d: %s\n", rc, omega_gpu_tcgemm_last_error()); prob_free(&p); return 1; }
    OmegaGpuMem xd, yd; memset(&xd, 0, sizeof xd); memset(&yd, 0, sizeof yd);
    const size_t ybytes = (size_t)nmax * n * 4u;
    if (omega_gpu_pf_alloc_dev((size_t)nmax * k * 2u, &xd) || omega_gpu_pf_alloc_dev(ybytes, &yd) || omega_gpu_pf_upload(&xd, 0, p.xp, (size_t)nmax * k * 2u)) {
        printf("tv: device buffers failed: %s\n", omega_gpu_tcgemm_last_error()); omega_gpu_fp8_tensor_free(t); prob_free(&p); return 1;
    }
    float *ref = malloc(ybytes), *y = malloc(ybytes), *poison = malloc(ybytes);
    OmegaGpuMem xfd; memset(&xfd, 0, sizeof xfd); /* the same rows as f32 x 2^64, natural order (xform F32) */
    {   float *xf = malloc((size_t)nmax * k * 4); for (size_t i = 0; i < (size_t)nmax * k; i++) xf[i] = p.x[i] * 18446744073709551616.0f;
        if (omega_gpu_pf_alloc_dev((size_t)nmax * k * 4u, &xfd) || omega_gpu_pf_upload(&xfd, 0, xf, (size_t)nmax * k * 4u)) { printf("tv: f32 x buffer failed\n"); fail++; }
        free(xf); }
    for (size_t i = 0; i < (size_t)nmax * n; i++) poison[i] = -999.0f;
    printf("tv %s: n %u k %u (weights %.1f MB), rows %s, weight cache %d\n", shape, n, k, (double)n * k / 1e6, rows_s, wcache);
    /* the largest N first: its output is the reference for every other N */
    uint32_t order[16]; int no = 0; order[no++] = nmax; for (int i = 0; i < nr; i++) if (rl[i] != nmax) order[no++] = rl[i];
    FILE *jf = outp ? fopen(outp, "w") : NULL;
    /* --tiles "rt:ct:w,...": extra row-tile shapes after the automatic one (0:0:0); every output of every shape and N must
     * equal the reference bits (the per-row result does not depend on the tile) */
    uint32_t tl[16][3]; int ntl = 1; tl[0][0] = tl[0][1] = tl[0][2] = 0;
    for (const char *q = tiles_s; q && *q && ntl < 16;) { char *e; tl[ntl][0] = (uint32_t)strtoul(q, &e, 10); if (*e != ':') break; tl[ntl][1] = (uint32_t)strtoul(e + 1, &e, 10); if (*e != ':') break; tl[ntl][2] = (uint32_t)strtoul(e + 1, &e, 10); ntl++; q = *e == ',' ? e + 1 : e; }
    for (int ti = 0; ti < ntl && !fail; ti++)
    for (int oi = 0; oi < no; oi++) {
        const uint32_t N = order[oi];
        omega_gpu_tv_set_shape(tl[ti][0], tl[ti][1], tl[ti][2]);
        OmegaTcGemmSpec sp;
        if (omega_gpu_tv_spec(N, n, k, &sp) != 0) { printf("tv N %u tile %u:%u:%u: shape refused, skipped\n", N, tl[ti][0], tl[ti][1], tl[ti][2]); if (ti == 0) fail++; continue; }
        if (ti > 0 && 16u * sp.rt < N) { printf("tv N %u tile %u:%u:%u: %u row tiles (weights read %u times), skipped\n", N, sp.rt, sp.ct, sp.warps, (N + 16u * sp.rt - 1u) / (16u * sp.rt), (N + 16u * sp.rt - 1u) / (16u * sp.rt)); continue; }
        uint64_t ns = 0;
        rc = omega_gpu_pf_upload(&yd, 0, poison, ybytes);
        if (!rc) rc = omega_gpu_tv_gemm(t, N, xd.va, yd.va, &ns);
        if (rc && ti > 0) { printf("tv N %u tile %u:%u:%u: refused (rc %d: %s), skipped\n", N, sp.rt, sp.ct, sp.warps, rc, omega_gpu_tcgemm_last_error()); continue; }
        if (rc || omega_gpu_pf_download(&yd, 0, y, ybytes)) {
            printf("tv N %u: call failed %d: %s\n", N, rc, omega_gpu_tcgemm_last_error()); fail++; break;
        }
        size_t unwritten = 0, beyond = 0;
        for (size_t i = 0; i < (size_t)N * n; i++) if (y[i] == -999.0f) unwritten++;
        for (size_t i = (size_t)N * n; i < (size_t)nmax * n; i++) if (y[i] != -999.0f) beyond++;
        if (oi == 0 && ti == 0) {
            memcpy(ref, y, ybytes);
            uint32_t ne = emu < N ? emu : N, *rows = calloc(ne ? ne : 1, 4); float *ye = malloc((size_t)ne * n * 4); size_t bad = 0;
            for (uint32_t i = 0; i < ne; i++) rows[i] = ne == 1 ? 0 : (uint32_t)((uint64_t)i * (N - 1) / (ne - 1));
            emulate_rows(&p, rows, ne, ye);
            for (uint32_t i = 0; i < ne; i++) for (uint32_t c = 0; c < n; c++) if (memcmp(&y[(size_t)rows[i] * n + c], &ye[(size_t)i * n + c], 4)) bad++;
            printf("tv N %u: emulation check on %u rows: %zu of %zu differ%s\n", N, ne, bad, (size_t)ne * n, bad ? "" : " (bit-identical)");
            if (bad) fail++;
            free(rows); free(ye);
        }
        size_t diff = 0; for (size_t i = 0; i < (size_t)N * n; i++) if (memcmp(&y[i], &ref[i], 4)) diff++;
        printf("tv N %u: rt %u ct %u warps %u; %zu of %zu outputs differ from the N=%u pass%s; %zu unwritten, %zu written beyond row N\n", N, sp.rt, sp.ct, sp.warps,
               diff, (size_t)N * n, nmax, diff ? "" : " (same bits)", unwritten, beyond);
        if (diff || unwritten || beyond) fail++;
        uint64_t ce[64]; uint32_t done = 0;
        for (uint32_t r = 0; r < reps; r++) { uint64_t e = 0; if (omega_gpu_tv_gemm(t, N, xd.va, yd.va, &e) == 0) ce[done++] = e; }
        if (done != reps) { printf("tv timing: %u of %u calls failed\n", reps - done, reps); fail++; }
        if (ti == 0) {   /* f32 activations converted in the kernel (the decode graph's xq form): same bits, timed */
            OmegaTcGemmSpec sx; size_t xd2 = 0; uint64_t cx[64]; uint32_t dx = 0; int xrc = omega_gpu_tv_spec_x(N, OMEGA_GPU_TV_X_F32, n, k, &sx);
            if (!xrc) xrc = omega_gpu_pf_upload(&yd, 0, poison, ybytes);
            if (!xrc) xrc = omega_gpu_tv_gemm_x(t, N, OMEGA_GPU_TV_X_F32, xfd.va, yd.va, NULL);
            if (!xrc) xrc = omega_gpu_pf_download(&yd, 0, y, ybytes);
            if (!xrc) for (size_t i = 0; i < (size_t)N * n; i++) if (memcmp(&y[i], &ref[i], 4)) xd2++;
            for (uint32_t r = 0; !xrc && r < reps; r++) { uint64_t e = 0; if (omega_gpu_tv_gemm_x(t, N, OMEGA_GPU_TV_X_F32, xfd.va, yd.va, &e) == 0) cx[dx++] = e; }
            if (dx) qsort(cx, dx, 8, cmp_u64);
            printf("tv xf32 N %u: rt %u ct %u warps %u; %s, %zu of %zu differ from the reference%s; chip median %.1f us (%u), weights %.1f GB/s\n", N, sx.rt, sx.ct, sx.warps, xrc ? "ERROR" : "ok", xd2, (size_t)N * n,
                   xd2 ? "" : " (same bits)", dx ? cx[dx / 2] / 1e3 : 0.0, dx, dx ? (double)n * k / cx[dx / 2] : 0.0);
            if (jf && dx) fprintf(jf, "{\"shape\":\"%s\",\"n\":%u,\"k\":%u,\"rows\":%u,\"xform\":\"f32\",\"rt\":%u,\"ct\":%u,\"warps\":%u,\"weight_cache\":%d,\"reps\":%u,\"chip_median_ns\":%" PRIu64 ",\"weights_gbps\":%.2f}\n",
                                  shape, n, k, N, sx.rt, sx.ct, sx.warps, wcache, dx, cx[dx / 2], (double)n * k / cx[dx / 2]);
            if (xrc || xd2 || dx != reps) fail++;
        }
        {   /* the host convenience call (f32 in, f32 out): same bits as the device-buffer pass, and its whole-call time */
            float *yf = malloc((size_t)N * n * 4); OmegaGpuTvInfo ti; uint64_t cc[8]; uint32_t cd = 0; size_t fd = 0; int frc = 0;
            for (uint32_t r = 0; r < 5; r++) { frc = omega_gpu_tv_f32(t, N, p.x, yf, &ti); if (frc) break; cc[cd++] = ti.call_ns; }
            if (!frc) for (size_t i = 0; i < (size_t)N * n; i++) if (memcmp(&yf[i], &ref[i], 4)) fd++;
            if (cd) qsort(cc, cd, 8, cmp_u64);
            printf("tv f32 call N %u: %s, %zu of %zu differ from the device pass; whole call median %.1f us (5 calls)\n", N, frc ? "ERROR" : "ok", fd, (size_t)N * n, cd ? cc[cd / 2] / 1e3 : 0.0);
            if (frc || fd) fail++;
            free(yf);
        }
        if (done) {
            qsort(ce, done, 8, cmp_u64);
            const double med = (double)ce[done / 2];
            double sum = 0; for (uint32_t i = 0; i < done; i++) sum += (double)ce[i];
            const double mean = sum / done;
            printf("TVGEMM %s N %u (median of %u, min %.1f, max %.1f, mean %.1f us): chip %.1f us; weights %.1f GB/s; %.2f TFLOPS\n", shape, N, done, ce[0] / 1e3, ce[done - 1] / 1e3,
                   mean / 1e3, med / 1e3, (double)n * k / med, 2.0 * N * n * k / med / 1e3);
            if (jf) fprintf(jf, "{\"shape\":\"%s\",\"n\":%u,\"k\":%u,\"rows\":%u,\"rt\":%u,\"ct\":%u,\"warps\":%u,\"weight_cache\":%d,\"reps\":%u,\"chip_median_ns\":%.0f,\"chip_min_ns\":%" PRIu64 ",\"weights_gbps\":%.2f}\n",
                            shape, n, k, N, sp.rt, sp.ct, sp.warps, wcache, done, med, ce[0], (double)n * k / med);
        }
    }
    omega_gpu_tv_set_shape(0, 0, 0);
    if (mut && !fail) for (int m = 1; m <= 4; m++) {
        OmegaTcGemmSpec sp; omega_gpu_tv_spec(nmax, n, k, &sp);
        if (m == 3 && sp.rt < 2) continue;
        omega_gpu_tv_test_set_mutant(m);
        rc = omega_gpu_pf_upload(&yd, 0, poison, ybytes); if (!rc) rc = omega_gpu_tv_gemm(t, nmax, xd.va, yd.va, NULL); if (!rc) rc = omega_gpu_pf_download(&yd, 0, y, ybytes);
        size_t b = 0; if (rc == 0) for (size_t q = 0; q < (size_t)nmax * n; q++) if (memcmp(&y[q], &ref[q], 4)) b++;
        printf("tv mutant %d (N %u): %s (%zu of %zu differ)\n", m, nmax, rc ? "ERROR (counts as FAIL)" : b ? "caught" : "NOT CAUGHT", b, (size_t)nmax * n);
        if (rc || !b) fail++;
    }
    omega_gpu_tv_test_set_mutant(0);
    if (jf) fclose(jf);
    omega_gpu_pf_free(&xd); omega_gpu_pf_free(&yd); omega_gpu_pf_free(&xfd); free(ref); free(y); free(poison);
    omega_gpu_fp8_tensor_free(t); prob_free(&p);
    printf("tv: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
