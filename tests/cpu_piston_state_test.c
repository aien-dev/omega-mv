/* cpu_piston_state_test.c (L321 E3a, 2026-10-21): the CPU state piston for the 35B's DeltaNet layers.
 *
 * One DeltaNet layer of one conversation carries 32 value heads x 128 x 128 f32 = 2 MiB of state (exactly one X925 L2),
 * read AND written every token. On the GPU those bytes cross the memory pipe each token (4 MiB per layer). Here the state
 * stays with CPU cores: each pinned worker owns some heads of every layer and keeps them in its caches; per token and layer
 * only the small vectors cross (q, k: 16 x 128; v: 32 x 128; a, b: 32 in; 32 x 128 out = 40 KB).
 *
 * The recurrence is the 35B host reference's (sc crates/aien-inference-abi/src/qwen38.rs, delta_net, statement order kept):
 *   g = -exp(a_log) * softplus(a + dt_bias); beta = sigmoid(b); s *= exp(g); kv = s^T k; delta = (v - kv) * beta;
 *   s += k delta^T; out = s^T q.
 * Variant "ref" runs it as four passes over the state like the reference; "fused" runs two passes (decay folded into the kv
 * pass, the out product folded into the update pass): the same operations per element in the same order, so bit-equal.
 * Every step of every cell is checked bit for bit against a single-thread "ref" copy (outputs and, at the end, the states).
 *
 * Cells: workers k in --workers lists x layers L (1: 2 MiB hot; 30: one conversation, 60 MiB) x variant. Per cell: per-step
 * time (main publishes the inputs, workers fire, main sees every done flag) median/p10/p90 over --tokens tokens.
 * Verdict line last: "CPU_PISTON_STATE: PASS|FAIL". Build with -ffp-contract=off (no fused multiply-add: the reference
 * does not fuse). */
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { VH = 32, KH = 16, KD = 128, VD = 128, HS = KD * VD, MAXW = 16, MAXL = 30 };
typedef struct { float q[KH * KD], k[KH * KD], v[VH * VD], a[VH], b[VH]; } In;
static float A_LOG[MAXL][VH], DT_BIAS[MAXL][VH];

static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static float sigmoidf_(float x) { return 1.0f / (1.0f + expf(-x)); }
static float softplusf_(float x) { return x > 20.0f ? x : log1pf(expf(x)); }

/* one head, one token: the reference order (four passes) */
static void head_ref(float *restrict s, const float *restrict kt, const float *restrict qt, const float *restrict vt, float g, float beta, float *restrict out) {
    const float decay = expf(g); float kv[VD], delta[VD];
    for (int i = 0; i < HS; i++) s[i] *= decay;
    for (int o = 0; o < VD; o++) kv[o] = 0.0f;
    for (int r = 0; r < KD; r++) for (int o = 0; o < VD; o++) kv[o] += s[r * VD + o] * kt[r];
    for (int o = 0; o < VD; o++) delta[o] = (vt[o] - kv[o]) * beta;
    for (int r = 0; r < KD; r++) for (int o = 0; o < VD; o++) s[r * VD + o] += kt[r] * delta[o];
    for (int o = 0; o < VD; o++) out[o] = 0.0f;
    for (int r = 0; r < KD; r++) for (int o = 0; o < VD; o++) out[o] += s[r * VD + o] * qt[r];
}
/* the same operations per element in the same order, two passes */
static void head_fused(float *restrict s, const float *restrict kt, const float *restrict qt, const float *restrict vt, float g, float beta, float *restrict out) {
    const float decay = expf(g); float kv[VD], delta[VD];
    for (int o = 0; o < VD; o++) kv[o] = 0.0f;
    for (int r = 0; r < KD; r++) { float *row = s + r * VD; const float kr = kt[r]; for (int o = 0; o < VD; o++) { row[o] *= decay; kv[o] += row[o] * kr; } }
    for (int o = 0; o < VD; o++) { delta[o] = (vt[o] - kv[o]) * beta; out[o] = 0.0f; }
    for (int r = 0; r < KD; r++) { float *row = s + r * VD; const float kr = kt[r], qr = qt[r]; for (int o = 0; o < VD; o++) { row[o] += kr * delta[o]; out[o] += row[o] * qr; } }
}
static void head_step(int fused, int layer, int hv, float *s, const In *in, float *out) {
    const int hk = hv / (VH / KH);
    const float g = -expf(A_LOG[layer][hv]) * softplusf_(in->a[hv] + DT_BIAS[layer][hv]);
    const float beta = sigmoidf_(in->b[hv]);
    (fused ? head_fused : head_ref)(s, in->k + hk * KD, in->q + hk * KD, in->v + hv * VD, g, beta, out);
}

