/* gpu_tgemm_test.c (trainer lane E1, 2026-10-16): device-resident bf16 training GEMM (omega_blackwell_codegen_tgemm.h,
 * omega_gpu_tgemm_api.h). C[M][N] = A[M][K] bf16 x B[K][N] bf16 in the layouts NN, NT (Bt[N][K]), TN (At[K][M]) with the
 * epilogues F32, F32_ACC (C += A B), BF16, and deterministic split-K (partials + fixed-order reduce).
 *   --host-only  budgets of the production specs: instructions, allocator peak and declared registers (refused past 1000 / 128)
 *   --sim        the kernels in the warp simulator (both fragment orders) bit-identical to the C emulation of the HMMA chain
 *                (tests/bw_hmma_model.h, GB10 member) plus the epilogue arithmetic, over the whole output; tile K ranges, every
 *                layout / epilogue / split path, mutants caught (kernel 1-4, 6, 7; reduce 7, 8)
 *   --chip       (caller holds the GB10 quiet lock) every output against a double reference over the same bf16-rounded inputs
 *                (|got - ref| / max(sum |terms|, 1e-6) < 1e-5: the rule of gpu_matmul_api_test.c; bf16 output additionally within
 *                2^-8 of the reference and bit-identical to the rounded emulation), first with uncached (host) buffers, then
 *                with GPU-cached device buffers (trainer rule R-8); bit-identical repeats, poison scan (every output written),
 *                negative controls (mutants differ), medians of chip time (all launches) and whole-call time, TFLOPS and percent
 *                of the measured bf16 HMMA peak (--peak, default 117.26: evidence TGEMM-CUT1-5b10b824/peak.log, the gate window;
 *                peak-window1.log of the same day read 120.64; probe gpu_tc_peak_test)
 *                --layout NN|NT|TN, --epi f32|acc|bf16, --splits N (0 auto), --shape MxNxK, --gate pct (cached runs)
 * Verdict line last: "GPU_TGEMM: PASS|FAIL". */
