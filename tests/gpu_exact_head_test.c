/*
 * gpu_exact_head_test: the exact two-pass lm_head (HUM lever L314) on the Qwen3.6-35B-A3B head.
 *   --sim   warp-simulator proofs of the new kernels: XBOUND (lo <= s - c|h|, hi >= s + c|h| on every row despite f32
 *           rounding, tight to 2^-8, rows past count untouched; mutant 1 caught). The MASK recheck form is proven in
 *           gpu_bf16_gemv_test --sim (candidates bit-identical to the plain kernel, mutant 4 caught).
 * Verdict line last: "gpu_exact_head_test: N checks, F failed: PASS|FAIL".
 */
#include "omega_blackwell_codegen_dec.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }

static void put64(uint32_t *cb, uint32_t off, const void *p) { uint64_t a = (uintptr_t)p; cb[off / 4] = (uint32_t)a; cb[off / 4 + 1] = (uint32_t)(a >> 32); }

/* XBOUND on n rows (grid ceil(n / 256), the last CTA partial). hscale spreads |h| over decades. Returns the number of
 * rows whose lo / hi break the bound (or are loose by more than 2^-8 relative); *untouched = rows past n left alone. */
static int sim_xbound(uint32_t n, int mutant, uint32_t seed, float hscale, size_t *bad, size_t *loose, int *untouched, char *msg, size_t msgn) {
    static BlackwellIRProgram prog;
    OmegaDecSpec spec; memset(&spec, 0, sizeof spec);
    spec.kind = OMEGA_DEC_XBOUND; spec.mutant = mutant;
    if (omega_blackwell_codegen_dec_prog(&spec, &prog) != 0) { snprintf(msg, msgn, "codegen refused"); return -1; }
    const uint32_t ctas = (n + 255u) / 256u, cap = ctas * 256u + 8u;
    float *s = aligned_alloc(16, cap * 4), *c = aligned_alloc(16, cap * 4), *h = aligned_alloc(16, 2048 * 4);
    float *lo = aligned_alloc(16, cap * 4), *hi = aligned_alloc(16, cap * 4);
    uint8_t *smem = aligned_alloc(16, OMEGA_DEC_SHARED_BYTES);
    uint32_t st = seed;
    for (uint32_t i = 0; i < 2048; i++) h[i] = frand(&st, -1.0f, 1.0f) * hscale * (i % 97 == 0 ? 40.0f : 1.0f);
    for (uint32_t i = 0; i < cap; i++) {
        s[i] = frand(&st, -30.0f, 30.0f) * (i % 7 == 0 ? 1e3f : 1.0f);
        c[i] = frand(&st, 0.0f, 0.02f);
        lo[i] = hi[i] = -999.0f;
    }
    s[0] = 0.0f; c[1] = 0.0f; s[2] = -0.0f;
    double hn = 0; for (uint32_t i = 0; i < 2048; i++) hn += (double)h[i] * h[i];
    hn = sqrt(hn);
    WsSim w; memset(&w, 0, sizeof w);
    w.p = &prog; w.threads = OMEGA_DEC_THREADS; w.order = (int)(seed & 1u); w.smem = smem; w.smem_bytes = OMEGA_DEC_SHARED_BYTES;
    put64(w.cbank, OMEGA_DEC_XB_S, s); put64(w.cbank, OMEGA_DEC_XB_C, c); put64(w.cbank, OMEGA_DEC_XB_H, h);
    put64(w.cbank, OMEGA_DEC_XB_LO, lo); put64(w.cbank, OMEGA_DEC_XB_HI, hi); w.cbank[OMEGA_DEC_XB_N / 4] = n;
    w.nranges = 5;
    w.ranges[0].lo = (uintptr_t)s; w.ranges[0].hi = (uintptr_t)(s + cap);
    w.ranges[1].lo = (uintptr_t)c; w.ranges[1].hi = (uintptr_t)(c + cap);
    w.ranges[2].lo = (uintptr_t)h; w.ranges[2].hi = (uintptr_t)(h + 2048);
    w.ranges[3].lo = (uintptr_t)lo; w.ranges[3].hi = (uintptr_t)(lo + cap);
    w.ranges[4].lo = (uintptr_t)hi; w.ranges[4].hi = (uintptr_t)(hi + cap);
    int err = ws_run(&w, ctas, 1);
    if (err) snprintf(msg, msgn, "%s (pc %d)", w.msg, w.err_pc);
    else {
        size_t b = 0, l = 0;
        for (uint32_t i = 0; i < n; i++) {
            const double B = (double)c[i] * hn, sv = s[i];
            if (!((double)lo[i] <= sv - B && (double)hi[i] >= sv + B)) b++;
            const double slack = B * 0x1p-8 + fabs(sv) * 0x1p-19 + 1e-29;
            if (sv - (double)lo[i] > B + slack || (double)hi[i] - sv > B + slack) l++;
        }
        int u = 1; for (uint32_t i = n; i < cap; i++) u &= lo[i] == -999.0f && hi[i] == -999.0f;
        *bad = b; *loose = l; *untouched = u;
        snprintf(msg, msgn, "%u rows: %zu break the bound, %zu looser than 2^-8, rows past count %s", n, b, l, u ? "untouched" : "WRITTEN");
    }
    free(s); free(c); free(h); free(lo); free(hi); free(smem);
    return err;
}

static void sim_battery(void) {
    static const struct { uint32_t n, seed; float hscale; } pts[] = { { 700, 3, 1.0f }, { 256, 4, 1e-3f }, { 1000, 5, 30.0f }, { 37, 6, 1.0f } };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        char msg[300]; size_t bad = 1, loose = 1; int u = 0;
        int err = sim_xbound(pts[i].n, 0, pts[i].seed, pts[i].hscale, &bad, &loose, &u, msg, sizeof msg);
        CHECK(!err && bad == 0 && loose == 0 && u, "sim XBOUND n %u: %s", pts[i].n, msg);
        printf("sim XBOUND n %u |h| x%g: %s %s\n", pts[i].n, pts[i].hscale, err ? "ERROR" : bad || loose || !u ? "MISMATCH" : "ok", msg);
    }
    char msg[300]; size_t bad = 0, loose = 0; int u = 0;
    int err = sim_xbound(700, 1, 3, 1.0f, &bad, &loose, &u, msg, sizeof msg);
    CHECK(!err && bad != 0, "sim XBOUND mutant 1 must be caught: %s", msg);
    printf("sim XBOUND mutant 1: %s %s\n", err ? "ERROR" : bad ? "caught" : "NOT CAUGHT", msg);
    OmegaDecSpec m2; memset(&m2, 0, sizeof m2); m2.kind = OMEGA_DEC_XBOUND; m2.mutant = 2;
    static BlackwellIRProgram p2;
    CHECK(omega_blackwell_codegen_dec_prog(&m2, &p2) != 0, "XBOUND refuses mutant 2");
}

int main(int argc, char **argv) {
    int sim = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else { fprintf(stderr, "usage: gpu_exact_head_test --sim\n"); return 2; }
    }
    if (sim) sim_battery();
    printf("gpu_exact_head_test: %u checks, %u failed: %s\n", g_checks, g_failed, g_failed || !g_checks ? "FAIL" : "PASS");
    return g_failed || !g_checks ? 1 : 0;
}
