/*
 * gpu_q38_graph_test: one Qwen3.8-27B decode token as ONE command list (omega_gpu_q38_graph.h, sc #642).
 *   --host-only   the gather table against the attention API's permutation, and the create / set_layer refusals that
 *                 need no device beyond the session
 *   (default)     chip, caller holds the GB10 lock. A synthetic Qwen3.8-shaped model on random weights: hidden 5120,
 *                 intermediate 17408, vocab 248320; --layers N (2) and --mask M (0x2: bit l = layer l is attention), so by
 *                 default one GDN layer then one attention layer: both kinds and the handover between them in every token.
 *                 Tokens at positions 0, 1, 2:
 *                   A  baseline = the APIs one call at a time (FP8 / bf16 GEMV, GDN step, hd256 attention), host glue in
 *                      C: rmsnorm f64, the gather table, sigmoid gate expf, SwiGLU expf, argmax strict >; its own GDN
 *                      state and KV cache
 *                   C  main = the same token recorded as one list (omega_gpu_q38_graph_token)
 *                   B  the same list submitted one launch per submission (flush_each)
 *                 Checks per token: B == C bit for bit (x, logits, token); A vs C token identical and logits within
 *                 tolerance; x within tolerance. Negative controls: graph mutants 1..4 (SWIGLU, GATHER, GATE, post-norm
 *                 weight) must each fail the A-vs-C comparison.
 *                 omega#569: the same tokens on a lifetime-shared layout (SHARE | VERIFY) must equal C bit for bit; mutant 5
 *                 (xq forced onto mix) must be refused before it runs, mutant 6 (the same, unchecked) must change the values,
 *                 mutant 7 (an undeclared table read) must fail the command-list check. Then --reps timed tokens (median).
 * Verdict: exit 0 when every CHECK passed.
 */
#include "omega_gpu_q38_graph.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_session.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define H OMEGA_GPU_Q38_HIDDEN
#define I OMEGA_GPU_Q38_INTER
#define EPS 1e-6f
#define MAX_POS 256u
#define TOKENS 3u
#define SCALE_2P64 18446744073709551616.0f
#define SCALE_2M64 5.42101086242752217e-20f
/* A vs C: |d| <= TOL_REL * max|ref|. The differences: RMSNorm f32 FFMA + MUFU.RSQ vs f64 host, SwiGLU / gate MUFU EX2 /
 * RCP vs expf (~1e-7 relative each) through 2 layers and 5120- to 17408-term sums; 1e-4 is ~100x that. */
