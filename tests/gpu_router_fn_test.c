/* gpu_router_fn_test.c: the Qwen3.8-Flash-Next router (omega_gpu_router_fn_api.h, flashnext/port).
 *
 *   ./gpu_router_fn_test --host-only            codegen budgets at every warps / depth
 *   ./gpu_router_fn_test --sim                  the kernel in tests/bw_warp_sim.h against the host oracle; mutants red
 *   ./gpu_router_fn_test --chip [--reps N] [--out f.json] [--fixture DIR]
 *        chip against the host oracle on random data, mutants red, timing; --fixture: the real layer router
 *        (router.bf16 [512][2560], shared_gate.bf16 [2560]) on the real router inputs (x.f32 [t][2560]) of the sc host
 *        reference, whose decisions are in routing.txt (ids | weights | shared gate per token).
 * Oracle: logits in double from the bf16 rows, softmax over 512, top 10 (ties: lower id), renormalised, sigmoid shared.
 * Bars: ids identical; weights within 2e-5 relative (MUFU.EX2 / RCP are approximate, 2^-22 class); x' exact; table
 * entries exact (directory slot ids[g], slot 512 for entry 10).
 */
#include "omega_gpu_router_fn_api.h"
#include "omega_blackwell_codegen_router_fn.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static double bf(uint16_t b) { uint32_t u = (uint32_t)b << 16; float f; memcpy(&f, &u, 4); return f; }
static uint16_t to_bf(float f) { uint32_t u; memcpy(&u, &f, 4); u += 0x7fffu + ((u >> 16) & 1u); return (uint16_t)(u >> 16); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

#define H OMEGA_RFN_HIDDEN
#define NE OMEGA_RFN_EXPERTS
#define K OMEGA_RFN_TOPK

typedef struct { uint32_t ids[K]; double w[K + 1]; double logit[NE + 1]; } Ref;
/* gate: [513][2560] bf16 (row 512 = shared gate) */
static void oracle(const uint16_t *gate, const float *x, int norm_topk, Ref *o) {
    for (uint32_t e = 0; e <= NE; e++) { double a = 0; for (uint32_t c = 0; c < H; c++) a += bf(gate[(size_t)e * H + c]) * x[c]; o->logit[e] = a; }
    double mx = -1e300; for (uint32_t e = 0; e < NE; e++) if (o->logit[e] > mx) mx = o->logit[e];
    double full = 0; for (uint32_t e = 0; e < NE; e++) full += exp(o->logit[e] - mx);
    int used[NE]; memset(used, 0, sizeof used);
    double sel = 0;
    for (uint32_t r = 0; r < K; r++) {
        int best = -1; for (uint32_t e = 0; e < NE; e++) if (!used[e] && (best < 0 || o->logit[e] > o->logit[best])) best = (int)e;
        used[best] = 1; o->ids[r] = (uint32_t)best; o->w[r] = exp(o->logit[best] - mx); sel += o->w[r];
    }
    for (uint32_t r = 0; r < K; r++) o->w[r] /= norm_topk ? sel : full;
    o->w[K] = 1.0 / (1.0 + exp(-o->logit[NE]));
}
/* smallest logit gap between selected and the next unselected (a tie the f32 kernel may order differently) */
static double margin(const Ref *o) {
    double m = 1e300;
    for (uint32_t r = 0; r + 1 < K; r++) { double d = o->logit[o->ids[r]] - o->logit[o->ids[r + 1]]; if (d < m) m = d; }
    double kth = o->logit[o->ids[K - 1]], next = -1e300; int in[NE]; memset(in, 0, sizeof in);
    for (uint32_t r = 0; r < K; r++) in[o->ids[r]] = 1;
    for (uint32_t e = 0; e < NE; e++) if (!in[e] && o->logit[e] > next) next = o->logit[e];
    return kth - next < m ? kth - next : m;
}

typedef struct { uint32_t ids[K]; float w[K + 1]; float xout[H]; uint64_t tgu[22], tdn[22]; } Got;
/* 0 ok, else counts of mismatches folded */
static int compare(const Ref *o, const Got *g, const float *x, float out_scale, const uint64_t *dgu, const uint64_t *ddn, char *msg, size_t ml) {
    int id_bad = 0, w_bad = 0, x_bad = 0, t_bad = 0; double wmax = 0;
    for (uint32_t r = 0; r < K; r++) if (g->ids[r] != o->ids[r]) id_bad++;
    for (uint32_t r = 0; r <= K; r++) { double rel = fabs(g->w[r] - o->w[r]) / fabs(o->w[r]); if (!(rel <= 2e-5)) w_bad++; if (rel > wmax || rel != rel) wmax = rel; }
    for (uint32_t c = 0; c < H; c++) if (g->xout[c] != x[c] * out_scale) x_bad++;
    for (uint32_t gi = 0; gi < K + 1; gi++) {
        const uint32_t slot = gi < K ? o->ids[gi] : NE;
        for (int q = 0; q < 2; q++) { if (g->tgu[2 * gi + q] != dgu[2 * slot + q]) t_bad++; if (g->tdn[2 * gi + q] != ddn[2 * slot + q]) t_bad++; }
    }
    snprintf(msg, ml, "ids %d bad, weights %d bad (worst %.2e), x' %d bad, table words %d bad, margin %.2e", id_bad, w_bad, wmax, x_bad, t_bad, margin(o));
    return id_bad + w_bad + x_bad + t_bad;
}

static void make(uint32_t seed, uint16_t *gate, float *x, uint64_t *dgu, uint64_t *ddn) {
    for (size_t i = 0; i < (size_t)(NE + 1) * H; i++) gate[i] = to_bf(frand(&seed, -0.05f, 0.05f));
    for (uint32_t c = 0; c < H; c++) x[c] = frand(&seed, -2.0f, 2.0f);
    for (uint32_t e = 0; e <= NE; e++) { dgu[2 * e] = 0x100000000ull + e * 0x1000u; dgu[2 * e + 1] = 0x200000000ull + e * 0x40u; ddn[2 * e] = 0x300000000ull + e * 0x800u; ddn[2 * e + 1] = 0x400000000ull + e * 0x20u; }
}

static void host_only(void) {
    const uint32_t ws[2] = { 8, 16 }, ds[4] = { 1, 2, 5, 10 };
    for (int a = 0; a < 2; a++) for (int b = 0; b < 4; b++) {
        OmegaRouterFnSpec s = { ws[a], ds[b], 0 }; uint32_t insns = 0, gprs = 0;
        int rc = omega_router_fn_prog_stats(&s, &insns, &gprs);
        if (rc == 0) printf("  codegen warps %u depth %u: %u insns, %u GPRs\n", ws[a], ds[b], insns, gprs);
        CHECK(rc == 0 && 32u * ws[a] * omega_bw_gpr_declared(gprs) <= 65536u, "codegen warps %u depth %u builds and fits", ws[a], ds[b]);
    }
    OmegaRouterFnSpec s = { 32, 5, 0 }; uint32_t i, g; CHECK(omega_router_fn_prog_stats(&s, &i, &g) != 0, "warps 32 refused (2560 not a multiple of 1024 threads)");
    s.warps = 16; s.depth = 4; CHECK(omega_router_fn_prog_stats(&s, &i, &g) != 0, "depth 4 refused (10 steps)");
}

static int sim_router(const OmegaRouterFnSpec *spec, const uint16_t *gate, const float *x, float out_scale, int ntk, const uint64_t *dgu, const uint64_t *ddn, Got *g, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_router_fn_prog(spec, &prog) != 0) { snprintf(msg, ml, "codegen refused"); return -1; }
    uint8_t *iw = aligned_alloc(16, 128); uint32_t *ids = (uint32_t *)iw; float *wts = (float *)(iw + 64);
    float *xo = aligned_alloc(16, H * 4); float *xi = aligned_alloc(16, H * 4);
    uint64_t *tt = aligned_alloc(16, 512), *tgu = tt, *tdn = tt + 32;
    memcpy(xi, x, H * 4);
    memset(ids, 0xff, 64); for (int q = 0; q < 16; q++) wts[q] = -999.0f; for (uint32_t c = 0; c < H; c++) xo[c] = -999.0f;
    memset(tgu, 0, 256); memset(tdn, 0, 256);
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32 * spec->warps;
    static uint8_t smem[OMEGA_RFN_SHARED_BYTES]; memset(smem, 0, sizeof smem); s.smem = smem; s.smem_bytes = OMEGA_RFN_SHARED_BYTES;
    uint64_t A[9] = { (uintptr_t)xi, (uintptr_t)gate, (uintptr_t)xo, (uintptr_t)ids, (uintptr_t)wts, (uintptr_t)dgu, (uintptr_t)ddn, (uintptr_t)tgu, (uintptr_t)tdn };
    for (int q = 0; q < 9; q++) { s.cbank[OMEGA_RFN_ARG_X / 4 + 2 * q] = (uint32_t)A[q]; s.cbank[OMEGA_RFN_ARG_X / 4 + 2 * q + 1] = (uint32_t)(A[q] >> 32); }
    uint32_t uo; memcpy(&uo, &out_scale, 4); s.cbank[OMEGA_RFN_ARG_OUT_SCALE / 4] = uo; s.cbank[OMEGA_RFN_ARG_NORM_TOPK / 4] = (uint32_t)ntk;
    /* 7 ranges (the simulator holds 8): x, gate, x', ids + weights, two directories, both tables */
    const uint64_t lo[7] = { A[0], A[1], A[2], (uintptr_t)iw, A[5], A[6], (uintptr_t)tt };
    const size_t L[7] = { H * 4, (size_t)(NE + 1) * H * 2, H * 4, 64 + 44, (NE + 1) * 16, (NE + 1) * 16, 256 + 176 };
    s.nranges = 7; for (int q = 0; q < 7; q++) { s.ranges[q].lo = lo[q]; s.ranges[q].hi = lo[q] + L[q]; }
    int err = ws_run(&s, 1, 1);
    if (err) snprintf(msg, ml, "sim: %s", s.msg);
    else { memcpy(g->ids, ids, K * 4); memcpy(g->w, wts, (K + 1) * 4); memcpy(g->xout, xo, H * 4); memcpy(g->tgu, tgu, 176); memcpy(g->tdn, tdn, 176); }
    free(iw); free(xo); free(xi); free(tt);
    return err ? -1 : 0;
}

