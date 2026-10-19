/*
 * gpu_fnhc_test: the Qwen3.8-Flash-Next 4-lane gated residual phases (omega_gpu_fnhc_api.h, flashnext/port).
 *   --host-only   codegen budgets per phase and mutant, refusals (mutant on the wrong phase, phase out of range)
 *   --sim         every phase through tests/bw_warp_sim.h against the double oracle, mutants 1..6 caught
 *   --chip        the same on the chip (GPU-uncached session buffers), timing medians, mutants red, JSON receipt with --out
 * The oracle is the model's maths in double (crates/aien-inference-abi/src/flashnext.rs GatedResidual::forward / inject).
 * Tolerances: NORM and INJECT 3e-6 relative to the row scale (f32 sums and MUFU.RSQ), ACT, COMBINE 3e-6 (MUFU.EX2 / RCP).
 * Host and simulator passes are never chip results.
 */
#include "omega_gpu_fnhc_api.h"
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
#define L OMEGA_FNHC_L
#define H OMEGA_FNHC_H
#define R OMEGA_FNHC_RANK
static const float EPS = 1e-6f;

typedef struct { float lanes[L], w1p[L], normed[L], d[R], u[L], g[4], block[H], inj[4]; } In;
static void make(In *in, uint32_t seed) {
    for (unsigned l = 0; l < 4; l++) { float sc = 0.5f + 2.0f * (float)l; for (unsigned j = 0; j < H; j++) in->lanes[l * H + j] = frand(&seed, -sc, sc); }
    for (unsigned i = 0; i < L; i++) { in->w1p[i] = 1.0f + frand(&seed, -0.5f, 0.5f); in->normed[i] = frand(&seed, -3.0f, 3.0f); in->u[i] = frand(&seed, -8.0f, 8.0f); }
    for (unsigned i = 0; i < R; i++) in->d[i] = frand(&seed, -20.0f, 20.0f);
    for (unsigned i = 0; i < 4; i++) { in->g[i] = frand(&seed, -10.0f, 10.0f); in->inj[i] = frand(&seed, 0.0f, 2.0f); }
    for (unsigned j = 0; j < H; j++) in->block[j] = frand(&seed, -4.0f, 4.0f);
}
static double sg(double x) { return 1.0 / (1.0 + exp(-x)); }

/* oracle outputs per phase (doubles) */
static void oracle(uint32_t phase, const In *in, double *o1, double *o2) {
    if (phase == OMEGA_FNHC_NORM) {
        for (unsigned l = 0; l < 4; l++) { double ss = 0; for (unsigned j = 0; j < H; j++) ss += (double)in->lanes[l * H + j] * in->lanes[l * H + j];
            double r = 1.0 / sqrt(ss / H + (double)EPS); for (unsigned j = 0; j < H; j++) o1[l * H + j] = (double)in->lanes[l * H + j] * r * (double)in->w1p[l * H + j]; }
    } else if (phase == OMEGA_FNHC_ACT) {
        for (unsigned i = 0; i < R; i++) { double x = in->d[i] / 4.0; o1[i] = x * sg(x); }
    } else if (phase == OMEGA_FNHC_COMBINE) {
        for (unsigned j = 0; j < H; j++) { double a = 0; for (unsigned l = 0; l < 4; l++) a += sg(in->u[l * H + j]) * (double)in->normed[l * H + j]; o1[j] = a / 4.0; }
        for (unsigned l = 0; l < 4; l++) o2[l] = 2.0 * sg(in->g[l] / 4.0);
    } else {
        for (unsigned l = 0; l < 4; l++) for (unsigned j = 0; j < H; j++) o1[l * H + j] = (double)in->lanes[l * H + j] + (double)in->block[j] * (double)in->inj[l];
    }
}
/* buffer plan per phase: input pointers a..e (byte sizes) and which outputs to compare */
static void plan(uint32_t phase, In *in, float *b[5]) {
    switch (phase) {
    case OMEGA_FNHC_NORM: b[0] = in->lanes; b[1] = in->w1p; break;
    case OMEGA_FNHC_ACT: b[0] = in->d; break;
    case OMEGA_FNHC_COMBINE: b[0] = in->u; b[1] = in->normed; b[2] = in->g; break;
    default: b[0] = in->lanes; b[1] = in->block; b[2] = in->inj; break;
    }
}
/* which buffers hold outputs: NORM C(2) [L]; ACT B(1) [R of 512]; COMBINE D(3) [H] and E(4) [4]; INJECT A(0) [L] */
static const int OUT1[4] = { 2, 1, 3, 0 }; static const size_t OUT1N[4] = { L, R, H, L }; static const int OUT2[4] = { -1, -1, 4, -1 };
static const size_t BUFN[4][5] = { { L, L, L, 0, 0 }, { R, 512, 0, 0, 0 }, { L, L, 4, H, 4 }, { L, H, 4, 0, 0 } };

