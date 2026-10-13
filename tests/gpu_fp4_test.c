/* gpu_fp4_test: OWN-30B cut L3 gate for omega_gpu_fp4_api (FP4 e2m1 + block-16 e4m3 scale + tensor scale decode GEMV).
 *
 *   ./gpu_fp4_test --host-only      format proofs (all 16 codes, all 127 scale bytes, nearest-code and e4m3 rounding against
 *                                   brute force, quantiser properties, API refusals), codegen budgets, nvdisasm listing
 *   ./gpu_fp4_test --sim            the kernel through tests/bw_warp_sim.h (no chip): bit-identical to the C emulation in both
 *                                   fragment orders, near the double oracle, one-hot code and scale tables, and the four
 *                                   mutants (scale dropped, nibble pairs swapped, block 32, group table swapped) + the host
 *                                   no-fold mutant, each of which the same check must catch
 *   ./gpu_fp4_test --out receipt.json   chip gate: parity (bit-identical to the emulation) + mutants + bandwidth table
 *   knobs: --lpa N --rows N --warps N --cta-budget N --reps N --spin-us N --no-evict --only STR
 * Host and simulator passes are never chip results. */
#include "omega_gpu_fp4_api.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static float bits_f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

typedef struct { uint32_t experts, n, k; uint8_t *w4, *sc; float *ts; } Fp4W;
/* random codes, scale bytes in 0x30..0x44 (0.5 .. ~6) unless wide != 0 (0x01..0x7E), tensor scales 0.001..0.1 */
static void fp4w_make(Fp4W *W, uint32_t experts, uint32_t n, uint32_t k, uint32_t seed, int wide) {
    W->experts = experts; W->n = n; W->k = k;
    size_t wb = (size_t)experts * n * (k / 2), sb = (size_t)experts * n * (k / 16);
    W->w4 = aligned_alloc(64, (wb + 63) & ~(size_t)63); W->sc = aligned_alloc(64, (sb + 63) & ~(size_t)63); W->ts = malloc(experts * sizeof(float));
    uint32_t st = seed;
    for (size_t i = 0; i < wb; i++) W->w4[i] = (uint8_t)(lcg(&st) >> 24);
    for (size_t i = 0; i < sb; i++) W->sc[i] = wide ? (uint8_t)(1 + (lcg(&st) >> 24) % 0x7E) : (uint8_t)(0x30 + (lcg(&st) >> 24) % 0x15);
    for (uint32_t e = 0; e < experts; e++) W->ts[e] = frand(&st, 0.001f, 0.1f);
}
static void fp4w_free(Fp4W *W) { free(W->w4); free(W->sc); free(W->ts); }

/* The reference y for group g of expert id: the bit-exact emulation. */
static void ref_group(const Fp4W *W, uint32_t id, const float *x, uint32_t lpa, float *y) {
    float *xp = malloc(W->k * sizeof(float)); omega_fp4_pack_x(x, W->k, xp);
    float st2; omega_fp4_pack_tscale(&W->ts[id], 1, &st2, 0);
    omega_fp4_gemv_emulate(W->w4 + (size_t)id * W->n * (W->k / 2), W->sc + (size_t)id * W->n * (W->k / 16), st2, xp, W->n, W->k, lpa, y);
    free(xp);
}
/* number of outputs that differ from the emulation (bit level); worst relative error to the double oracle */
static size_t compare(const Fp4W *W, uint32_t groups, const uint32_t *ids, const float *x, uint32_t x_rows, uint32_t lpa, const float *y, double *worst_oracle) {
    size_t diff = 0; double worst = 0.0;
    float *ref = malloc(W->n * sizeof(float)); double *o = malloc(W->n * sizeof(double)), *m = malloc(W->n * sizeof(double));
    for (uint32_t g = 0; g < groups; g++) {
        const float *xg = x + (size_t)(x_rows == 1 ? 0 : g) * W->k;
        ref_group(W, ids[g], xg, lpa, ref);
        omega_fp4_gemv_oracle(W->w4 + (size_t)ids[g] * W->n * (W->k / 2), W->sc + (size_t)ids[g] * W->n * (W->k / 16), W->ts[ids[g]], W->n, W->k, xg, o, m);
        for (uint32_t r = 0; r < W->n; r++) {
            float yy = y[(size_t)g * W->n + r];
            if (memcmp(&yy, &ref[r], 4) != 0) diff++;
            double rel = fabs(o[r] - (double)yy) / (m[r] > 1e-30 ? m[r] : 1e-30);
            if (!(rel <= worst)) worst = isnan(rel) ? 1.0 : rel;
        }
    }
    free(ref); free(o); free(m);
    if (worst_oracle) *worst_oracle = worst;
    return diff;
}