static void sim_battery(void) {
    char msg[300];
    uint16_t *gate = malloc((size_t)(NE + 1) * H * 2); float x[H]; static uint64_t dgu[(NE + 1) * 2], ddn[(NE + 1) * 2];
    const float scale = 18446744073709551616.0f; /* 2^64 */
    for (int t = 0; t < 3; t++) {
        make(7u + (uint32_t)t, gate, x, dgu, ddn);
        for (int ntk = 1; ntk >= (t == 0 ? 0 : 1); ntk--) {
            Ref o; oracle(gate, x, ntk, &o);
            OmegaRouterFnSpec s = { 8u, t == 1 ? 10u : t == 2 ? 2u : 5u, 0 }; /* the simulator runs at most 256 threads: 8 warps (16 runs on the chip) */
            Got g; int rc = sim_router(&s, gate, x, scale, ntk, dgu, ddn, &g, msg, sizeof msg);
            int bad = rc == 0 ? compare(&o, &g, x, scale, dgu, ddn, msg, sizeof msg) : 1;
            CHECK(rc == 0 && bad == 0, "sim case %d warps %u depth %u norm_topk %d: %s", t, s.warps, s.depth, ntk, msg);
            if (t == 0 && ntk == 1)
                for (int m = 2; m <= 7; m++) {
                    OmegaRouterFnSpec sm = s; sm.mutant = m; Got gm;
                    int mrc = sim_router(&sm, gate, x, scale, ntk, dgu, ddn, &gm, msg, sizeof msg);
                    int mb = mrc == 0 ? compare(&o, &gm, x, scale, dgu, ddn, msg, sizeof msg) : 0;
                    /* caught = ran and differs, or a kernel fault; a refused or misconfigured run proves nothing */
                    const int caught = (mrc == 0 && mb > 0) || (mrc != 0 && !strstr(msg, "configuration") && !strstr(msg, "codegen"));
                    CHECK(caught, "sim mutant %d caught (rc %d: %s)", m, mrc, msg);
                }
        }
    }
    free(gate);
}

