/*
 * gpu_fnattn_test: the Qwen3.8-Flash-Next decode attention (omega_gpu_fnattn_api.h, flashnext/port): 24 q heads over 2 KV heads,
 * head_dim 256, partial RoPE (64), zero-centred q/k norms, and a SPARSE list of attended positions (the QSA indexer's output).
 *   --host-only   host pieces: reference sanity (context 1 returns v_new; a list of one entry returns that row), codegen budget
 *                 per depth and mutant, refusals, shared-memory map. No device.
 *   --sim         the kernel through tests/bw_warp_sim_k.h (no chip): dense lists (contexts 1, 7, 256, 301, 701), sparse lists
 *                 like the indexer's (blocks of 4 plus the tail; with and without the new token's own block), 1 to 3 splits,
 *                 per_split 256 and 512, both fragment orders; every mutant 1..8 caught.
 *   (default)     the chip: parity against the double reference on dense and sparse lists up to a 131072 context, the appended
 *                 K/V rows read back, second call identical, out_scale 2^64, timing medians, mutants 1..7 red on the chip
 *                 (mutant 8 reads outside the cache planes: simulator only), JSON receipt with --out.
 * The reference is the model's maths (crates/aien-inference-abi/src/flashnext.rs `indexed_attention`, read as the spec):
 * zero-centred RMSNorm x * rsqrt(mean + eps) * (1 + w), RoPE on the first 64 dims with pairs (j, j + 32), scale 1/sqrt(256),
 * softmax over the listed positions only. q, k and the norm weights are permuted with omega_dattn256_perm_vec before the kernel;
 * V and the output stay in model order. The gate (sigmoid) and o_proj are outside the kernel.
 * Tolerance: |out - ref| <= 3e-3 * max|v| + 1e-6 per element. Host and simulator passes are never chip results.
 */
#include "omega_gpu_fnattn_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_hostmath.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static const double THETA = 10000000.0; /* Qwen3.8-Flash-Next rope_theta */
static const float EPS = 1e-6f;
#define HD 256u
#define QH 24u
#define KVH 2u
#define QW OMEGA_FNA_QKV_WORDS   /* 7168 */
#define OW OMEGA_FNA_OUT_WORDS   /* 6144 */
#define QEND (QH * HD)           /* 6144: k block starts here */
#define KEND (QEND + KVH * HD)   /* 6656: v block starts here */
#define GY OMEGA_FNA_GRID_Y

/* The list modes: 0 dense (0..pos); 1 sparse like the indexer: `topk` pseudo-random full blocks of 4 plus the incomplete tail
 * block (so the new token's own position is listed unless its block is complete and not chosen); 2 as 1 but the last full block
 * is forced out of the choice (the new token hidden when (pos + 1) % 4 == 0). */
typedef struct {
    uint32_t max_pos, pos, rotary, count;
    uint32_t *plist;
    float qkv[QW], qn[HD], kn[HD];
    uint16_t *kc, *vc;   /* [2][max_pos][256] model order (history 0..pos-1 filled; the rest zero) */
    float vmax;
} Case;
static void make_list(Case *c, int mode, uint32_t topk, uint32_t seed) {
    uint32_t visible = c->pos + 1, nb = visible / 4, s = seed;
    c->plist = malloc((size_t)(visible + 8) * 4); c->count = 0;
    if (mode == 0) { for (uint32_t t = 0; t < visible; t++) c->plist[c->count++] = t; return; }
    uint8_t *take = calloc(nb ? nb : 1, 1);
    uint32_t want = topk < nb ? topk : nb, got = 0;
    if (mode == 2 && nb > 0 && want == nb) want = nb - 1;
    while (got < want) { uint32_t b = lcg(&s) % nb; if (mode == 2 && b == nb - 1) continue; if (!take[b]) { take[b] = 1; got++; } }
    for (uint32_t b = 0; b < nb; b++) if (take[b]) for (uint32_t j = 0; j < 4; j++) c->plist[c->count++] = b * 4 + j;
    for (uint32_t t = nb * 4; t < visible; t++) c->plist[c->count++] = t;
    free(take);
    if (c->count == 0) c->plist[c->count++] = c->pos; /* nothing listed: attend the new token only (pos is always a legal entry) */
}
static void case_make(Case *c, uint32_t max_pos, uint32_t pos, uint32_t rotary, uint32_t seed, float qscale, int mode, uint32_t topk) {
    uint32_t s = seed;
    c->max_pos = max_pos; c->pos = pos; c->rotary = rotary;
    for (size_t i = 0; i < QW; i++) c->qkv[i] = frand(&s, -1.0f, 1.0f) * (i < QEND ? qscale : 1.0f);
    for (unsigned i = 0; i < HD; i++) { c->qn[i] = frand(&s, -0.2f, 0.2f); c->kn[i] = frand(&s, -0.2f, 0.2f); }
    size_t words = (size_t)KVH * max_pos * HD;
    c->kc = calloc(words, 2); c->vc = calloc(words, 2);
    c->vmax = 1.0f;
    for (unsigned g = 0; g < KVH; g++) for (uint32_t t = 0; t < pos; t++) for (unsigned i = 0; i < HD; i++) {
        size_t o = ((size_t)g * max_pos + t) * HD + i;
        c->kc[o] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f)); c->vc[o] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f));
    }
    make_list(c, mode, topk, seed ^ 0x5bd1e995u);
}
static void case_free(Case *c) { free(c->kc); free(c->vc); free(c->plist); }
static void case_kernel_inputs(const Case *c, float *qkv_k, float *qn_k, float *kn_k) {
    for (unsigned h = 0; h < QH + KVH; h++) omega_dattn256_perm_vec(c->qkv + h * HD, qkv_k + h * HD, c->rotary);
    memcpy(qkv_k + KEND, c->qkv + KEND, (size_t)KVH * HD * 4);
    float t[HD];
    for (unsigned i = 0; i < HD; i++) t[i] = 1.0f + c->qn[i];
    omega_dattn256_perm_vec(t, qn_k, c->rotary);
    for (unsigned i = 0; i < HD; i++) t[i] = 1.0f + c->kn[i];
    omega_dattn256_perm_vec(t, kn_k, c->rotary);
}
static uint16_t *case_kc_perm(const Case *c) {
    size_t rows = (size_t)KVH * c->max_pos;
    uint16_t *p = calloc(rows * HD, 2);
    for (size_t r = 0; r < rows; r++) omega_dattn256_perm_vec_bf16(c->kc + r * HD, p + r * HD, c->rotary);
    return p;
}

