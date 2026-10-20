/*
 * gpu_q38_channels_test: two Qwen3.8-27B decode streams on one GPU, one after the other on one channel versus both in
 * flight on two channels (L136, roadblock R3, OSC-0 decision 12). omega_gpu_q38_graph_token_pair, omega_gpu_cmdlist_submit_pair.
 *   (default) chip, caller holds the GB10 lock, AIEN_OMEGA_GPU_CHANNELS=2. A synthetic Qwen3.8-shaped model on random
 *             weights at the real shapes (hidden 5120, intermediate 17408, vocab 248320, bf16 lm_head): --layers N (64)
 *             with every fourth layer full attention (--mask, default 0x8888888888888888), the weights uploaded once and
 *             shared by four graphs, each graph with its own GDN states and attention cache:
 *               S_A, S_B  serial: token of A, then token of B, each its own submission on queue 0 (today's path)
 *               P_A, P_B  pair:   one token of A on queue 0 and one of B on queue 1, both in flight at once
 *             --steps T (64) decode steps; the input of step t of stream k is drawn from a seed of (k, t, the token
 *             stream k produced at step t - 1), so a wrong token changes everything after it.
 *             Gates: G2  every step's token of P_A == S_A and P_B == S_B, and after the last step x and logits bit for
 *                        bit (memcmp); negative control: A and B differ (the comparison can fail);
 *                    G3  the timing: per step, the serial pair of tokens and the paired tokens are run back to back
 *                        (interleaved, so drift hits both), wall time per step; medians, aggregate tok/s, the ratio.
 *   --refusal  the same model, AIEN_OMEGA_GPU_CHANNELS unset: the pair must be refused with the stage named and the
 *             serial path must still decode (the single-channel contract is unchanged).
 * Verdict: exit 0 when every CHECK passed. Timing lines start "timing:".
 */