#include "omega_blackwell_codegen_tgemm.h"
#include "omega_gpu_tgemm_api.h"
#include "omega_gpu_prefill.h"
#include "bw_hmma_model.h"
#include "bw_warp_sim_tc.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const HmmaModel GB10 = { 16, 2, 0, 2, 0 };
static uint64_t rng = 0x243f6a8885a308d3ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static float frnd(void) { return ((float)(rnd() & 0xffffff) / 16777216.0f) * 2.0f - 1.0f; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint16_t bf16_rn(float f) { uint32_t u = f2u(f); if ((u & 0x7fffffffu) > 0x7f800000u) return 0x7fc0u; return (uint16_t)((u + 0x7fffu + ((u >> 16) & 1u)) >> 16); }
static float bf(uint16_t h) { return u2f((uint32_t)h << 16); }
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static const char *lname(uint32_t l) { return l == OMEGA_TGEMM_LAYOUT_NN ? "NN" : l == OMEGA_TGEMM_LAYOUT_NT ? "NT" : "TN"; }
static const char *ename(uint32_t e) { return e == OMEGA_TGEMM_EPI_F32 ? "f32" : e == OMEGA_TGEMM_EPI_F32_ACC ? "acc" : "bf16"; }

/* logical A[M][K], B[K][N], old C[M][N]; device images in the call's layout */
typedef struct { uint32_t M, N, K, layout; uint16_t *a, *b, *ai, *bi; float *c0; } Prob;
static void prob_make(Prob *p, uint32_t M, uint32_t N, uint32_t K, uint32_t layout) {
    p->M = M; p->N = N; p->K = K; p->layout = layout;
    p->a = malloc((size_t)M * K * 2); p->b = malloc((size_t)K * N * 2); p->c0 = malloc((size_t)M * N * 4);
    for (size_t i = 0; i < (size_t)M * K; i++) p->a[i] = bf16_rn(frnd() * (rnd() % 16 == 0 ? 8.0f : 1.0f));
    for (size_t i = 0; i < (size_t)K * N; i++) p->b[i] = bf16_rn(frnd() * (rnd() % 16 == 0 ? 8.0f : 1.0f));
    for (size_t i = 0; i < (size_t)M * N; i++) p->c0[i] = frnd() * 4.0f;
    if (layout == OMEGA_TGEMM_LAYOUT_TN) { p->ai = malloc((size_t)M * K * 2); for (uint32_t i = 0; i < M; i++) for (uint32_t k = 0; k < K; k++) p->ai[(size_t)k * M + i] = p->a[(size_t)i * K + k]; }
    else p->ai = p->a;
    if (layout == OMEGA_TGEMM_LAYOUT_NT) { p->bi = malloc((size_t)K * N * 2); for (uint32_t k = 0; k < K; k++) for (uint32_t c = 0; c < N; c++) p->bi[(size_t)c * K + k] = p->b[(size_t)k * N + c]; }
    else p->bi = p->b;
}
static void prob_free(Prob *p) { if (p->ai != p->a) free(p->ai); if (p->bi != p->b) free(p->bi); free(p->a); free(p->b); free(p->c0); }

/* the kernel's arithmetic for output (i, c) over blocks kb0 .. kb0 + nkb - 1: one fused 16-term HMMA per (32-k group, step) */
static float emu(const Prob *p, uint32_t i, uint32_t c, uint32_t kb0, uint32_t nkb) {
    float acc = 0.0f;
    for (uint32_t G = kb0 * 4u; G < (kb0 + nkb) * 4u; G++) for (uint32_t s = 0; s < 2; s++) {
        float a[16], b[16]; int m = 0;
        for (uint32_t t = 0; t < 4; t++) for (uint32_t j = 0; j < 4; j++) {
            const uint32_t k = G * 32u + 8u * t + 4u * s + j;
            a[m] = bf(p->a[(size_t)i * p->K + k]); b[m] = bf(p->b[(size_t)k * p->N + c]); m++;
        }
        acc = hm_dot16(&GB10, a, b, acc);
    }
    return acc;
}
/* the whole call's arithmetic: partials over S equal K ranges (blocks kb0 .. of the table), summed in the kernel's order, the
 * epilogue applied; returns the f32 value (BF16: the rounded value as f32). Build with -ffp-contract=off. */
static float expect(const Prob *p, uint32_t i, uint32_t c, uint32_t S, uint32_t epi, uint32_t kb0, uint32_t nkb) {
    const uint32_t per = nkb / S;
    float sum;
    if (S == 1) { sum = emu(p, i, c, kb0, per); if (epi == OMEGA_TGEMM_EPI_F32_ACC) sum = sum + p->c0[(size_t)i * p->N + c]; }
    else {
        float part[16] = { 0 };
        for (uint32_t s = 0; s < S && s < 16; s++) part[s] = emu(p, i, c, kb0 + s * per, per);
        uint32_t s0 = 0;
        if (epi == OMEGA_TGEMM_EPI_F32_ACC) sum = p->c0[(size_t)i * p->N + c] + part[0], s0 = 1;
        else sum = part[0] + part[1], s0 = 2;
        for (uint32_t s = s0; s < S; s++) sum = sum + part[s];
    }
    return epi == OMEGA_TGEMM_EPI_BF16 ? bf(bf16_rn(sum)) : sum;
}

static int host_only(void) {
    int fail = 0;
    static const struct { OmegaTgemmSpec s; int production; } specs[] = {
        /* k, n, rt, ct, warps, kc, aqn, layout, epi, m, tfast, mutant */
        { { 2304, 768, 8, 2, 16, 32, 1, 0, 0, 8192, 0, 0 }, 1 }, { { 768, 3072, 8, 2, 16, 32, 1, 0, 0, 8192, 0, 0 }, 1 },
        { { 768, 50304, 4, 2, 8, 32, 1, 1, 0, 1024, 1, 0 }, 1 } /* lm_head forward: NT, n % 256 != 0 picks rt 4 x 8 warps, B-slab order */,
        { { 768, 2304, 8, 2, 16, 32, 1, 1, 0, 8192, 0, 0 }, 1 }, { { 3072, 768, 8, 2, 16, 32, 1, 1, 0, 8192, 0, 0 }, 1 } /* forward NT */,
        { { 768, 2304, 8, 2, 16, 32, 1, 1, 2, 8192, 0, 0 }, 1 } /* forward NT, bf16 out */,
        { { 2304, 768, 8, 2, 16, 32, 1, 0, 1, 8192, 0, 0 }, 1 } /* backward dX NN, accumulate */,
        { { 8192, 768, 8, 2, 16, 32, 1, 2, 0, 2304, 0, 0 }, 1 }, { { 8192, 3072, 8, 2, 16, 32, 1, 2, 0, 768, 0, 0 }, 1 } /* dW TN */,
        { { 8192, 768, 4, 2, 8, 32, 1, 2, 0, 768, 0, 0 }, 0 }, { { 8192, 768, 8, 2, 8, 32, 1, 2, 0, 768, 0, 0 }, 0 } /* TN at 8 warps */,
        { { 768, 768, 8, 2, 8, 32, 1, 1, 0, 8192, 0, 0 }, 0 }, { { 768, 768, 8, 2, 8, 32, 1, 0, 0, 8192, 0, 0 }, 0 } /* rt 8 x 8 warps: NT / NN */,
        { { 768, 2304, 4, 2, 16, 32, 2, 0, 0, 8192, 0, 0 }, 0 }, { { 768, 768, 4, 2, 8, 32, 1, 0, 0, 8192, 0, 0 }, 0 }, { { 768, 768, 2, 2, 8, 32, 1, 0, 0, 8192, 0, 0 }, 0 },
    };
    static BlackwellIRProgram prog;
    for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        const OmegaTgemmSpec *s = &specs[i].s;
        int rc = omega_blackwell_codegen_tgemm_prog(s, &prog);
        uint32_t peak = rc == 0 ? prog.regalloc.peak_gpr_usage : 0, decl = omega_bw_gpr_declared(peak);
        int ok = rc == 0 && prog.count <= OMEGA_TGEMM_MAX_INSNS && decl <= 128u && 32u * s->warps * decl <= 65536u;
        printf("spec %s %s k %u n %u m %u rt %u warps %u aqn %u: %s, %zu instructions, peak %u, declared %u, shared %u B -> %s%s%s\n", lname(s->layout), ename(s->epi), s->k, s->n, s->m, s->rt, s->warps, s->aqn,
               rc ? "REFUSED" : "built", rc ? (size_t)0 : prog.count, peak, decl, omega_tgemm_shared_bytes(s), ok ? "fits" : "DOES NOT FIT",
               ok && 2u * 32u * s->warps * decl <= 65536u ? " (2 CTAs per SM)" : "", specs[i].production ? " (production)" : "");
        if (!ok && specs[i].production) fail++;
    }
    static const OmegaTgemmReduceSpec rs[] = { { 2, 0, 768 * 768 * 4, 0 }, { 16, 1, 768 * 768 * 4, 0 }, { 8, 2, 3072 * 768 * 4, 0 } };
    for (size_t i = 0; i < sizeof rs / sizeof rs[0]; i++) {
        int rc = omega_blackwell_codegen_tgemm_reduce_prog(&rs[i], &prog);
        uint32_t peak = rc == 0 ? prog.regalloc.peak_gpr_usage : 0, decl = omega_bw_gpr_declared(peak);
        printf("reduce splits %u %s: %s, %zu instructions, peak %u, declared %u\n", rs[i].splits, ename(rs[i].epi), rc ? "REFUSED" : "built", rc ? (size_t)0 : prog.count, peak, decl);
        if (rc) fail++;
    }
    return fail;
}

