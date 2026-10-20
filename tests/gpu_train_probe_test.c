/* gpu_train_probe_test.c (aien-train L279, 2026-10-20): does a training step stay on the math cores?
 *   --host-only   the LMS probe of src/omega_blackwell_codegen_train.h in the warp simulator, resident and streamed,
 *                 every weight and loss checked bit for bit against a host fmaf() reference.
 *   (default)     the chip (caller holds the GB10 lock, under quiet-measure): one model, three ways, same steps:
 *                   resident          W loaded once, all steps in registers, W stored once
 *                   streamed_cached   W loaded and stored every step, GPU-cached buffer (L2 can hold it)
 *                   streamed_dram     W loaded and stored every step, GPU-uncached buffer (memory every step)
 *                 Each: 5 launches, median kernel time. Checks: final W and loss bit-identical across the three modes
 *                 for every row; a host fmaf() reference for every 97th row; training loss on the data set before
 *                 (W = 0) and after, from the downloaded weights.
 * Output: "TRAIN <mode> ..." lines, "TRAIN check ..." lines, then "GPU_TRAIN_PROBE: PASS|FAIL". */
#include "omega_blackwell_codegen_train.h"
#include "omega_gpu_session.h"
#include "omega_gpu_prefill.h"
#include "bw_warp_sim_tc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN 32u
#define SAMPLES 64u
static const float LR = 0.02f;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float uni(uint64_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33;
    return (float)((double)(k >> 40) / (double)(1ull << 24)) * 2.0f - 1.0f; }

/* data set: x[s][i], targets y[s][g] = teacher row g . x[s] (fmaf chain from zero) */
static void make_data(uint32_t rows, float *x, float *y) {
    for (uint32_t s = 0; s < SAMPLES; s++) for (uint32_t i = 0; i < IN; i++) x[s * IN + i] = uni(0x1000000ull + s * IN + i);
    for (uint32_t g = 0; g < rows; g++) {
        float t[IN]; for (uint32_t i = 0; i < IN; i++) t[i] = uni(0x9000000000ull + (uint64_t)g * IN + i);
        for (uint32_t s = 0; s < SAMPLES; s++) { float a = 0; for (uint32_t i = 0; i < IN; i++) a = fmaf(t[i], x[s * IN + i], a); y[(size_t)s * rows + g] = a; }
    }
}
/* host reference for one row: the same instruction order as the kernel */
static void ref_row(uint32_t g, uint32_t rows, uint32_t steps, const float *x, const float *y, float *w, float *loss) {
    for (uint32_t i = 0; i < IN; i++) w[i] = 0;
    float l = 0;
    for (uint32_t cnt = steps; cnt >= 1; cnt--) {
        uint32_t s = cnt & (SAMPLES - 1u); const float *xs = x + s * IN;
        float a = 0; for (uint32_t i = 0; i < IN; i++) a = fmaf(w[i], xs[i], a);
        float e = a - y[(size_t)s * rows + g];
        l = fmaf(e, e, l);
        float sc = e * -LR;
        for (uint32_t i = 0; i < IN; i++) w[i] = fmaf(xs[i], sc, w[i]);
    }
    *loss = l;
}
/* W device layout -> row g weight i */
static float w_at(const float *wd, uint32_t rows, uint32_t g, uint32_t i) { return wd[((size_t)(i / 4) * rows + g) * 4 + (i % 4)]; }
static double data_loss(const float *wd, uint32_t rows, uint32_t g, const float *x, const float *y) {
    double l = 0;
    for (uint32_t s = 0; s < SAMPLES; s++) { double a = 0; for (uint32_t i = 0; i < IN; i++) a += (double)(wd ? w_at(wd, rows, g, i) : 0) * x[s * IN + i];
        double e = a - y[(size_t)s * rows + g]; l += e * e; }
    return l / SAMPLES;
}