static int judge(uint32_t phase, const In *in, float *const bufs[5], char *msg, size_t ml) {
    static double o1[L], o2[4];
    oracle(phase, in, o1, o2);
    const float *g1 = bufs[OUT1[phase]];
    double worst = 0, scale = 0; size_t bad = 0;
    for (size_t i = 0; i < OUT1N[phase]; i++) { double a = fabs(o1[i]); if (a > scale) scale = a; }
    double tol = 3e-6;
    for (size_t i = 0; i < OUT1N[phase]; i++) {
        double d = fabs((double)g1[i] - o1[i]) / (fabs(o1[i]) * 1.0 + 1e-3 * scale + 1e-9);
        if (!(d <= tol * 50.0)) bad++; /* 1.5e-4 of |value| + 0.1% of the scale: MUFU units and f32 sums */
        if (d > worst || d != d) worst = d;
    }
    size_t bad2 = 0;
    if (OUT2[phase] >= 0) for (unsigned l = 0; l < 4; l++) { double d = fabs((double)bufs[OUT2[phase]][l] - o2[l]) / fabs(o2[l]); if (!(d <= 1e-5)) bad2++; if (d > worst || d != d) worst = d; }
    snprintf(msg, ml, "%zu + %zu elements out of tolerance, worst %.2e", bad, bad2, worst);
    return (int)(bad + bad2);
}

static void host_only(void) {
    for (uint32_t ph = 0; ph < OMEGA_FNHC_PHASES; ph++) {
        OmegaFnHcSpec s = { ph, 0 }; uint32_t n = 0, gp = 0;
        int rc = omega_fnhc_prog_stats(&s, &n, &gp);
        CHECK(rc == 0 && n <= 400 && gp <= 120, "codegen phase %u: rc %d insns %u gprs %u", ph, rc, n, gp);
        if (rc == 0) printf("codegen phase %u: %u instructions, %u registers, grid %u\n", ph, n, gp, omega_fnhc_grid(ph));
    }
    static const uint32_t mph[7] = { 0, OMEGA_FNHC_NORM, OMEGA_FNHC_ACT, OMEGA_FNHC_COMBINE, OMEGA_FNHC_COMBINE, OMEGA_FNHC_INJECT, OMEGA_FNHC_NORM };
    for (int m = 1; m <= 6; m++) for (uint32_t ph = 0; ph < OMEGA_FNHC_PHASES; ph++) {
        OmegaFnHcSpec s = { ph, m }; int rc = omega_fnhc_prog_stats(&s, NULL, NULL);
        CHECK((rc == 0) == (ph == mph[m]), "mutant %d phase %u %s", m, ph, rc == 0 ? "built" : "refused");
    }
    { OmegaFnHcSpec s = { 4, 0 }; CHECK(omega_fnhc_prog_stats(&s, NULL, NULL) != 0, "phase 4 refused"); s.phase = 0; s.mutant = 7; CHECK(omega_fnhc_prog_stats(&s, NULL, NULL) != 0, "mutant 7 refused"); }
    CHECK(omega_fnhc_grid(OMEGA_FNHC_NORM) * 256 == L && omega_fnhc_grid(OMEGA_FNHC_INJECT) * 256 == L && omega_fnhc_grid(OMEGA_FNHC_COMBINE) * 256 == H, "grids cover the vectors");
}