static double median_ns(uint64_t *v, uint32_t n) { qsort(v, n, sizeof *v, cmp_u64); return (double)v[n / 2]; }
static void *slurp(const char *dir, const char *name, size_t *len) {
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc((size_t)n + 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    ((char *)b)[n] = 0; fclose(f); if (len) *len = (size_t)n; return b;
}

static int chip_run(uint32_t reps, const char *out_path, const char *fixture) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    char msg[300];
    const float scale = 18446744073709551616.0f;
    OmegaGpuRouterFn *r = NULL;
    int rc = omega_gpu_router_fn_alloc(1, &r);
    CHECK(rc == 0, "alloc rc %d (%s)", rc, rc ? omega_gpu_router_fn_last_error() : "");
    if (rc) return 0;
    uint16_t *gate = malloc((size_t)(NE + 1) * H * 2); float x[H]; static uint64_t dgu[(NE + 1) * 2], ddn[(NE + 1) * 2];
    double med = 0;
    for (int t = 0; t < 4 && rc == 0; t++) {
        make(31u + (uint32_t)t, gate, x, dgu, ddn);
        rc = omega_gpu_router_fn_set_layer(r, 0, gate, gate + (size_t)NE * H);
        if (rc == 0) rc = omega_gpu_router_fn_set_directory(r, 0, dgu, ddn);
        const int ntk = t != 3;
        Ref o; oracle(gate, x, ntk, &o);
        Got g; OmegaGpuRouterFnInfo info;
        if (rc == 0) rc = omega_gpu_router_fn(r, 0, x, ntk, scale, g.ids, g.w, &info);
        if (rc == 0) rc = omega_gpu_router_fn_read_back(r, g.xout, g.tgu, g.tdn);
        int bad = rc == 0 ? compare(&o, &g, x, scale, dgu, ddn, msg, sizeof msg) : 1;
        CHECK(rc == 0 && bad == 0, "chip case %d norm_topk %d: rc %d (%s) %s", t, ntk, rc, rc ? omega_gpu_router_fn_last_error() : "", rc ? "" : msg);
        if (t == 0 && rc == 0) {
            for (int m = 2; m <= 7; m++) {
                omega_gpu_router_fn_test_set_mutant(m); Got gm;
                int mrc = omega_gpu_router_fn(r, 0, x, ntk, scale, gm.ids, gm.w, NULL);
                if (mrc == 0) mrc = omega_gpu_router_fn_read_back(r, gm.xout, gm.tgu, gm.tdn);
                int mb = mrc == 0 ? compare(&o, &gm, x, scale, dgu, ddn, msg, sizeof msg) : 0;
                CHECK((mrc == 0 && mb > 0) || mrc == OMEGA_GPU_RFN_CHIP_FAIL, "chip mutant %d caught (rc %d: %s)", m, mrc, mrc ? omega_gpu_router_fn_last_error() : msg);
                omega_gpu_router_fn_test_set_mutant(0);
            }
            uint64_t *ns = calloc(reps, 8);
            for (uint32_t q = 0; q < reps && rc == 0; q++) { OmegaGpuRouterFnInfo in; rc = omega_gpu_router_fn(r, 0, x, ntk, scale, g.ids, g.w, &in); ns[q] = in.elapsed_ns; }
            if (rc == 0) { med = median_ns(ns, reps); printf("  router 513 x 2560 bf16, top-10 of 512 + shared, warps %u depth %u: median %.1f us (%.1f GB/s of the router weights)\n", info.warps, info.depth, med / 1e3, (double)info.weight_bytes / med); }
            free(ns);
        }
    }
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_ROUTER_FN\",\"median_ns\":%.0f,\"reps\":%u", med, reps);
    if (fixture && rc == 0) {
        size_t gl = 0, sl = 0, xl = 0;
        uint16_t *rw = slurp(fixture, "router.bf16", &gl), *sg = slurp(fixture, "shared_gate.bf16", &sl); float *xs = slurp(fixture, "x.f32", &xl);
        char *routing = slurp(fixture, "routing.txt", NULL);
        CHECK(rw && sg && xs && routing && gl == (size_t)NE * H * 2 && sl == H * 2, "fixture files readable with the right sizes");
        if (rw && sg && xs && routing) {
            const uint32_t T = (uint32_t)(xl / (H * 4));
            memcpy(gate, rw, (size_t)NE * H * 2); memcpy(gate + (size_t)NE * H, sg, H * 2);
            for (uint32_t e = 0; e <= NE; e++) { dgu[2 * e] = 0x100000000ull + e * 0x1000u; dgu[2 * e + 1] = 0x200000000ull + e; ddn[2 * e] = 0x300000000ull + e; ddn[2 * e + 1] = 0x400000000ull + e; }
            rc = omega_gpu_router_fn_set_layer(r, 0, gate, gate + (size_t)NE * H);
            if (rc == 0) rc = omega_gpu_router_fn_set_directory(r, 0, dgu, ddn);
            char *line = strtok(routing, "\n");
            uint32_t tok = 0, id_match = 0, oracle_match = 0; double wworst = 0, sgworst = 0;
            for (; line && tok < T && rc == 0; line = strtok(NULL, "\n")) {
                if (line[0] == '#') continue;
                uint32_t rid[K]; double rwt[K], rsg; char *p = line;
                for (uint32_t q = 0; q < K; q++) rid[q] = (uint32_t)strtoul(p, &p, 10);
                while (*p == ' ' || *p == '|') p++;
                for (uint32_t q = 0; q < K; q++) rwt[q] = strtod(p, &p);
                while (*p == ' ' || *p == '|') p++;
                rsg = strtod(p, &p);
                Got g; rc = omega_gpu_router_fn(r, 0, xs + (size_t)tok * H, 1, scale, g.ids, g.w, NULL);
                Ref o; oracle(gate, xs + (size_t)tok * H, 1, &o);
                int same = 1, same_o = 1;
                for (uint32_t q = 0; q < K; q++) { if (g.ids[q] != rid[q]) same = 0; if (g.ids[q] != o.ids[q]) same_o = 0; }
                id_match += (uint32_t)same; oracle_match += (uint32_t)same_o;
                for (uint32_t q = 0; q < K && same; q++) { double rel = fabs(g.w[q] - rwt[q]) / rwt[q]; if (rel > wworst) wworst = rel; }
                double srel = fabs(g.w[K] - rsg) / rsg; if (srel > sgworst) sgworst = srel;
                if (!same) printf("  token %u: chip ids differ from the host reference (oracle margin %.2e)\n", tok, margin(&o));
                tok++;
            }
            printf("  fixture: %u tokens, chip ids == sc host reference on %u, == double oracle on %u; worst weight rel %.2e, shared gate rel %.2e\n", tok, id_match, oracle_match, wworst, sgworst);
            CHECK(rc == 0 && tok == T && id_match == T && wworst < 1e-4 && sgworst < 1e-4, "fixture: real router on the reference's inputs: ids %u/%u, weight rel %.2e, shared %.2e", id_match, T, wworst, sgworst);
            if (out) fprintf(out, ",\"fixture\":{\"tokens\":%u,\"ids_equal_host_ref\":%u,\"ids_equal_oracle\":%u,\"weight_rel_worst\":%.3e,\"shared_rel_worst\":%.3e}", tok, id_match, oracle_match, wworst, sgworst);
        }
        free(rw); free(sg); free(xs); free(routing);
    }
    if (out) { fprintf(out, ",\"checks\":%d,\"failed\":%d}\n", g_checks, g_failed); fclose(out); }
    omega_gpu_router_fn_free(r); free(gate);
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, chip = 0; uint32_t reps = 30; const char *out = NULL, *fixture = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--sim")) sim = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
        else if (!strcmp(argv[i], "--shape") && i + 2 < argc) { omega_gpu_router_fn_set_shape((uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2])); i += 2; }
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (!host && !sim && !chip) host = 1;
    if (reps < 1) reps = 1;
    if (host) host_only();
    if (sim) sim_battery();
    if (chip) chip_run(reps, out, fixture);
    printf("GPU_ROUTER_FN_TEST %s%s%s: %d checks, %d failed -> %s\n", host ? "host " : "", sim ? "sim " : "", chip ? "chip" : "", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