static int host_only(void) {
    static BlackwellIRProgram p; static WsSim sim; int fail = 0;
    const uint32_t T = 64, C = 2, rows = T * C, steps = 9;
    static float x[SAMPLES * IN], y[SAMPLES * 128], wd[2][IN * 128], l[2][128];
    make_data(rows, x, y);
    for (int mode = 0; mode < 2; mode++) {
        OmegaTrainProbeSpec sp = { IN, T, C, steps, SAMPLES - 1u, f2u(LR), mode };
        if (omega_blackwell_codegen_train_lms_prog(&sp, &p) != 0) { printf("codegen refused mode %d\n", mode); return 1; }
        memset(wd[mode], 0, sizeof wd[mode]); memset(l[mode], 0xff, sizeof l[mode]);
        memset(&sim, 0, sizeof sim); sim.p = &p; sim.threads = T;
        uint64_t a[4] = { (uintptr_t)wd[mode], (uintptr_t)x, (uintptr_t)y, (uintptr_t)l[mode] };
        size_t n[4] = { sizeof wd[mode], sizeof x, sizeof y, sizeof l[mode] };
        for (int k = 0; k < 4; k++) { sim.cbank[(0x380 + 8 * k) / 4] = (uint32_t)a[k]; sim.cbank[(0x384 + 8 * k) / 4] = (uint32_t)(a[k] >> 32);
            sim.ranges[k].lo = a[k]; sim.ranges[k].hi = a[k] + n[k]; }
        sim.nranges = 4;
        int err = ws_run(&sim, C, 1); size_t bad = 0;
        for (uint32_t g = 0; g < rows; g++) { float w[IN], lo; ref_row(g, rows, steps, x, y, w, &lo);
            for (uint32_t i = 0; i < IN; i++) if (f2u(w_at(wd[mode], rows, g, i)) != f2u(w[i])) bad++;
            if (f2u(l[mode][g]) != f2u(lo)) bad++; }
        printf("sim %s: %s, %u registers, %zu wrong of %u\n", mode ? "streamed" : "resident", err ? sim.msg : "ran", p.regalloc.peak_gpr_usage, bad, rows * (IN + 1));
        if (err || bad) fail++;
    }
    return fail;
}

