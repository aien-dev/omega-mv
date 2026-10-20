/*
 * gpu_fnidx_test: the Qwen3.8-Flash-Next QSA indexer phases (omega_gpu_fnidx_api.h, flashnext/port).
 *   --host-only   codegen budgets per phase and mutant, refusals
 *   --sim         SCORE against the double oracle; SELECT against an exact host selection of the SAME scores (sort by score
 *                 descending, ties to the lower index, take K, ascending, 4 positions each, then the tail), list equal word for word,
 *                 including heavy ties; mutants 1..7 caught
 *   --chip        the same on the chip (GPU-uncached session buffers) up to the model's 65536 blocks (262144 tokens), timing medians,
 *                 mutants red, JSON receipt with --out
 * Host and simulator passes are never chip results.
 */
#include "omega_gpu_fnidx_api.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
#define D OMEGA_FNIDX_DIM
#define HEADS OMEGA_FNIDX_HEADS
static const float SCALE = 0.08838834764831845f;

/* ---- oracles ---- */
static double score_oracle(const float *q, const float *kb) {
    double s = 0;
    for (unsigned h = 0; h < HEADS; h++) { double a = 0; for (unsigned d = 0; d < D; d++) a += (double)q[h * D + d] * (double)kb[d]; if (a > 0) s += a; }
    return s / sqrt((double)D);
}
typedef struct { uint32_t nb; const uint32_t *key; } SelCtx;
static SelCtx g_sel;
static int by_rank(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; uint32_t kx = g_sel.key[x], ky = g_sel.key[y]; if (kx != ky) return kx > ky ? -1 : 1; return x < y ? -1 : x > y; }
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
/* the exact list: returns its length; out must hold 4 * 512 + 3 words */
static uint32_t select_oracle(const float *scores, uint32_t nb, uint32_t vis, uint32_t *out) {
    uint32_t *key = malloc((size_t)nb * 4), *ord = malloc((size_t)nb * 4);
    memcpy(key, scores, (size_t)nb * 4);
    for (uint32_t i = 0; i < nb; i++) ord[i] = i;
    g_sel.nb = nb; g_sel.key = key;
    qsort(ord, nb, 4, by_rank);
    uint32_t K = nb < 512 ? nb : 512;
    qsort(ord, K, 4, cmp_u32);
    uint32_t n = 0;
    for (uint32_t i = 0; i < K; i++) for (uint32_t q = 0; q < 4; q++) out[n++] = ord[i] * 4 + q;
    for (uint32_t t = nb * 4; t < vis; t++) out[n++] = t;
    free(key); free(ord);
    return n;
}