/* ---- double-precision oracle (test-owned: libm) in MODEL order ---- */
static void norm_rope(double *x, const float *w, float eps, uint32_t rotary, uint32_t pos, double theta, int rope) {
    double ss = 0; for (unsigned i = 0; i < HD; i++) ss += x[i] * x[i];
    double r = 1.0 / sqrt(ss / (double)HD + (double)eps);
    for (unsigned i = 0; i < HD; i++) x[i] = x[i] * r * (1.0 + (double)w[i]);
    if (!rope) return;
    const unsigned half = rotary / 2;
    for (unsigned j = 0; j < half; j++) {
        double inv = 1.0 / pow(theta, (double)(2 * j) / (double)rotary), ang = (double)pos * inv, cs = cos(ang), sn = sin(ang);
        double x1 = x[j], x2 = x[j + half];
        x[j] = x1 * cs - x2 * sn; x[j + half] = x2 * cs + x1 * sn;
    }
}
static void reference(const float *qkv, const uint16_t *kc, const uint16_t *vc, uint32_t max_pos, uint32_t pos, const uint32_t *plist, uint32_t count, uint32_t rotary,
                      const float *q_w, const float *k_w, float eps, double theta, float out_scale, double *out, uint16_t *k_new, uint16_t *v_new) {
    const double qscale = 1.0 / sqrt((double)HD);
    for (unsigned gk = 0; gk < KVH; gk++) {
        double k[HD];
        for (unsigned i = 0; i < HD; i++) k[i] = qkv[QEND + gk * HD + i];
        norm_rope(k, k_w, eps, rotary, pos, theta, 1);
        for (unsigned i = 0; i < HD; i++) { k_new[gk * HD + i] = omega_dattn_bf16_rne((float)k[i]); v_new[gk * HD + i] = omega_dattn_bf16_rne(qkv[KEND + gk * HD + i]); }
    }
    double *p = calloc((size_t)count + 1, sizeof(double));
    for (unsigned h = 0; h < QH; h++) {
        unsigned gk = h / (QH / KVH);
        double q[HD];
        for (unsigned i = 0; i < HD; i++) q[i] = qkv[h * HD + i];
        norm_rope(q, q_w, eps, rotary, pos, theta, 1);
        double mx = -1e300;
        for (uint32_t e = 0; e < count; e++) {
            uint32_t t = plist[e];
            const uint16_t *kr = t == pos ? k_new + gk * HD : kc + ((size_t)gk * max_pos + t) * HD;
            double s = 0; for (unsigned i = 0; i < HD; i++) s += q[i] * (double)omega_dattn_bf16_f32(kr[i]);
            s *= qscale; p[e] = s; if (s > mx) mx = s;
        }
        double l = 0; for (uint32_t e = 0; e < count; e++) { p[e] = exp(p[e] - mx); l += p[e]; }
        double o[HD]; memset(o, 0, sizeof o);
        for (uint32_t e = 0; e < count; e++) {
            uint32_t t = plist[e];
            const uint16_t *vr = t == pos ? v_new + gk * HD : vc + ((size_t)gk * max_pos + t) * HD;
            for (unsigned i = 0; i < HD; i++) o[i] += p[e] * (double)omega_dattn_bf16_f32(vr[i]);
        }
        for (unsigned i = 0; i < HD; i++) out[h * HD + i] = o[i] / l * (double)out_scale;
    }
    free(p);
}
static size_t parity(const Case *c, const float *out, float out_scale, double *worst, uint16_t *k_new, uint16_t *v_new) {
    double *ref = malloc(OW * sizeof(double));
    reference(c->qkv, c->kc, c->vc, c->max_pos, c->pos, c->plist, c->count, c->rotary, c->qn, c->kn, EPS, THETA, out_scale, ref, k_new, v_new);
    double tol = 3e-3 * c->vmax * out_scale + 1e-6 * out_scale; size_t bad = 0; *worst = 0;
    for (size_t i = 0; i < OW; i++) { double d = fabs((double)out[i] - ref[i]); if (d / tol > *worst) *worst = d / tol; if (d > tol) bad++; }
    free(ref);
    return bad;
}
static int rows_ok(const uint16_t *k_chip, const uint16_t *v_chip, const uint16_t *k_ref, const uint16_t *v_ref, uint32_t rotary, char *why, size_t n) {
    unsigned kflips = 0;
    for (unsigned g = 0; g < KVH; g++) for (unsigned i = 0; i < HD; i++) {
        unsigned ci = g * HD + i, ri = g * HD + omega_dattn256_perm_slot(i, rotary);
        if (v_chip[ci] != v_ref[ci]) { snprintf(why, n, "v[%u] chip %04x ref %04x", ci, v_chip[ci], v_ref[ci]); return 0; }
        if (k_chip[ci] != k_ref[ri]) { int d = (int)k_chip[ci] - (int)k_ref[ri]; if (d < -1 || d > 1) { snprintf(why, n, "k slot %u (model dim %u) chip %04x ref %04x", ci, ri, k_chip[ci], k_ref[ri]); return 0; } kflips++; }
    }
    if (kflips > 32) { snprintf(why, n, "%u one-ulp k flips", kflips); return 0; }
    snprintf(why, n, "ok (%u one-ulp k flips)", kflips);
    return 1;
}

