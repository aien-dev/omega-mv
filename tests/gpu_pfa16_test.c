/* gpu_pfa16_test.c (OWN-30B W-L1H, 2026-10-14): the W8A16 whole-prompt prefill kernels (omega_gpu_pfa16.h).
 *   --host-only  host mirrors: the hx bf16 conversion equals omega_tcgemm_permute_x; the C emulation against a double oracle
 *   --sim        hx kernels (convert f32, convert bf16, swiglu) and the cut-1 GEMM (identity and gathered rows) in the warp simulator,
 *                bit-identical to the host mirrors / emulation; mutants caught (a simulator error is a FAIL)
 *   --chip       (under flock /tmp/aien-gb10.lock) the chain on the device API with real Qwen3-30B-A3B layer shapes and synthetic
 *                weights read IN PLACE from an FP8 GEMV tensor: hconv bit-identical, hgemm bit-identical to the emulation on
 *                sampled rows, hswiglu within 1 bf16 ulp of its mirror (MUFU), counted, second launch identical,
 *                mutants caught, medians at --tokens
 * Verdict line last: "GPU_PFA16: PASS|FAIL". */
#include "omega_blackwell_codegen_tc.h"
#include "omega_blackwell_codegen_hx.h"
#include "omega_gpu_pfa16.h"
#include "omega_gpu_prefill.h"
#include "omega_gpu_fp8_api.h"
#include "bw_warp_sim_tc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const HmmaModel GB10 = { 16, 2, 0, 2, 0 }; /* chip model, evidence OWN30B-T3-HMMA-MODEL-38fe3d98 */
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static float frnd(void) { return ((float)(rnd() & 0xffffff) / 16777216.0f) * 2.0f - 1.0f; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint32_t perm_ix(uint32_t j) { static const uint32_t o[4] = { 0, 2, 1, 3 }; return (j & ~3u) | o[j & 3u]; } /* stored slot of element j */

/* host mirrors of the hx kernels (one row) */
static void conv_ref(const float *x, uint32_t k, uint16_t *out, int mutant) {
    for (uint32_t j = 0; j < k; j++) out[mutant == 1 ? j : perm_ix(j)] = omega_hx_bf16_rn(x[j] * (mutant == 2 ? 1.0f : 0x1p64f));
}
static void swiglu_ref(const float *gu, uint32_t I, uint16_t *out, int mutant) {
    for (uint32_t a = 0; a < I; a++) {
        const float g = mutant == 1 ? gu[I + a] : gu[a], u = mutant == 1 ? gu[a] : gu[I + a];
        out[perm_ix(a)] = omega_hx_bf16_rn(omega_hx_swiglu(g, u) * (mutant == 2 ? 1.0f : 0x1p64f));
    }
}
/* bf16 ulp distance of two finite values of the same sign class (0 = identical bits) */
static uint32_t ulp16(uint16_t a, uint16_t b) { int32_t x = (a & 0x8000u) ? -(int32_t)(a & 0x7fffu) : (int32_t)a, y = (b & 0x8000u) ? -(int32_t)(b & 0x7fffu) : (int32_t)b; return (uint32_t)(x > y ? x - y : y - x); }
/* the SwiGLU rule (as gpu_qgemm_test.c for the W8A8 epilogue): MUFU.EX2 / MUFU.RCP are approximations, so within 1 bf16 ulp
 * of the exp2f / 1/x mirror is allowed and counted; anything further is a FAIL */
static float rand_act(void) { return frnd() * (rnd() % 16 == 0 ? 8.0f : 1.0f); }
static float rand_gate(void) { uint32_t c = rnd() % 8; return c == 0 ? -80.0f - 20.0f * fabsf(frnd()) : c == 1 ? 12.0f * frnd() : 3.0f * frnd(); } /* covers the 125 cap */

/* ---- a GEMM problem: E experts n x k e4m3, M sorted rows, xh source rows (bf16 stored form) ---- */
typedef struct {
    uint32_t E, n, k, M, xr;
    uint8_t *w; uint16_t *sbf; float *sf; uint16_t *xh; uint32_t *eid, *src, *counts;
} Prob;
static uint8_t rand_e4m3(void) { for (;;) { uint8_t c = (uint8_t)rnd(); if ((c & 0x7f) != 0x7f) return c; } }
static uint16_t bf16_trunc(float f) { uint32_t u; memcpy(&u, &f, 4); return (uint16_t)(u >> 16); }
/* xr source rows; direct: src[i] = i (xr = M); the xh buffer holds xr + pad rows (pad rows zero) */
static void prob_make(Prob *p, uint32_t E, uint32_t n, uint32_t k, uint32_t M, uint32_t xr, int direct, uint32_t pad) {
    memset(p, 0, sizeof *p);
    p->E = E; p->n = n; p->k = k; p->M = M; p->xr = direct ? M : xr;
    const size_t wn = (size_t)E * n * k, sn = (size_t)E * (n / 128) * (k / 128);
    p->w = malloc(wn); p->sbf = malloc(sn * 2); p->sf = malloc(sn * 4); p->xh = calloc((size_t)(p->xr + pad) * k, 2);
    p->eid = malloc((size_t)M * 4); p->src = malloc((size_t)M * 4); p->counts = calloc(E, 4);
    for (size_t i = 0; i < wn; i++) p->w[i] = rand_e4m3();
    for (size_t i = 0; i < sn; i++) p->sbf[i] = bf16_trunc(ldexpf(1.0f + (float)(rnd() & 127) / 128.0f, -(int)(rnd() % 12) - 4));
    if (omega_fp8_pack_scales(p->sbf, sn, p->sf, 0) != 0) { printf("scale pack failed\n"); exit(2); }
    for (uint32_t i = 0; i < M; i++) { uint32_t e = (uint32_t)((uint64_t)i * E / M); p->eid[i] = e; p->counts[e]++; p->src[i] = direct ? i : rnd() % p->xr; }
}
static void prob_free(Prob *p) { free(p->w); free(p->sbf); free(p->sf); free(p->xh); free(p->eid); free(p->src); free(p->counts); }
/* exactly the kernel's arithmetic for row i (omega_blackwell_codegen_tc.h): 8 HMMA steps per 128 block from zero, fma with S' */
static void emulate_row(const Prob *p, uint32_t i, float *y) {
    const uint32_t n = p->n, k = p->k, KB = k / 128;
    const uint16_t *xs = p->xh + (size_t)p->src[i] * k;
    for (uint32_t c = 0; c < n; c++) {
        const uint8_t *wr = p->w + ((size_t)p->eid[i] * n + c) * k;
        const float *sr = p->sf + ((size_t)p->eid[i] * (n / 128) + c / 128) * KB;
        float acc = 0.0f;
        for (uint32_t kb = 0; kb < KB; kb++) {
            float tmp = 0.0f;
            for (uint32_t ch = 0; ch < 2; ch++) for (uint32_t s = 0; s < 4; s++) {
                float a[16], b[16]; int m = 0;
                for (uint32_t t = 0; t < 4; t++) for (uint32_t j = 0; j < 4; j++) {
                    const uint32_t e = kb * 128 + ch * 64 + 16 * t + 4 * s + j;
                    a[m] = u2f((uint32_t)xs[perm_ix(e)] << 16); b[m] = u2f((uint32_t)omega_tcgemm_w_bf16(wr[e]) << 16); m++;
                }
                tmp = hm_dot16(&GB10, a, b, tmp);
            }
            acc = fmaf(tmp, sr[kb], acc);
        }
        y[c] = acc;
    }
}
/* double oracle of row i, column c from the true values (e4m3 * scale) x (bf16 activation) */
static double oracle(const Prob *p, uint32_t i, uint32_t c, double *mag) {
    const uint32_t k = p->k, KB = k / 128; double s = 0, m = 0;
    const uint16_t *xs = p->xh + (size_t)p->src[i] * k;
    const uint8_t *wr = p->w + ((size_t)p->eid[i] * p->n + c) * k;
    for (uint32_t e = 0; e < k; e++) {
        const double sc = u2f((uint32_t)p->sbf[((size_t)p->eid[i] * (p->n / 128) + c / 128) * KB + e / 128] << 16);
        const double w = (double)u2f((uint32_t)omega_tcgemm_w_bf16(wr[e]) << 16) * 0x1p120 * sc;
        const double x = (double)u2f((uint32_t)xs[perm_ix(e)] << 16) * 0x1p-64;
        s += w * x; m += fabs(w * x);
    }
    *mag = m; return s;
}

/* ---- host ---- */
static int host_tests(void) {
    int fail = 0;
    {   float x[512]; uint16_t a[512], b[512];
        for (int r = 0; r < 64; r++) { for (int j = 0; j < 512; j++) x[j] = rand_act() * ldexpf(1.0f, (int)(rnd() % 40) - 20); conv_ref(x, 512, a, 0); omega_tcgemm_permute_x(x, 512, b); if (memcmp(a, b, sizeof a)) fail++; }
        printf("host: hx conversion mirror equals omega_tcgemm_permute_x on 64 rows: %s\n", fail ? "FAIL" : "ok");
    }
    {   Prob p; prob_make(&p, 2, 256, 384, 9, 5, 0, 0);
        for (uint32_t r = 0; r < p.xr; r++) { float x[384]; for (int j = 0; j < 384; j++) x[j] = rand_act(); conv_ref(x, 384, p.xh + (size_t)r * 384, 0); }
        double worst = 0; float y[256];
        for (uint32_t i = 0; i < p.M; i++) { emulate_row(&p, i, y); for (uint32_t c = 0; c < p.n; c++) { double mag, o = oracle(&p, i, c, &mag); double e = fabs(y[c] - o) / (mag > 0 ? mag : 1); if (e > worst) worst = e; } }
        printf("host: emulation vs double oracle (bf16 activations): worst error %.3g of the sum of |terms| %s\n", worst, worst < 1e-6 ? "ok" : "FAIL");
        if (worst >= 1e-6) fail++;
        prob_free(&p);
    }
    printf("host: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

/* ---- simulator ---- */
static int sim_hx(uint32_t mode, uint32_t k, uint32_t in_type, uint32_t rows, int mutant, size_t *bad_out, char *msg, size_t msgn) {
    static BlackwellIRProgram prog;
    OmegaHxSpec sp = { mode, k, in_type, mutant };
    if (omega_blackwell_codegen_hx_prog(&sp, &prog) != 0) { snprintf(msg, msgn, "codegen refused"); return -1; }
    const uint32_t kin = mode == OMEGA_HX_SWIGLU ? 2 * k : k;
    float *x = malloc((size_t)rows * kin * 4); uint16_t *xb = malloc((size_t)rows * kin * 2), *out = malloc((size_t)rows * k * 2), *ref = malloc((size_t)rows * k * 2);
    for (size_t i = 0; i < (size_t)rows * kin; i++) {
        float v = mode == OMEGA_HX_SWIGLU && (i % kin) < k ? rand_gate() : rand_act();
        if (in_type == OMEGA_HX_IN_BF16) { xb[i] = bf16_trunc(v); v = u2f((uint32_t)xb[i] << 16); }
        x[i] = v;
    }
    for (uint32_t r = 0; r < rows; r++) {
        if (mode == OMEGA_HX_CONVERT) conv_ref(x + (size_t)r * k, k, ref + (size_t)r * k, 0);
        else swiglu_ref(x + (size_t)r * kin, k, ref + (size_t)r * k, 0);
    }
    memset(out, 0xff, (size_t)rows * k * 2);
    static WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = k / 4; s.order = 0;
    const void *in = in_type == OMEGA_HX_IN_BF16 ? (const void *)xb : (const void *)x;
    const uint64_t a[2] = { (uintptr_t)in, (uintptr_t)out };
    for (int i = 0; i < 2; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)a[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(a[i] >> 32); }
    s.ranges[0].lo = (uintptr_t)in; s.ranges[0].hi = (uintptr_t)in + (size_t)rows * kin * (in_type == OMEGA_HX_IN_BF16 ? 2 : 4);
    s.ranges[1].lo = (uintptr_t)out; s.ranges[1].hi = (uintptr_t)(out + (size_t)rows * k);
    s.nranges = 2;
    int err = ws_run(&s, rows, 1);
    if (err) snprintf(msg, msgn, "simulator %.150s", s.msg);
    size_t bad = 0; if (!err) for (size_t i = 0; i < (size_t)rows * k; i++) if (out[i] != ref[i]) bad++;
    *bad_out = bad;
    free(x); free(xb); free(out); free(ref);
    return err ? -1 : 0;
}
typedef struct { uint64_t w_va, s_va; uint32_t row0, cnt, pad[2]; } Tile;
static uint32_t make_tiles(const Prob *p, uint32_t bm, Tile *t) {
    uint32_t nt = 0;
    for (uint32_t i = 0; i < p->M;) {
        uint32_t j = i; while (j < p->M && p->eid[j] == p->eid[i]) j++;
        for (uint32_t r = i; r < j; r += bm) {
            memset(&t[nt], 0, sizeof t[nt]);
            t[nt].w_va = (uintptr_t)(p->w + (size_t)p->eid[i] * p->n * p->k);
            t[nt].s_va = (uintptr_t)(p->sf + (size_t)p->eid[i] * (p->n / 128) * (p->k / 128));
            t[nt].row0 = r; t[nt].cnt = j - r < bm ? j - r : bm; nt++;
        }
        i = j;
    }
    return nt;
}
static int sim_gemm(const Prob *p, const OmegaTcGemmSpec *sp, int order, float *y, char *msg, size_t msgn) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_tcgemm_prog(sp, &prog) != 0) { snprintf(msg, msgn, "codegen refused"); return -1; }
    const uint32_t bm = 16u * sp->rt, span = omega_tcgemm_span(sp);
    Tile *tiles = calloc(p->M + 1, sizeof *tiles);
    const uint32_t nt = make_tiles(p, bm, tiles);
    uint32_t *ridx = malloc((size_t)(p->M + bm) * 4); for (uint32_t i = 0; i < p->M + bm; i++) ridx[i] = i < p->M ? p->src[i] : 0;
    for (size_t i = 0; i < (size_t)p->M * p->n; i++) y[i] = -999.0f;
    uint8_t *smem = malloc(omega_tcgemm_shared_bytes(sp));
    static WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32u * sp->warps; s.smem = smem; s.smem_bytes = omega_tcgemm_shared_bytes(sp); s.order = order;
    const uint64_t a[4] = { (uintptr_t)tiles, (uintptr_t)ridx, (uintptr_t)p->xh, (uintptr_t)y };
    for (int i = 0; i < 4; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)a[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(a[i] >> 32); }
    s.cbank[0x3a0 / 4] = p->n;
    s.ranges[0].lo = (uintptr_t)tiles; s.ranges[0].hi = (uintptr_t)(tiles + nt);
    s.ranges[1].lo = (uintptr_t)ridx; s.ranges[1].hi = (uintptr_t)(ridx + p->M + bm);
    s.ranges[2].lo = (uintptr_t)p->xh; s.ranges[2].hi = (uintptr_t)(p->xh + (size_t)(p->xr + bm) * p->k);
    s.ranges[3].lo = (uintptr_t)y; s.ranges[3].hi = (uintptr_t)(y + (size_t)p->M * p->n);
    s.ranges[4].lo = (uintptr_t)p->w; s.ranges[4].hi = (uintptr_t)(p->w + (size_t)p->E * p->n * p->k);
    s.ranges[5].lo = (uintptr_t)p->sf; s.ranges[5].hi = (uintptr_t)(p->sf + (size_t)p->E * (p->n / 128) * (p->k / 128));
    s.nranges = 6;
    int err = ws_run(&s, p->n / span, nt);
    if (err) snprintf(msg, msgn, "simulator %.150s", s.msg);
    free(tiles); free(ridx); free(smem);
    return err ? -1 : 0;
}
static int sim_tests(void) {
    int fail = 0;
    static const struct { uint32_t mode, k, in, rows; const char *name; } hx[] = {
        { OMEGA_HX_CONVERT, 256, OMEGA_HX_IN_F32, 5, "convert f32 k256" }, { OMEGA_HX_CONVERT, 384, OMEGA_HX_IN_BF16, 3, "convert bf16 k384" },
        { OMEGA_HX_SWIGLU, 256, OMEGA_HX_IN_F32, 6, "swiglu I256" }, { OMEGA_HX_CONVERT, 768, OMEGA_HX_IN_F32, 2, "convert f32 k768" },
    };
    for (size_t c = 0; c < sizeof hx / sizeof hx[0]; c++) {
        size_t bad = 0; char msg[200] = ""; int rc = sim_hx(hx[c].mode, hx[c].k, hx[c].in, hx[c].rows, 0, &bad, msg, sizeof msg);
        printf("sim hx %s: %s %s%zu differ\n", hx[c].name, rc ? "ERROR" : bad ? "MISMATCH" : "ok (bit-identical)", msg, bad);
        if (rc || bad) fail++;
    }
    for (int m = 1; m <= 2; m++) for (uint32_t mode = 0; mode <= 1; mode++) {
        size_t bad = 0; char msg[200] = ""; int rc = sim_hx(mode, 256, OMEGA_HX_IN_F32, 3, m, &bad, msg, sizeof msg);
        printf("sim hx %s mutant %d: %s %s(%zu differ)\n", mode ? "swiglu" : "convert", m, rc ? "ERROR (counts as FAIL)" : bad ? "caught" : "NOT CAUGHT", msg, bad);
        if (rc || !bad) fail++;
    }
    static const struct { uint32_t E, n, k, M, xr; int direct; OmegaTcGemmSpec sp; } g[] = {
        { 3, 256, 256, 70, 0, 1, { 256, 4, 2, 8, 0 } },  /* identity rows: the down GEMM's form */
        { 2, 128, 384, 37, 0, 1, { 384, 2, 2, 4, 0 } },
        { 3, 256, 256, 40, 13, 0, { 256, 4, 2, 8, 0 } }, /* row index: unchanged cut-1 behaviour */
    };
    for (size_t c = 0; c < sizeof g / sizeof g[0]; c++) {
        Prob p; prob_make(&p, g[c].E, g[c].n, g[c].k, g[c].M, g[c].xr, g[c].direct, 16u * g[c].sp.rt);
        for (uint32_t r = 0; r < p.xr; r++) { float x[384]; for (uint32_t j = 0; j < p.k; j++) x[j] = rand_act(); conv_ref(x, p.k, p.xh + (size_t)r * p.k, 0); }
        float *ye = malloc((size_t)p.M * p.n * 4), *ys = malloc((size_t)p.M * p.n * 4);
        for (uint32_t i = 0; i < p.M; i++) emulate_row(&p, i, ye + (size_t)i * p.n);
        for (int order = 0; order < 2; order++) {
            char msg[200] = ""; int rc = sim_gemm(&p, &g[c].sp, order, ys, msg, sizeof msg);
            size_t bad = 0; if (!rc) for (size_t q = 0; q < (size_t)p.M * p.n; q++) if (memcmp(&ys[q], &ye[q], 4)) bad++;
            printf("sim hgemm E %u n %u k %u M %u %s order %d: %s %s%zu differ\n", p.E, p.n, p.k, p.M, g[c].direct ? "identity rows" : "rowidx", order, rc ? "ERROR" : bad ? "MISMATCH" : "ok (bit-identical)", msg, bad);
            if (rc || bad) fail++;
        }
        free(ye); free(ys); prob_free(&p);
    }
    printf("sim: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

/* ---- chip ---- */
static int chip_shape(const char *name, uint32_t tokens, int reps, int check_rows, int mutants) {
    uint32_t E, n, k, M, xr, direct, swiglu = 0;
    if (!strcmp(name, "gateup")) { E = 128; n = 1536; k = 2048; M = tokens * 8; xr = tokens; direct = 0; swiglu = 1; }
    else if (!strcmp(name, "down")) { E = 128; n = 2048; k = 768; M = tokens * 8; xr = 0; direct = 1; }
    else if (!strcmp(name, "qkv")) { E = 1; n = 5120; k = 2048; M = tokens; xr = tokens; direct = 0; }
    else if (!strcmp(name, "o")) { E = 1; n = 2048; k = 4096; M = tokens; xr = tokens; direct = 0; }
    else { printf("unknown shape %s\n", name); return 1; }
    const uint32_t bm = omega_gpu_pf_hgemm_bm();
    if (bm != omega_gpu_pf_qgemm_bm()) { printf("hgemm bm %u != qgemm bm %u: the shared tile builder does not apply\n", bm, omega_gpu_pf_qgemm_bm()); return 1; }
    Prob p; prob_make(&p, E, n, k, M, xr, (int)direct, bm);
    const uint32_t XR = p.xr + bm;
    /* the chain's input: down reads hswiglu(gu) of random gate|up rows; the others hconv(x) of random f32 rows */
    const uint32_t kin = direct ? 2 * k : k;
    float *xin = calloc((size_t)XR * kin, 4);
    for (size_t i = 0; i < (size_t)p.xr * kin; i++) xin[i] = direct && (i % kin) < k ? rand_gate() : rand_act();
    for (uint32_t r = 0; r < p.xr; r++) { if (direct) swiglu_ref(xin + (size_t)r * kin, k, p.xh + (size_t)r * k, 0); else conv_ref(xin + (size_t)r * k, k, p.xh + (size_t)r * k, 0); }
    int fail = 0, rc;
    OmegaGpuFp8Tensor *wt = NULL; OmegaGpuMem xd = { 0 }, hd = { 0 }, yd = { 0 }, ad = { 0 }, tab = { 0 }, ridx = { 0 };
    const size_t xb = ((size_t)XR * kin * 4 + 15) & ~(size_t)15, hb = ((size_t)XR * k * 2 + 15) & ~(size_t)15, yb = ((size_t)(M + bm) * n * 4 + 15) & ~(size_t)15;
    const size_t ab = ((size_t)(M + bm) * (n / 2) * 2 + 15) & ~(size_t)15;
    rc = omega_gpu_fp8_tensor_upload(E, n, k, p.w, p.sbf, &wt) == 0 ? 0 : OMEGA_GPU_PF_CHIP_FAIL; /* the GEMV tensor, read in place */
    if (!rc) rc = omega_gpu_pf_alloc_dev(xb, &xd);
    if (!rc) rc = omega_gpu_pf_alloc_dev(hb, &hd);
    if (!rc) rc = omega_gpu_pf_alloc_dev(yb, &yd);
    if (!rc && swiglu) rc = omega_gpu_pf_alloc_dev(ab, &ad);
    if (!rc) rc = omega_gpu_pf_upload(&xd, 0, xin, xb & ~(size_t)15);
    const uint32_t maxt = M / bm + E + 1;
    if (!rc) rc = omega_gpu_pf_alloc_host((size_t)maxt * 32, &tab);
    if (!rc) rc = omega_gpu_pf_alloc_host((size_t)(M + bm) * 4, &ridx);
    if (rc) { printf("%s setup: %s (%s)\n", name, omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error()); prob_free(&p); free(xin); return 1; }
    const uint32_t nt = omega_gpu_pf_qgemm_tiles(wt, p.counts, tab.cpu, maxt);
    if (!nt) { printf("%s tile table failed\n", name); return 1; }
    { uint32_t *ri = ridx.cpu; for (uint32_t i = 0; i < M + bm; i++) ri[i] = i < M ? p.src[i] : 0; }
    omega_gpu_session_lock(); (void)omega_gpu_session_sync_to_gpu(&tab); (void)omega_gpu_session_sync_to_gpu(&ridx); omega_gpu_session_unlock();
    const uint64_t rva = ridx.va; /* down: identity rows (p.src[i] = i) */
    uint64_t nsx[32] = { 0 }, ns[32] = { 0 }, nss[32] = { 0 };
    if (reps > 32) reps = 32;
    /* 1. activation conversion on the chip (down: hswiglu of gu), bit-identity of every converted row */
    for (int r = 0; r < reps && !rc; r++) rc = direct ? omega_gpu_pf_hswiglu(k, XR, xd.va, hd.va, &nsx[r]) : omega_gpu_pf_hconv(k, OMEGA_GPU_PF_HX_F32, XR, xd.va, hd.va, &nsx[r]);
    uint16_t *hx = malloc(hb);
    if (!rc) rc = omega_gpu_pf_download(&hd, 0, hx, hb & ~(size_t)15);
    if (!rc) {
        size_t bad = 0, ulp1 = 0; for (size_t i = 0; i < (size_t)p.xr * k; i++) if (hx[i] != p.xh[i]) { if (direct && ulp16(hx[i], p.xh[i]) == 1) ulp1++; else bad++; }
        qsort(nsx, reps, 8, cmp_u64);
        printf("%s %s on %u rows of %u: %zu of %zu differ from the host mirror%s (%zu within 1 bf16 ulp); median %.3f ms\n", name, direct ? "hswiglu" : "hconv", XR, k, bad, (size_t)p.xr * k, direct ? " beyond 1 bf16 ulp" : "", ulp1, nsx[reps / 2] / 1e6);
        if (bad) fail++;
        memcpy(p.xh, hx, (size_t)p.xr * k * 2); /* the GEMM check below uses the chip's own converted rows */
    }
    /* 2. the GEMM */
    float *y1 = malloc(yb), *y2 = calloc(1, yb);
    for (int r = 0; r < reps && !rc; r++) rc = omega_gpu_pf_hgemm(n, k, tab.va, nt, rva, hd.va, yd.va, &ns[r]);
    if (!rc) rc = omega_gpu_pf_download(&yd, 0, y1, yb & ~(size_t)15);
    if (!rc) rc = omega_gpu_pf_upload(&yd, 0, y2, yb & ~(size_t)15);
    if (!rc) rc = omega_gpu_pf_hgemm(n, k, tab.va, nt, rva, hd.va, yd.va, NULL);
    if (!rc) rc = omega_gpu_pf_download(&yd, 0, y2, yb & ~(size_t)15);
    if (rc) { printf("%s run: %s (%s)\n", name, omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error()); fail++; }
    else {
        const size_t outb = (size_t)M * n * 4;
        printf("%s second launch identical: %s\n", name, memcmp(y1, y2, outb) ? "NO" : "yes");
        if (memcmp(y1, y2, outb)) fail++;
        uint32_t *rows = malloc(check_rows * 4); for (int q = 0; q < check_rows; q++) rows[q] = q == 0 ? 0 : q == 1 ? M - 1 : rnd() % M;
        float *ye = malloc((size_t)n * 4); size_t bad = 0; double worst = 0;
        for (int q = 0; q < check_rows; q++) {
            emulate_row(&p, rows[q], ye);
            for (uint32_t c = 0; c < n; c++) if (memcmp(&ye[c], &y1[(size_t)rows[q] * n + c], 4)) bad++;
            for (uint32_t c = 0; c < n; c += 97) { double mag, o = oracle(&p, rows[q], c, &mag); double e = fabs(y1[(size_t)rows[q] * n + c] - o) / (mag > 0 ? mag : 1); if (e > worst) worst = e; }
        }
        printf("%s hgemm bit-identity on %d sampled rows: %zu of %zu differ; worst error vs the double oracle %.3g of the sum of |terms|\n", name, check_rows, bad, (size_t)check_rows * n, worst);
        if (bad || worst >= 1e-6) fail++;
        qsort(ns, reps, 8, cmp_u64);
        const double fl = 2.0 * M * n * k, med = (double)ns[reps / 2];
        printf("HGEMM-inplace %s tokens %u M %u n %u k %u tiles %u: median %.3f ms (min %.3f), %.1f TFLOPS\n", name, tokens, M, n, k, nt, med / 1e6, ns[0] / 1e6, fl / med / 1e3);
        /* 3. gate|up: hswiglu of the chip's own GEMM output, bit-identical to the mirror */
        if (swiglu) {
            for (int r = 0; r < reps && !rc; r++) rc = omega_gpu_pf_hswiglu(n / 2, M, yd.va, ad.va, &nss[r]);
            uint16_t *act = malloc(ab), *aref = malloc((size_t)(n / 2) * 2);
            if (!rc) rc = omega_gpu_pf_download(&ad, 0, act, ab & ~(size_t)15);
            size_t sbad = 0, s1 = 0;
            if (!rc) for (uint32_t i = 0; i < M; i++) { swiglu_ref(y1 + (size_t)i * n, n / 2, aref, 0); for (uint32_t a = 0; a < n / 2; a++) { const uint32_t d = ulp16(aref[a], act[(size_t)i * (n / 2) + a]); if (d == 1) s1++; else if (d) sbad++; } }
            qsort(nss, reps, 8, cmp_u64);
            printf("%s hswiglu of the chip GEMM output (%u rows): %s%zu differ beyond 1 bf16 ulp (%zu within 1 ulp); median %.3f ms\n", name, M, rc ? "launch failed " : "", sbad, s1, nss[reps / 2] / 1e6);
            if (rc || sbad) fail++;
            free(act); free(aref);
        }
        if (mutants) {
            for (int m = 1; m <= 4; m++) {
                if (m == 3 && omega_gpu_pf_hgemm_bm() < 32) continue;
                omega_gpu_pf_hgemm_test_set_mutant(m);
                int mrc = omega_gpu_pf_hgemm(n, k, tab.va, nt, rva, hd.va, yd.va, NULL);
                omega_gpu_pf_hgemm_test_set_mutant(0);
                if (!mrc) mrc = omega_gpu_pf_download(&yd, 0, y2, yb & ~(size_t)15);
                const size_t mb = mrc ? 0 : (size_t)(memcmp(y1, y2, outb) != 0);
                printf("%s hgemm mutant %d: %s\n", name, m, mrc ? "launch failed" : mb ? "caught" : "NOT CAUGHT");
                if (mrc || !mb) fail++;
            }
            omega_gpu_pf_hx_test_set_mutant(1);
            int mrc = direct ? omega_gpu_pf_hswiglu(k, XR, xd.va, hd.va, NULL) : omega_gpu_pf_hconv(k, OMEGA_GPU_PF_HX_F32, XR, xd.va, hd.va, NULL);
            omega_gpu_pf_hx_test_set_mutant(0);
            if (!mrc) mrc = omega_gpu_pf_download(&hd, 0, hx, hb & ~(size_t)15);
            const int caught = !mrc && memcmp(hx, p.xh, (size_t)p.xr * k * 2) != 0;
            printf("%s %s mutant 1: %s\n", name, direct ? "hswiglu" : "hconv", mrc ? "launch failed" : caught ? "caught" : "NOT CAUGHT");
            if (!caught) fail++;
        }
        free(rows); free(ye);
    }
    free(y1); free(y2); free(hx); free(xin);
    omega_gpu_fp8_tensor_free(wt); omega_gpu_pf_free(&xd); omega_gpu_pf_free(&hd); omega_gpu_pf_free(&yd); omega_gpu_pf_free(&ad); omega_gpu_pf_free(&tab); omega_gpu_pf_free(&ridx);
    prob_free(&p);
    return fail;
}

int main(int argc, char **argv) {
    int host = 1, sim = 1, chip = 0, reps = 7, rows = 32; uint32_t tokens = 2048, rt = 0, ct = 0, warps = 0; const char *only = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) sim = 0;
        else if (!strcmp(argv[i], "--sim")) host = 0;
        else if (!strcmp(argv[i], "--chip")) { chip = 1; host = 0; sim = 0; }
        else if (!strcmp(argv[i], "--tokens") && i + 1 < argc) tokens = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shape") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--rt") && i + 1 < argc) rt = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ct") && i + 1 < argc) ct = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warps") && i + 1 < argc) warps = (uint32_t)atoi(argv[++i]);
    }
    int fail = 0;
    if (host) fail += host_tests();
    if (sim) fail += sim_tests();
    if (chip) {
        if (rt || ct || warps) omega_gpu_pf_hgemm_set_shape(rt, ct, warps);
        static const char *shapes[] = { "gateup", "down", "qkv", "o" };
        for (int s = 0; s < 4; s++) if (!only || !strcmp(only, shapes[s])) fail += chip_shape(shapes[s], tokens, reps, rows, 1);
    }
    printf("GPU_PFA16: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