static void host_only(void) {
    for (uint32_t ph = 0; ph < OMEGA_FNIDX_PHASES; ph++) {
        OmegaFnIdxSpec s = { ph, 0 }; uint32_t n = 0, gp = 0;
        int rc = omega_fnidx_prog_stats(&s, &n, &gp);
        CHECK(rc == 0 && n <= 700 && gp <= 120 && 32u * 8u * omega_bw_gpr_declared(gp) <= 65536u, "codegen phase %u: rc %d insns %u gprs %u", ph, rc, n, gp);
        if (rc == 0) printf("codegen phase %u: %u instructions, %u registers\n", ph, n, gp);
    }
    static const uint32_t mph[8] = { 0, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT };
    for (int m = 1; m <= 7; m++) for (uint32_t ph = 0; ph < OMEGA_FNIDX_PHASES; ph++) {
        OmegaFnIdxSpec s = { ph, m }; int rc = omega_fnidx_prog_stats(&s, NULL, NULL);
        CHECK((rc == 0) == (ph == mph[m]), "mutant %d phase %u %s", m, ph, rc == 0 ? "built" : "refused");
    }
    { OmegaFnIdxSpec s = { 2, 0 }; CHECK(omega_fnidx_prog_stats(&s, NULL, NULL) != 0, "phase 2 refused"); s.phase = 0; s.mutant = 8; CHECK(omega_fnidx_prog_stats(&s, NULL, NULL) != 0, "mutant 8 refused"); }
    CHECK(omega_fnidx_grid(OMEGA_FNIDX_SCORE, 1) == 1 && omega_fnidx_grid(OMEGA_FNIDX_SCORE, 256) == 1 && omega_fnidx_grid(OMEGA_FNIDX_SCORE, 257) == 2 &&
          omega_fnidx_grid(OMEGA_FNIDX_SCORE, 65536) == 256 && omega_fnidx_grid(OMEGA_FNIDX_SELECT, 65536) == 1, "grids");
    /* the oracle itself: a hand case. scores 5 4 4 4 9 (nb 5, K 512 -> all): list is every position; and K = 2 of ties */
    { float sc[5] = { 5, 4, 4, 4, 9 }; uint32_t out[32], n = select_oracle(sc, 5, 22, out); CHECK(n == 22 && out[0] == 0 && out[19] == 19 && out[21] == 21, "oracle: all blocks when nb <= 512 plus the 2-token tail"); }
    { uint32_t nbx = 600; float *sc = malloc(nbx * 4); for (uint32_t i = 0; i < nbx; i++) sc[i] = (float)(i % 3); /* 200 twos, 200 ones, 200 zeros: K 512 takes 200 + 200 + first 112 zeros */
      uint32_t *out = malloc((4 * 512 + 8) * 4); uint32_t n = select_oracle(sc, nbx, nbx * 4, out);
      uint32_t cnt0 = 0; for (uint32_t i = 0; i < n; i += 4) if (sc[out[i] / 4] == 0.0f) cnt0++;
      int asc = 1; for (uint32_t i = 1; i < n; i++) asc &= out[i] > out[i - 1];
      CHECK(n == 2048 && cnt0 == 112 && asc, "oracle: ties at the cut go to the lower index (112 zero blocks taken, %u)", cnt0);
      uint32_t lastzero = 0; for (uint32_t i = 0; i < n; i += 4) if (sc[out[i] / 4] == 0.0f) lastzero = out[i] / 4;
      CHECK(lastzero < 3 * 112 + 3, "oracle: the zero blocks taken are the earliest ones (last at block %u)", lastzero);
      free(sc); free(out); }
}

/* ---- cases ---- */
typedef struct { uint32_t nb; float *q, *kb, *scores; } ScoreCase;
static void score_case(ScoreCase *c, uint32_t nb, uint32_t seed) {
    c->nb = nb; c->q = malloc(HEADS * D * 4); c->kb = malloc((size_t)nb * D * 4); c->scores = malloc((size_t)nb * 4);
    for (unsigned i = 0; i < HEADS * D; i++) c->q[i] = frand(&seed, -1.0f, 1.0f);
    for (size_t i = 0; i < (size_t)nb * D; i++) c->kb[i] = frand(&seed, -1.0f, 1.0f);
}
static void score_free(ScoreCase *c) { free(c->q); free(c->kb); free(c->scores); }
static int score_judge(const ScoreCase *c, const float *got, char *msg, size_t ml) {
    double mx = 0; for (uint32_t b = 0; b < c->nb; b++) { double o = score_oracle(c->q, c->kb + (size_t)b * D); if (o > mx) mx = o; }
    size_t bad = 0; double worst = 0;
    for (uint32_t b = 0; b < c->nb; b++) {
        double o = score_oracle(c->q, c->kb + (size_t)b * D), d = fabs((double)got[b] - o) / (1e-3 * mx + 1e-9);
        if (!(d <= 0.05)) bad++;   /* 5e-5 of the largest score: f32 sums of 128 products */
        if (d > worst || d != d) worst = d;
    }
    snprintf(msg, ml, "%zu of %u scores out of tolerance, worst %.3f of tol", bad, c->nb, worst);
    return (int)bad;
}
/* select inputs: kind 0 random; 1 heavy ties (values from a small set, even bit patterns); 2 random with the K-th key forced odd
 * (distinct keys: mutant 5 is EQUIVALENT here, see the README); 3 heavy ties on values whose bit pattern is ODD, so the K-th key is an odd value
 * shared by many blocks (the case where a search that never tries bit 0 selects too many blocks) */