/* ------------------------------------------------------------- host only */
static void host_only(void) {
    /* list builder: shapes the indexer produces */
    { Case c; memset(&c, 0, sizeof c); c.pos = 4099; make_list(&c, 1, 512, 7);
      CHECK(c.count == 512 * 4 + (4100 % 4), "sparse list at 4100 visible: 512 blocks of 4 plus an empty tail: %u entries", c.count);
      int asc = 1; for (uint32_t i = 1; i < c.count; i++) asc &= c.plist[i] > c.plist[i - 1];
      CHECK(asc, "list strictly ascending");
      free(c.plist); }
    { Case c; memset(&c, 0, sizeof c); c.pos = 4100; make_list(&c, 1, 512, 7);
      CHECK(c.count == 512 * 4 + 1 && c.plist[c.count - 1] == 4100, "4101 visible: 512 blocks plus the 1-token tail holding pos (%u entries)", c.count); free(c.plist); }
    { Case c; memset(&c, 0, sizeof c); c.pos = 4099; make_list(&c, 2, 100, 9);
      CHECK(c.count == 400 && c.plist[c.count - 1] != 4099, "mode 2 hides pos 4099 (its block is complete and not chosen): %u entries", c.count); free(c.plist); }
    { Case c; memset(&c, 0, sizeof c); c.pos = 1000; make_list(&c, 0, 0, 1); CHECK(c.count == 1001, "dense list at pos 1000 has 1001 entries"); free(c.plist); }
    /* reference sanity */
    { Case c; case_make(&c, 256, 0, 64, 11, 1.0f, 0, 0); double *ref = malloc(OW * sizeof(double)); uint16_t kn[KVH * HD], vn[KVH * HD];
      reference(c.qkv, c.kc, c.vc, 256, 0, c.plist, c.count, 64, c.qn, c.kn, EPS, THETA, 1.0f, ref, kn, vn);
      int good = 1; for (unsigned h = 0; h < QH && good; h++) for (unsigned i = 0; i < HD; i++) if (ref[h * HD + i] != (double)omega_dattn_bf16_f32(vn[(h / 12) * HD + i])) { good = 0; break; }
      CHECK(good, "reference: context 1 returns the rounded v_new of the head's KV group (12 q heads per KV head)");
      free(ref); case_free(&c); }
    { Case c; case_make(&c, 256, 40, 64, 12, 1.0f, 0, 0); uint32_t one = 17; double *ref = malloc(OW * sizeof(double)); uint16_t kn[KVH * HD], vn[KVH * HD];
      reference(c.qkv, c.kc, c.vc, 256, 40, &one, 1, 64, c.qn, c.kn, EPS, THETA, 1.0f, ref, kn, vn);
      int good = 1; for (unsigned h = 0; h < QH && good; h++) for (unsigned i = 0; i < HD; i++) if (ref[h * HD + i] != (double)omega_dattn_bf16_f32(c.vc[((size_t)(h / 12) * 256 + 17) * HD + i])) { good = 0; break; }
      CHECK(good, "reference: a list of one entry returns that position's V row (the new token is hidden)");
      free(ref); case_free(&c); }
    /* codegen budget */
    for (uint32_t d = 1; d <= 4; d++) for (int m = 0; m <= 8; m++) {
        OmegaFnAttnSpec s = { d, m, 0 }; uint32_t n = 0, gp = 0;
        int rc = omega_fnattn_prog_stats(&s, &n, &gp);
        CHECK(rc == 0 && n <= 1100 && gp <= 126, "codegen depth %u mutant %d: rc %d insns %u gprs %u", d, m, rc, n, gp);
        if (rc == 0 && m == 0) printf("codegen depth %u: %u instructions, %u registers (declared %u, 256 threads -> %u of 65536)\n", d, n, gp, omega_bw_gpr_declared(gp), 256 * omega_bw_gpr_declared(gp));
    }
    { OmegaFnAttnSpec s = { 0, 0, 0 }; CHECK(omega_fnattn_prog_stats(&s, NULL, NULL) != 0, "depth 0 refused");
      s.depth = 5; CHECK(omega_fnattn_prog_stats(&s, NULL, NULL) != 0, "depth 5 refused");
      s.depth = 1; s.mutant = 9; CHECK(omega_fnattn_prog_stats(&s, NULL, NULL) != 0, "mutant 9 refused");
      s.mutant = 0; s.prep = 1; CHECK(omega_fnattn_prog_stats(&s, NULL, NULL) != 0, "prefill prep refused (27B-only)"); }
    CHECK(omega_gpu_fnattn_cache_alloc(0, 256, THETA, 64, NULL) == OMEGA_GPU_DATTN_BAD_ARGS, "NULL out refused");
    { OmegaGpuFnAttnCache *c = (OmegaGpuFnAttnCache *)1;
      CHECK(omega_gpu_fnattn_cache_alloc(1, 100, THETA, 64, &c) == OMEGA_GPU_DATTN_BAD_ARGS && c == NULL, "max_pos not a multiple of 256 refused before any device work");
      c = (OmegaGpuFnAttnCache *)1;
      CHECK(omega_gpu_fnattn_cache_alloc(1, 256, THETA, 63, &c) == OMEGA_GPU_DATTN_BAD_ARGS && c == NULL, "odd rotary_dim refused");
      c = (OmegaGpuFnAttnCache *)1;
      CHECK(omega_gpu_fnattn_cache_alloc(1, 262144 + 256, THETA, 64, &c) == OMEGA_GPU_DATTN_BAD_ARGS && c == NULL, "max_pos > 262144 refused"); }
    CHECK(OMEGA_FNA_SHARED_BYTES <= 16384u, "shared memory within the QMD cap (%u)", OMEGA_FNA_SHARED_BYTES);
    CHECK(OMEGA_FNA_SM_FLAG + 16u == OMEGA_FNA_SM_POS && OMEGA_FNA_SM_POS + 4u * OMEGA_FNA_POS_WORDS == OMEGA_FNA_SHARED_BYTES && OMEGA_FNA_SM_S + 8192u <= OMEGA_FNA_SM_M &&
          OMEGA_FNA_SM_M + 288u <= OMEGA_FNA_SM_L && OMEGA_FNA_SM_L + 256u <= OMEGA_FNA_SM_FLAG, "shared map does not overlap");
    CHECK(OMEGA_FNA_QKV_WORDS == 7168u && OMEGA_FNA_ARGS_WORDS * 4u == OMEGA_FNA_ARG_COUNT - 0x380u + 4u, "qkv words and argument block size");
}

