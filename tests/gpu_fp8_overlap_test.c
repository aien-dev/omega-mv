/* gpu_fp8_overlap_test: overlapped dependent FP8 GEMV launches (OWN-30B W-L2e, 2026-10-14). Chip only: caller holds the GB10 lock.
 *
 * Question: every dependent launch of a decode token pays ~20-28 us (W-L2c/W-L2d) behind WAIT_FOR_IDLE. Does launching the
 * consumer GEMV with no barrier, letting it read (warm) its weights and then spin on the producer's QMD release semaphore
 * before it reads x (omega_gpu_fp8_gemv_grouped_record_overlap), recover part of that, with bit-identical results?
 *
 * Two chains of N dependent launches, each launch with its OWN weight tensor (so L2 keeps nothing between launches by reuse):
 *   single : A (k=2048 -> n=4096), B (k=4096 -> n=2048), A, B, ...      (qkv / o_proj sized)
 *   grouped: G1 (8 experts, k=2048 -> n=768), G2 (8 experts, k=768 -> n=2048, x per expert), ...   (expert sized)
 * Launch i reads buffer i, writes buffer i + 1 (every buffer poisoned by the host before every submit).
 * Modes, interleaved in every rep (one variable each against base):
 *   base  every launch after a WAIT_FOR_IDLE + invalidate (the production list)
 *   wait  launches 1.. overlapped (no barrier, semaphore spin, no warm)
 *   warm  as wait, plus warm = W row groups (cached-weights pass only)
 *   red   NEGATIVE CONTROL: launches 1.. with no barrier and NO wait (plain kernel, mark_independent): must NOT equal base
 * Checks: wait / warm outputs (all N + 1 buffers) bit-identical to base on every rep; red differs from base on >= 1 rep;
 * no spin timeout (the list checks). Timing: list elapsed (submit to completion, host clock), median over reps;
 * saved per launch = (base - mode) / (N - 1).  Output lines "OVL ..." and a receipt JSON (--out). */
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_session.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NMAX 16u
#define REPMAX 64u
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median(const uint64_t *v0, uint32_t n) { uint64_t v[REPMAX]; memcpy(v, v0, n * sizeof *v); qsort(v, n, sizeof *v, cmp_u64); return v[n / 2]; }

typedef struct { uint32_t k, n, groups, x_rows; } Shape;
enum { M_BASE, M_WAIT, M_WARM, M_RED, M_COUNT };
static const char *MODE[M_COUNT] = { "base", "wait", "warm", "red" };

typedef struct {
    const char *name; uint32_t N; Shape sh[NMAX];
    OmegaGpuFp8Tensor *t[NMAX]; OmegaGpuMem tab[NMAX], buf[NMAX + 1]; size_t bytes[NMAX + 1];
    float *ref[NMAX + 1];
} Chain;

static int upload(Chain *c, uint32_t *st) {
    for (uint32_t i = 0; i < c->N; i++) {
        const Shape *s = &c->sh[i]; const uint32_t E = s->groups;
        const size_t wn = (size_t)E * s->n * s->k, sn = (size_t)E * (s->n / 128) * (s->k / 128);
        uint8_t *w = malloc(wn); uint16_t *sc = malloc(sn * 2);
        for (size_t j = 0; j < wn; j++) { uint8_t b = (uint8_t)(lcg(st) >> 24); b = (uint8_t)((b & 0x80) | (b & 0x3F)); w[j] = b; } /* |w| < 2: no NaN code */
        /* y = sum w * (x'/2^64) * S: S = 2^64 / 2^(log2 sqrt(k) + 2) keeps x' near 2^64 along the chain */
        const uint16_t sbits = (uint16_t)(((127u + 64u - (s->k >= 4096 ? 8u : s->k >= 2048 ? 7u : 7u)) << 7));
        for (size_t j = 0; j < sn; j++) sc[j] = sbits;
        int rc = omega_gpu_fp8_tensor_upload(E, s->n, s->k, w, sc, &c->t[i]);
        free(w); free(sc);
        if (rc != OMEGA_GPU_FP8_OK) { printf("upload %s[%u]: %s (%s)\n", c->name, i, omega_gpu_fp8_rc_name(rc), omega_gpu_fp8_last_error()); return -1; }
        omega_gpu_session_lock();
        int arc = omega_gpu_session_alloc((size_t)E * 16u, &c->tab[i]);
        omega_gpu_session_unlock();
        if (arc != 0) return -1;
        uint64_t *tw = c->tab[i].cpu;
        for (uint32_t g = 0; g < E; g++) if (omega_gpu_fp8_tensor_entry(c->t[i], g, &tw[2 * g], &tw[2 * g + 1]) != 0) return -1;
        if (omega_gpu_session_sync_to_gpu(&c->tab[i]) != 0) return -1;
    }
    for (uint32_t i = 0; i <= c->N; i++) {
        c->bytes[i] = i == 0 ? (size_t)c->sh[0].x_rows * c->sh[0].k * 4u : (size_t)c->sh[i - 1].groups * c->sh[i - 1].n * 4u;
        omega_gpu_session_lock();
        int arc = omega_gpu_session_alloc(c->bytes[i], &c->buf[i]);
        omega_gpu_session_unlock();
        if (arc != 0) return -1;
        c->ref[i] = malloc(c->bytes[i]);
    }
    float *x = malloc(c->bytes[0]);
    for (size_t j = 0; j < c->bytes[0] / 4; j++) x[j] = (float)(lcg(st) >> 8) / 16777216.0f * 2.0f - 1.0f;
    for (uint32_t r = 0; r < c->sh[0].x_rows; r++) omega_fp8_pack_x_f32(x + (size_t)r * c->sh[0].k, c->sh[0].k, (float *)c->buf[0].cpu + (size_t)r * c->sh[0].k);
    free(x);
    return omega_gpu_session_sync_to_gpu(&c->buf[0]);
}