/* ---- simulator ---- */
typedef struct { uint32_t M, N, K, rt, warps, aqn, layout, epi, splits, kb0, nkb; } SimCase;
static int sim_run(const Prob *p, const SimCase *c, int mutant, int rmutant, int order, float *y, char *msg, size_t msgn) {
    static BlackwellIRProgram prog, rprog;
    const uint32_t S = c->splits;
    OmegaTgemmSpec sp = { p->K, p->N, c->rt, 2, c->warps, 32, c->aqn, c->layout, S > 1 ? OMEGA_TGEMM_EPI_F32 : c->epi, p->M, (uint32_t)(p->K * p->N > p->M * p->K), mutant };
    if (omega_blackwell_codegen_tgemm_prog(&sp, &prog) != 0) { snprintf(msg, msgn, "codegen refused (spec_ok %d)", omega_tgemm_spec_ok(&sp)); return -1; }
    const uint32_t bm = omega_tgemm_bm(&sp), span = omega_tgemm_span(&sp), nt = p->M / bm, per = c->nkb / S;
    float *part = S > 1 ? malloc((size_t)S * p->M * p->N * 4) : NULL;
    OmegaTgemmTile *tiles = calloc((size_t)nt * S, sizeof *tiles);
    for (uint32_t s = 0; s < S; s++) for (uint32_t t = 0; t < nt; t++) { OmegaTgemmTile *x = &tiles[s * nt + t]; x->c_va = (uintptr_t)(S > 1 ? part + (size_t)s * p->M * p->N : y); x->row0 = t * bm; x->kb0 = c->kb0 + s * per; x->nkb = per; }
    if (c->epi == OMEGA_TGEMM_EPI_F32_ACC) memcpy(y, p->c0, (size_t)p->M * p->N * 4); else memset(y, 0xee, (size_t)p->M * p->N * 4);
    uint8_t *smem = malloc(omega_tgemm_shared_bytes(&sp));
    static WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32u * sp.warps; s.smem = smem; s.smem_bytes = omega_tgemm_shared_bytes(&sp); s.order = order;
    const uint64_t a[3] = { (uintptr_t)tiles, (uintptr_t)p->ai, (uintptr_t)p->bi };
    for (int i = 0; i < 3; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)a[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(a[i] >> 32); }
    s.ranges[0].lo = (uintptr_t)tiles; s.ranges[0].hi = (uintptr_t)(tiles + (size_t)nt * S);
    s.ranges[1].lo = (uintptr_t)p->ai; s.ranges[1].hi = (uintptr_t)(p->ai + (size_t)p->M * p->K);
    s.ranges[2].lo = (uintptr_t)p->bi; s.ranges[2].hi = (uintptr_t)(p->bi + (size_t)p->K * p->N);
    s.ranges[3].lo = (uintptr_t)y; s.ranges[3].hi = (uintptr_t)y + (size_t)p->M * p->N * 4;
    s.nranges = 4;
    if (S > 1) { s.ranges[4].lo = (uintptr_t)part; s.ranges[4].hi = (uintptr_t)(part + (size_t)S * p->M * p->N); s.nranges = 5; }
    int err = ws_run(&s, sp.tfast ? nt * S : p->N / span, sp.tfast ? p->N / span : nt * S);
    if (err) snprintf(msg, msgn, "simulator %s", s.msg);
    if (!err && S > 1) {
        OmegaTgemmReduceSpec r = { S, c->epi, p->M * p->N * 4u, rmutant };
        if (omega_blackwell_codegen_tgemm_reduce_prog(&r, &rprog) != 0) { snprintf(msg, msgn, "reduce codegen refused"); err = -1; }
        else {
            memset(&s, 0, sizeof s);
            s.p = &rprog; s.threads = OMEGA_TGEMM_REDUCE_THREADS; s.order = order;
            const uint64_t ra[2] = { (uintptr_t)y, (uintptr_t)part };
            for (int i = 0; i < 2; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)ra[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(ra[i] >> 32); }
            s.ranges[0].lo = (uintptr_t)y; s.ranges[0].hi = (uintptr_t)y + (size_t)p->M * p->N * 4;
            s.ranges[1].lo = (uintptr_t)part; s.ranges[1].hi = (uintptr_t)(part + (size_t)S * p->M * p->N); s.nranges = 2;
            err = ws_run(&s, (uint32_t)((size_t)p->M * p->N / OMEGA_TGEMM_REDUCE_WORDS_PER_CTA), 1);
            if (err) snprintf(msg, msgn, "reduce simulator %s", s.msg);
        }
    }
    free(tiles); free(smem); free(part);
    return err ? -1 : 0;
}
static size_t sim_compare(const Prob *p, const SimCase *c, const void *y) {
    size_t bad = 0;
    for (uint32_t i = 0; i < p->M; i++) for (uint32_t col = 0; col < p->N; col++) {
        const float want = expect(p, i, col, c->splits, c->epi, c->kb0, c->nkb);
        const size_t at = (size_t)i * p->N + col;
        const uint32_t got = c->epi == OMEGA_TGEMM_EPI_BF16 ? (uint32_t)((const uint16_t *)y)[at] << 16 : ((const uint32_t *)y)[at];
        if (got != f2u(want)) { if (getenv("TGDBG") && bad < 8) printf("  row %u col %u got %.9g want %.9g\n", i, col, u2f(got), want); bad++; }
    }
    return bad;
}
static int sim_tests(void) {
    int fail = 0; char msg[256];
    /* M, N, K, rt, warps, aqn, layout, epi, splits, kb0, nkb; mutants: kernel mutants to catch, reduce mutants to catch */
    static const struct { SimCase c; const char *km, *rm; } cs[] = {
        { { 128, 512, 256, 4, 16, 1, 0, 0, 1, 0, 2 }, "1234", "" }, { { 64, 256, 384, 2, 8, 1, 0, 0, 1, 1, 2 }, "", "" },
        { { 128, 256, 128, 4, 16, 2, 0, 0, 1, 0, 1 }, "", "" }, { { 128, 256, 256, 4, 8, 2, 0, 0, 1, 0, 2 }, "", "" }, { { 256, 512, 128, 8, 16, 1, 0, 0, 1, 0, 1 }, "", "" },
        { { 128, 512, 256, 8, 16, 1, 1, 0, 1, 0, 2 }, "134", "" }, { { 256, 256, 128, 8, 16, 1, 1, 2, 1, 0, 1 }, "7", "" }, { { 64, 256, 128, 4, 8, 1, 1, 0, 1, 0, 1 }, "", "" },
        { { 128, 512, 256, 8, 16, 1, 2, 0, 1, 0, 2 }, "1234", "" }, { { 64, 256, 256, 4, 8, 1, 2, 0, 1, 1, 1 }, "", "" }, { { 128, 256, 128, 4, 8, 1, 2, 2, 1, 0, 1 }, "7", "" },
        { { 128, 256, 128, 4, 16, 1, 0, 1, 1, 0, 1 }, "6", "" }, { { 128, 256, 128, 8, 16, 1, 1, 1, 1, 0, 1 }, "", "" }, { { 128, 256, 128, 4, 8, 1, 0, 2, 1, 0, 1 }, "7", "" },
        { { 128, 256, 256, 8, 16, 1, 2, 0, 2, 0, 2 }, "", "8" }, { { 128, 256, 512, 8, 16, 1, 2, 1, 4, 0, 4 }, "", "8" }, { { 128, 256, 256, 4, 16, 1, 0, 2, 2, 0, 2 }, "", "78" },
    };
    for (size_t c = 0; c < sizeof cs / sizeof cs[0]; c++) {
        const SimCase *k = &cs[c].c;
        Prob p; prob_make(&p, k->M, k->N, k->K, k->layout);
        float *y = malloc((size_t)p.M * p.N * 4);
        for (int order = 0; order < 2; order++) {
            int rc = sim_run(&p, k, 0, 0, order, y, msg, sizeof msg);
            size_t bad = rc ? 0 : sim_compare(&p, k, y);
            printf("sim case %zu (%s %s M %u N %u K %u rt %u warps %u aqn %u splits %u blocks %u..%u) order %d: %s, %zu of %zu differ\n", c, lname(k->layout), ename(k->epi), p.M, p.N, p.K, k->rt, k->warps, k->aqn, k->splits,
                   k->kb0, k->kb0 + k->nkb - 1, order, rc ? msg : "ran", bad, (size_t)p.M * p.N);
            if (rc || bad) fail++;
        }
        for (const char *m = cs[c].km; *m; m++) {
            int rc = sim_run(&p, k, *m - '0', 0, 0, y, msg, sizeof msg);
            size_t bad = rc ? 0 : sim_compare(&p, k, y);
            printf("sim case %zu kernel mutant %c: %s\n", c, *m, rc ? msg : bad ? "caught" : "NOT CAUGHT");
            if (rc || !bad) fail++;
        }
        for (const char *m = cs[c].rm; *m; m++) {
            int rc = sim_run(&p, k, 0, *m - '0', 0, y, msg, sizeof msg);
            size_t bad = rc ? 0 : sim_compare(&p, k, y);
            printf("sim case %zu reduce mutant %c: %s\n", c, *m, rc ? msg : bad ? "caught" : "NOT CAUGHT");
            if (rc || !bad) fail++;
        }
        free(y); prob_free(&p);
    }
    return fail;
}