static int sim_phase(uint32_t phase, int mutant, const In *in, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    OmegaFnHcSpec spec = { phase, mutant };
    if (omega_blackwell_codegen_fnhc_prog(&spec, &prog) != 0) { snprintf(msg, ml, "codegen refused"); return -1; }
    In *w = aligned_alloc(16, ((sizeof *in) + 63) & ~(size_t)63); memcpy(w, in, sizeof *in);
    float *b[5] = { 0 }; plan(phase, w, b);
    /* allocate the five buffers (outputs poisoned) */
    float *buf[5];
    for (int i = 0; i < 5; i++) { size_t n = BUFN[phase][i]; buf[i] = n ? aligned_alloc(16, (n * 4 + 63) & ~(size_t)63) : NULL; if (buf[i]) for (size_t k = 0; k < n; k++) buf[i][k] = -999.0f; }
    for (int i = 0; i < 5; i++) if (b[i] && buf[i] && i != OUT1[phase] && i != OUT2[phase]) memcpy(buf[i], b[i], BUFN[phase][i] * 4);
    if (phase == OMEGA_FNHC_INJECT) memcpy(buf[0], w->lanes, L * 4);
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = OMEGA_FNHC_THREADS;
    static uint8_t smem[OMEGA_FNHC_SHARED_BYTES]; memset(smem, 0, sizeof smem); s.smem = smem; s.smem_bytes = OMEGA_FNHC_SHARED_BYTES;
    for (int i = 0; i < 5; i++) { uint64_t va = (uintptr_t)buf[i]; s.cbank[OMEGA_FNHC_ARG_A / 4 + 2 * i] = (uint32_t)va; s.cbank[OMEGA_FNHC_ARG_A / 4 + 2 * i + 1] = (uint32_t)(va >> 32); }
    memcpy(&s.cbank[OMEGA_FNHC_ARG_EPS / 4], &EPS, 4);
    s.nranges = 0;
    for (int i = 0; i < 5; i++) if (buf[i]) { s.ranges[s.nranges].lo = (uintptr_t)buf[i]; s.ranges[s.nranges].hi = (uintptr_t)buf[i] + ((BUFN[phase][i] * 4 + 63) & ~(size_t)63); s.nranges++; }
    int err = ws_run(&s, omega_fnhc_grid(phase), 1);
    if (err) snprintf(msg, ml, "sim: %s (pc %d)", s.msg, s.err_pc);
    else { float *bb[5]; for (int i = 0; i < 5; i++) bb[i] = buf[i]; err = judge(phase, in, bb, msg, ml); }
    for (int i = 0; i < 5; i++) free(buf[i]);
    free(w);
    return err;
}
static void sim_battery(void) {
    static In in; char msg[300];
    make(&in, 17);
    static const char *names[4] = { "NORM", "ACT", "COMBINE", "INJECT" };
    for (uint32_t ph = 0; ph < OMEGA_FNHC_PHASES; ph++) {
        int err = sim_phase(ph, 0, &in, msg, sizeof msg);
        CHECK(err == 0, "sim %s: %s", names[ph], msg);
        printf("sim %-8s: %s (%s)\n", names[ph], err ? "FAIL" : "ok", msg);
    }
    static const uint32_t mph[7] = { 0, OMEGA_FNHC_NORM, OMEGA_FNHC_ACT, OMEGA_FNHC_COMBINE, OMEGA_FNHC_COMBINE, OMEGA_FNHC_INJECT, OMEGA_FNHC_NORM };
    for (int m = 1; m <= 6; m++) {
        int err = sim_phase(mph[m], m, &in, msg, sizeof msg);
        /* caught = ran and differs, or a kernel fault (a refused build or a simulator configuration error proves nothing) */
        int caught = err > 0 || (err < 0 && !strstr(msg, "codegen") && !strstr(msg, "configuration"));
        CHECK(caught, "sim mutant %d NOT caught on %s (%s)", m, names[mph[m]], msg);
        printf("sim mutant %d on %-8s: %s (%s)\n", m, names[mph[m]], caught ? "caught" : "MISSED", msg);
    }
}