static void make_scores(float *sc, uint32_t nb, int kind, uint32_t seed) {
    for (uint32_t i = 0; i < nb; i++) sc[i] = (kind == 1 || kind == 3) ? (float)(lcg(&seed) % 9) * 0.125f : frand(&seed, 0.0f, 4.0f);
    if (kind == 3) for (uint32_t i = 0; i < nb; i++) { uint32_t u; memcpy(&u, &sc[i], 4); u |= 1u; memcpy(&sc[i], &u, 4); }
    if (kind == 2 && nb > 512) {
        uint32_t *k = malloc((size_t)nb * 4); memcpy(k, sc, (size_t)nb * 4); qsort(k, nb, 4, cmp_u32);   /* ascending bit patterns = ascending values (non-negative) */
        uint32_t T = k[nb - 512];
        if ((T & 1u) == 0) { for (uint32_t i = 0; i < nb; i++) { uint32_t u; memcpy(&u, &sc[i], 4); if (u == T) { u |= 1u; memcpy(&sc[i], &u, 4); break; } } }
        free(k);
    }
}

/* ---- simulator ---- */
static int sim_run(uint32_t phase, int mutant, const void *a_in, size_t a_bytes, const void *b_in, size_t b_bytes, void *c_out, size_t c_bytes, uint32_t nb, uint32_t K, uint32_t vis, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    OmegaFnIdxSpec spec = { phase, mutant };
    if (omega_blackwell_codegen_fnidx_prog(&spec, &prog) != 0) { snprintf(msg, ml, "codegen refused"); return -1; }
    size_t ab = (a_bytes + 63) & ~(size_t)63, bb = (b_bytes + 63) & ~(size_t)63, cb = (c_bytes + 63) & ~(size_t)63;
    uint8_t *a = aligned_alloc(16, ab), *b = aligned_alloc(16, bb ? bb : 64), *c = aligned_alloc(16, cb);
    memset(a, 0, ab); memcpy(a, a_in, a_bytes);
    if (b_bytes) { memset(b, 0, bb); memcpy(b, b_in, b_bytes); }
    memset(c, 0xff, cb);
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = OMEGA_FNIDX_THREADS;
    static uint8_t smem[OMEGA_FNIDX_SHARED_BYTES]; memset(smem, 0, sizeof smem); s.smem = smem; s.smem_bytes = OMEGA_FNIDX_SHARED_BYTES;
    uint64_t va[3] = { (uintptr_t)a, (uintptr_t)(phase == OMEGA_FNIDX_SCORE ? b : c), (uintptr_t)c };
    if (phase == OMEGA_FNIDX_SCORE) va[2] = (uintptr_t)c;
    for (int i = 0; i < 3; i++) { s.cbank[OMEGA_FNIDX_ARG_A / 4 + 2 * i] = (uint32_t)va[i]; s.cbank[OMEGA_FNIDX_ARG_A / 4 + 2 * i + 1] = (uint32_t)(va[i] >> 32); }
    s.cbank[OMEGA_FNIDX_ARG_NB / 4] = nb; s.cbank[OMEGA_FNIDX_ARG_K / 4] = K; s.cbank[OMEGA_FNIDX_ARG_VIS / 4] = vis; memcpy(&s.cbank[OMEGA_FNIDX_ARG_SCALE / 4], &SCALE, 4);
    s.nranges = 0;
    s.ranges[s.nranges].lo = (uintptr_t)a; s.ranges[s.nranges++].hi = (uintptr_t)a + ab;
    if (phase == OMEGA_FNIDX_SCORE) { s.ranges[s.nranges].lo = (uintptr_t)b; s.ranges[s.nranges++].hi = (uintptr_t)b + bb; }
    s.ranges[s.nranges].lo = (uintptr_t)c; s.ranges[s.nranges++].hi = (uintptr_t)c + cb;
    int err = ws_run(&s, omega_fnidx_grid(phase, nb), 1);
    if (err) snprintf(msg, ml, "sim: %s (pc %d)", s.msg, s.err_pc);
    else memcpy(c_out, c, c_bytes);
    free(a); free(b); free(c);
    return err ? -1 : 0;
}
static int sim_score(const ScoreCase *c, int mutant, float *out, char *msg, size_t ml) {
    return sim_run(OMEGA_FNIDX_SCORE, mutant, c->q, HEADS * D * 4, c->kb, (size_t)c->nb * D * 4, out, (size_t)c->nb * 4, c->nb, 0, 0, msg, ml);
}
static int sim_select(const float *sc, uint32_t nb, uint32_t vis, int mutant, uint32_t *out, size_t out_words, char *msg, size_t ml) {
    uint32_t K = nb < 512 ? nb : 512;
    return sim_run(OMEGA_FNIDX_SELECT, mutant, sc, (size_t)nb * 4, NULL, 0, out, out_words * 4, nb, K, vis, msg, ml);
}
static int lists_equal(const uint32_t *got, const uint32_t *want, uint32_t n, char *msg, size_t ml) {
    for (uint32_t i = 0; i < n; i++) if (got[i] != want[i]) { snprintf(msg, ml, "word %u: got %u want %u (of %u)", i, got[i], want[i], n); return 0; }
    snprintf(msg, ml, "%u words equal", n);
    return 1;
}
static void sim_battery(void) {
    char msg[300];
    static const uint32_t nbs[] = { 1, 7, 256, 257, 700 };
    for (unsigned i = 0; i < sizeof nbs / sizeof nbs[0]; i++) {
        ScoreCase c; score_case(&c, nbs[i], 40 + i);
        float *got = malloc((size_t)nbs[i] * 4);
        int rc = sim_score(&c, 0, got, msg, sizeof msg);
        int bad = rc == 0 ? score_judge(&c, got, msg, sizeof msg) : 1;
        CHECK(rc == 0 && bad == 0, "sim SCORE nb %u: %s", nbs[i], msg);
        printf("sim SCORE nb %-5u: %s (%s)\n", nbs[i], rc || bad ? "FAIL" : "ok", msg);
        free(got); score_free(&c);
    }
    { ScoreCase c; score_case(&c, 700, 99); float *got = malloc(700 * 4);
      for (int m = 1; m <= 3; m++) { int rc = sim_score(&c, m, got, msg, sizeof msg); int bad = rc == 0 ? score_judge(&c, got, msg, sizeof msg) : 1;
          int caught = bad > 0 || (rc != 0 && !strstr(msg, "codegen") && !strstr(msg, "configuration")); CHECK(caught, "sim SCORE mutant %d NOT caught (%s)", m, msg); printf("sim SCORE mutant %d: %s (%s)\n", m, caught ? "caught" : "MISSED", msg); }
      free(got); score_free(&c); }
    struct { uint32_t nb, tail; int kind; const char *name; } pts[] = {
        { 1, 0, 0, "nb 1" }, { 5, 3, 0, "nb 5 (all), tail 3" }, { 300, 1, 0, "nb 300 (all), tail 1" }, { 512, 0, 0, "nb 512 (all)" }, { 513, 2, 0, "nb 513 (cut), tail 2" },
        { 1000, 0, 1, "nb 1000, heavy ties" }, { 1000, 2, 3, "nb 1000, heavy ties on odd bit patterns" }, { 2000, 3, 0, "nb 2000 random, tail 3" }, { 2000, 1, 2, "nb 2000, K-th key odd" }, { 6000, 0, 1, "nb 6000, heavy ties" },
    };
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        uint32_t nb = pts[i].nb, vis = nb * 4 + pts[i].tail, K = nb < 512 ? nb : 512, words = 4 * K + pts[i].tail;
        float *sc = malloc((size_t)nb * 4); make_scores(sc, nb, pts[i].kind, 7 + i);
        uint32_t *want = malloc((4 * 512 + 8) * 4), *got = malloc((size_t)(words + 4) * 4);
        uint32_t n = select_oracle(sc, nb, vis, want);
        int rc = sim_select(sc, nb, vis, 0, got, words, msg, sizeof msg);
        int eq = rc == 0 && n == words && lists_equal(got, want, n, msg, sizeof msg);
        CHECK(rc == 0 && eq, "sim SELECT %s: %s", pts[i].name, msg);
        printf("sim SELECT %-26s: %s (%s)\n", pts[i].name, eq ? "ok" : "FAIL", msg);
        free(sc); free(want); free(got);
    }
    { uint32_t nb = 1000, vis = nb * 4 + 2, K = 512, words = 4 * K + 2; float *sc = malloc(nb * 4); uint32_t *want = malloc((4 * 512 + 8) * 4), *got = malloc(((size_t)words + 4u * nb + 4) * 4); /* room for a mutant that selects too many blocks */
      for (int m = 4; m <= 7; m++) {
          make_scores(sc, nb, m == 4 ? 1 : (m == 5 ? 3 : 0), 123); uint32_t n = select_oracle(sc, nb, vis, want);
          int rc = sim_select(sc, nb, vis, m, got, (size_t)words + 4u * nb, msg, sizeof msg);
          int same = rc == 0 && n == words && lists_equal(got, want, n, msg, sizeof msg);
          int caught = (rc == 0 && !same) || (rc != 0 && !strstr(msg, "codegen") && !strstr(msg, "configuration"));
          CHECK(caught, "sim SELECT mutant %d NOT caught (%s)", m, msg); printf("sim SELECT mutant %d: %s (%s)\n", m, caught ? "caught" : "MISSED", msg);
      }
      /* mutant 5 on DISTINCT keys is equivalent: the search without bit 0 lands on T - 1, nothing equals T - 1, and the keys above it are
       * exactly the K keys >= T. Recorded here so the claim is a checked fact, not a comment. */
      make_scores(sc, nb, 2, 123); uint32_t n = select_oracle(sc, nb, vis, want);
      int rc = sim_select(sc, nb, vis, 5, got, (size_t)words + 4u * nb, msg, sizeof msg);
      CHECK(rc == 0 && n == words && lists_equal(got, want, n, msg, sizeof msg), "sim SELECT mutant 5 on distinct keys is equivalent (%s)", msg);
      printf("sim SELECT mutant 5 on distinct keys (odd K-th key): equivalent to production, as proven (%s)\n", msg);
      free(sc); free(want); free(got); }
}