/* ------------------------------------------------------------- host-only */
static int nvdisasm_check(const OmegaFp4GemvSpec *spec, const char *name) {
    const char *nv = "/usr/local/cuda/bin/nvdisasm";
    if (access(nv, X_OK) != 0) { printf("nvdisasm: not present, listing check skipped for %s\n", name); return 0; }
    OmegaBlackwellKernel k; memset(&k, 0, sizeof k);
    if (omega_blackwell_codegen_fp4_gemv(spec, &k) != 0) return -1;
    char bin[96], lst[96], cmd[320];
    snprintf(bin, sizeof bin, "/tmp/omega_fp4_%s_%d.bin", name, (int)getpid());
    snprintf(lst, sizeof lst, "/tmp/omega_fp4_%s_%d.sass", name, (int)getpid());
    FILE *f = fopen(bin, "wb"); if (!f) { omega_blackwell_kernel_free(&k); return -1; }
    fwrite(k.code, 1, k.code_size, f); fclose(f);
    snprintf(cmd, sizeof cmd, "%s -b SM121 %s > %s 2>&1", nv, bin, lst);
    int rc = system(cmd);
    omega_blackwell_kernel_free(&k);
    static const char *const must[] = { "LDG.E.64", "LDS.128", "FFMA", "FMUL", "SHFL.DOWN", "BSSY", "BSYNC", "STG.E", "LOP3.LUT", "IMAD", "SHF.R", "SHF.L", "BAR.SYNC" };
    enum { NM = sizeof must / sizeof must[0] };
    FILE *l = fopen(lst, "r"); if (!l) return -1;
    char line[512]; int seen[NM] = { 0 }; int bad = 0, insns = 0;
    while (fgets(line, sizeof line, l)) {
        if (!strstr(line, "/*") || !strstr(line, "*/")) continue;
        insns++;
        if (strstr(line, "?") || strstr(line, "error")) bad++;
        if (strstr(line, ".FTZ")) { printf("  %s: flush-to-zero instruction: %s", name, line); bad++; } /* the code 1 weight is an f32 subnormal */
        for (int i = 0; i < NM; i++) if (strstr(line, must[i])) seen[i] = 1;
    }
    fclose(l);
    int miss = 0;
    for (int i = 0; i < NM; i++) if (!seen[i]) { printf("  %s: nvdisasm listing lacks %s\n", name, must[i]); miss++; }
    printf("nvdisasm %s: rc=%d insns=%d undecodable=%d missing=%d (%s)\n", name, rc, insns, bad, miss, lst);
    unlink(bin);
    return (rc == 0 && bad == 0 && miss == 0) ? 0 : -1;
}

static double brute_e4m3_nearest_err(float v, uint8_t got) {
    /* returns 0 when `got` is a nearest byte (ties: the even byte), else the excess distance */
    double best = 1e300; int bestb = -1;
    for (int b = 0; b <= 0x7E; b++) { double d = fabs((double)omega_fp4_e4m3_unsigned((uint8_t)b) - (double)v); if (d < best - 0.0 || (d == best && (b & 1) == 0 && (bestb & 1))) { best = d; bestb = b; } }
    double dg = fabs((double)omega_fp4_e4m3_unsigned(got) - (double)v);
    return dg > best ? dg - best : (dg == best && got != bestb && (got & 1) ? 1.0 : 0.0);
}