typedef struct { OmegaGpuMem code; uint32_t decl; } Kern;
static int upload(BlackwellIRProgram *p, Kern *k) {
    OmegaBlackwellKernel bk; size_t need = omega_bw_program_code_bytes(p);
    k->decl = omega_bw_gpr_declared(p->regalloc.peak_gpr_usage);
    return omega_gpu_session_alloc_code(need, &k->code) != 0 || omega_blackwell_kernel_encode_into(p, k->code.cpu, need, &bk) != 0 ||
           omega_gpu_session_sync_to_gpu(&k->code) != 0;
}
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static int chip(uint32_t steps) {
    static BlackwellIRProgram p; int fail = 0;
    /* threads: the most rows per SM the register file allows (one CTA per SM, 48 SMs) */
    const uint32_t C = 48;
    OmegaTrainProbeSpec sp = { IN, 1024, C, steps, SAMPLES - 1u, f2u(LR), 1 };
    if (omega_blackwell_codegen_train_lms_prog(&sp, &p) != 0) { printf("codegen refused\n"); return 1; }
    uint32_t decl = omega_bw_gpr_declared(p.regalloc.peak_gpr_usage), T = 65536u / decl / 32u * 32u;
    if (T > 1024) T = 1024;
    const uint32_t rows = T * C;
    printf("TRAIN config in=%u rows=%u (threads %u x CTAs %u) params=%u steps=%u samples=%u lr=%g registers=%u declared=%u\n",
           IN, rows, T, C, rows * IN, steps, SAMPLES, (double)LR, p.regalloc.peak_gpr_usage, decl);
    size_t wb = (size_t)rows * IN * 4, xb = (size_t)SAMPLES * IN * 4, yb = (size_t)SAMPLES * rows * 4, lb = (size_t)rows * 4;
    float *x = malloc(xb), *y = malloc(yb), *wout[3];
    float *ls[3];
    for (int m = 0; m < 3; m++) { wout[m] = calloc(1, wb); ls[m] = calloc(1, lb); }
    if (!x || !y) return 1;
    make_data(rows, x, y);
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); return 1; }
    OmegaGpuMem X = { 0 }, Y = { 0 }, L = { 0 }, Wc = { 0 }, Wu = { 0 };
    if (omega_gpu_pf_alloc_dev(xb, &X) || omega_gpu_pf_alloc_dev(yb, &Y) || omega_gpu_session_alloc(lb, &L) ||
        omega_gpu_pf_alloc_dev(wb, &Wc) || omega_gpu_session_alloc(wb, &Wu) ||
        omega_gpu_pf_upload(&X, 0, x, xb) || omega_gpu_pf_upload(&Y, 0, y, yb)) { printf("alloc/upload failed: %s\n", omega_gpu_session_last_error()); return 1; }
    float *zero = calloc(1, wb);
    static const char *name[3] = { "resident", "streamed_cached", "streamed_dram" };
    double med_ns[3] = { 0 };
    for (int m = 0; m < 3; m++) {
        OmegaTrainProbeSpec s2 = { IN, T, C, steps, SAMPLES - 1u, f2u(LR), m != 0 };
        Kern k;
        if (omega_blackwell_codegen_train_lms_prog(&s2, &p) != 0 || upload(&p, &k)) { printf("codegen/upload failed %s\n", name[m]); return 1; }
        OmegaGpuMem *W = m == 1 ? &Wc : &Wu;
        uint64_t ns[5];
        for (int r = 0; r < 5; r++) {
            if (m == 1 ? omega_gpu_pf_upload(W, 0, zero, wb) : (memset(W->cpu, 0, wb), omega_gpu_session_sync_to_gpu(W))) { printf("reset failed\n"); return 1; }
            memset(L.cpu, 0xff, lb); if (omega_gpu_session_sync_to_gpu(&L)) return 1;
            uint32_t args[8] = { (uint32_t)W->va, (uint32_t)(W->va >> 32), (uint32_t)X.va, (uint32_t)(X.va >> 32),
                                 (uint32_t)Y.va, (uint32_t)(Y.va >> 32), (uint32_t)L.va, (uint32_t)(L.va >> 32) };
            OmegaGpuLaunch Lh = { .code_va = k.code.va, .gpr_count = k.decl, .threads_x = T, .threads_y = 1, .grid_x = C, .grid_y = 1,
                                  .num_elements = rows, .args = args, .n_args = 8, .timeout_ms = 60000 };
            ns[r] = 0;
            if (omega_gpu_session_launch(&Lh, &ns[r], NULL)) { printf("launch failed %s: %s\n", name[m], omega_gpu_session_last_error()); return 1; }
        }
        if (m == 1 ? omega_gpu_pf_download(W, 0, wout[m], wb) : (omega_gpu_session_sync_to_cpu(W) || (memcpy(wout[m], W->cpu, wb), 0))) return 1;
        if (omega_gpu_session_sync_to_cpu(&L)) return 1;
        memcpy(ls[m], L.cpu, lb);
        qsort(ns, 5, sizeof ns[0], cmp_u64); med_ns[m] = (double)ns[2];
        double per = med_ns[m] / steps, wbytes = m ? 2.0 * wb : 0, dbytes = (double)rows * 4 + IN * 4;
        printf("TRAIN %s median_ms=%.3f min_ms=%.3f max_ms=%.3f us_per_step=%.4f W_bytes_per_step=%.0f data_bytes_per_step=%.0f W_GBps=%.1f\n",
               name[m], med_ns[m] / 1e6, ns[0] / 1e6, ns[4] / 1e6, per / 1e3, wbytes, dbytes, m ? wbytes / per : 0.0);
        omega_gpu_session_free(&k.code);
    }
    /* checks */
    size_t diff = 0;
    for (int m = 1; m < 3; m++) { if (memcmp(wout[0], wout[m], wb)) for (size_t q = 0; q < wb / 4; q++) diff += f2u(wout[0][q]) != f2u(wout[m][q]);
                                  if (memcmp(ls[0], ls[m], lb)) for (size_t q = 0; q < rows; q++) diff += f2u(ls[0][q]) != f2u(ls[m][q]); }
    printf("TRAIN check modes_identical words_differing=%zu\n", diff); if (diff) fail++;
    size_t bad = 0, checked = 0; double l0 = 0, l1 = 0;
    for (uint32_t g = 0; g < rows; g += 97) { float w[IN], lo; ref_row(g, rows, steps, x, y, w, &lo); checked++;
        for (uint32_t i = 0; i < IN; i++) bad += f2u(w_at(wout[0], rows, g, i)) != f2u(w[i]);
        bad += f2u(ls[0][g]) != f2u(lo);
        l0 += data_loss(NULL, rows, g, x, y); l1 += data_loss(wout[0], rows, g, x, y); }
    printf("TRAIN check host_reference rows=%zu words_wrong=%zu\n", checked, bad); if (bad) fail++;
    printf("TRAIN check data_loss before=%.6e after=%.6e ratio=%.3e\n", l0 / checked, l1 / checked, l1 / l0);
    if (!(l1 < l0 * 1e-2)) fail++;
    printf("TRAIN speedup resident_vs_streamed_dram=%.2f resident_vs_streamed_cached=%.2f\n", med_ns[2] / med_ns[0], med_ns[1] / med_ns[0]);
    omega_gpu_pf_free(&X); omega_gpu_pf_free(&Y); omega_gpu_pf_free(&Wc); omega_gpu_session_free(&Wu); omega_gpu_session_free(&L);
    omega_gpu_session_close();
    printf("GPU_TRAIN_PROBE: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--host-only")) { int f = host_only(); printf("GPU_TRAIN_PROBE host: %s\n", f ? "FAIL" : "PASS"); return f ? 1 : 0; }
    uint32_t steps = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 20000u;
    return chip(steps);
}