#define TOL_REL 1e-4

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static uint16_t bf16_rne(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t r = x + 0x7fffu + ((x >> 16) & 1u); return (uint16_t)(r >> 16); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median_u64(uint64_t *v, unsigned n) { qsort(v, n, sizeof *v, cmp_u64); return v[n / 2]; }

static void rmsnorm(const float *x, const float *w, float *out) {
    double sq = 0; for (unsigned i = 0; i < H; i++) sq += (double)x[i] * x[i];
    float s = (float)(1.0 / sqrt(sq / H + (double)EPS));
    for (unsigned i = 0; i < H; i++) out[i] = x[i] * s * w[i];
}
static uint32_t argmax(const float *v, uint32_t n) { uint32_t b = 0; for (uint32_t i = 1; i < n; i++) if (v[i] > v[b]) b = i; return b; }
static double maxabs(const float *v, size_t n) { double m = 0; for (size_t i = 0; i < n; i++) if (fabs(v[i]) > m) m = fabs(v[i]); return m; }
static double maxdiff(const float *a, const float *b, size_t n) { double m = 0; for (size_t i = 0; i < n; i++) { double d = fabs((double)a[i] - b[i]); if (d > m || d != d) m = d != d ? 1e30 : d; } return m; }

static int fp8_random(uint32_t seed, uint32_t n, uint32_t k, OmegaGpuFp8Tensor **out) {
    size_t wb = (size_t)n * k, sb = (size_t)(n / 128) * (k / 128);
    uint8_t *w = malloc(wb); uint16_t *s = malloc(sb * 2);
    if (!w || !s) { free(w); free(s); return -1; }
    uint32_t st = seed;
    for (size_t i = 0; i < wb; i += 4) { uint32_t r = lcg(&st); for (unsigned j = 0; j < 4 && i + j < wb; j++) w[i + j] = (uint8_t)((r >> (8 * j)) & 0xbfu); } /* |v| <= 1.875: never a NaN code */
    const float sc = 1.5f / sqrtf((float)k); /* outputs O(1) for O(1) inputs */
    for (size_t i = 0; i < sb; i++) s[i] = bf16_rne(frand(&st, 0.5f * sc, sc));
    int rc = omega_gpu_fp8_tensor_upload(1, n, k, w, s, out);
    free(w); free(s);
    return rc;
}
static int bf16_random(uint32_t seed, uint32_t n, uint32_t k, float lo, float hi, float mul, OmegaGpuBf16Tensor **out) {
    uint16_t *w = malloc((size_t)n * k * 2);
    if (!w) return -1;
    uint32_t st = seed;
    for (size_t i = 0; i < (size_t)n * k; i++) w[i] = bf16_rne(frand(&st, lo, hi) * mul);
    int rc = omega_gpu_bf16_tensor_upload(n, k, w, out);
    free(w);
    return rc;
}

/* ---- the synthetic model ---- */
typedef struct {
    uint32_t layers, vocab, n_gdn, n_attn;
    uint64_t mask;
    struct { OmegaGpuFp8Tensor *mix, *o, *gu, *dn; OmegaGpuBf16Tensor *ab; float in_norm[H], post_norm[H]; } L[8];
    OmegaGpuBf16Tensor *lm;
    float fnorm[H];
    float x0[TOKENS][H];
    /* the two state copies: [0] the graph's, [1] the baseline's */
    OmegaGpuGdn *gdn[2];
    OmegaGpuDecodeAttn256Cache *attn[2];
} Model;

static int state_alloc(Model *m, int which, uint32_t seed) {
    uint32_t s = seed;
    if (omega_gpu_gdn_alloc(m->n_gdn ? m->n_gdn : 1, &m->gdn[which]) != 0) { printf("gdn alloc: %s\n", omega_gpu_gdn_last_error()); return -1; }
    float *conv = malloc((size_t)OMEGA_GPU_Q38_GDN_QKV * 4 * 4), a_log[48], dtb[48], nw[128];
    if (!conv) return -1;
    for (uint32_t l = 0; l < (m->n_gdn ? m->n_gdn : 1); l++) {
        for (size_t i = 0; i < (size_t)OMEGA_GPU_Q38_GDN_QKV * 4; i++) conv[i] = frand(&s, -0.6f, 0.6f);
        for (unsigned h = 0; h < 48; h++) { a_log[h] = frand(&s, -1.5f, 1.5f); dtb[h] = frand(&s, -3.0f, 1.0f); }
        for (unsigned i = 0; i < 128; i++) nw[i] = frand(&s, 0.8f, 1.2f);
        if (omega_gpu_gdn_set_layer(m->gdn[which], l, conv, a_log, dtb, nw) != 0) { printf("gdn set: %s\n", omega_gpu_gdn_last_error()); free(conv); return -1; }
    }
    free(conv);
    if (omega_gpu_decode_attn256_cache_alloc(m->n_attn ? m->n_attn : 1, MAX_POS, 10000000.0, OMEGA_GPU_Q38_ROTARY, &m->attn[which]) != 0) { printf("attn alloc: %s\n", omega_gpu_decode_attn_last_error()); return -1; }
    for (uint32_t l = 0; l < (m->n_attn ? m->n_attn : 1); l++) {
        float qn[256], kn[256], qp[256], kp[256];
        for (unsigned i = 0; i < 256; i++) { qn[i] = frand(&s, 0.8f, 1.2f); kn[i] = frand(&s, 0.8f, 1.2f); }
        omega_dattn256_perm_vec(qn, qp, OMEGA_GPU_Q38_ROTARY); omega_dattn256_perm_vec(kn, kp, OMEGA_GPU_Q38_ROTARY);
        if (omega_gpu_decode_attn256_set_norms(m->attn[which], l, qp, kp) != 0) return -1;
    }
    return 0;
}

static int model_build(Model *m, uint32_t layers, uint64_t mask, uint32_t vocab) {
    memset(m, 0, sizeof *m); m->layers = layers; m->mask = mask; m->vocab = vocab;
    uint32_t s = 4321;
    for (uint32_t l = 0; l < layers; l++) {
        const int attn = (int)((mask >> l) & 1u);
        if (attn) m->n_attn++; else m->n_gdn++;
        if (fp8_random(100 + l, attn ? OMEGA_GPU_Q38_ATT_MIX : OMEGA_GPU_Q38_GDN_MIX, H, &m->L[l].mix) || fp8_random(200 + l, H, OMEGA_GPU_Q38_MIXER_OUT, &m->L[l].o) ||
            fp8_random(300 + l, 2 * I, H, &m->L[l].gu) || fp8_random(400 + l, H, I, &m->L[l].dn)) { printf("fp8 upload: %s\n", omega_gpu_fp8_last_error()); return -1; }
        if (!attn && bf16_random(500 + l, OMEGA_GPU_Q38_AB, H, -0.03f, 0.03f, SCALE_2M64, &m->L[l].ab)) { printf("bf16 upload: %s\n", omega_gpu_bf16_last_error()); return -1; }
        for (unsigned i = 0; i < H; i++) { m->L[l].in_norm[i] = frand(&s, 0.8f, 1.2f); m->L[l].post_norm[i] = frand(&s, 0.8f, 1.2f); }
    }
    if (bf16_random(600, vocab, H, -0.05f, 0.05f, 1.0f, &m->lm)) { printf("lm_head upload: %s\n", omega_gpu_bf16_last_error()); return -1; }
    for (unsigned i = 0; i < H; i++) m->fnorm[i] = frand(&s, 0.8f, 1.2f);
    for (unsigned t = 0; t < TOKENS; t++) for (unsigned i = 0; i < H; i++) m->x0[t][i] = frand(&s, -1.0f, 1.0f);
    if (state_alloc(m, 0, 777) || state_alloc(m, 1, 777)) return -1;
    return 0;
}

static int state_reset(Model *m) {
    /* GDN state to zero; the KV rows at the test's positions are rewritten by every token */
    return omega_gpu_gdn_reset(m->gdn[0]) || omega_gpu_gdn_reset(m->gdn[1]);
}

/* ---- A: the APIs one call at a time ---- */
typedef struct { float x[H], logits[248320]; uint32_t token; } Out;

static int baseline_token(Model *m, const float *x0, uint32_t pos, Out *o) {
    static float xn[H], xs[H], mix[OMEGA_GPU_Q38_GDN_MIX], ab[OMEGA_GPU_Q38_AB], agg[OMEGA_GPU_Q38_ATT_MIX], att[OMEGA_GPU_Q38_MIXER_OUT], y[H], gu[2 * I], act[I];
    static uint32_t tab[OMEGA_GPU_Q38_ATT_MIX];
    omega_q38_gather_table(tab);
    memcpy(o->x, x0, sizeof o->x);
    uint32_t ng = 0, na = 0;
    for (uint32_t l = 0; l < m->layers; l++) {
        rmsnorm(o->x, m->L[l].in_norm, xn);
        { int r_ = omega_gpu_fp8_gemv_f32(m->L[l].mix, 0, xn, mix, NULL); if (r_ != 0) { printf("fp8 gemv rc %d: %s\n", r_, omega_gpu_fp8_last_error()); return -1; } }
        if (!((m->mask >> l) & 1u)) {
            for (unsigned i = 0; i < H; i++) xs[i] = xn[i] * SCALE_2P64; /* the a|b rows are uploaded * 2^-64 */
            { int r_ = omega_gpu_bf16_gemv_f32(m->L[l].ab, xs, ab, NULL); if (r_ != 0) { printf("bf16 gemv rc %d: %s\n", r_, omega_gpu_bf16_last_error()); return -1; } }
            if (omega_gpu_gdn_step(m->gdn[1], ng++, mix, mix + OMEGA_GPU_Q38_GDN_QKV, ab, EPS, EPS, 1.0f, 1.0f, att, NULL) != 0) { printf("gdn step: %s\n", omega_gpu_gdn_last_error()); return -1; }
        } else {
            for (unsigned n = 0; n < OMEGA_GPU_Q38_ATT_MIX; n++) agg[n] = mix[tab[n]];
            if (omega_gpu_decode_attn256(m->attn[1], na++, pos, agg, EPS, 1.0f, att, NULL) != 0) { printf("attn: %s\n", omega_gpu_decode_attn_last_error()); return -1; }
            for (unsigned n = 0; n < OMEGA_GPU_Q38_MIXER_OUT; n++) att[n] = att[n] * (1.0f / (1.0f + expf(-agg[OMEGA_GPU_Q38_ATT_QKV + n])));
        }
        { int r_ = omega_gpu_fp8_gemv_f32(m->L[l].o, 0, att, y, NULL); if (r_ != 0) { printf("fp8 gemv rc %d: %s\n", r_, omega_gpu_fp8_last_error()); return -1; } }
        for (unsigned i = 0; i < H; i++) o->x[i] = o->x[i] + y[i];
        rmsnorm(o->x, m->L[l].post_norm, xn);
        { int r_ = omega_gpu_fp8_gemv_f32(m->L[l].gu, 0, xn, gu, NULL); if (r_ != 0) { printf("fp8 gemv rc %d: %s\n", r_, omega_gpu_fp8_last_error()); return -1; } }
        for (unsigned j = 0; j < I; j++) { float v = gu[j]; act[j] = v / (1.0f + expf(-v)) * gu[I + j]; }
        { int r_ = omega_gpu_fp8_gemv_f32(m->L[l].dn, 0, act, y, NULL); if (r_ != 0) { printf("fp8 gemv rc %d: %s\n", r_, omega_gpu_fp8_last_error()); return -1; } }
        for (unsigned i = 0; i < H; i++) o->x[i] = o->x[i] + y[i];
    }
    rmsnorm(o->x, m->fnorm, xn);
    { int r_ = omega_gpu_bf16_gemv_f32(m->lm, xn, o->logits, NULL); if (r_ != 0) { printf("bf16 gemv rc %d: %s\n", r_, omega_gpu_bf16_last_error()); return -1; } }
    o->token = argmax(o->logits, m->vocab);
    return 0;
}

static int graph_build(Model *m, uint32_t flags, OmegaGpuQ38Graph **out) {
    OmegaGpuQ38GraphConfig c; memset(&c, 0, sizeof c);
    c.layers = m->layers; c.attn_mask = m->mask; c.vocab = m->vocab; c.eps = EPS; c.eps_l2 = EPS;
    c.gdn = m->gdn[0]; c.attn = m->attn[0]; c.lm_head = m->lm;
    int rc = omega_gpu_q38_graph_create_ex(&c, flags, out);
    if (rc != 0) { printf("graph create %d: %s\n", rc, omega_gpu_session_last_error()); return -1; }
    for (uint32_t l = 0; l < m->layers; l++)
        if ((rc = omega_gpu_q38_graph_set_layer(*out, l, m->L[l].mix, m->L[l].ab, m->L[l].o, m->L[l].gu, m->L[l].dn, m->L[l].in_norm, m->L[l].post_norm)) != 0) { printf("set_layer %u: %d\n", l, rc); return -1; }
    if (omega_gpu_q38_graph_set_final_norm(*out, m->fnorm) != 0) return -1;
    return 0;
}

static int graph_token(OmegaGpuQ38Graph *g, Model *m, const float *x0, uint32_t pos, Out *o, OmegaGpuDecodeGraphStats *st) {
    int rc = omega_gpu_q38_graph_token(g, x0, pos, &o->token, st);
    if (rc != 0) { printf("graph token rc %d: %s\n", rc, omega_gpu_session_last_error()); return -1; }
    (void)m;
    return omega_gpu_q38_graph_read(g, o->x, NULL, o->logits) != 0 ? -1 : 0;
}

static int host_only(void) {
    static uint32_t tab[OMEGA_GPU_Q38_ATT_MIX];
    omega_q38_gather_table(tab);
    int bad = 0;
    for (uint32_t h = 0; h < 24; h++) for (uint32_t i = 0; i < 256; i++) if (tab[h * 256 + i] != h * 512 + omega_dattn256_perm_slot(i, 64)) bad++;
    for (uint32_t h = 0; h < 4; h++) for (uint32_t i = 0; i < 256; i++) if (tab[6144 + h * 256 + i] != 12288 + h * 256 + omega_dattn256_perm_slot(i, 64)) bad++;
    for (uint32_t i = 0; i < 1024; i++) if (tab[7168 + i] != 13312 + i) bad++;
    for (uint32_t h = 0; h < 24; h++) for (uint32_t d = 0; d < 256; d++) if (tab[8192 + h * 256 + d] != h * 512 + 256 + d) bad++;
    CHECK(bad == 0, "gather table: q, k permuted, v and gate in place (%d wrong)", bad);
    static uint8_t seen[OMEGA_GPU_Q38_ATT_MIX];
    int dup = 0; for (uint32_t n = 0; n < OMEGA_GPU_Q38_ATT_MIX; n++) { if (tab[n] >= OMEGA_GPU_Q38_ATT_MIX || seen[tab[n]]) dup++; else seen[tab[n]] = 1; }
    CHECK(dup == 0, "gather table is a permutation of the 14336 mix values (%d repeats or out of range)", dup);
    OmegaGpuQ38Graph *g = NULL;
    OmegaGpuQ38GraphConfig c; memset(&c, 0, sizeof c);
    CHECK(omega_gpu_q38_graph_create(&c, &g) == OMEGA_GPU_DGRAPH_BAD_ARGS && !g, "create refuses an empty config");
    CHECK(omega_gpu_q38_graph_create(NULL, &g) == OMEGA_GPU_DGRAPH_BAD_ARGS, "create refuses NULL");
    return g_failed ? 1 : 0;
}

static int chip_run(uint32_t layers, uint64_t mask, uint32_t reps) {
    Model m;
    const uint32_t vocab = 248320u;
    if (model_build(&m, layers, mask, vocab) != 0) { CHECK(0, "model build"); return 1; }
    printf("model: %u layers (%u GDN, %u attention, mask 0x%" PRIx64 "), vocab %u\n", layers, m.n_gdn, m.n_attn, mask, vocab);
    static Out A[TOKENS], C[TOKENS], B, M;
    /* A first: it also warms every kernel the record paths need */
    for (uint32_t t = 0; t < TOKENS; t++) if (baseline_token(&m, m.x0[t], t, &A[t]) != 0) { CHECK(0, "baseline token %u", t); return 1; }
    OmegaGpuQ38Graph *g = NULL;
    if (graph_build(&m, 0, &g) != 0) { CHECK(0, "graph build"); return 1; }
    OmegaGpuDecodeGraphStats st;
    for (uint32_t t = 0; t < TOKENS; t++) {
        if (graph_token(g, &m, m.x0[t], t, &C[t], &st) != 0) { CHECK(0, "graph token %u", t); return 1; }
        const double ref = maxabs(A[t].logits, vocab), dl = maxdiff(A[t].logits, C[t].logits, vocab);
        const double xref = maxabs(A[t].x, H), dx = maxdiff(A[t].x, C[t].x, H);
        CHECK(C[t].token == A[t].token, "token %u: graph %u == baseline %u", t, C[t].token, A[t].token);
        CHECK(dl <= TOL_REL * ref, "token %u: logits max diff %.3g <= %.0e * %.3g", t, dl, TOL_REL, ref);
        CHECK(dx <= TOL_REL * xref, "token %u: x max diff %.3g <= %.0e * %.3g", t, dx, TOL_REL, xref);
        CHECK(st.launches == m.n_gdn * 9u + m.n_attn * 10u + 4u && st.submits == 1u, "token %u: %u launches in %u submit", t, st.launches, st.submits);
    }
    /* B: flush_each over the same three tokens from a reset state, bit-identical to C */
    if (state_reset(&m) != 0) { CHECK(0, "state reset"); return 1; }
    omega_gpu_q38_graph_set_flush_each(g, 1);
    int bsame = 1;
    for (uint32_t t = 0; t < TOKENS; t++) {
        if (graph_token(g, &m, m.x0[t], t, &B, &st) != 0) { CHECK(0, "flush_each token %u", t); return 1; }
        if (memcmp(B.x, C[t].x, sizeof B.x) || memcmp(B.logits, C[t].logits, vocab * 4u) || B.token != C[t].token) bsame = 0;
    }
    omega_gpu_q38_graph_set_flush_each(g, 0);
    CHECK(bsame, "flush_each == one list, bit for bit, all %u tokens", TOKENS);
    /* omega#569: the lifetime-shared layout (SHARE | VERIFY) over the same three tokens from a reset state: bit-identical
     * to C; then its negative controls */
    {
        OmegaGpuQ38Graph *s = NULL;
        if (graph_build(&m, OMEGA_GPU_Q38_SHARE | OMEGA_GPU_Q38_VERIFY, &s) != 0) { CHECK(0, "shared graph build"); return 1; }
        OmegaGpuQ38Plan p;
        CHECK(omega_gpu_q38_graph_get_plan(s, &p) == 0 && p.shared_bytes < p.flat_bytes, "shared plan smaller");
        printf("shared layout: buffer %" PRIu64 " B (flat %" PRIu64 " B, saved %" PRIu64 " B, peak live %" PRIu64 " B)\n", p.shared_bytes, p.flat_bytes,
               p.flat_bytes - p.shared_bytes, p.peak_live);
        if (state_reset(&m) != 0) { CHECK(0, "state reset"); return 1; }
        int same = 1;
        for (uint32_t t = 0; t < TOKENS; t++) {
            if (graph_token(s, &m, m.x0[t], t, &B, &st) != 0) { CHECK(0, "shared token %u", t); return 1; }
            if (memcmp(B.x, C[t].x, sizeof B.x) || memcmp(B.logits, C[t].logits, vocab * 4u) || B.token != C[t].token) same = 0;
            printf("shared token %u: %u (unshared %u)\n", t, B.token, C[t].token);
        }
        CHECK(same, "shared layout == unshared layout, bit for bit (x, logits, token), all %u tokens", TOKENS);
        static float rb[2 * I];
        CHECK(omega_gpu_q38_graph_read_buf(s, OMEGA_GPU_Q38_BUF_X, rb, H) == 0, "shared: x still readable");
        CHECK(omega_gpu_q38_graph_read_buf(s, OMEGA_GPU_Q38_BUF_MIX, rb, 16) != 0, "shared: mix (bytes reused by the logits) refused");
        /* 5: xq on mix's bytes, the check on: the token must be refused before it runs */
        if (state_reset(&m) != 0) return 1;
        omega_gpu_q38_graph_test_set_mutant(s, 5);
        int r5 = omega_gpu_q38_graph_token(s, m.x0[0], 0, &B.token, &st);
        printf("negative control 5 (xq forced onto mix, check on): rc %d: %s\n", r5, omega_gpu_session_last_error());
        CHECK(r5 != 0 && strstr(omega_gpu_session_last_error(), "unsafe") != NULL, "mutant 5 refused by the pre-submit check");
        /* 6: the same layout run unchecked: the values must differ from C (the check is what keeps them right) */
        if (state_reset(&m) != 0) return 1;
        omega_gpu_q38_graph_test_set_mutant(s, 6);
        int differs = 0;
        for (uint32_t t = 0; t < TOKENS; t++) {
            if (graph_token(s, &m, m.x0[t], t, &M, &st) != 0) { differs = 1; break; }
            if (memcmp(M.x, C[t].x, sizeof M.x) || memcmp(M.logits, C[t].logits, vocab * 4u) || M.token != C[t].token) differs = 1;
        }
        printf("negative control 6 (xq forced onto mix, unchecked): values %s\n", differs ? "DIFFER (red, as required)" : "identical");
        CHECK(differs, "mutant 6: the unsafe layout changes the values");
        /* 7: the gather op stops declaring its table read: VERIFY must find the launch pointing at an undeclared region */
        if (state_reset(&m) != 0) return 1;
        omega_gpu_q38_graph_test_set_mutant(s, 7);
        int r7 = omega_gpu_q38_graph_token(s, m.x0[0], 0, &B.token, &st);
        printf("negative control 7 (gather table read undeclared): rc %d: %s\n", r7, omega_gpu_session_last_error());
        CHECK(m.n_attn == 0 || (r7 != 0 && strstr(omega_gpu_session_last_error(), "verify") != NULL), "mutant 7 caught by the list check");
        /* back to the plan: identical again */
        omega_gpu_q38_graph_test_set_mutant(s, 0);
        if (state_reset(&m) != 0) return 1;
        same = 1;
        for (uint32_t t = 0; t < TOKENS; t++) {
            if (graph_token(s, &m, m.x0[t], t, &B, &st) != 0) { CHECK(0, "shared token %u after the mutants", t); return 1; }
            if (memcmp(B.x, C[t].x, sizeof B.x) || memcmp(B.logits, C[t].logits, vocab * 4u) || B.token != C[t].token) same = 0;
        }
        CHECK(same, "shared layout after the mutants == unshared, bit for bit");
        omega_gpu_q38_graph_free(s);
    }
    /* negative controls: each graph mutant must fail the A comparison on some token */
    const char *mname[5] = { "", "swiglu up half -1", "gather index +1", "gate dropped", "post-norm weight dropped" };
    for (int mu = 1; mu <= 4; mu++) {
        if (state_reset(&m) != 0) return 1;
        omega_gpu_q38_graph_test_set_mutant(g, mu);
        int caught = 0;
        for (uint32_t t = 0; t < TOKENS; t++) {
            if (graph_token(g, &m, m.x0[t], t, &M, &st) != 0) { caught = 1; break; }
            if (maxdiff(A[t].x, M.x, H) > TOL_REL * maxabs(A[t].x, H)) caught = 1;
        }
        CHECK(caught, "mutant %d (%s) caught", mu, mname[mu]);
    }
    omega_gpu_q38_graph_test_set_mutant(g, 0);
    /* timing: reps tokens at position 2 over the reset state (the time, not the values, is measured here) */
    uint64_t *chip = calloc(reps, 8), *tot = calloc(reps, 8);
    if (!chip || !tot) return 1;
    for (uint32_t r = 0; r < reps; r++) {
        if (state_reset(&m) != 0 || graph_token(g, &m, m.x0[0], 2, &M, &st) != 0) { CHECK(0, "timed token %u", r); return 1; }
        chip[r] = st.chip_ns; tot[r] = st.total_ns;
    }
    printf("timing: %u reps, median chip %.1f us, median total %.1f us per token (%u layers)\n", reps, median_u64(chip, reps) / 1e3, median_u64(tot, reps) / 1e3, layers);
    free(chip); free(tot);
    /* timeline: one stamped token, GPU time per launch kind */
    {
        static uint64_t stp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1]; static uint8_t kd[OMEGA_GPU_CMDLIST_MAX_LAUNCHES];
        omega_gpu_q38_graph_set_stamps(g, 1);
        if (state_reset(&m) != 0 || graph_token(g, &m, m.x0[0], 2, &M, &st) != 0) { CHECK(0, "stamped token"); return 1; }
        int n = omega_gpu_q38_graph_stamps(g, stp, kd, OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1);
        CHECK(n == (int)st.launches, "timeline: %d stamped launches", n);
        uint64_t ns[OMEGA_GPU_Q38_K_COUNT] = { 0 }; uint32_t cnt[OMEGA_GPU_Q38_K_COUNT] = { 0 };
        for (int i = 0; i < n; i++) { ns[kd[i]] += stp[i + 1] - stp[i]; cnt[kd[i]]++; }
        for (unsigned k = 0; k < OMEGA_GPU_Q38_K_COUNT; k++)
            if (cnt[k]) printf("timeline %-8s %3u launches %9.1f us\n", omega_gpu_q38_graph_kind_name((uint8_t)k), cnt[k], ns[k] / 1e3);
        if (n > 0) printf("timeline span %.1f us\n", (stp[n] - stp[0]) / 1e3);
        omega_gpu_q38_graph_set_stamps(g, 0);
    }
    omega_gpu_q38_graph_free(g);
    return g_failed ? 1 : 0;
}

int main(int argc, char **argv) {
    int host = 0; uint32_t layers = 2, reps = 10; uint64_t mask = 0x2;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--layers") == 0 && i + 1 < argc) layers = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--mask") == 0 && i + 1 < argc) mask = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else { printf("usage: %s [--host-only] [--layers N (<= 8)] [--mask M] [--reps R]\n", argv[0]); return 2; }
    }
    if (layers == 0 || layers > 8 || reps == 0) { printf("layers 1..8, reps >= 1\n"); return 2; }
    int rc = host ? host_only() : chip_run(layers, mask, reps);
    printf("gpu_q38_graph_test%s: %d checks, %d failed: %s\n", host ? " (host-only)" : "", g_checks, g_failed, rc ? "FAIL" : "PASS");
    return rc;
}