static void host_only(void) {
    /* 1. the 16 codes: kernel bits * 2^126 equal the value, both signs; code 1 is an f32 subnormal */
    int exact = 0, sub = 0;
    for (int c = 0; c < 16; c++) {
        float f = bits_f(omega_fp4_code_bits((uint8_t)c));
        float v = ldexpf(f, 126);
        if (memcmp(&v, &(float){ omega_fp4_value((uint8_t)c) }, 4) == 0 || (v == 0.0f && omega_fp4_value((uint8_t)c) == 0.0f)) exact++;
        else CHECK(0, "code %d: kernel %a vs value %a", c, v, omega_fp4_value((uint8_t)c));
        if ((omega_fp4_code_bits((uint8_t)c) & 0x7f800000u) == 0 && (omega_fp4_code_bits((uint8_t)c) & 0x007fffffu)) sub++;
    }
    CHECK(exact == 16, "16 codes exact (%d)", exact);
    CHECK(sub == 2, "exactly the two codes 1 and 9 travel as f32 subnormals (%d)", sub);
    /* 2. all legal scale bytes: bits * 2^120 == the e4m3 value; the illegal ones are refused by the scan */
    int sexact = 0, nan_refused = 0;
    for (int b = 0; b < 256; b++) {
        uint8_t byte = (uint8_t)b; size_t bad = omega_fp4_scan_scales(&byte, 1);
        if (bad) { nan_refused++; continue; }
        float v = ldexpf(bits_f(omega_fp4_scale_bits(byte)), 120);
        float ref = omega_fp4_e4m3_unsigned(byte);
        if (memcmp(&v, &ref, 4) == 0) sexact++; else CHECK(0, "scale byte 0x%02x: %a vs %a", b, v, ref);
    }
    CHECK(sexact == 127 && nan_refused == 129, "scale bytes: %d exact, %d refused (127 legal, 129 refused: bit 7 set or 0x7F)", sexact, nan_refused);
    CHECK(omega_fp4_e4m3_unsigned(0x7E) == 448.0f && omega_fp4_e4m3_unsigned(0x01) == ldexpf(1.0f, -9) && omega_fp4_e4m3_unsigned(0x38) == 1.0f, "e4m3 anchors 448, 2^-9, 1");
    /* 3. nearest code against brute force over the grid, incl. every midpoint +- 1 ulp and ties-to-even */
    {
        static const float mids[7] = { 0.25f, 0.75f, 1.25f, 1.75f, 2.5f, 3.5f, 5.0f };
        static const uint8_t tie[7] = { 0, 2, 2, 4, 4, 6, 6 };
        int ok = 1;
        for (int i = 0; i < 7; i++) {
            if (omega_fp4_nearest_code(mids[i]) != tie[i] || omega_fp4_nearest_code(-mids[i]) != (tie[i] ? (tie[i] | 8) : 0)) { ok = 0; printf("  tie %g -> %d\n", mids[i], omega_fp4_nearest_code(mids[i])); }
            if (omega_fp4_nearest_code(nextafterf(mids[i], 0.0f)) != (tie[i] == ((i == 0) ? 0 : (i == 1 ? 2 : (i == 2 ? 2 : tie[i]))) ? omega_fp4_nearest_code(nextafterf(mids[i], 0.0f)) : 0)) ok = 0;
        }
        uint32_t st = 5; int mism = 0;
        for (int i = 0; i < 200000; i++) {
            float v = frand(&st, -8.0f, 8.0f); uint8_t c = omega_fp4_nearest_code(v);
            double best = 1e300; for (int q = 0; q < 8; q++) { double d = fabs(fabs((double)v) - (double)omega_fp4_value((uint8_t)q)); if (d < best) best = d; }
            double dg = fabs(fabs((double)v) - (double)omega_fp4_value(c & 7));
            if (dg > best + 1e-12) mism++;
            if (((c & 8) != 0) != (v < 0.0f && (c & 7) != 0)) mism++;
        }
        CHECK(ok && mism == 0, "nearest code: ties %s, %d mismatches against brute force", ok ? "to even" : "WRONG", mism);
    }
    /* 4. e4m3 rounding against brute force */
    {
        uint32_t st = 9; int mism = 0;
        for (int i = 0; i < 100000; i++) {
            float v = i < 50000 ? frand(&st, 0.0f, 500.0f) : ldexpf(frand(&st, 0.0f, 1.0f), -(int)(lcg(&st) % 12));
            if (brute_e4m3_nearest_err(v, omega_fp4_e4m3_round(v)) > 0.0) mism++;
        }
        for (int b = 0; b < 0x7E; b++) { float mid = 0.5f * (omega_fp4_e4m3_unsigned((uint8_t)b) + omega_fp4_e4m3_unsigned((uint8_t)(b + 1))); if (brute_e4m3_nearest_err(mid, omega_fp4_e4m3_round(mid)) > 0.0) mism++; }
        CHECK(mism == 0 && omega_fp4_e4m3_round(1000.0f) == 0x7E && omega_fp4_e4m3_round(0.0f) == 0, "e4m3 rounding: %d mismatches against brute force, saturation at 0x7E", mism);
    }
    /* 5. quantiser: legal output, bounded error, idempotent on its own output */
    {
        const uint32_t n = 64, k = 512; uint32_t st = 21;
        float *w = malloc((size_t)n * k * 4), *d = malloc((size_t)n * k * 4), *d2 = malloc((size_t)n * k * 4);
        for (size_t i = 0; i < (size_t)n * k; i++) { float g = frand(&st, -1, 1) + frand(&st, -1, 1) + frand(&st, -1, 1); w[i] = g * 0.02f * (1.0f + 20.0f * (lcg(&st) % 997 == 0)); }
        uint8_t *w4 = malloc((size_t)n * k / 2), *sc = malloc((size_t)n * k / 16), *w4b = malloc((size_t)n * k / 2), *scb = malloc((size_t)n * k / 16);
        float ts = omega_fp4_quantize(w, n, k, w4, sc);
        omega_fp4_dequantize(w4, sc, ts, n, k, d);
        double num = 0, den = 0; for (size_t i = 0; i < (size_t)n * k; i++) { double e = (double)w[i] - d[i]; num += e * e; den += (double)w[i] * w[i]; }
        double rel = sqrt(num / den);
        CHECK(omega_fp4_scan_scales(sc, (size_t)n * k / 16) == 0, "quantiser emits only legal scale bytes");
        CHECK(rel < 0.14, "quantiser relative RMS error %.4f < 0.14", rel);
        float ts2 = omega_fp4_quantize(d, n, k, w4b, scb);
        omega_fp4_dequantize(w4b, scb, ts2, n, k, d2);
        CHECK(ts2 == ts && memcmp(w4, w4b, (size_t)n * k / 2) == 0 && memcmp(sc, scb, (size_t)n * k / 16) == 0, "quantise(dequantise(q)) == q (codes, scales, tensor scale)");
        printf("quantiser: rel RMS error %.4f, tensor scale %.4e\n", rel, ts);
        free(w); free(d); free(d2); free(w4); free(sc); free(w4b); free(scb);
    }
    /* 6. the emulation against the double oracle (a wrong emulation would make every later test vacuous) */
    {
        Fp4W W; fp4w_make(&W, 1, 64, 2048, 33, 1); uint32_t st = 3; float x[2048]; for (int i = 0; i < 2048; i++) x[i] = frand(&st, -1, 1);
        for (int lpa = 16; lpa <= 32; lpa *= 2) {
            float y[64]; uint32_t id = 0; ref_group(&W, 0, x, lpa, y);
            double worst; size_t d = compare(&W, 1, &id, x, 1, lpa, y, &worst);
            CHECK(d == 0 && worst < 3e-6, "emulation lpa %d vs oracle: worst %.3e (wide scale range)", lpa, worst);
        }
        fp4w_free(&W);
    }
    /* 7. API refusals (no device needed) */
    {
        uint8_t w4[256 * 256 / 2] = { 0 }, sc[256 * 256 / 16] = { 0 }; float ts[1] = { 0.01f }; OmegaGpuFp4Tensor *t = NULL;
        sc[7] = 0x7F; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_SCALE && !t, "NaN scale byte refused");
        sc[7] = 0x80; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_SCALE, "scale byte with bit 7 refused");
        sc[7] = 0; ts[0] = 0.0f; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_SCALE, "zero tensor scale refused");
        ts[0] = NAN; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_SCALE, "NaN tensor scale refused");
        ts[0] = 1e30f; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_SCALE, "tensor scale overflowing the 2^120 fold refused");
        ts[0] = 0.01f; CHECK(omega_gpu_fp4_tensor_upload(1, 256, 200, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_ARGS, "k not a multiple of 256 refused");
        CHECK(omega_gpu_fp4_tensor_upload(0, 256, 256, w4, sc, ts, &t) == OMEGA_GPU_FP4_BAD_ARGS, "zero experts refused");
    }
    /* 8. codegen: every production and test shape inside the instruction and register budgets */
    {
        struct { uint32_t k, lpa, rows, warps; } sh[] = {
            { 2048, 32, 2, 16 }, { 2048, 32, 1, 16 }, { 2048, 32, 2, 8 }, { 2048, 32, 1, 32 }, { 768, 16, 2, 12 }, { 768, 16, 1, 12 }, { 768, 16, 2, 24 },
            { 512, 32, 2, 4 }, { 256, 16, 4, 8 }, { 4096, 32, 1, 16 }, { 4096, 32, 2, 16 }, { 1024, 16, 2, 16 }, { 8192, 32, 1, 32 } };
        for (size_t i = 0; i < sizeof sh / sizeof sh[0]; i++) {
            OmegaFp4GemvSpec sp = { .k = sh[i].k, .lpa = sh[i].lpa, .rows = sh[i].rows, .warps = sh[i].warps };
            uint32_t insns = 0, gprs = 0;
            int rc = omega_fp4_gemv_prog_stats(&sp, &insns, &gprs);
            uint32_t decl = omega_bw_gpr_declared(gprs);
            int fits = rc == 0 && insns <= BW_MAX_IR_INSNS && gprs + 2 <= BW_PHYS_GPR_MAX && (uint64_t)32 * sp.warps * decl <= 65536u;
            printf("codegen k=%u lpa=%u rows=%u warps=%u: rc=%d insns=%u peak=%u declared=%u regfile=%u/65536 %s\n", sp.k, sp.lpa, sp.rows, sp.warps, rc, insns, gprs, decl, 32 * sp.warps * decl, fits ? "fits" : "does not fit");
            if (i < 8) CHECK(fits, "production/test shape k=%u lpa=%u rows=%u warps=%u within budgets", sp.k, sp.lpa, sp.rows, sp.warps);
        }
        OmegaFp4GemvSpec bad[] = { { .k = 2048, .lpa = 24, .rows = 2, .warps = 16 }, { .k = 2000, .lpa = 32, .rows = 2, .warps = 16 }, { .k = 768, .lpa = 32, .rows = 2, .warps = 12 },
                                   { .k = 2048, .lpa = 32, .rows = 3, .warps = 16 }, { .k = 2048, .lpa = 32, .rows = 2, .warps = 33 }, { .k = 2048, .lpa = 32, .rows = 2, .warps = 7 }, { .k = 2048, .lpa = 32, .rows = 2, .warps = 16, .mutant = 9 } };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) { BlackwellIRProgram *p = calloc(1, sizeof *p); CHECK(omega_blackwell_codegen_fp4_gemv_prog(&bad[i], p) != 0, "codegen refuses bad spec %zu", i); free(p); }
    }
    /* 9. disassembly listing of the production shapes */
    {
        OmegaFp4GemvSpec a = { .k = 2048, .lpa = 32, .rows = 2, .warps = 16 }, b = { .k = 768, .lpa = 16, .rows = 2, .warps = 12 };
        CHECK(nvdisasm_check(&a, "k2048_lpa32_r2") == 0, "nvdisasm listing k=2048");
        CHECK(nvdisasm_check(&b, "k768_lpa16_r2") == 0, "nvdisasm listing k=768");
    }
}