/* ---- chip: double reference over the bf16 inputs, threaded over rows ---- */
typedef struct { const Prob *p; const void *y; uint32_t epi, S, r0, r1; size_t bad, emu_same; double worst; size_t bc4[4], br8[2], brt[4], bw[16], nan, shown; } RefJob;
static void *ref_rows(void *arg) {
    RefJob *j = arg; const Prob *p = j->p;
    double *col = malloc((size_t)p->N * 16);
    for (uint32_t i = j->r0; i < j->r1; i++) {
        memset(col, 0, (size_t)p->N * 16);
        const uint16_t *ar = p->a + (size_t)i * p->K;
        for (uint32_t k = 0; k < p->K; k++) {
            const double av = bf(ar[k]); const uint16_t *br = p->b + (size_t)k * p->N;
            for (uint32_t c = 0; c < p->N; c++) { const double t = av * (double)bf(br[c]); col[2 * c] += t; col[2 * c + 1] += fabs(t); }
        }
        for (uint32_t c = 0; c < p->N; c++) {
            const size_t at = (size_t)i * p->N + c;
            if (j->epi == OMEGA_TGEMM_EPI_F32_ACC) { col[2 * c] += p->c0[at]; col[2 * c + 1] += fabs(p->c0[at]); }
            const float got = j->epi == OMEGA_TGEMM_EPI_BF16 ? bf(((const uint16_t *)j->y)[at]) : ((const float *)j->y)[at];
            const double scale = col[2 * c + 1] > 1e-6 ? col[2 * c + 1] : 1e-6, err = fabs((double)got - col[2 * c]);
            const double rel = j->epi == OMEGA_TGEMM_EPI_BF16 ? err / (scale * 1e-5 + fabs(col[2 * c]) / 256.0) * 1e-5 : err / scale; /* bf16: within 2^-8 of the value plus the f32 slack */
            const float want = expect(p, i, c, j->S, j->epi, 0, p->K / 128u);
            if (!(rel < 1e-5)) { j->bad++; j->bc4[c % 4]++; j->br8[(i >> 3) & 1]++; j->brt[(i >> 4) & 3]++; j->bw[(c % 256) / 16]++; j->nan += isnan(got) || isinf(got);
                if (getenv("TGDBG") && j->shown < 6) { j->shown++; printf("  bad row %u col %u got %.9g ref %.9g emu %.9g\n", i, c, got, col[2 * c], want); } }
            if (rel > j->worst) j->worst = rel;
            if (f2u(got) == f2u(want)) j->emu_same++;
        }
    }
    free(col);
    return NULL;
}
static size_t ref_check(const Prob *p, const void *y, uint32_t epi, uint32_t S, size_t *emu_same, double *worst) {
    long nproc = sysconf(_SC_NPROCESSORS_ONLN); int nt = nproc < 1 ? 1 : nproc > 32 ? 32 : (int)nproc;
    pthread_t th[32]; RefJob jobs[32]; memset(jobs, 0, sizeof jobs);
    for (int t = 0; t < nt; t++) { jobs[t].p = p; jobs[t].y = y; jobs[t].epi = epi; jobs[t].S = S; jobs[t].r0 = (uint32_t)((uint64_t)p->M * t / nt); jobs[t].r1 = (uint32_t)((uint64_t)p->M * (t + 1) / nt); pthread_create(&th[t], NULL, ref_rows, &jobs[t]); }
    size_t bad = 0; *emu_same = 0; *worst = 0;
    for (int t = 0; t < nt; t++) { pthread_join(th[t], NULL); bad += jobs[t].bad; *emu_same += jobs[t].emu_same; if (jobs[t].worst > *worst) *worst = jobs[t].worst; }
    if (getenv("TGDBG") && bad) { size_t c4[4] = { 0 }, r8[2] = { 0 }, rt[4] = { 0 }, w[16] = { 0 }, nn = 0;
        for (int t = 0; t < nt; t++) { for (int q = 0; q < 4; q++) { c4[q] += jobs[t].bc4[q]; rt[q] += jobs[t].brt[q]; } for (int q = 0; q < 2; q++) r8[q] += jobs[t].br8[q]; for (int q = 0; q < 16; q++) w[q] += jobs[t].bw[q]; nn += jobs[t].nan; }
        printf("  bad by col%%4: %zu %zu %zu %zu; by row half: %zu %zu; by row tile: %zu %zu %zu %zu; nan/inf %zu\n  bad by warp:", c4[0], c4[1], c4[2], c4[3], r8[0], r8[1], rt[0], rt[1], rt[2], rt[3], nn);
        for (int q = 0; q < 16; q++) printf(" %zu", w[q]);
        printf("\n"); }
    return bad;
}
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