/* ------------------------------------------------------------- simulator */
static uint64_t g_sim_issues;
typedef struct { uint32_t max_pos, pos, rotary, depth, per; float qscale; int mode; uint32_t topk; const char *name; } Pt;
static int sim_point(const Pt *pt, int mutant, uint32_t seed, char *msg, size_t n, double *worst, size_t *bad, char *rows, size_t rn) {
    static BlackwellIRProgram prog;
    OmegaFnAttnSpec spec = { pt->depth, mutant, 0 };
    if (omega_blackwell_codegen_fnattn_prog(&spec, &prog) != 0) { snprintf(msg, n, "codegen refused"); return -1; }
    const uint32_t max_pos = pt->max_pos, pos = pt->pos, rotary = pt->rotary, per_split = pt->per;
    Case c; case_make(&c, max_pos, pos, rotary, seed, pt->qscale, pt->mode, pt->topk);
    uint16_t *kcp = case_kc_perm(&c);
    const uint32_t splits = (c.count + per_split - 1) / per_split;
    size_t plane = (size_t)KVH * max_pos * OMEGA_FNA_ROW_BYTES + OMEGA_FNA_PLANE_SLACK, hist = (size_t)KVH * max_pos * OMEGA_FNA_ROW_BYTES;
    uint8_t *kv = aligned_alloc(16, 2 * plane); memset(kv, 0, 2 * plane);
    float *rope = aligned_alloc(16, (size_t)max_pos * OMEGA_FNA_ROPE_ROW_BYTES); for (uint32_t p = 0; p < max_pos; p++) omega_dattn256_rope_row(p, THETA, rotary, rope + (size_t)p * 256);
    float *norms = aligned_alloc(16, 2048);
    float *qkv = aligned_alloc(16, sizeof c.qkv);
    case_kernel_inputs(&c, qkv, norms, norms + HD);
    float *out = aligned_alloc(16, OW * 4), *copy = malloc(OW * 4);
    size_t partb = (size_t)GY * splits * OMEGA_FNA_PARTIAL_WORDS * 4;
    float *part = aligned_alloc(16, partb);
    uint32_t *cnt = aligned_alloc(16, 16);
    uint32_t *pl = aligned_alloc(16, ((size_t)c.count * 4 + 63) & ~(size_t)63); memcpy(pl, c.plist, (size_t)c.count * 4);
    int err = 0;
    for (int order = 0; order < 2 && !err; order++) {
        WsSim s; memset(&s, 0, sizeof s);
        s.p = &prog; s.threads = 256; s.order = order;
        static uint8_t smem[OMEGA_FNA_SHARED_BYTES]; s.smem = smem; s.smem_bytes = OMEGA_FNA_SHARED_BYTES;
        memset(cnt, 0, 16); for (unsigned i = 0; i < OW; i++) out[i] = -999.0f;
        memcpy(kv, kcp, hist); memcpy(kv + plane, c.vc, hist); /* fresh history (the kernel appends) */
        uint64_t A[9] = { (uintptr_t)qkv, (uintptr_t)kv, (uintptr_t)(kv + plane), (uintptr_t)rope, (uintptr_t)norms, (uintptr_t)(norms + HD), (uintptr_t)out, (uintptr_t)part, (uintptr_t)cnt };
        for (unsigned i = 0; i < 9; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)A[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(A[i] >> 32); }
        float fe = EPS, fo = 1.0f, fq = (float)((1.0 / 16.0) * 1.4426950408889634);
        memcpy(&s.cbank[OMEGA_FNA_ARG_EPS / 4], &fe, 4); memcpy(&s.cbank[OMEGA_FNA_ARG_OUT_SCALE / 4], &fo, 4); memcpy(&s.cbank[OMEGA_FNA_ARG_QK_SCALE / 4], &fq, 4);
        s.cbank[OMEGA_FNA_ARG_POS / 4] = pos; s.cbank[OMEGA_FNA_ARG_PER_SPLIT / 4] = per_split; s.cbank[OMEGA_FNA_ARG_SPLITS / 4] = splits; s.cbank[OMEGA_FNA_ARG_KV_STRIDE / 4] = max_pos * OMEGA_FNA_ROW_BYTES;
        s.cbank[OMEGA_FNA_ARG_PLIST / 4] = (uint32_t)(uintptr_t)pl; s.cbank[OMEGA_FNA_ARG_PLIST / 4 + 1] = (uint32_t)((uint64_t)(uintptr_t)pl >> 32); s.cbank[OMEGA_FNA_ARG_COUNT / 4] = c.count;
        s.nranges = 8;
        s.ranges[0].lo = (uintptr_t)qkv; s.ranges[0].hi = (uintptr_t)qkv + sizeof c.qkv;
        s.ranges[1].lo = (uintptr_t)kv; s.ranges[1].hi = (uintptr_t)kv + 2 * plane;
        s.ranges[2].lo = (uintptr_t)rope; s.ranges[2].hi = (uintptr_t)rope + (size_t)max_pos * OMEGA_FNA_ROPE_ROW_BYTES;
        s.ranges[3].lo = (uintptr_t)norms; s.ranges[3].hi = (uintptr_t)norms + 2048;
        s.ranges[4].lo = (uintptr_t)out; s.ranges[4].hi = (uintptr_t)out + OW * 4;
        s.ranges[5].lo = (uintptr_t)part; s.ranges[5].hi = (uintptr_t)part + partb;
        s.ranges[6].lo = (uintptr_t)cnt; s.ranges[6].hi = (uintptr_t)cnt + 16;
        s.ranges[7].lo = (uintptr_t)pl; s.ranges[7].hi = (uintptr_t)pl + (((size_t)c.count * 4 + 63) & ~(size_t)63);
        err = ws_run(&s, splits, GY);
        if (err) { snprintf(msg, n, "order %d: %s (pc %d)", order, s.msg, s.err_pc); break; }
        g_sim_issues += s.warp_issues;
        size_t unwritten = 0; for (unsigned i = 0; i < OW; i++) if (out[i] == -999.0f) unwritten++;
        if (unwritten) { err = -1; snprintf(msg, n, "%zu output words never written", unwritten); break; }
        if (cnt[0] | cnt[1] | cnt[2] | cnt[3]) { err = -1; snprintf(msg, n, "counters not reset: %u %u %u %u", cnt[0], cnt[1], cnt[2], cnt[3]); break; }
        if (order == 0) memcpy(copy, out, OW * 4);
        else if (memcmp(copy, out, OW * 4) != 0) { err = -1; snprintf(msg, n, "the fragment order changed the result"); }
    }
    if (!err) {
        uint16_t kn[KVH * HD], vn[KVH * HD];
        *bad = parity(&c, out, 1.0f, worst, kn, vn);
        uint16_t kchip[KVH * HD], vchip[KVH * HD];
        for (unsigned g = 0; g < KVH; g++) { memcpy(kchip + g * HD, kv + ((size_t)g * max_pos + pos) * OMEGA_FNA_ROW_BYTES, OMEGA_FNA_ROW_BYTES); memcpy(vchip + g * HD, kv + plane + ((size_t)g * max_pos + pos) * OMEGA_FNA_ROW_BYTES, OMEGA_FNA_ROW_BYTES); }
        if (!rows_ok(kchip, vchip, kn, vn, rotary, rows, rn)) *bad += 1000;
        snprintf(msg, n, "count %u splits %u bad=%zu worst=%.3f rows: %s", c.count, splits, *bad, *worst, rows);
    }
    free(kv); free(rope); free(norms); free(qkv); free(out); free(copy); free(part); free(cnt); free(pl); free(kcp); case_free(&c);
    return err;
}
static void sim_battery(void) {
    char msg[300], rows[160]; double worst; size_t bad;
    const Pt pts[] = {
        { 256, 0, 64, 1, 256, 1.0f, 0, 0, "context 1" },
        { 256, 6, 64, 2, 256, 1.0f, 0, 0, "dense, context 7" },
        { 256, 6, 64, 4, 256, 4.0f, 0, 0, "dense, context 7, sharp q (x4)" },
        { 256, 6, 256, 4, 256, 1.0f, 0, 0, "dense, context 7, full rope (rotary 256)" },
        { 256, 255, 64, 4, 256, 1.0f, 0, 0, "dense, context 256 (full chunk, 1 split)" },
        { 512, 300, 64, 3, 256, 1.0f, 0, 0, "dense, context 301 (2 splits)" },
        { 1024, 700, 64, 4, 256, 1.0f, 0, 0, "dense, context 701 (3 splits)" },
        { 1024, 700, 64, 2, 512, 1.0f, 0, 0, "dense, context 701 (per_split 512)" },
        { 1024, 700, 64, 3, 256, 1.0f, 1, 40, "sparse 40 blocks of 701 (161 entries, 1 split)" },
        { 1024, 701, 64, 4, 256, 4.0f, 1, 120, "sparse 120 blocks of 702, sharp q (482 entries, 2 splits)" },
        { 1024, 1000, 64, 2, 256, 1.0f, 1, 150, "sparse 150 blocks of 1001 (601 entries, 3 splits)" },
        { 1024, 1000, 64, 4, 512, 1.0f, 1, 150, "sparse 150 blocks of 1001, per_split 512" },
        { 1024, 999, 64, 4, 256, 1.0f, 2, 100, "sparse, new token hidden (its block complete, not chosen)" },
        { 1024, 3, 64, 4, 256, 1.0f, 2, 100, "sparse mode 2 at context 4 (nothing chosen: the new token only)" },
        { 1024, 6, 64, 2, 256, 1.0f, 1, 1, "sparse 1 block of 7 plus tail (7 entries)" },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        int err = sim_point(&pts[i], 0, 100 + (uint32_t)i, msg, sizeof msg, &worst, &bad, rows, sizeof rows);
        CHECK(err == 0 && bad == 0, "sim %s depth %u: %s", pts[i].name, pts[i].depth, msg);
        printf("sim %-66s depth %u: %s %s\n", pts[i].name, pts[i].depth, err ? "ERROR" : bad ? "MISMATCH" : "ok", msg);
    }
    const Pt mp = { 1024, 701, 64, 2, 256, 1.0f, 1, 120, "mutant point" };
    for (int m = 1; m <= 8; m++) {
        int err = sim_point(&mp, m, 200, msg, sizeof msg, &worst, &bad, rows, sizeof rows);
        int caught = err != 0 || bad > 0;
        CHECK(caught, "sim mutant %d NOT caught (%s)", m, msg);
        printf("sim mutant %d at sparse 120 blocks of 702: %s (%s)\n", m, caught ? "caught" : "MISSED", msg);
    }
    printf("sim: %" PRIu64 " warp issues in total\n", g_sim_issues);
}