#include "omega_gpu_q38_graph.h"
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
#define SCALE_2M64 5.42101086242752217e-20f
#define VOCAB 248320u
#define MAX_STEPS 256u

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static uint16_t bf16_rne(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t r = x + 0x7fffu + ((x >> 16) & 1u); return (uint16_t)(r >> 16); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median_u64(uint64_t *v, unsigned n) { qsort(v, n, sizeof *v, cmp_u64); return v[n / 2]; }

/* the same generators as gpu_q38_graph_test.c */
static int fp8_random(uint32_t seed, uint32_t n, uint32_t k, OmegaGpuFp8Tensor **out) {
    size_t wb = (size_t)n * k, sb = (size_t)(n / 128) * (k / 128);
    uint8_t *w = malloc(wb); uint16_t *s = malloc(sb * 2);
    if (!w || !s) { free(w); free(s); return -1; }
    uint32_t st = seed;
    for (size_t i = 0; i < wb; i += 4) { uint32_t r = lcg(&st); for (unsigned j = 0; j < 4 && i + j < wb; j++) w[i + j] = (uint8_t)((r >> (8 * j)) & 0xbfu); } /* |v| <= 1.875: never a NaN code */
    const float sc = 1.5f / sqrtf((float)k);
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

typedef struct {
    uint32_t layers, n_gdn, n_attn;
    uint64_t mask;
    struct { OmegaGpuFp8Tensor *mix, *o, *gu, *dn; OmegaGpuBf16Tensor *ab; float in_norm[H], post_norm[H]; } L[OMEGA_GPU_Q38_MAX_LAYERS];
    OmegaGpuBf16Tensor *lm;
    float fnorm[H];
} Model;

/* one state set: GDN states + attention cache, the same seed for every set so all four graphs start equal */
typedef struct { OmegaGpuGdn *gdn; OmegaGpuDecodeAttn256Cache *attn; } State;

static int state_alloc(const Model *m, State *st) {
    uint32_t s = 777;
    if (omega_gpu_gdn_alloc(m->n_gdn ? m->n_gdn : 1, &st->gdn) != 0) { printf("gdn alloc: %s\n", omega_gpu_gdn_last_error()); return -1; }
    float *conv = malloc((size_t)OMEGA_GPU_Q38_GDN_QKV * 4 * 4), a_log[48], dtb[48], nw[128];
    if (!conv) return -1;
    for (uint32_t l = 0; l < (m->n_gdn ? m->n_gdn : 1); l++) {
        for (size_t i = 0; i < (size_t)OMEGA_GPU_Q38_GDN_QKV * 4; i++) conv[i] = frand(&s, -0.6f, 0.6f);
        for (unsigned h = 0; h < 48; h++) { a_log[h] = frand(&s, -1.5f, 1.5f); dtb[h] = frand(&s, -3.0f, 1.0f); }
        for (unsigned i = 0; i < 128; i++) nw[i] = frand(&s, 0.8f, 1.2f);
        if (omega_gpu_gdn_set_layer(st->gdn, l, conv, a_log, dtb, nw) != 0) { printf("gdn set: %s\n", omega_gpu_gdn_last_error()); free(conv); return -1; }
    }
    free(conv);
    if (omega_gpu_decode_attn256_cache_alloc(m->n_attn ? m->n_attn : 1, MAX_POS, 10000000.0, OMEGA_GPU_Q38_ROTARY, &st->attn) != 0) { printf("attn alloc: %s\n", omega_gpu_decode_attn_last_error()); return -1; }
    for (uint32_t l = 0; l < (m->n_attn ? m->n_attn : 1); l++) {
        float qn[256], kn[256], qp[256], kp[256];
        for (unsigned i = 0; i < 256; i++) { qn[i] = frand(&s, 0.8f, 1.2f); kn[i] = frand(&s, 0.8f, 1.2f); }
        omega_dattn256_perm_vec(qn, qp, OMEGA_GPU_Q38_ROTARY); omega_dattn256_perm_vec(kn, kp, OMEGA_GPU_Q38_ROTARY);
        if (omega_gpu_decode_attn256_set_norms(st->attn, l, qp, kp) != 0) return -1;
    }
    return 0;
}

static int model_build(Model *m, uint32_t layers, uint64_t mask) {
    memset(m, 0, sizeof *m); m->layers = layers; m->mask = mask;
    uint32_t s = 4321;
    for (uint32_t l = 0; l < layers; l++) {
        const int attn = (int)((mask >> l) & 1u);
        if (attn) m->n_attn++; else m->n_gdn++;
        if (fp8_random(100 + l, attn ? OMEGA_GPU_Q38_ATT_MIX : OMEGA_GPU_Q38_GDN_MIX, H, &m->L[l].mix) || fp8_random(200 + l, H, OMEGA_GPU_Q38_MIXER_OUT, &m->L[l].o) ||
            fp8_random(300 + l, 2 * I, H, &m->L[l].gu) || fp8_random(400 + l, H, I, &m->L[l].dn)) { printf("fp8 upload: %s\n", omega_gpu_fp8_last_error()); return -1; }
        if (!attn && bf16_random(500 + l, OMEGA_GPU_Q38_AB, H, -0.03f, 0.03f, SCALE_2M64, &m->L[l].ab)) { printf("bf16 upload: %s\n", omega_gpu_bf16_last_error()); return -1; }
        for (unsigned i = 0; i < H; i++) { m->L[l].in_norm[i] = frand(&s, 0.8f, 1.2f); m->L[l].post_norm[i] = frand(&s, 0.8f, 1.2f); }
    }
    if (bf16_random(600, VOCAB, H, -0.05f, 0.05f, 1.0f, &m->lm)) { printf("lm_head upload: %s\n", omega_gpu_bf16_last_error()); return -1; }
    for (unsigned i = 0; i < H; i++) m->fnorm[i] = frand(&s, 0.8f, 1.2f);
    return 0;
}

static int graph_build(const Model *m, const State *st, OmegaGpuQ38Graph **out) {
    OmegaGpuQ38GraphConfig c; memset(&c, 0, sizeof c);
    c.layers = m->layers; c.attn_mask = m->mask; c.vocab = VOCAB; c.eps = EPS; c.eps_l2 = EPS;
    c.gdn = st->gdn; c.attn = st->attn; c.lm_head = m->lm;
    int rc = omega_gpu_q38_graph_create(&c, out);
    if (rc != 0) { printf("graph create %d: %s\n", rc, omega_gpu_session_last_error()); return -1; }
    for (uint32_t l = 0; l < m->layers; l++)
        if ((rc = omega_gpu_q38_graph_set_layer(*out, l, m->L[l].mix, m->L[l].ab, m->L[l].o, m->L[l].gu, m->L[l].dn, m->L[l].in_norm, m->L[l].post_norm)) != 0) { printf("set_layer %u: %d\n", l, rc); return -1; }
    return omega_gpu_q38_graph_set_final_norm(*out, m->fnorm) != 0 ? -1 : 0;
}

/* the input of step t of stream k: a function of (k, t, the stream's previous token) */
static void step_input(uint32_t k, uint32_t t, uint32_t prev, float *x) {
    uint32_t s = 0x9e3779b9u ^ (k * 0x85ebca6bu) ^ (t * 0xc2b2ae35u) ^ (prev * 0x27d4eb2fu);
    for (unsigned i = 0; i < H; i++) x[i] = frand(&s, -1.0f, 1.0f);
}

static uint64_t now_ns(void) { return omega_gpu_session_now_ns(); }

int main(int argc, char **argv) {
    uint32_t layers = 64, steps = 64;
    uint64_t mask = 0x8888888888888888ull;
    int refusal = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--mask") && i + 1 < argc) mask = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--refusal")) refusal = 1;
        else { printf("usage: %s [--layers N] [--steps T] [--mask M] [--refusal]\n", argv[0]); return 2; }
    }
    if (layers == 0 || layers > OMEGA_GPU_Q38_MAX_LAYERS || steps == 0 || steps > MAX_STEPS || steps > MAX_POS) { printf("bad --layers / --steps\n"); return 2; }
    if (layers < 64) mask &= (1ull << layers) - 1u;
    const char *env = getenv("AIEN_OMEGA_GPU_CHANNELS");
    printf("gpu_q38_channels_test: layers %u mask 0x%" PRIx64 " steps %u AIEN_OMEGA_GPU_CHANNELS=%s%s\n", layers, mask, steps, env ? env : "(unset)", refusal ? " (refusal)" : "");

    static Model m;
    const uint64_t tb = now_ns();
    if (model_build(&m, layers, mask) != 0) { printf("FAIL: model build\n"); return 1; }
    State st[4]; OmegaGpuQ38Graph *g[4] = { 0 };
    for (int k = 0; k < 4; k++) if (state_alloc(&m, &st[k]) != 0 || graph_build(&m, &st[k], &g[k]) != 0) { printf("FAIL: graph %d\n", k); return 1; }
    printf("model: %u layers (%u GDN, %u attention), vocab %u, built in %.1f s\n", layers, m.n_gdn, m.n_attn, VOCAB, (double)(now_ns() - tb) / 1e9);
    OmegaGpuQ38Graph *sA = g[0], *sB = g[1], *pA = g[2], *pB = g[3];

    static float x[2][H];
    if (refusal) {
        uint32_t ta = 0, tb2 = 0;
        step_input(0, 0, 0, x[0]); step_input(1, 0, 0, x[1]);
        int rc = omega_gpu_q38_graph_token_pair(pA, x[0], 0, pB, x[1], 0, &ta, &tb2, NULL, NULL);
        CHECK(rc != 0 && strstr(omega_gpu_session_last_error(), "queue") != NULL, "pair refused without AIEN_OMEGA_GPU_CHANNELS: rc %d, stage \"%s\"", rc, omega_gpu_session_last_error());
        CHECK(!omega_gpu_session_is_blocked(), "the refusal did not latch the session");
        rc = omega_gpu_q38_graph_token(sA, x[0], 0, &ta, NULL);
        CHECK(rc == 0 && ta < VOCAB, "the serial path still decodes (rc %d, token %u)", rc, ta);
        printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
        return g_failed ? 1 : 0;
    }

    static uint32_t tok_s[2][MAX_STEPS], tok_p[2][MAX_STEPS];
    static uint64_t t_ser[MAX_STEPS], t_par[MAX_STEPS];
    uint32_t prev_s[2] = { 0, 0 }, prev_p[2] = { 0, 0 };
    int ok = 1;
    for (uint32_t t = 0; t < steps && ok; t++) {
        /* serial: A then B on queue 0 */
        uint64_t t0 = now_ns();
        for (uint32_t k = 0; k < 2 && ok; k++) {
            step_input(k, t, prev_s[k], x[k]);
            if (omega_gpu_q38_graph_token(k ? sB : sA, x[k], t, &tok_s[k][t], NULL) != 0) { printf("FAIL: serial step %u stream %u: %s\n", t, k, omega_gpu_session_last_error()); ok = 0; }
        }
        t_ser[t] = now_ns() - t0;
        if (!ok) break;
        /* pair: A on queue 0 and B on queue 1, both in flight */
        step_input(0, t, prev_p[0], x[0]); step_input(1, t, prev_p[1], x[1]);
        t0 = now_ns();
        if (omega_gpu_q38_graph_token_pair(pA, x[0], t, pB, x[1], t, &tok_p[0][t], &tok_p[1][t], NULL, NULL) != 0) { printf("FAIL: pair step %u: %s\n", t, omega_gpu_session_last_error()); ok = 0; break; }
        t_par[t] = now_ns() - t0;
        for (int k = 0; k < 2; k++) { prev_s[k] = tok_s[k][t]; prev_p[k] = tok_p[k][t]; }
    }
    CHECK(ok, "%u steps ran on both paths", steps);
    if (!ok) { printf("FAIL: %d checks, %d failed\n", g_checks, g_failed); return 1; }

    /* G2: bit for bit */
    int first_bad[2] = { -1, -1 };
    for (int k = 0; k < 2; k++) for (uint32_t t = 0; t < steps; t++) if (tok_s[k][t] != tok_p[k][t]) { first_bad[k] = (int)t; break; }
    CHECK(first_bad[0] < 0, "G2 stream A: pair tokens == serial tokens at all %u steps (first differing step %d)", steps, first_bad[0]);
    CHECK(first_bad[1] < 0, "G2 stream B: pair tokens == serial tokens at all %u steps (first differing step %d)", steps, first_bad[1]);
    static float xs[H], xp[H], ls[VOCAB], lp[VOCAB];
    OmegaGpuQ38Graph *S[2] = { sA, sB }, *P[2] = { pA, pB };
    static float la[VOCAB];
    for (int k = 0; k < 2; k++) {
        if (omega_gpu_q38_graph_read(S[k], xs, NULL, ls) != 0 || omega_gpu_q38_graph_read(P[k], xp, NULL, lp) != 0) { CHECK(0, "read stream %d", k); continue; }
        CHECK(memcmp(xs, xp, sizeof xs) == 0, "G2 stream %c: last x bit for bit", 'A' + k);
        CHECK(memcmp(ls, lp, sizeof ls) == 0, "G2 stream %c: last logits bit for bit", 'A' + k);
        if (k == 0) memcpy(la, ls, sizeof la);
        else CHECK(memcmp(la, ls, sizeof la) != 0, "negative control: streams A and B logits differ (the comparison can fail)");
    }
    uint32_t same = 0;
    for (uint32_t t = 0; t < steps; t++) same += tok_s[0][t] == tok_s[1][t];
    printf("streams A and B share the token at %u of %u steps\n", same, steps);

    /* G3: timing (step 0 includes first-use costs; it is reported, not in the medians) */
    const uint32_t nt = steps > 1 ? steps - 1 : 1;
    static uint64_t a[MAX_STEPS], b[MAX_STEPS];
    uint64_t sum_s = 0, sum_p = 0;
    for (uint32_t t = 0; t < nt; t++) { a[t] = t_ser[t + (steps > 1)]; b[t] = t_par[t + (steps > 1)]; sum_s += a[t]; sum_p += b[t]; }
    const uint64_t ms = median_u64(a, nt), mp = median_u64(b, nt);
    printf("timing: step 0 serial %.3f ms pair %.3f ms (first use, not in the medians)\n", (double)t_ser[0] / 1e6, (double)t_par[0] / 1e6);
    printf("timing: serial (2 tokens, queue 0 one after the other) median %.3f ms per step, mean %.3f ms, %.3f tok/s aggregate\n", (double)ms / 1e6, (double)sum_s / nt / 1e6, 2e9 / (double)ms);
    printf("timing: pair   (2 tokens, queue 0 + queue 1 in flight) median %.3f ms per step, mean %.3f ms, %.3f tok/s aggregate\n", (double)mp / 1e6, (double)sum_p / nt / 1e6, 2e9 / (double)mp);
    printf("timing: pair / serial step time %.4f (below 1 = the two channels overlapped)\n", (double)mp / (double)ms);
    printf("timing: tokens %u (2 streams x %u steps per path)\n", 2 * steps, steps);
    printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
