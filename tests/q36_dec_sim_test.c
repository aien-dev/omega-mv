/*
 * q36_dec_sim_test: warp-simulator checks of the decode glue kernels at the Qwen3.6-35B-A3B MoE shapes (sc L306):
 *   RES COMBINE|NORM with 9 groups (8 routed experts + the shared expert in slot 8): x += sum_g w[g] * d[g], then
 *   y = RMSNorm(x) * nw * scale, against a double oracle; mutants 2 (expert 7 skipped) and 3 (shared slot skipped)
 *   must be caught. SWIGLU at width 512 over 9 groups (grid 2 x 9) against a double oracle; mutant 1 caught.
 * Host only (no device). Verdict line last: "q36_dec_sim_test: N checks, F failed: PASS|FAIL".
 */
#include "omega_blackwell_codegen_dec.h"
#include "bw_warp_sim_k.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }

#define H 2048u
#define G 9u
#define I 512u
static const float EPS = 1e-6f;
static void put64(WsSim *s, uint32_t off, const void *p) { uintptr_t v = (uintptr_t)p; s->cbank[off / 4] = (uint32_t)v; s->cbank[off / 4 + 1] = (uint32_t)((uint64_t)v >> 32); }

/* worst relative error of x and y against the oracle, or -1 on a simulator error */
static double combine_point(int mutant, uint32_t seed, float scale, char *msg, size_t n) {
    static BlackwellIRProgram prog;
    OmegaDecSpec sp = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM, .mutant = mutant, .groups = G };
    if (omega_blackwell_codegen_dec_prog(&sp, &prog) != 0) { snprintf(msg, n, "codegen refused"); return -1; }
    float *x = aligned_alloc(16, H * 4), *d = aligned_alloc(16, (size_t)G * H * 4), *w = aligned_alloc(16, 64), *nw = aligned_alloc(16, H * 4), *y = aligned_alloc(16, H * 4);
    double *xr = malloc(H * sizeof(double)), *yr = malloc(H * sizeof(double));
    uint32_t s = seed;
    for (unsigned i = 0; i < H; i++) { x[i] = frand(&s, -2.0f, 2.0f); nw[i] = frand(&s, 0.8f, 1.2f); y[i] = -999.0f; }
    for (size_t i = 0; i < (size_t)G * H; i++) d[i] = frand(&s, -1.0f, 1.0f);
    for (unsigned g = 0; g < 16; g++) w[g] = g < G ? frand(&s, 0.05f, 0.4f) : 0.0f;
    for (unsigned i = 0; i < H; i++) { double m = 0; for (unsigned g = 0; g < G; g++) m += (double)w[g] * d[(size_t)g * H + i]; xr[i] = (double)x[i] + m; }
    double ss = 0; for (unsigned i = 0; i < H; i++) ss += xr[i] * xr[i];
    const double r = 1.0 / sqrt(ss / H + (double)EPS);
    for (unsigned i = 0; i < H; i++) yr[i] = xr[i] * r * nw[i] * scale;
    WsSim sim; memset(&sim, 0, sizeof sim);
    static uint8_t smem[OMEGA_DEC_SHARED_BYTES];
    sim.p = &prog; sim.threads = 256; sim.smem = smem; sim.smem_bytes = OMEGA_DEC_SHARED_BYTES;
    put64(&sim, OMEGA_DEC_RES_X, x); put64(&sim, OMEGA_DEC_RES_A, d); put64(&sim, OMEGA_DEC_RES_W, w); put64(&sim, OMEGA_DEC_RES_NW, nw); put64(&sim, OMEGA_DEC_RES_Y, y);
    memcpy(&sim.cbank[OMEGA_DEC_RES_EPS / 4], &EPS, 4); memcpy(&sim.cbank[OMEGA_DEC_RES_SCALE / 4], &scale, 4);
    sim.nranges = 5;
    sim.ranges[0] = (WsRange){ (uintptr_t)x, (uintptr_t)x + H * 4 }; sim.ranges[1] = (WsRange){ (uintptr_t)d, (uintptr_t)d + (size_t)G * H * 4 };
    sim.ranges[2] = (WsRange){ (uintptr_t)w, (uintptr_t)w + 64 }; sim.ranges[3] = (WsRange){ (uintptr_t)nw, (uintptr_t)nw + H * 4 };
    sim.ranges[4] = (WsRange){ (uintptr_t)y, (uintptr_t)y + H * 4 };
    double worst = -1;
    if (ws_run(&sim, 1, 1) != 0) snprintf(msg, n, "sim: %.150s (pc %d)", sim.msg, sim.err_pc);
    else {
        worst = 0;
        double xmax = 0, ymax = 0; for (unsigned i = 0; i < H; i++) { if (fabs(xr[i]) > xmax) xmax = fabs(xr[i]); if (fabs(yr[i]) > ymax) ymax = fabs(yr[i]); }
        for (unsigned i = 0; i < H; i++) {
            double ex = fabs((double)x[i] - xr[i]) / xmax, ey = fabs((double)y[i] - yr[i]) / ymax;
            if (ex > worst) worst = ex;
            if (ey > worst) worst = ey;
        }
        snprintf(msg, n, "x and y worst rel %.2e", worst);
    }
    free(x); free(d); free(w); free(nw); free(y); free(xr); free(yr);
    return worst;
}