/* one list for the mode, poisoned buffers, submit; *ns = list elapsed. 0 ok */
static int run(OmegaGpuCmdList *cl, Chain *c, int mode, uint32_t warm, uint64_t *ns) {
    for (uint32_t i = 1; i <= c->N; i++) { uint32_t *p = c->buf[i].cpu; for (size_t j = 0; j < c->bytes[i] / 4; j++) p[j] = 0xC479C000u; /* -999.0f */
        if (omega_gpu_session_sync_to_gpu(&c->buf[i]) != 0) return -1; }
    int ok = 1;
    omega_gpu_session_lock();
    omega_gpu_cmdlist_reset(cl);
    for (uint32_t i = 0; i < c->N && ok; i++) {
        const Shape *s = &c->sh[i]; int rc;
        if (i == 0 || mode == M_BASE || mode == M_RED) rc = omega_gpu_fp8_gemv_grouped_record(cl, c->t[i], s->groups, c->tab[i].va, c->buf[i].va, s->x_rows, c->buf[i + 1].va);
        else rc = omega_gpu_fp8_gemv_grouped_record_overlap(cl, c->t[i], s->groups, c->tab[i].va, c->buf[i].va, s->x_rows, c->buf[i + 1].va, mode == M_WARM ? warm : 0);
        if (rc == OMEGA_GPU_FP8_OK && i > 0 && mode == M_RED) rc = omega_gpu_cmdlist_mark_independent(cl) == 0 ? OMEGA_GPU_FP8_OK : OMEGA_GPU_FP8_CHIP_FAIL;
        if (rc != OMEGA_GPU_FP8_OK) { printf("record %s %s[%u]: %s (%s)\n", MODE[mode], c->name, i, omega_gpu_fp8_rc_name(rc), omega_gpu_fp8_last_error()); ok = 0; }
    }
    OmegaGpuCmdListOpts o; memset(&o, 0, sizeof o); o.mode = OMEGA_GPU_CMDLIST_PCAS; o.barrier = OMEGA_GPU_CMDLIST_BARRIER_WFI;
    OmegaGpuCmdListStats s; memset(&s, 0, sizeof s);
    if (ok && omega_gpu_cmdlist_submit(cl, &o, &s) != 0) { printf("submit %s %s: %s\n", MODE[mode], c->name, omega_gpu_session_last_error()); ok = 0; }
    omega_gpu_session_unlock();
    *ns = s.elapsed_ns;
    for (uint32_t i = 1; i <= c->N && ok; i++) if (omega_gpu_session_sync_to_cpu(&c->buf[i]) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int same(const Chain *c) { for (uint32_t i = 1; i <= c->N; i++) if (memcmp(c->buf[i].cpu, c->ref[i], c->bytes[i]) != 0) return 0; return 1; }

int main(int argc, char **argv) {
    uint32_t reps = 10, N = 8, warm = 2; const char *out_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) N = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--warm") == 0 && i + 1 < argc) warm = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else { printf("usage: %s [--reps R] [--n N] [--warm W] [--out receipt.json]\n", argv[0]); return 2; }
    }
    if (reps < 3 || reps > REPMAX) reps = 10;
    if (N < 2 || N > NMAX) N = 8;
    if (warm < 1 || warm > OMEGA_FP8_MAX_WARM) warm = 2;
    (void)omega_gpu_session_set_spin_us(2000);
    omega_gpu_fp8_set_variant(1);
    OmegaGpuCmdList *cl = NULL;
    if (omega_gpu_cmdlist_create(NMAX + 2, &cl) != 0) { printf("cmdlist create: %s\n", omega_gpu_session_last_error()); return 1; }
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_FP8_OVERLAP\",\"n_launches\":%u,\"reps\":%u,\"warm\":%u,\"points\":[", N, reps, warm);
    int first_pt = 1;
    uint32_t st = 2026;
    for (int cached = 0; cached <= 1; cached++) {
        omega_gpu_fp8_set_weight_cache(cached);
        Chain ch[2]; memset(ch, 0, sizeof ch);
        ch[0].name = "single"; ch[1].name = "grouped"; ch[0].N = ch[1].N = N;
        for (uint32_t i = 0; i < N; i++) {
            ch[0].sh[i] = (i % 2 == 0) ? (Shape){ 2048, 4096, 1, 1 } : (Shape){ 4096, 2048, 1, 1 };
            ch[1].sh[i] = (i % 2 == 0) ? (Shape){ 2048, 768, 8, i == 0 ? 1u : 8u } : (Shape){ 768, 2048, 8, 8 };
        }
        for (int ci = 0; ci < 2; ci++) {
            Chain *c = &ch[ci];
            if (upload(c, &st) != 0) { printf("setup %s cached=%d failed\n", c->name, cached); g_failed++; continue; }
            uint64_t t[M_COUNT][REPMAX]; int ident[M_COUNT] = { 1, 1, 1, 1 }, red_differs = 0, ok = 1;
            const int modes[] = { M_BASE, M_WAIT, M_RED, M_WARM };
            const int nm = cached ? 4 : 3;
            /* reference: one base run */
            uint64_t dummy;
            if (run(cl, c, M_BASE, warm, &dummy) != 0) { g_failed++; ok = 0; }
            for (uint32_t i = 1; i <= N && ok; i++) memcpy(c->ref[i], c->buf[i].cpu, c->bytes[i]);
            if (ok) { /* chain sanity: final buffer finite and non-zero */
                const float *y = c->ref[N]; size_t nz = 0, bad = 0; float mx = 0;
                for (size_t j = 0; j < c->bytes[N] / 4; j++) { if (y[j] != y[j] || y[j] == -999.0f) bad++; else if (y[j] != 0) { nz++; float a = y[j] < 0 ? -y[j] : y[j]; if (a > mx) mx = a; } }
                CHECK(bad == 0 && nz > c->bytes[N] / 8, "%s cached=%d: base chain output finite, written, %zu/%zu non-zero (max |y| %.3g)", c->name, cached, nz, c->bytes[N] / 4, (double)mx);
            }
            for (uint32_t r = 0; r < reps && ok; r++)
                for (int m = 0; m < nm && ok; m++) {
                    const int mode = modes[(m + r) % nm]; /* rotate the order every rep */
                    if (run(cl, c, mode, warm, &t[mode][r]) != 0) { ok = 0; break; }
                    const int eq = same(c);
                    if (mode == M_RED) { if (!eq) red_differs = 1; } else if (!eq) ident[mode] = 0;
                    if (omega_gpu_session_is_blocked()) { printf("session latched\n"); ok = 0; }
                }
            CHECK(ok, "%s cached=%d: %u reps x %d modes ran (no spin timeout, every semaphore DONE)", c->name, cached, reps, nm);
            if (!ok) break;
            CHECK(ident[M_BASE], "%s cached=%d: base repeats bit-identical", c->name, cached);
            CHECK(ident[M_WAIT], "%s cached=%d: wait bit-identical to base on every rep", c->name, cached);
            if (cached) CHECK(ident[M_WARM], "%s cached=%d: warm=%u bit-identical to base on every rep", c->name, cached, warm);
            CHECK(red_differs, "%s cached=%d: NEGATIVE CONTROL red (no barrier, no wait) differs from base on >= 1 rep", c->name, cached);
            const double b = median(t[M_BASE], reps) / 1e3;
            for (int m = 0; m < nm; m++) {
                const int mode = modes[m]; const double v = median(t[mode], reps) / 1e3, per = (b - v) / (N - 1);
                printf("OVL chain=%s cached=%d mode=%s launches=%u median_us=%.1f saved_per_launch_us=%.2f\n", c->name, cached, MODE[mode], N, v, per);
                if (out) { fprintf(out, "%s{\"chain\":\"%s\",\"cached\":%d,\"mode\":\"%s\",\"median_ns\":%" PRIu64 ",\"saved_per_launch_us\":%.3f,\"times_ns\":[", first_pt ? "" : ",", c->name, cached, MODE[mode], median(t[mode], reps), per);
                           for (uint32_t r = 0; r < reps; r++) { fprintf(out, "%s%" PRIu64, r ? "," : "", t[mode][r]); } fprintf(out, "]}"); first_pt = 0; }
            }
            for (uint32_t i = 0; i < N; i++) omega_gpu_fp8_tensor_free(c->t[i]);
            for (uint32_t i = 0; i <= N; i++) free(c->ref[i]);
        }
        if (g_failed && omega_gpu_session_is_blocked()) break;
    }
    omega_gpu_fp8_set_weight_cache(0);
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"verdict\":\"%s\"}\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS"); fclose(out); }
    printf("gpu_fp8_overlap_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    omega_gpu_cmdlist_free(cl);
    return g_failed ? 1 : 0;
}