typedef struct { uint32_t layout, epi, splits; } Variant;
static int chip_shape(uint32_t M, uint32_t N, uint32_t K, const Variant *v, int cached, int reps, double peak, double gate_pct) {
    Prob p; prob_make(&p, M, N, K, v->layout);
    const uint32_t es = v->epi == OMEGA_TGEMM_EPI_BF16 ? 2u : 4u;
    const size_t ab = (size_t)M * K * 2, bb = (size_t)K * N * 2, cb = (size_t)M * N * es;
    OmegaGpuMem ad = { 0 }, bd = { 0 }, cd = { 0 };
    int rc = cached ? omega_gpu_pf_alloc_dev(ab, &ad) : omega_gpu_pf_alloc_host(ab, &ad);
    if (!rc) rc = cached ? omega_gpu_pf_alloc_dev(bb, &bd) : omega_gpu_pf_alloc_host(bb, &bd);
    if (!rc) rc = cached ? omega_gpu_pf_alloc_dev(cb, &cd) : omega_gpu_pf_alloc_host(cb, &cd);
    if (!rc) rc = omega_gpu_pf_upload(&ad, 0, p.ai, ab);
    if (!rc) rc = omega_gpu_pf_upload(&bd, 0, p.bi, bb);
    /* the output starts as poison (every word must be written), or as the old C for the accumulate epilogue */
    uint8_t *init = malloc(cb); if (v->epi == OMEGA_TGEMM_EPI_F32_ACC) memcpy(init, p.c0, cb); else memset(init, 0xff, cb);
    uint8_t *y1 = malloc(cb), *y2 = malloc(cb);
    if (!rc) rc = omega_gpu_pf_upload(&cd, 0, init, cb);
    OmegaTgemmCall call = { v->layout, v->epi, M, N, K, v->splits, ad.va, bd.va, cd.va };
    OmegaTgemmSpec sp; uint32_t S = 0;
    if (!rc && omega_gpu_tgemm_spec(&call, &sp, &S) != 0) { printf("shape %ux%ux%u %s %s: spec refused\n", M, N, K, lname(v->layout), ename(v->epi)); rc = -1; }
    if (rc) {
        if (rc != -1) printf("shape %ux%ux%u setup: %s (%s)\n", M, N, K, omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error());
        free(init); free(y1); free(y2); omega_gpu_pf_free(&ad); omega_gpu_pf_free(&bd); omega_gpu_pf_free(&cd); prob_free(&p);
        return 1;
    }
    const char *kind = cached ? "cached" : "uncached";
    char tag[96]; snprintf(tag, sizeof tag, "TGEMM %s %s%s %ux%ux%u", lname(v->layout), ename(v->epi), S > 1 ? " splitK" : "", M, N, K);
    int fail = 0;
    if (reps > 64) reps = 64;
    double chip[64], wall[64];
    /* the accumulate epilogue needs the old C restored before every timed call: the restore is outside the timing */
    for (int r = 0; r < reps && !rc; r++) {
        if (v->epi == OMEGA_TGEMM_EPI_F32_ACC && r) rc = omega_gpu_pf_upload(&cd, 0, init, cb);
        uint64_t ns = 0; const double t0 = now_ns(); if (!rc) rc = omega_gpu_tgemm(&call, &ns); wall[r] = now_ns() - t0; chip[r] = (double)ns;
    }
    if (!rc) rc = omega_gpu_pf_download(&cd, 0, y1, cb);
    if (!rc) rc = omega_gpu_pf_upload(&cd, 0, init, cb);
    if (!rc) rc = omega_gpu_tgemm(&call, NULL);
    if (!rc) rc = omega_gpu_pf_download(&cd, 0, y2, cb);
    if (rc) { printf("%s %s run: %s (%s)\n", tag, kind, omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error()); fail++; }
    else {
        size_t left = 0;
        if (v->epi != OMEGA_TGEMM_EPI_F32_ACC) for (size_t i = 0; i < (size_t)M * N; i++) left += es == 4 ? ((const uint32_t *)y1)[i] == 0xffffffffu : ((const uint16_t *)y1)[i] == 0xffffu;
        const int same = !memcmp(y1, y2, cb);
        size_t emu_same = 0; double worst = 0;
        const double t0 = now_ns();
        const size_t bad = ref_check(&p, y1, v->epi, S, &emu_same, &worst);
        qsort(chip, reps, 8, cmp_d); qsort(wall, reps, 8, cmp_d);
        const double fl = 2.0 * M * N * K, mc = chip[reps / 2], mw = wall[reps / 2], tc = fl / mc / 1e3, tw = fl / mw / 1e3;
        printf("%s %s rt %u warps %u tiles %u grid_x %u splits %u order %s: reference %zu of %zu outside tolerance (worst %.3g), %zu bit-identical to the emulation, repeats identical %s, poison left %zu (reference %.1f s)\n",
               tag, kind, sp.rt, sp.warps, M / omega_tgemm_bm(&sp), N / omega_tgemm_span(&sp), S, sp.tfast ? "B-slab" : "A-tile", bad, (size_t)M * N, worst, emu_same, same ? "yes" : "NO", left, (now_ns() - t0) / 1e9);
        printf("%s %s: chip median %.3f ms (min %.3f) = %.2f TFLOPS = %.1f%% of peak %.2f; whole call median %.3f ms = %.2f TFLOPS = %.1f%%%s\n",
               tag, kind, mc / 1e6, chip[0] / 1e6, tc, 100.0 * tc / peak, peak, mw / 1e6, tw, 100.0 * tw / peak, cached && gate_pct > 0 ? (100.0 * tc / peak >= gate_pct ? " GATE ok" : " GATE FAIL") : "");
        if (bad || !same || left || emu_same != (size_t)M * N) fail++;
        if (cached && gate_pct > 0 && !(100.0 * tc / peak >= gate_pct)) fail++; /* the gate is on device-resident (GPU-cached) buffers; the uncached pass is the correctness-first step (R-8) */
        if (!cached) { /* negative controls: a mutant kernel must give a different result (a comparison that cannot fail is no comparison) */
            int ms[4] = { 4, 1, 0, 0 }; int rms[4] = { 0, 0, 0, 0 }; int nm = 2;
            if (v->epi == OMEGA_TGEMM_EPI_F32_ACC && S == 1) ms[nm++] = 6;
            if (v->epi == OMEGA_TGEMM_EPI_BF16 && S == 1) ms[nm++] = 7;
            if (S > 1) { rms[nm] = 8; ms[nm++] = 0; }
            for (int q = 0; q < nm; q++) {
                omega_gpu_tgemm_test_set_mutant(ms[q]); omega_gpu_tgemm_test_set_reduce_mutant(rms[q]);
                int mrc = omega_gpu_pf_upload(&cd, 0, init, cb);
                if (!mrc) mrc = omega_gpu_tgemm(&call, NULL);
                omega_gpu_tgemm_test_set_mutant(0); omega_gpu_tgemm_test_set_reduce_mutant(0);
                if (!mrc) mrc = omega_gpu_pf_download(&cd, 0, y2, cb);
                const int diff = !mrc && memcmp(y1, y2, cb) != 0;
                printf("%s %s mutant %d: %s\n", tag, rms[q] ? "reduce" : "kernel", rms[q] ? rms[q] : ms[q], mrc ? "launch failed" : diff ? "caught" : "NOT CAUGHT");
                if (!diff) fail++;
            }
        }
    }
    free(init); free(y1); free(y2);
    omega_gpu_pf_free(&ad); omega_gpu_pf_free(&bd); omega_gpu_pf_free(&cd);
    prob_free(&p);
    return fail;
}