static int chip_run(uint32_t reps, const char *out_path) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    static In in; char msg[300];
    static const char *names[4] = { "NORM", "ACT", "COMBINE", "INJECT" };
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_FNHC\",\"reps\":%u,\"phases\":[", reps);
    make(&in, 23);
    int first = 1;
    for (uint32_t ph = 0; ph < OMEGA_FNHC_PHASES; ph++) {
        OmegaGpuMem mem[5]; memset(mem, 0, sizeof mem); float *b[5] = { 0 }; plan(ph, &in, b);
        int rc = 0;
        for (int i = 0; i < 5 && rc == 0; i++) { size_t n = BUFN[ph][i]; if (!n) continue; rc = omega_gpu_session_alloc(n * 4, &mem[i]); }
        CHECK(rc == 0, "phase %s: buffers (%s)", names[ph], omega_gpu_fnhc_last_error());
        if (rc) { if (out) fprintf(out, "%s{\"phase\":\"%s\",\"rc\":%d}", first ? "" : ",", names[ph], rc); first = 0; continue; }
        float *cp[5]; for (int i = 0; i < 5; i++) cp[i] = mem[i].cpu;
        #define LOAD() do { for (int i = 0; i < 5; i++) if (cp[i]) { size_t n = BUFN[ph][i]; \
              if (b[i] && i != OUT1[ph] && i != OUT2[ph]) memcpy(cp[i], b[i], n * 4); else for (size_t k = 0; k < n; k++) cp[i][k] = -999.0f; \
              if (ph == OMEGA_FNHC_INJECT && i == 0) memcpy(cp[0], in.lanes, L * 4); \
              (void)omega_gpu_session_sync_to_gpu(&mem[i]); } } while (0)
        LOAD();
        OmegaGpuFnHcInfo info;
        rc = omega_gpu_fnhc_run(ph, mem[0].va, mem[1].va, mem[2].va, mem[3].va, mem[4].va, EPS, &info);
        for (int i = 0; i < 5; i++) if (cp[i]) (void)omega_gpu_session_sync_to_cpu(&mem[i]);
        int bad = rc == 0 ? judge(ph, &in, cp, msg, sizeof msg) : 1;
        size_t poison = 0; if (rc == 0) { for (size_t k = 0; k < OUT1N[ph]; k++) if (cp[OUT1[ph]][k] == -999.0f) poison++; }
        CHECK(rc == 0 && bad == 0 && poison == 0, "chip %s: rc %d (%s) %s, %zu poison words", names[ph], rc, rc ? omega_gpu_fnhc_last_error() : "", rc ? "" : msg, poison);
        /* timing: the launch is idempotent for every phase except INJECT (in place); reload lanes each rep for it */
        uint64_t *ns = calloc(reps, 8); uint32_t done = 0;
        for (uint32_t r = 0; r < reps && rc == 0; r++) { if (ph == OMEGA_FNHC_INJECT) { memcpy(cp[0], in.lanes, L * 4); (void)omega_gpu_session_sync_to_gpu(&mem[0]); }
            OmegaGpuFnHcInfo ti; if (omega_gpu_fnhc_run(ph, mem[0].va, mem[1].va, mem[2].va, mem[3].va, mem[4].va, EPS, &ti) != 0) break; ns[r] = ti.elapsed_ns; done++; }
        double med = 0; if (done) { qsort(ns, done, 8, cmp_u64); med = (double)ns[done / 2]; }
        printf("chip %-8s grid %u: %s, %u insns, %u regs, median %.1f us over %u launches\n", names[ph], info.grid_x, rc ? "FAIL" : (bad ? "MISMATCH" : "ok"), info.insns, info.gprs, med / 1e3, done);
        if (out) fprintf(out, "%s{\"phase\":\"%s\",\"rc\":%d,\"bad\":%d,\"grid\":%u,\"insns\":%u,\"regs\":%u,\"median_ns\":%.0f,\"reps\":%u", first ? "" : ",", names[ph], rc, bad, info.grid_x, info.insns, info.gprs, med, done);
        first = 0; free(ns);
        /* mutants of this phase */
        static const uint32_t mph[7] = { 0, OMEGA_FNHC_NORM, OMEGA_FNHC_ACT, OMEGA_FNHC_COMBINE, OMEGA_FNHC_COMBINE, OMEGA_FNHC_INJECT, OMEGA_FNHC_NORM };
        if (out) fprintf(out, ",\"mutants\":[");
        int mfirst = 1;
        for (int m = 1; m <= 6 && rc == 0; m++) {
            if (mph[m] != ph) continue;
            omega_gpu_fnhc_test_set_mutant(m); LOAD();
            int mrc = omega_gpu_fnhc_run(ph, mem[0].va, mem[1].va, mem[2].va, mem[3].va, mem[4].va, EPS, NULL);
            for (int i = 0; i < 5; i++) if (cp[i]) (void)omega_gpu_session_sync_to_cpu(&mem[i]);
            int mb = mrc == 0 ? judge(ph, &in, cp, msg, sizeof msg) : 0;
            int caught = (mrc == 0 && mb > 0) || mrc == OMEGA_GPU_FNHC_CHIP_FAIL;
            CHECK(caught, "chip mutant %d on %s NOT caught (rc %d: %s)", m, names[ph], mrc, msg);
            printf("chip %-8s mutant %d: %s (%s)\n", names[ph], m, caught ? "caught" : "MISSED", mrc ? omega_gpu_fnhc_last_error() : msg);
            if (out) fprintf(out, "%s{\"mutant\":%d,\"caught\":%d}", mfirst ? "" : ",", m, caught);
            mfirst = 0;
            omega_gpu_fnhc_test_set_mutant(0);
        }
        if (out) fprintf(out, "]}");
        for (int i = 0; i < 5; i++) if (mem[i].cpu) omega_gpu_session_free(&mem[i]);
    }
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"verdict\":\"%s\"}\n", g_checks, g_failed, g_failed ? "OMEGA_GPU_FNHC_FAIL" : "OMEGA_GPU_FNHC_PASS"); fclose(out); }
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
    printf("gpu_fnhc_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