/* ------------------------------------------------------------- simulator */
static uint64_t g_sim_issues;
/* Runs the kernel on the warp simulator in both fragment orders: 0 and y on success, -1 on a simulator error or an order-dependent result. */
static int sim_gemv(const OmegaFp4GemvSpec *spec, const Fp4W *W, uint32_t groups, const uint32_t *ids, const float *x, uint32_t x_rows,
                    float *y, uint32_t cta_budget, int no_fold, char *msg, size_t msglen) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_fp4_gemv_prog(spec, &prog) != 0) { snprintf(msg, msglen, "codegen refused"); return -1; }
    const uint32_t n = W->n, k = W->k, rpw = omega_fp4_gemv_rows_per_warp(spec), RG = n / rpw;
    float *t2 = malloc(W->experts * sizeof(float));
    if (omega_fp4_pack_tscale(W->ts, W->experts, t2, no_fold) != 0) { free(t2); snprintf(msg, msglen, "tensor scale pack refused"); return -1; }
    float *xf = aligned_alloc(16, ((size_t)x_rows * k * 4 + 15) & ~(size_t)15);
    for (uint32_t r = 0; r < x_rows; r++) omega_fp4_pack_x(x + (size_t)r * k, k, xf + (size_t)r * k);
    uint64_t *tab = aligned_alloc(16, 32 * (size_t)groups);
    memset(tab, 0, 32 * (size_t)groups);
    for (uint32_t g = 0; g < groups; g++) {
        tab[4 * g] = (uintptr_t)(W->w4 + (size_t)ids[g] * n * (k / 2));
        tab[4 * g + 1] = (uintptr_t)(W->sc + (size_t)ids[g] * n * (k / 16));
        uint32_t w2[2] = { 0, 0 }; memcpy(&w2[0], &t2[ids[g]], 4); memcpy(&tab[4 * g + 2], w2, 8);
    }
    float *yd = aligned_alloc(16, ((size_t)groups * n * 4 + 15) & ~(size_t)15);
    float *copy = malloc((size_t)groups * n * 4);
    uint32_t grid_x = (RG + spec->warps - 1) / spec->warps; if (grid_x > cta_budget) grid_x = cta_budget; if (!grid_x) grid_x = 1;
    int err = 0;
    for (int order = 0; order < 2 && !err; order++) {
        WsSim s; memset(&s, 0, sizeof s);
        s.p = &prog; s.threads = 32 * spec->warps; s.order = order;
        static uint8_t smem[OMEGA_FP4_MAX_K * 4]; s.smem = smem; s.smem_bytes = omega_fp4_gemv_shared_bytes(spec);
        uint64_t ta = (uintptr_t)tab, xa = (uintptr_t)xf, ya = (uintptr_t)yd;
        s.cbank[OMEGA_FP4_ARG_TAB / 4] = (uint32_t)ta; s.cbank[OMEGA_FP4_ARG_TAB / 4 + 1] = (uint32_t)(ta >> 32);
        s.cbank[OMEGA_FP4_ARG_X / 4] = (uint32_t)xa; s.cbank[OMEGA_FP4_ARG_X / 4 + 1] = (uint32_t)(xa >> 32);
        s.cbank[OMEGA_FP4_ARG_Y / 4] = (uint32_t)ya; s.cbank[OMEGA_FP4_ARG_Y / 4 + 1] = (uint32_t)(ya >> 32);
        s.cbank[OMEGA_FP4_ARG_RG / 4] = RG; s.cbank[OMEGA_FP4_ARG_XSTRIDE / 4] = x_rows == 1 ? 0 : k * 4;
        s.cbank[OMEGA_FP4_ARG_YSTRIDE / 4] = n * 4; s.cbank[OMEGA_FP4_ARG_STRIDE / 4] = grid_x * spec->warps;
        s.nranges = 5;
        s.ranges[0].lo = (uintptr_t)W->w4; s.ranges[0].hi = (uintptr_t)W->w4 + (size_t)W->experts * n * (k / 2);
        s.ranges[1].lo = (uintptr_t)W->sc; s.ranges[1].hi = (uintptr_t)W->sc + (size_t)W->experts * n * (k / 16);
        s.ranges[2].lo = (uintptr_t)xf; s.ranges[2].hi = (uintptr_t)xf + (size_t)x_rows * k * 4;
        s.ranges[3].lo = (uintptr_t)yd; s.ranges[3].hi = (uintptr_t)yd + (size_t)groups * n * 4;
        s.ranges[4].lo = (uintptr_t)tab; s.ranges[4].hi = (uintptr_t)tab + 32 * (size_t)groups;
        for (size_t i = 0; i < (size_t)groups * n; i++) yd[i] = -999.0f;
        err = ws_run(&s, grid_x, groups);
        if (err) { snprintf(msg, msglen, "order %d: %s", order, s.msg); break; }
        g_sim_issues += s.warp_issues;
        size_t unwritten = 0; for (size_t i = 0; i < (size_t)groups * n; i++) if (yd[i] == -999.0f) unwritten++;
        if (unwritten) { err = -1; snprintf(msg, msglen, "%zu output words never written", unwritten); break; }
        if (order == 0) memcpy(copy, yd, (size_t)groups * n * 4);
        else if (memcmp(copy, yd, (size_t)groups * n * 4) != 0) { err = -1; snprintf(msg, msglen, "the fragment order changed the result"); }
    }
    if (!err) memcpy(y, yd, (size_t)groups * n * 4);
    free(t2); free(xf); free(tab); free(yd); free(copy);
    return err ? -1 : 0;
}