int main(int argc, char **argv) {
    int host = 1, sim = 1, chip = 0, reps = 7, cached_only = 0, uncached_only = 0; double peak = 117.26, gate = 0; const char *shape = NULL;
    Variant v = { OMEGA_TGEMM_LAYOUT_NN, OMEGA_TGEMM_EPI_F32, 1 };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) sim = 0;
        else if (!strcmp(argv[i], "--sim")) host = 0;
        else if (!strcmp(argv[i], "--chip")) { chip = 1; host = 0; sim = 0; }
        else if (!strcmp(argv[i], "--shape") && i + 1 < argc) shape = argv[++i];
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--peak") && i + 1 < argc) peak = atof(argv[++i]);
        else if (!strcmp(argv[i], "--gate") && i + 1 < argc) gate = atof(argv[++i]);
        else if (!strcmp(argv[i], "--cached-only")) cached_only = 1;
        else if (!strcmp(argv[i], "--mutant") && i + 1 < argc) omega_gpu_tgemm_test_set_mutant(atoi(argv[++i])); /* timing probes: results are wrong on purpose */
        else if (!strcmp(argv[i], "--uncached-only")) uncached_only = 1;
        else if (!strcmp(argv[i], "--shape-knob") && i + 3 < argc) { omega_gpu_tgemm_set_shape((uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2]), (uint32_t)atoi(argv[i + 3])); i += 3; }
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc) { const char *l = argv[++i]; v.layout = !strcmp(l, "NT") ? OMEGA_TGEMM_LAYOUT_NT : !strcmp(l, "TN") ? OMEGA_TGEMM_LAYOUT_TN : OMEGA_TGEMM_LAYOUT_NN; }
        else if (!strcmp(argv[i], "--epi") && i + 1 < argc) { const char *e = argv[++i]; v.epi = !strcmp(e, "acc") ? OMEGA_TGEMM_EPI_F32_ACC : !strcmp(e, "bf16") ? OMEGA_TGEMM_EPI_BF16 : OMEGA_TGEMM_EPI_F32; }
        else if (!strcmp(argv[i], "--splits") && i + 1 < argc) v.splits = (uint32_t)atoi(argv[++i]);
    }
    int fail = 0;
    if (host) fail += host_only();
    if (sim) fail += sim_tests();
    if (chip) {
        static const char *gate_shapes[] = { "8192x768x2304", "8192x768x768", "8192x768x3072", "8192x3072x768" };
        for (size_t s = 0; s < (shape ? 1u : sizeof gate_shapes / sizeof gate_shapes[0]); s++) {
            unsigned M = 0, N = 0, K = 0;
            if (sscanf(shape ? shape : gate_shapes[s], "%ux%ux%u", &M, &N, &K) != 3) { printf("bad shape\n"); fail++; break; }
            if (!cached_only) fail += chip_shape(M, N, K, &v, 0, reps, peak, gate);
            if (!uncached_only) fail += chip_shape(M, N, K, &v, 1, reps, peak, gate);
        }
    }
    printf("GPU_TGEMM: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