static double swiglu_point(int mutant, uint32_t seed, char *msg, size_t n) {
    static BlackwellIRProgram prog;
    OmegaDecSpec sp = { .kind = OMEGA_DEC_SWIGLU, .mutant = mutant, .width = I };
    if (omega_blackwell_codegen_dec_prog(&sp, &prog) != 0) { snprintf(msg, n, "codegen refused"); return -1; }
    float *gu = aligned_alloc(16, (size_t)G * 2 * I * 4), *act = aligned_alloc(16, (size_t)G * I * 4);
    const float scale = 18446744073709551616.0f; /* 2^64: the FP8 GEMV x' row */
    uint32_t s = seed;
    for (size_t i = 0; i < (size_t)G * 2 * I; i++) gu[i] = frand(&s, -4.0f, 4.0f);
    for (size_t i = 0; i < (size_t)G * I; i++) act[i] = -999.0f;
    WsSim sim; memset(&sim, 0, sizeof sim);
    static uint8_t smem[OMEGA_DEC_SHARED_BYTES];
    sim.p = &prog; sim.threads = 256; sim.smem = smem; sim.smem_bytes = OMEGA_DEC_SHARED_BYTES;
    put64(&sim, OMEGA_DEC_SW_GU, gu); put64(&sim, OMEGA_DEC_SW_ACT, act); memcpy(&sim.cbank[OMEGA_DEC_SW_SCALE / 4], &scale, 4);
    sim.nranges = 2;
    sim.ranges[0] = (WsRange){ (uintptr_t)gu, (uintptr_t)gu + (size_t)G * 2 * I * 4 }; sim.ranges[1] = (WsRange){ (uintptr_t)act, (uintptr_t)act + (size_t)G * I * 4 };
    double worst = -1;
    if (ws_run(&sim, I / 256u, G) != 0) snprintf(msg, n, "sim: %.150s (pc %d)", sim.msg, sim.err_pc);
    else {
        worst = 0;
        for (unsigned g = 0; g < G; g++) for (unsigned c = 0; c < I; c++) {
            const double v = gu[(size_t)g * 2 * I + c], u = gu[(size_t)g * 2 * I + I + c];
            const double ref = v / (1.0 + exp(-v)) * u * scale, got = act[(size_t)g * I + c];
            const double e = fabs(got - ref) / (fabs(ref) + 1e-3 * scale);
            if (e > worst) worst = e;
        }
        snprintf(msg, n, "act worst rel %.2e", worst);
    }
    free(gu); free(act);
    return worst;
}

int main(void) {
    char msg[300];
    { OmegaDecSpec sp = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_ADD | OMEGA_DEC_RES_NORM, .groups = 9 }; CHECK(omega_dec_prog_stats(&sp, NULL, NULL) != 0, "groups 9 without COMBINE refused"); }
    { OmegaDecSpec sp = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_COMBINE, .groups = 7 }; CHECK(omega_dec_prog_stats(&sp, NULL, NULL) != 0, "groups 7 refused"); }
    { OmegaDecSpec sp = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_COMBINE, .mutant = 3 }; CHECK(omega_dec_prog_stats(&sp, NULL, NULL) != 0, "mutant 3 without the shared slot refused"); }
    const struct { uint32_t seed; float scale; } pts[] = { { 1, 1.0f }, { 2, 18446744073709551616.0f }, { 3, 1.0f } };
    for (unsigned i = 0; i < 3; i++) {
        double w = combine_point(0, pts[i].seed, pts[i].scale, msg, sizeof msg);
        CHECK(w >= 0 && w < 2e-6, "COMBINE|NORM 9 groups seed %u: %s", pts[i].seed, msg);
        printf("COMBINE|NORM 9 groups seed %u scale %s: %s\n", pts[i].seed, pts[i].scale > 2.0f ? "2^64" : "1", msg);
    }
    for (int m = 2; m <= 3; m++) {
        double w = combine_point(m, 7, 1.0f, msg, sizeof msg);
        CHECK(w > 1e-3, "COMBINE mutant %d must be caught: %s", m, msg);
        printf("COMBINE mutant %d: %s (%s)\n", m, w > 1e-3 ? "caught" : "NOT CAUGHT", msg);
    }
    for (uint32_t seed = 11; seed <= 12; seed++) {
        double w = swiglu_point(0, seed, msg, sizeof msg);
        CHECK(w >= 0 && w < 1e-5, "SWIGLU 512 x 9 seed %u: %s", seed, msg);
        printf("SWIGLU width 512 x 9 groups seed %u: %s\n", seed, msg);
    }
    { double w = swiglu_point(1, 13, msg, sizeof msg); CHECK(w > 1e-3, "SWIGLU mutant 1 must be caught: %s", msg); printf("SWIGLU mutant 1: %s (%s)\n", w > 1e-3 ? "caught" : "NOT CAUGHT", msg); }
    printf("q36_dec_sim_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