typedef struct { uint32_t experts, n, k, groups, x_rows, lpa, rows, warps; const char *name; } SimPoint;

static void sim_battery(uint32_t cta_budget) {
    char msg[240];
    /* 1. one-hot tables through the kernel: rows 0..15 hold code r at column 0 (scale byte 0x38 = 1.0), rows 16..142 hold code 2 (= 1.0) with scale
     *    byte r - 16 (0x00 .. 0x7E): y[r] must equal the exact product. Both are checked bit for bit. */
    {
        Fp4W W; W.experts = 1; W.n = 256; W.k = 512;
        W.w4 = aligned_alloc(64, 256 * 256); memset(W.w4, 0, 256 * 256); W.sc = aligned_alloc(64, 256 * 32); memset(W.sc, 0, 256 * 32); W.ts = malloc(4); W.ts[0] = 0.01f;
        for (int r = 0; r < 16; r++) { W.w4[r * 256] = (uint8_t)r; W.sc[r * 32] = 0x38; }
        for (int r = 16; r < 143; r++) { W.w4[r * 256] = 2; W.sc[r * 32] = (uint8_t)(r - 16); }
        float x[512] = { 0 }; x[0] = 1.0f; float y[256]; uint32_t id = 0;
        OmegaFp4GemvSpec sp = { .k = 512, .lpa = 32, .rows = 2, .warps = 4 };
        int rc = sim_gemv(&sp, &W, 1, &id, x, 1, y, cta_budget, 0, msg, sizeof msg);
        CHECK(rc == 0, "one-hot sim run: %s", msg);
        int exact = 0, total = 0;
        if (rc == 0) for (int r = 0; r < 143; r++) {
            float code_v = r < 16 ? omega_fp4_value((uint8_t)r) : 1.0f;
            float sv = r < 16 ? 1.0f : omega_fp4_e4m3_unsigned((uint8_t)(r - 16));
            double refd = (double)code_v * (double)sv * (double)W.ts[0];
            float ref = (float)refd; /* both factors are exact in f32; the kernel rounds sv * ts once and multiplies the exact dot */
            float kern_ref = code_v * (sv * W.ts[0]);
            total++;
            if (y[r] == kern_ref) exact++; /* -0 and +0 both mean zero: a -0 weight times x accumulates onto +0 */ else CHECK(0, "one-hot row %d: kernel %a, expected %a (double product %a)", r, y[r], kern_ref, ref);
        }
        printf("sim one-hot tables: %d/%d rows exact (16 codes incl. subnormal code 1, 127 scale bytes incl. subnormal scales)\n", exact, total);
        fp4w_free(&W);
    }
    /* 2. parity battery */
    static const SimPoint pts[] = {
        { 1, 64, 512, 1, 1, 32, 1, 1, "k512 lpa32 r1 w1" },
        { 1, 128, 256, 1, 1, 16, 2, 4, "k256 lpa16 r2 w4" },
        { 1, 96, 768, 1, 1, 16, 2, 12, "k768 lpa16 r2 w12 (down width)" },
        { 1, 128, 256, 1, 1, 16, 4, 8, "k256 lpa16 r4 w8" },
        { 1, 96, 2048, 1, 1, 32, 2, 16, "k2048 lpa32 r2 w16 (gate width)" },
        { 1, 64, 2048, 1, 1, 32, 1, 8, "k2048 lpa32 r1 w8" },
        { 1, 64, 1024, 1, 1, 16, 2, 8, "k1024 lpa16 r2 w8" },
        { 1, 64, 4096, 1, 1, 32, 1, 16, "k4096 lpa32 r1 w16 (8 steps)" },
        { 4, 128, 2048, 4, 1, 32, 2, 16, "4 experts shared x (gate+up)" },
        { 6, 128, 768, 8, 8, 16, 2, 12, "8 groups per-group x, repeated ids (down)" },
        { 3, 64, 512, 2, 2, 32, 1, 4, "2 groups per-group x" },
        { 2, 80, 768, 2, 2, 16, 1, 3, "k768 w3 tail row groups (n not a multiple of the grid)" },
    };
    const size_t npts = sizeof pts / sizeof pts[0];
    double worst_all = 0.0;
    for (size_t i = 0; i < npts; i++) {
        const SimPoint *p = &pts[i];
        Fp4W W; fp4w_make(&W, p->experts, p->n, p->k, 1000 + (uint32_t)i, i & 1);
        uint32_t ids[8]; uint32_t st = 77 + (uint32_t)i;
        for (uint32_t g = 0; g < p->groups; g++) ids[g] = lcg(&st) % p->experts;
        if (p->groups > 1 && p->experts > 1) ids[1] = (ids[0] + 1) % p->experts; /* mutant 4 needs two different experts */
        float *x = malloc((size_t)p->x_rows * p->k * 4); for (size_t j = 0; j < (size_t)p->x_rows * p->k; j++) x[j] = frand(&st, -1.0f, 1.0f);
        float *y = malloc((size_t)p->groups * p->n * 4);
        OmegaFp4GemvSpec sp = { .k = p->k, .lpa = p->lpa, .rows = p->rows, .warps = p->warps };
        double t0 = now_ms();
        int rc = sim_gemv(&sp, &W, p->groups, ids, x, p->x_rows, y, cta_budget, 0, msg, sizeof msg);
        double worst = 1.0; size_t diff = (size_t)-1;
        if (rc == 0) diff = compare(&W, p->groups, ids, x, p->x_rows, p->lpa, y, &worst);
        if (rc == 0 && worst > worst_all) worst_all = worst;
        CHECK(rc == 0 && diff == 0 && worst < 3e-6, "sim %s: rc=%d outputs differing from the emulation=%zu worst_vs_oracle=%.3e %s", p->name, rc, diff, worst, msg);
        printf("sim %-52s rc=%d bit-identical to emulation: %s, worst vs oracle %.3e (%.0f ms)\n", p->name, rc, diff == 0 ? "yes" : "NO", worst, now_ms() - t0);
        /* mutants: each must change the output (bit level) or fail */
        if (i == 1 || i == 4 || i == 8 || i == 9) {
            for (int m = 1; m <= 5; m++) {
                if (m == 4 && p->groups == 1) continue;
                OmegaFp4GemvSpec ms = sp; ms.mutant = m == 5 ? 0 : m;
                rc = sim_gemv(&ms, &W, p->groups, ids, x, p->x_rows, y, cta_budget, m == 5, msg, sizeof msg);
                size_t d2 = (size_t)-1; double w2 = 1.0;
                if (rc == 0) d2 = compare(&W, p->groups, ids, x, p->x_rows, p->lpa, y, &w2);
                int caught = rc != 0 || d2 > 0;
                CHECK(caught, "sim %s mutant %d NOT caught", p->name, m);
                printf("sim %-52s mutant %d: %s (rc=%d differing=%zu worst_vs_oracle=%.3e)\n", p->name, m, caught ? "caught" : "MISSED", rc, d2, w2);
            }
        }
        free(x); free(y); fp4w_free(&W);
    }
    printf("sim battery: worst relative error vs oracle %.3e over %zu points, %" PRIu64 " warp issues\n", worst_all, npts, g_sim_issues);
}