/* ---- the piston: workers own heads of every layer ---- */
static int g_w, g_cpus[MAXW], g_L, g_fused;
static float *g_state[MAXW];          /* worker w: L layers x its heads x HS, allocated and first touched by the worker */
static const In *volatile g_in; static float g_out[VH * VD];
static atomic_int g_seq, g_done, g_layer;
static void *worker(void *arg) {
    const int w = (int)(intptr_t)arg; cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(g_cpus[w], &cs); sched_setaffinity(0, sizeof cs, &cs);
    const int per = VH / g_w, h0 = w * per;
    g_state[w] = aligned_alloc(64, sizeof(float) * (size_t)g_L * per * HS); memset(g_state[w], 0, sizeof(float) * (size_t)g_L * per * HS);
    atomic_fetch_add(&g_done, 1);
    int seen = 0;
    for (;;) {
        int q; while ((q = atomic_load_explicit(&g_seq, memory_order_acquire)) == seen) { }
        if (q < 0) return NULL;
        seen = q; const int layer = atomic_load(&g_layer); const In *in = g_in;
        for (int j = 0; j < per; j++) head_step(g_fused, layer, h0 + j, g_state[w] + ((size_t)layer * per + j) * HS, in, g_out + (h0 + j) * VD);
        atomic_fetch_add_explicit(&g_done, 1, memory_order_release);
    }
}
static int cmp_u(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static uint32_t g_rng = 12345u;
static float frand(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (float)(g_rng & 0xffffff) / 16777216.0f - 0.5f; }
static void l2n(float *x, int n, float scale) { float s = 0; for (int i = 0; i < n; i++) s += x[i] * x[i]; const float inv = 1.0f / sqrtf(s + 1e-6f); for (int i = 0; i < n; i++) x[i] = x[i] * inv * scale; }

int main(int argc, char **argv) {
    int tokens = 64, fail = 0, main_cpu = 14; const char *lists[8] = { "19", "18,19", "16,17,18,19", "6,7,8,9,16,17,18,19", "5,6,7,8,9,15,16,17,18,19" }; int nl = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tokens") && i + 1 < argc) tokens = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--workers") && i + 1 < argc) { lists[0] = argv[++i]; nl = 1; }
        else if (!strcmp(argv[i], "--main") && i + 1 < argc) main_cpu = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [--tokens N] [--workers 5,6,...] [--main CPU]\n", argv[0]); return 2; }
    }
    if (tokens < 8) tokens = 8;
    { cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(main_cpu, &cs); if (sched_setaffinity(0, sizeof cs, &cs)) { printf("pin main to %d refused\n", main_cpu); return 1; } }
    for (int l = 0; l < MAXL; l++) for (int h = 0; h < VH; h++) { A_LOG[l][h] = frand() * 2.0f; DT_BIAS[l][h] = frand(); }
    enum { POOL = 64 };
    static In pool[POOL];
    for (int p = 0; p < POOL; p++) {
        In *x = &pool[p];
        for (int i = 0; i < KH * KD; i++) { x->q[i] = frand(); x->k[i] = frand(); }
        for (int h = 0; h < KH; h++) { l2n(x->q + h * KD, KD, 1.0f / sqrtf((float)KD)); l2n(x->k + h * KD, KD, 1.0f); }
        for (int i = 0; i < VH * VD; i++) x->v[i] = frand();
        for (int h = 0; h < VH; h++) { x->a[h] = frand() * 4.0f; x->b[h] = frand() * 4.0f; }
    }
    float *ref_state = aligned_alloc(64, sizeof(float) * (size_t)MAXL * VH * HS); float ref_out[VH * VD];
    printf("cpu_piston_state: main core %d, tokens %d, 2 MiB state per layer, inputs+outputs 40 KB per step\n", main_cpu, tokens);
    const int Ls[2] = { 1, 30 };
    for (int li = 0; li < nl; li++) for (int lx = 0; lx < 2; lx++) for (int fused = 0; fused < 2; fused++) {
        g_w = 0; for (const char *s = lists[li]; *s && g_w < MAXW;) { g_cpus[g_w++] = (int)strtol(s, (char **)&s, 10); if (*s == ',') s++; else break; }
        if (VH % g_w) { printf("workers %d do not divide %d heads: skipped\n", g_w, VH); continue; }
        g_L = Ls[lx]; g_fused = fused; atomic_store(&g_seq, 0); atomic_store(&g_done, 0);
        memset(ref_state, 0, sizeof(float) * (size_t)g_L * VH * HS);
        pthread_t th[MAXW];
        for (int w = 0; w < g_w; w++) pthread_create(&th[w], NULL, worker, (void *)(intptr_t)w);
        while (atomic_load(&g_done) < g_w) { }
        const int steps = tokens * g_L; uint64_t *t = malloc(sizeof(uint64_t) * steps); size_t wrong = 0; int seq = 0;
        for (int tok = 0; tok < tokens; tok++) for (int layer = 0; layer < g_L; layer++) {
            const In *in = &pool[(tok * 7 + layer) % POOL];
            atomic_store(&g_done, 0); g_in = in; atomic_store(&g_layer, layer);
            const uint64_t t0 = now_ns();
            atomic_store_explicit(&g_seq, ++seq, memory_order_release);
            while (atomic_load_explicit(&g_done, memory_order_acquire) < g_w) { }
            t[tok * g_L + layer] = now_ns() - t0;
            /* the single-thread reference, outside the timed region */
            for (int hv = 0; hv < VH; hv++) head_step(0, layer, hv, ref_state + ((size_t)layer * VH + hv) * HS, in, ref_out + hv * VD);
            if (memcmp(ref_out, g_out, sizeof ref_out)) wrong++;
        }
        size_t swrong = 0; const int per = VH / g_w;
        for (int w = 0; w < g_w; w++) for (int layer = 0; layer < g_L; layer++) for (int j = 0; j < per; j++)
            if (memcmp(g_state[w] + ((size_t)layer * per + j) * HS, ref_state + ((size_t)layer * VH + w * per + j) * HS, sizeof(float) * HS)) swrong++;
        atomic_store(&g_seq, -1); for (int w = 0; w < g_w; w++) pthread_join(th[w], NULL);
        for (int w = 0; w < g_w; w++) free(g_state[w]);
        /* the first token of every layer warms the caches: timed steps are tokens 2.. */
        const int skip = 2 * g_L; qsort(t + skip, steps - skip, 8, cmp_u); const int n = steps - skip;
        double la = 0; FILE *f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf", &la) != 1) la = -1; fclose(f); }
        printf("STATE workers=%d cpus=%s layers=%d variant=%s steps=%d step_med_us=%.2f step_p10_us=%.2f step_p90_us=%.2f token_med_us=%.1f state_gbs=%.1f wrong_out=%zu wrong_state=%zu load1=%.1f\n",
               g_w, lists[li], g_L, fused ? "fused" : "ref", n, t[skip + n / 2] / 1e3, t[skip + n / 10] / 1e3, t[skip + n * 9 / 10] / 1e3,
               t[skip + n / 2] * g_L / 1e3, 2.0 * 2097152.0 / (double)t[skip + n / 2], wrong, swrong, la);
        fflush(stdout);
        if (wrong || swrong) fail++;
        free(t);
    }
    free(ref_state);
    printf("CPU_PISTON_STATE: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