/* ------------------------------------------------------------- chip */
static int g_evict = 1; static const char *g_only; static uint32_t g_maxp = 32768;
static void evict_host_caches(void) {
    static uint8_t *buf; const size_t sz = 512u << 20; static uint8_t pass;
    if (!buf) buf = malloc(sz);
    if (!buf) return;
    memset(buf, ++pass, sz);
    volatile uint64_t sum = 0; for (size_t i = 0; i < sz; i += 64) sum += buf[i];
}
static int fill_history(OmegaGpuFnAttnCache *cache, const Case *c, const uint16_t *kcp) {
    if (c->pos == 0) return OMEGA_GPU_DATTN_OK;
    for (unsigned g = 0; g < KVH; g++) {
        int rc = omega_gpu_fnattn_cache_write(cache, 0, g, 0, c->pos, kcp + (size_t)g * c->max_pos * HD, c->vc + (size_t)g * c->max_pos * HD);
        if (rc != OMEGA_GPU_DATTN_OK) return rc;
    }
    return OMEGA_GPU_DATTN_OK;
}
static int chip_run(const char *out_path, uint32_t reps) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out_path && !out) { printf("cannot write %s\n", out_path); return 1; }
    if (out) fprintf(out, "{\"receipt\":\"OMEGA_GPU_FNATTN_V1\",\"reps\":%u,\"evict\":%d,\"max_pos\":%u,\"rotary_dim\":64,\"points\":[", reps, g_evict, g_maxp);
    const uint32_t MAXP = g_maxp;
    OmegaGpuFnAttnCache *cache = NULL;
    double t0 = now_ms();
    int rc = omega_gpu_fnattn_cache_alloc(1, MAXP, THETA, 64, &cache);
    printf("cache alloc (1 layer, %u positions, %.0f MiB): rc=%s %.1f ms (%s)\n", MAXP, (double)MAXP * 2 * KVH * 512 / 1048576.0, omega_gpu_decode_attn_rc_name(rc), now_ms() - t0, rc ? omega_gpu_decode_attn_last_error() : "");
    CHECK(rc == OMEGA_GPU_DATTN_OK, "cache alloc");
    if (rc != OMEGA_GPU_DATTN_OK) { if (out) fprintf(out, "],\"verdict\":\"OMEGA_GPU_FNATTN_FAIL\"}\n"); return 1; }
    const Pt pts[] = {
        { 0, 0, 64, 0, 0, 1.0f, 0, 0, "context 1" }, { 0, 6, 64, 0, 0, 1.0f, 0, 0, "dense context 7" }, { 0, 255, 64, 0, 0, 1.0f, 0, 0, "dense context 256" },
        { 0, 300, 64, 0, 0, 4.0f, 0, 0, "dense context 301 sharp q" }, { 0, 1023, 64, 0, 0, 1.0f, 0, 0, "dense context 1024" }, { 0, 2050, 64, 0, 0, 1.0f, 0, 0, "dense context 2051" },
        { 0, 4095, 64, 0, 0, 1.0f, 1, 512, "sparse context 4096 (512 blocks)" }, { 0, 4098, 64, 0, 0, 1.0f, 1, 512, "sparse context 4099 (512 blocks + 3 tail)" },
        { 0, 4099, 64, 0, 0, 1.0f, 2, 400, "sparse context 4100, new token hidden" },
        { 0, 8191, 64, 0, 0, 1.0f, 1, 512, "sparse context 8192" }, { 0, 32767, 64, 0, 0, 1.0f, 1, 512, "sparse context 32768" }, { 0, 131071, 64, 0, 0, 1.0f, 1, 512, "sparse context 131072" },
    };
    int first = 1;
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        if (pts[i].pos >= MAXP) continue;
        if (g_only && !strstr(pts[i].name, g_only)) continue;
        Case c; case_make(&c, MAXP, pts[i].pos, 64, 500 + (uint32_t)i, pts[i].qscale, pts[i].mode, pts[i].topk);
        uint16_t *kcp = case_kc_perm(&c);
        float *qkv_k = malloc(QW * 4), qn_k[HD], kn_k[HD];
        case_kernel_inputs(&c, qkv_k, qn_k, kn_k);
        CHECK(omega_gpu_fnattn_set_norms(cache, 0, qn_k, kn_k) == OMEGA_GPU_DATTN_OK, "set norms");
        t0 = now_ms();
        rc = fill_history(cache, &c, kcp);
        double fill_ms = now_ms() - t0;
        float *y = malloc(OW * 4), *y2 = malloc(OW * 4);
        OmegaGpuFnAttnInfo info; memset(&info, 0, sizeof info);
        if (rc == OMEGA_GPU_DATTN_OK) rc = omega_gpu_fnattn(cache, 0, c.pos, qkv_k, c.plist, c.count, EPS, 1.0f, y, &info);
        size_t bad = 0; double worst = 1e9; uint16_t kn[KVH * HD], vn[KVH * HD], kchip[KVH * HD], vchip[KVH * HD]; char rows[160] = "n/a";
        if (rc == OMEGA_GPU_DATTN_OK) {
            bad = parity(&c, y, 1.0f, &worst, kn, vn);
            for (unsigned g = 0; g < KVH; g++) if (omega_gpu_fnattn_cache_read(cache, 0, g, c.pos, kchip + g * HD, vchip + g * HD) != OMEGA_GPU_DATTN_OK) bad += 1000;
            if (!rows_ok(kchip, vchip, kn, vn, 64, rows, sizeof rows)) bad += 1000;
        }
        int ok = rc == OMEGA_GPU_DATTN_OK && bad == 0;
        int same = 0; OmegaGpuFnAttnInfo i2; memset(&i2, 0, sizeof i2);
        if (ok) { int r2 = omega_gpu_fnattn(cache, 0, c.pos, qkv_k, c.plist, c.count, EPS, 1.0f, y2, &i2); same = r2 == OMEGA_GPU_DATTN_OK && memcmp(y, y2, OW * 4) == 0 && i2.kernel_cache_hit; }
        CHECK(ok && same, "chip %s: rc=%s bad=%zu worst=%.3f rows=%s second-call-identical=%d (%s)", pts[i].name, omega_gpu_decode_attn_rc_name(rc), bad, worst, rows, same, omega_gpu_decode_attn_last_error());
        if (ok) {
            float *ys = malloc(OW * 4); int r3 = omega_gpu_fnattn(cache, 0, c.pos, qkv_k, c.plist, c.count, EPS, 18446744073709551616.0f, ys, NULL);
            int good = r3 == OMEGA_GPU_DATTN_OK; double wr = 0;
            for (unsigned k = 0; k < OW && good; k++) { double e = fabs((double)ys[k] / 18446744073709551616.0 - (double)y[k]); double r = e / (fabs((double)y[k]) + 1e-6); if (r > wr) wr = r; }
            CHECK(good && wr < 1e-5, "chip %s: out_scale 2^64 matches the unscaled output (worst rel %.2e)", pts[i].name, wr);
            free(ys);
        }
        uint64_t *el = calloc(reps, sizeof *el), *cl = calloc(reps, sizeof *cl); uint32_t done = 0;
        if (g_evict) evict_host_caches();
        for (uint32_t r = 0; r < reps && ok; r++) { OmegaGpuFnAttnInfo ti; if (omega_gpu_fnattn(cache, 0, c.pos, qkv_k, c.plist, c.count, EPS, 1.0f, y2, &ti) != OMEGA_GPU_DATTN_OK) break; el[r] = ti.elapsed_ns; cl[r] = ti.call_ns; done++; }
        uint64_t med_el = 0, med_cl = 0; double gbs = 0;
        if (done) { qsort(el, done, sizeof *el, cmp_u64); qsort(cl, done, sizeof *cl, cmp_u64); med_el = el[done / 2]; med_cl = cl[done / 2]; gbs = med_el ? (double)info.kv_bytes / (double)med_el : 0; }
        printf("chip %-44s rc=%-9s bad=%zu worst=%.3f rows=%s count=%u grid=%ux%u kv_bytes=%" PRIu64 " fill=%.1fms chip_med=%.1fus (%.1f GB/s) call_med=%.1fus reps=%u\n",
               pts[i].name, omega_gpu_decode_attn_rc_name(rc), bad, worst, rows, c.count, info.grid_x, info.grid_y, info.kv_bytes, fill_ms, med_el / 1e3, gbs, med_cl / 1e3, done);
        if (out) fprintf(out, "%s{\"name\":\"%s\",\"pos\":%u,\"count\":%u,\"rc\":\"%s\",\"bad\":%zu,\"worst_of_tol\":%.3f,\"rows\":\"%s\",\"second_call_identical\":%d,\"grid_x\":%u,\"grid_y\":%u,\"per_split\":%u,\"depth\":%u,"
                         "\"kv_bytes\":%" PRIu64 ",\"fill_ms\":%.1f,\"chip_median_ns\":%" PRIu64 ",\"gb_per_s\":%.1f,\"call_median_ns\":%" PRIu64 ",\"reps\":%u}",
                         first ? "" : ",", pts[i].name, c.pos, c.count, omega_gpu_decode_attn_rc_name(rc), bad, worst, rows, same, info.grid_x, info.grid_y, info.per_split, info.depth, info.kv_bytes, fill_ms, med_el, gbs, med_cl, done);
        first = 0;
        free(el); free(cl);
        if (ok && c.pos == 4098) {
            for (int m = 1; m <= 7; m++) {
                omega_gpu_fnattn_test_set_mutant(m);
                (void)fill_history(cache, &c, kcp); /* mutant 3 must not inherit the row the honest kernel wrote */
                uint16_t zk[HD] = { 0 }, zv[HD] = { 0 };
                for (unsigned g = 0; g < KVH; g++) (void)omega_gpu_fnattn_cache_write(cache, 0, g, c.pos, 1, zk, zv);
                int r = omega_gpu_fnattn(cache, 0, c.pos, qkv_k, c.plist, c.count, EPS, 1.0f, y2, NULL);
                size_t mb = 0; double mw = 0; uint16_t k2[KVH * HD], v2[KVH * HD];
                if (r == OMEGA_GPU_DATTN_OK) { mb = parity(&c, y2, 1.0f, &mw, k2, v2); }
                int caught = r != OMEGA_GPU_DATTN_OK || mb > 0;
                CHECK(caught, "chip mutant %d NOT caught", m);
                printf("chip %-44s mutant %d: %s (rc=%s bad=%zu worst=%.3f)\n", pts[i].name, m, caught ? "caught" : "MISSED", omega_gpu_decode_attn_rc_name(r), mb, mw);
                if (out) fprintf(out, ",{\"name\":\"%s mutant %d\",\"caught\":%d,\"rc\":\"%s\",\"bad\":%zu,\"worst_of_tol\":%.3f}", pts[i].name, m, caught, omega_gpu_decode_attn_rc_name(r), mb, mw);
                omega_gpu_fnattn_test_set_mutant(0);
            }
        }
        free(y); free(y2); free(qkv_k); free(kcp); case_free(&c);
        if (omega_gpu_session_is_blocked()) { printf("session latched: stopping\n"); break; }
    }
    omega_gpu_fnattn_cache_free(cache);
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"device_opens\":%u,\"verdict\":\"%s\"}\n", g_checks, g_failed, omega_gpu_session_open_count(), g_failed == 0 ? "OMEGA_GPU_FNATTN_PASS" : "OMEGA_GPU_FNATTN_FAIL"); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0; const char *out_path = NULL; uint32_t reps = 20, depth = 0, per = 0, spin = 2000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) depth = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--per-split") == 0 && i + 1 < argc) per = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--spin-us") == 0 && i + 1 < argc) spin = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-evict") == 0) g_evict = 0;
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) g_only = argv[++i];
        else if (strcmp(argv[i], "--max-pos") == 0 && i + 1 < argc) g_maxp = (uint32_t)atoi(argv[++i]);
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (host) host_only();
    if (sim) sim_battery();
    if (!host && !sim) {
        if (depth) omega_gpu_fnattn_set_depth(depth);
        if (per) omega_gpu_fnattn_set_per_split(per);
        (void)omega_gpu_session_set_spin_us(spin);
        chip_run(out_path, reps ? reps : 1);
        omega_gpu_session_close();
    }
    printf("gpu_fnattn_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