/* ------------------------------------------------------------------- chip */
typedef struct { uint32_t experts, n, k, groups, x_rows; const char *name; } ChipPoint;
static int g_evict = 1;
static void evict_host_caches(void) {
    static uint8_t *buf; const size_t sz = 512u << 20; static uint8_t pass;
    if (!buf) buf = malloc(sz);
    if (!buf) return;
    memset(buf, ++pass, sz);
    volatile uint64_t sum = 0; for (size_t i = 0; i < sz; i += 64) sum += buf[i];
}
static const char *g_only;

static int chip_run(const char *out_path, uint32_t reps) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out_path && !out) { printf("cannot open %s\n", out_path); return 1; }
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_FP4_GEMV\",\"points\":[");
    static const ChipPoint pts[] = {
        { 1, 128, 512, 1, 1, "k512 n128" },
        { 1, 256, 768, 1, 1, "k768 n256" },
        { 1, 1536, 2048, 1, 1, "gate+up 1536x2048" },
        { 1, 2048, 768, 1, 1, "down 2048x768" },
        { 8, 1536, 2048, 8, 1, "8 experts gate+up shared x" },
        { 8, 2048, 768, 8, 8, "8 experts down per-group x" },
        { 16, 1536, 2048, 8, 1, "8 of 16 experts gate+up" },
    };
    /* 0. one-hot tables on the chip (codes incl. subnormal code 1, scale bytes incl. subnormal scales): no flush to zero */
    {
        Fp4W W; W.experts = 1; W.n = 256; W.k = 512;
        W.w4 = aligned_alloc(64, 256 * 256); memset(W.w4, 0, 256 * 256); W.sc = aligned_alloc(64, 256 * 32); memset(W.sc, 0, 256 * 32); W.ts = malloc(4); W.ts[0] = 0.01f;
        for (int r = 0; r < 16; r++) { W.w4[r * 256] = (uint8_t)r; W.sc[r * 32] = 0x38; }
        for (int r = 16; r < 143; r++) { W.w4[r * 256] = 2; W.sc[r * 32] = (uint8_t)(r - 16); }
        float x[512] = { 0 }, y[256]; x[0] = 1.0f; uint32_t id = 0; OmegaGpuFp4Tensor *t = NULL;
        int rc = omega_gpu_fp4_tensor_upload(1, 256, 512, W.w4, W.sc, W.ts, &t);
        if (rc == OMEGA_GPU_FP4_OK) rc = omega_gpu_fp4_gemv_grouped_f32(t, 1, &id, x, 1, y, NULL);
        int exact = 0;
        if (rc == OMEGA_GPU_FP4_OK) for (int r = 0; r < 143; r++) {
            float code_v = r < 16 ? omega_fp4_value((uint8_t)r) : 1.0f;
            float sv = r < 16 ? 1.0f : omega_fp4_e4m3_unsigned((uint8_t)(r - 16));
            float ref = code_v * (sv * W.ts[0]);
            if (y[r] == ref) exact++; else CHECK(0, "chip one-hot row %d: kernel %a, expected %a", r, y[r], ref);
        }
        CHECK(rc == OMEGA_GPU_FP4_OK && exact == 143, "chip one-hot tables: rc=%s exact=%d/143 (%s)", omega_gpu_fp4_rc_name(rc), exact, omega_gpu_fp4_last_error());
        printf("chip one-hot tables: rc=%s %d/143 rows exact (16 codes, 127 scale bytes: no flush to zero)\n", omega_gpu_fp4_rc_name(rc), exact);
        if (out) fprintf(out, "{\"name\":\"one-hot tables\",\"rc\":\"%s\",\"exact_rows\":%d,\"of\":143}", omega_gpu_fp4_rc_name(rc), exact);
        if (t) omega_gpu_fp4_tensor_free(t);
        fp4w_free(&W);
    }
    const size_t npts = sizeof pts / sizeof pts[0];
    int first = out ? 0 : 1;
    for (size_t i = 0; i < npts; i++) {
        const ChipPoint *p = &pts[i];
        if (g_only && !strstr(p->name, g_only)) continue;
        Fp4W W; fp4w_make(&W, p->experts, p->n, p->k, 5000 + (uint32_t)i, 0);
        uint32_t ids[8]; uint32_t st = 99 + (uint32_t)i;
        for (uint32_t g = 0; g < p->groups; g++) ids[g] = (p->experts > p->groups ? lcg(&st) % p->experts : (p->groups - 1 - g) % p->experts);
        if (p->groups > 1 && p->experts > 1 && ids[0] == ids[1]) ids[1] = (ids[0] + 1) % p->experts;
        float *x = malloc((size_t)p->x_rows * p->k * 4); for (size_t j = 0; j < (size_t)p->x_rows * p->k; j++) x[j] = frand(&st, -1.0f, 1.0f);
        float *y = malloc((size_t)p->groups * p->n * 4), *y2 = malloc((size_t)p->groups * p->n * 4);
        OmegaGpuFp4Tensor *t = NULL;
        double t0 = now_ms();
        int rc = omega_gpu_fp4_tensor_upload(p->experts, p->n, p->k, W.w4, W.sc, W.ts, &t);
        double up_ms = now_ms() - t0;
        OmegaGpuFp4Info info; memset(&info, 0, sizeof info);
        if (rc == OMEGA_GPU_FP4_OK) rc = omega_gpu_fp4_gemv_grouped_f32(t, p->groups, ids, x, p->x_rows, y, &info);
        double worst = 1.0; size_t diff = (size_t)-1;
        if (rc == OMEGA_GPU_FP4_OK) diff = compare(&W, p->groups, ids, x, p->x_rows, info.lpa, y, &worst);
        int ok = rc == OMEGA_GPU_FP4_OK && diff == 0;
        int same = 0; OmegaGpuFp4Info i2; memset(&i2, 0, sizeof i2);
        if (ok) { int r2 = omega_gpu_fp4_gemv_grouped_f32(t, p->groups, ids, x, p->x_rows, y2, &i2); same = r2 == OMEGA_GPU_FP4_OK && memcmp(y, y2, (size_t)p->groups * p->n * 4) == 0 && i2.kernel_cache_hit; }
        CHECK(ok && same && worst < 3e-6, "chip %s: rc=%s differing-from-emulation=%zu worst_vs_oracle=%.3e second-call-identical=%d (%s)", p->name, omega_gpu_fp4_rc_name(rc), diff, worst, same, omega_gpu_fp4_last_error());
        uint64_t *el = calloc(reps, sizeof *el), *cl = calloc(reps, sizeof *cl);
        uint32_t done = 0;
        if (g_evict) evict_host_caches();
        for (uint32_t r = 0; r < reps && ok; r++) {
            OmegaGpuFp4Info ti; if (omega_gpu_fp4_gemv_grouped_f32(t, p->groups, ids, x, p->x_rows, y2, &ti) != OMEGA_GPU_FP4_OK) break;
            el[r] = ti.elapsed_ns; cl[r] = ti.call_ns; done++;
        }
        uint64_t med_el = 0, med_cl = 0, min_el = 0, max_el = 0; double gbs = 0.0;
        if (done) { qsort(el, done, sizeof *el, cmp_u64); qsort(cl, done, sizeof *cl, cmp_u64); med_el = el[done / 2]; med_cl = cl[done / 2]; min_el = el[0]; max_el = el[done - 1];
                    gbs = med_el ? (double)(info.weight_bytes + info.scale_bytes) / (double)med_el : 0.0; }
        printf("chip %-30s rc=%-9s differing=%zu worst_vs_oracle=%.3e grid=%ux%u lpa=%u rows=%u warps=%u bytes=%" PRIu64 " upload=%.1fms chip_med=%.1fus (min %.1f max %.1f) %.1f GB/s call_med=%.1fus reps=%u\n",
               p->name, omega_gpu_fp4_rc_name(rc), diff, worst, info.grid_x, info.grid_y, info.lpa, info.rows, info.warps, info.weight_bytes + info.scale_bytes, up_ms, med_el / 1e3, min_el / 1e3, max_el / 1e3, gbs, med_cl / 1e3, done);
        if (out) fprintf(out, "%s{\"name\":\"%s\",\"experts\":%u,\"n\":%u,\"k\":%u,\"groups\":%u,\"x_rows\":%u,\"rc\":\"%s\",\"differing\":%zu,\"worst_vs_oracle\":%.3e,\"second_call_identical\":%d,"
                         "\"grid_x\":%u,\"grid_y\":%u,\"lpa\":%u,\"rows\":%u,\"warps\":%u,\"bytes\":%" PRIu64 ",\"upload_ms\":%.2f,\"chip_median_ns\":%" PRIu64 ",\"chip_min_ns\":%" PRIu64 ",\"chip_max_ns\":%" PRIu64 ",\"gbps_chip\":%.2f,\"call_median_ns\":%" PRIu64 ",\"reps\":%u,\"evict\":%d}",
                         first ? "" : ",", p->name, p->experts, p->n, p->k, p->groups, p->x_rows, omega_gpu_fp4_rc_name(rc), diff, worst, same,
                         info.grid_x, info.grid_y, info.lpa, info.rows, info.warps, info.weight_bytes + info.scale_bytes, up_ms, med_el, min_el, max_el, gbs, med_cl, done, g_evict);
        first = 0;
        free(el); free(cl);
        if (ok && (i == 2 || i == 4 || i == 5)) {
            for (int m = 1; m <= 5; m++) {
                if (m == 4 && p->groups == 1) continue;
                if (m == 5) omega_gpu_fp4_test_set_no_fold(1); else omega_gpu_fp4_test_set_mutant(m);
                OmegaGpuFp4Tensor *tm = t;
                if (m == 5) { tm = NULL; if (omega_gpu_fp4_tensor_upload(p->experts, p->n, p->k, W.w4, W.sc, W.ts, &tm) != OMEGA_GPU_FP4_OK) tm = NULL; }
                int r = tm ? omega_gpu_fp4_gemv_grouped_f32(tm, p->groups, ids, x, p->x_rows, y2, NULL) : OMEGA_GPU_FP4_CHIP_FAIL;
                size_t md = (size_t)-1; double mw = 1.0;
                if (r == OMEGA_GPU_FP4_OK) md = compare(&W, p->groups, ids, x, p->x_rows, info.lpa, y2, &mw);
                int caught = r != OMEGA_GPU_FP4_OK || md > 0;
                CHECK(caught, "chip %s mutant %d NOT caught", p->name, m);
                printf("chip %-30s mutant %d: %s (rc=%s differing=%zu worst_vs_oracle=%.3e)\n", p->name, m, caught ? "caught" : "MISSED", omega_gpu_fp4_rc_name(r), md, mw);
                if (out) fprintf(out, ",{\"name\":\"%s mutant %d\",\"caught\":%d,\"rc\":\"%s\",\"differing\":%zu,\"worst_vs_oracle\":%.3e}", p->name, m, caught, omega_gpu_fp4_rc_name(r), md, mw);
                if (m == 5) { omega_gpu_fp4_test_set_no_fold(0); if (tm) omega_gpu_fp4_tensor_free(tm); } else omega_gpu_fp4_test_set_mutant(0);
            }
        }
        omega_gpu_fp4_tensor_free(t);
        free(x); free(y); free(y2); fp4w_free(&W);
        if (omega_gpu_session_is_blocked()) { printf("session latched: stopping\n"); break; }
    }
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"device_opens\":%u,\"verdict\":\"%s\"}\n", g_checks, g_failed, omega_gpu_session_open_count(), g_failed == 0 ? "OMEGA_GPU_FP4_GEMV_PASS" : "OMEGA_GPU_FP4_GEMV_FAIL"); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0; const char *out_path = NULL; uint32_t reps = 20, lpa = 0, rows = 0, warps = 0, budget = 0, spin = 2000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--lpa") == 0 && i + 1 < argc) lpa = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) rows = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--warps") == 0 && i + 1 < argc) warps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--cta-budget") == 0 && i + 1 < argc) budget = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--spin-us") == 0 && i + 1 < argc) spin = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-evict") == 0) g_evict = 0;
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) g_only = argv[++i];
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (host) host_only();
    if (sim) sim_battery(budget ? budget : 4);
    if (!host && !sim) {
        if (lpa || rows || warps) omega_gpu_fp4_set_shape(lpa, rows, warps);
        if (budget) omega_gpu_fp4_set_cta_budget(budget);
        (void)omega_gpu_session_set_spin_us(spin);
        chip_run(out_path, reps ? reps : 1);
        omega_gpu_session_close();
    }
    printf("gpu_fp4_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