/* ---- chip ---- */
static int chip_run(uint32_t reps, const char *out_path) {
    /* the QSA API opens the device lazily; this test allocates session buffers directly, so open it first */
    if (!omega_gpu_session_open()) { CHECK(0, "chip: device open failed: %s", omega_gpu_session_last_error()); return 1; }
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    char msg[300];
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_FNIDX\",\"reps\":%u,\"points\":[", reps);
    int first = 1;
    static const uint32_t nbs[] = { 1, 7, 256, 257, 700, 4096, 65536 };
    for (unsigned i = 0; i < sizeof nbs / sizeof nbs[0]; i++) {
        uint32_t nb = nbs[i];
        ScoreCase c; score_case(&c, nb, 500 + i);
        OmegaGpuMem mq, mk, ms; memset(&mq, 0, sizeof mq); memset(&mk, 0, sizeof mk); memset(&ms, 0, sizeof ms);
        int rc = omega_gpu_session_alloc(HEADS * D * 4, &mq);
        if (rc == 0) rc = omega_gpu_session_alloc((size_t)nb * D * 4, &mk);
        if (rc == 0) rc = omega_gpu_session_alloc((size_t)nb * 4 + 64, &ms);
        CHECK(rc == 0, "SCORE nb %u: buffers (%s)", nb, omega_gpu_fnidx_last_error());
        if (rc) { score_free(&c); continue; }
        memcpy(mq.cpu, c.q, HEADS * D * 4); memcpy(mk.cpu, c.kb, (size_t)nb * D * 4); memset(ms.cpu, 0xff, (size_t)nb * 4 + 64);
        (void)omega_gpu_session_sync_to_gpu(&mq); (void)omega_gpu_session_sync_to_gpu(&mk); (void)omega_gpu_session_sync_to_gpu(&ms);
        OmegaGpuFnIdxInfo info;
        rc = omega_gpu_fnidx_score_run(mq.va, mk.va, ms.va, nb, &info);
        (void)omega_gpu_session_sync_to_cpu(&ms);
        int bad = rc == 0 ? score_judge(&c, ms.cpu, msg, sizeof msg) : 1;
        CHECK(rc == 0 && bad == 0, "chip SCORE nb %u: rc %d (%s) %s", nb, rc, rc ? omega_gpu_fnidx_last_error() : "", rc ? "" : msg);
        uint64_t *ns = calloc(reps, 8); uint32_t done = 0;
        for (uint32_t r = 0; r < reps && rc == 0; r++) { OmegaGpuFnIdxInfo ti; if (omega_gpu_fnidx_score_run(mq.va, mk.va, ms.va, nb, &ti) != 0) break; ns[r] = ti.elapsed_ns; done++; }
        double med = 0; if (done) { qsort(ns, done, 8, cmp_u64); med = (double)ns[done / 2]; }
        printf("chip SCORE  nb %-6u grid %-3u: %s, %u insns, %u regs, median %.1f us (%.1f GB/s of block keys) over %u launches\n", nb, info.grid_x, rc ? "FAIL" : (bad ? "MISMATCH" : "ok"), info.insns, info.gprs, med / 1e3, med ? (double)nb * D * 4 / med : 0.0, done);
        if (out) fprintf(out, "%s{\"phase\":\"SCORE\",\"nb\":%u,\"rc\":%d,\"bad\":%d,\"grid\":%u,\"median_ns\":%.0f,\"reps\":%u}", first ? "" : ",", nb, rc, bad, info.grid_x, med, done);
        first = 0; free(ns);
        if (nb == 700) {
            for (int m = 1; m <= 3; m++) {
                omega_gpu_fnidx_test_set_mutant(m); memset(ms.cpu, 0xff, (size_t)nb * 4 + 64); (void)omega_gpu_session_sync_to_gpu(&ms);
                int mrc = omega_gpu_fnidx_score_run(mq.va, mk.va, ms.va, nb, NULL); (void)omega_gpu_session_sync_to_cpu(&ms);
                int mb = mrc == 0 ? score_judge(&c, ms.cpu, msg, sizeof msg) : 0;
                int caught = (mrc == 0 && mb > 0) || mrc == OMEGA_GPU_FNIDX_CHIP_FAIL;
                CHECK(caught, "chip SCORE mutant %d NOT caught (%s)", m, msg); printf("chip SCORE mutant %d: %s (%s)\n", m, caught ? "caught" : "MISSED", msg);
                omega_gpu_fnidx_test_set_mutant(0);
            }
        }
        omega_gpu_session_free(&mq); omega_gpu_session_free(&mk); omega_gpu_session_free(&ms); score_free(&c);
    }
    struct { uint32_t nb, tail; int kind; } sp[] = { { 1, 0, 0 }, { 300, 1, 0 }, { 513, 2, 0 }, { 1000, 0, 1 }, { 2000, 3, 2 }, { 2000, 0, 3 }, { 6000, 0, 1 }, { 20000, 1, 0 }, { 65536, 3, 0 }, { 65536, 0, 1 } };
    for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) {
        uint32_t nb = sp[i].nb, vis = nb * 4 + sp[i].tail, K = nb < 512 ? nb : 512, words = 4 * K + sp[i].tail;
        float *sc = malloc((size_t)nb * 4); make_scores(sc, nb, sp[i].kind, 900 + i);
        uint32_t *want = malloc((4 * 512 + 8) * 4); uint32_t n = select_oracle(sc, nb, vis, want);
        OmegaGpuMem ma, mp; memset(&ma, 0, sizeof ma); memset(&mp, 0, sizeof mp);
        int rc = omega_gpu_session_alloc((size_t)nb * 4 + 64, &ma);
        if (rc == 0) rc = omega_gpu_session_alloc(((size_t)words + 4u * nb + 16) * 4, &mp); /* room for a mutant that selects too many blocks */
        CHECK(rc == 0, "SELECT nb %u: buffers", nb);
        if (rc) { free(sc); free(want); continue; }
        memcpy(ma.cpu, sc, (size_t)nb * 4); memset(mp.cpu, 0xff, ((size_t)words + 4u * nb + 16) * 4);
        (void)omega_gpu_session_sync_to_gpu(&ma); (void)omega_gpu_session_sync_to_gpu(&mp);
        OmegaGpuFnIdxInfo info;
        rc = omega_gpu_fnidx_select_run(ma.va, mp.va, nb, vis, &info);
        (void)omega_gpu_session_sync_to_cpu(&mp);
        int eq = rc == 0 && n == words && lists_equal(mp.cpu, want, n, msg, sizeof msg);
        const uint32_t *pl = mp.cpu; size_t poison = 0; for (uint32_t k = 0; k < words; k++) if (pl[k] == 0xffffffffu) poison++;
        CHECK(rc == 0 && eq && poison == 0, "chip SELECT nb %u tail %u kind %d: rc %d (%s) %s", nb, sp[i].tail, sp[i].kind, rc, rc ? omega_gpu_fnidx_last_error() : "", rc ? "" : msg);
        uint64_t *ns = calloc(reps, 8); uint32_t done = 0;
        for (uint32_t r = 0; r < reps && rc == 0; r++) { OmegaGpuFnIdxInfo ti; if (omega_gpu_fnidx_select_run(ma.va, mp.va, nb, vis, &ti) != 0) break; ns[r] = ti.elapsed_ns; done++; }
        double med = 0; if (done) { qsort(ns, done, 8, cmp_u64); med = (double)ns[done / 2]; }
        printf("chip SELECT nb %-6u tail %u kind %d: %s (%s), %u insns, %u regs, median %.1f us over %u launches\n", nb, sp[i].tail, sp[i].kind, rc ? "FAIL" : (eq ? "ok" : "MISMATCH"), msg, info.insns, info.gprs, med / 1e3, done);
        if (out) fprintf(out, "%s{\"phase\":\"SELECT\",\"nb\":%u,\"tail\":%u,\"kind\":%d,\"rc\":%d,\"equal\":%d,\"median_ns\":%.0f,\"reps\":%u}", first ? "" : ",", nb, sp[i].tail, sp[i].kind, rc, eq, med, done);
        first = 0; free(ns);
        if (nb == 2000 || nb == 6000) {
            for (int m = 4; m <= 7; m++) {
                if ((m == 4 && sp[i].kind != 1) || (m == 5 && sp[i].kind != 3)) continue;
                omega_gpu_fnidx_test_set_mutant(m); memset(mp.cpu, 0xff, ((size_t)words + 4u * nb + 16) * 4); (void)omega_gpu_session_sync_to_gpu(&mp);
                int mrc = omega_gpu_fnidx_select_run(ma.va, mp.va, nb, vis, NULL); (void)omega_gpu_session_sync_to_cpu(&mp);
                int same = mrc == 0 && lists_equal(mp.cpu, want, n, msg, sizeof msg);
                int caught = (mrc == 0 && !same) || mrc == OMEGA_GPU_FNIDX_CHIP_FAIL;
                CHECK(caught, "chip SELECT mutant %d at nb %u NOT caught", m, nb); printf("chip SELECT mutant %d at nb %u: %s (%s)\n", m, nb, caught ? "caught" : "MISSED", msg);
                omega_gpu_fnidx_test_set_mutant(0);
            }
        }
        omega_gpu_session_free(&ma); omega_gpu_session_free(&mp); free(sc); free(want);
    }
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"verdict\":\"%s\"}\n", g_checks, g_failed, g_failed ? "OMEGA_GPU_FNIDX_FAIL" : "OMEGA_GPU_FNIDX_PASS"); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, chip = 0; uint32_t reps = 30; const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--sim")) sim = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (!host && !sim && !chip) host = sim = 1;
    if (host) host_only();
    if (sim) sim_battery();
    if (chip) { chip_run(reps, out); omega_gpu_session_close(); }
    printf("gpu_fnidx_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
