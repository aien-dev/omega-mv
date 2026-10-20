/* gpu_train_probe_test.c (aien-train L279, 2026-10-20): does a training step stay on the math cores?
 *   --host-only   the LMS probe of src/omega_blackwell_codegen_train.h in the warp simulator, resident and streamed,
 *                 every weight and loss checked bit for bit against a host fmaf() reference.
 *   (default)     the chip (caller holds the GB10 lock, under quiet-measure): one model, the same steps, many ways:
 *                   read_once         W loaded once, all steps in registers, W stored once (and a quarter-length run for the slope)
 *                   streamed_dram_bN  W loaded and stored every N samples, GPU-uncached buffer, L1 invalidated first (memory)
 *                   controls          batch 1 without the L1 invalidate, and from a GPU-cached buffer (L2), with and without
 *                 Each: 5 launches, median kernel time. Checks: final W and loss bit-identical to read_once in every mode
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
    const uint32_t T = 64, C = 2, rows = T * C, steps = 8;
    static float x[SAMPLES * IN] __attribute__((aligned(16))), y[SAMPLES * 128], wd[IN * 128] __attribute__((aligned(16))), l[128];
    static const uint32_t batch[3] = { 0, 1, 4 };
    make_data(rows, x, y);
    for (int m = 0; m < 3; m++) {
        OmegaTrainProbeSpec sp = { IN, T, C, steps, SAMPLES - 1u, f2u(LR), batch[m], batch[m] != 0 };
        if (omega_blackwell_codegen_train_lms_prog(&sp, &p) != 0) { printf("codegen refused batch %u\n", batch[m]); return 1; }
        memset(wd, 0, sizeof wd); memset(l, 0xff, sizeof l);
        memset(&sim, 0, sizeof sim); sim.p = &p; sim.threads = T;
        uint64_t a[4] = { (uintptr_t)wd, (uintptr_t)x, (uintptr_t)y, (uintptr_t)l };
        size_t n[4] = { sizeof wd, sizeof x, sizeof y, sizeof l };
        for (int k = 0; k < 4; k++) { sim.cbank[(0x380 + 8 * k) / 4] = (uint32_t)a[k]; sim.cbank[(0x384 + 8 * k) / 4] = (uint32_t)(a[k] >> 32);
            sim.ranges[k].lo = a[k]; sim.ranges[k].hi = a[k] + n[k]; }
        sim.nranges = 4;
        int err = ws_run(&sim, C, 1); size_t bad = 0;
        for (uint32_t g = 0; g < rows; g++) { float w[IN], lo; ref_row(g, rows, steps, x, y, w, &lo);
            for (uint32_t i = 0; i < IN; i++) if (f2u(w_at(wd, rows, g, i)) != f2u(w[i])) bad++;
            if (f2u(l[g]) != f2u(lo)) bad++; }
        printf("sim batch=%u%s: %s, %u registers, %zu wrong of %u\n", batch[m], batch[m] ? "" : " (read once)", err ? sim.msg : "ran",
               p.regalloc.peak_gpr_usage, bad, rows * (IN + 1));
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


/* One run configuration. buf: 0 GPU-uncached session buffer (weights to and from memory), 1 GPU-cached device buffer. */
typedef struct { const char *name; uint32_t steps, batch; int ivall, buf; } Run;

static int chip(uint32_t steps) {
    static BlackwellIRProgram p; int fail = 0;
    const uint32_t C = 48; /* one CTA per SM */
    OmegaTrainProbeSpec sp = { IN, 1024, C, steps, SAMPLES - 1u, f2u(LR), 1, 1 };
    if (omega_blackwell_codegen_train_lms_prog(&sp, &p) != 0) { printf("codegen refused\n"); return 1; }
    uint32_t decl = omega_bw_gpr_declared(p.regalloc.peak_gpr_usage), T = 65536u / decl / 32u * 32u;
    if (T > 1024) T = 1024;
    const uint32_t rows = T * C;
    const double flops_step = 4.0 * IN * rows; /* forward 2*in, update 2*in per row (the gradient product rides in the update FMA) */
    printf("TRAIN config in=%u rows=%u (threads %u x CTAs %u) params=%u steps=%u samples=%u lr=%g registers=%u declared=%u flops_per_step=%.0f\n",
           IN, rows, T, C, rows * IN, steps, SAMPLES, (double)LR, p.regalloc.peak_gpr_usage, decl, flops_step);
    size_t wb = (size_t)rows * IN * 4, xb = (size_t)SAMPLES * IN * 4, yb = (size_t)SAMPLES * rows * 4, lb = (size_t)rows * 4;
    float *x = malloc(xb), *y = malloc(yb), *zero = calloc(1, wb), *w0 = malloc(wb), *wt = malloc(wb), *l0 = malloc(lb), *lt = malloc(lb);
    if (!x || !y || !zero || !w0 || !wt || !l0 || !lt) return 1;
    make_data(rows, x, y);
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); return 1; }
    OmegaGpuMem X = { 0 }, Y = { 0 }, L = { 0 }, Wc = { 0 }, Wu = { 0 };
    if (omega_gpu_pf_alloc_dev(xb, &X) || omega_gpu_pf_alloc_dev(yb, &Y) || omega_gpu_session_alloc(lb, &L) ||
        omega_gpu_pf_alloc_dev(wb, &Wc) || omega_gpu_session_alloc(wb, &Wu) ||
        omega_gpu_pf_upload(&X, 0, x, xb) || omega_gpu_pf_upload(&Y, 0, y, yb)) { printf("alloc/upload failed: %s\n", omega_gpu_session_last_error()); return 1; }
    const Run runs[] = {
        { "read_once", steps, 0, 0, 0 }, { "read_once_quarter", steps / 4, 0, 0, 0 },
        { "streamed_dram_b1", steps, 1, 1, 0 }, { "streamed_dram_b4", steps, 4, 1, 0 }, { "streamed_dram_b16", steps, 16, 1, 0 },
        { "streamed_dram_b64", steps, 64, 1, 0 }, { "streamed_dram_b256", steps, 256, 1, 0 }, { "streamed_dram_b1024", steps, 1024, 1, 0 },
        { "streamed_dram_b1_noivall", steps, 1, 0, 0 }, { "streamed_cached_b1", steps, 1, 1, 1 }, { "streamed_cached_b1_noivall", steps, 1, 0, 1 },
    };
    const int nr = (int)(sizeof runs / sizeof runs[0]);
    double med[16] = { 0 }; size_t diff = 0;
    for (int m = 0; m < nr; m++) {
        const Run *R = &runs[m];
        OmegaTrainProbeSpec s2 = { IN, T, C, R->steps, SAMPLES - 1u, f2u(LR), R->batch, R->ivall };
        Kern k;
        if (omega_blackwell_codegen_train_lms_prog(&s2, &p) != 0 || upload(&p, &k)) { printf("codegen/upload failed %s\n", R->name); return 1; }
        OmegaGpuMem *W = R->buf ? &Wc : &Wu;
        uint64_t ns[5];
        for (int r = 0; r < 5; r++) {
            if (R->buf ? omega_gpu_pf_upload(W, 0, zero, wb) : (memset(W->cpu, 0, wb), omega_gpu_session_sync_to_gpu(W))) { printf("reset failed\n"); return 1; }
            memset(L.cpu, 0xff, lb); if (omega_gpu_session_sync_to_gpu(&L)) return 1;
            uint32_t args[8] = { (uint32_t)W->va, (uint32_t)(W->va >> 32), (uint32_t)X.va, (uint32_t)(X.va >> 32),
                                 (uint32_t)Y.va, (uint32_t)(Y.va >> 32), (uint32_t)L.va, (uint32_t)(L.va >> 32) };
            OmegaGpuLaunch Lh = { .code_va = k.code.va, .gpr_count = k.decl, .threads_x = T, .threads_y = 1, .grid_x = C, .grid_y = 1,
                                  .num_elements = rows, .args = args, .n_args = 8, .timeout_ms = 60000 };
            ns[r] = 0;
            if (omega_gpu_session_launch(&Lh, &ns[r], NULL)) { printf("launch failed %s: %s\n", R->name, omega_gpu_session_last_error()); return 1; }
        }
        float *wd = m == 0 ? w0 : wt, *ld = m == 0 ? l0 : lt;
        if (R->buf ? omega_gpu_pf_download(W, 0, wd, wb) : (omega_gpu_session_sync_to_cpu(W) || (memcpy(wd, W->cpu, wb), 0))) return 1;
        if (omega_gpu_session_sync_to_cpu(&L)) return 1;
        memcpy(ld, L.cpu, lb);
        qsort(ns, 5, sizeof ns[0], cmp_u64); med[m] = (double)ns[2];
        double per = med[m] / R->steps, wbytes = R->batch ? 2.0 * wb / R->batch : 0;
        printf("TRAIN %s steps=%u batch=%u ivall=%d buffer=%s median_ms=%.3f min_ms=%.3f max_ms=%.3f ns_per_step=%.1f W_bytes_per_step=%.0f "
               "data_bytes_per_step=%.0f W_GBps=%.1f TFLOPS=%.4f\n", R->name, R->steps, R->batch, R->ivall, R->buf ? "gpu_cached" : "gpu_uncached",
               med[m] / 1e6, ns[0] / 1e6, ns[4] / 1e6, per, wbytes, (double)rows * 4 + IN * 4, wbytes / per, flops_step / per / 1e3);
        if (m != 1) { /* every full-length run must end bit-identical to read_once */
            size_t d = 0; for (size_t q = 0; q < wb / 4; q++) d += f2u(wd[q]) != f2u(w0[q]);
            for (size_t q = 0; q < rows; q++) d += f2u(ld[q]) != f2u(l0[q]);
            if (d) printf("TRAIN check %s differs from read_once in %zu words\n", R->name, d);
            diff += d;
        }
        omega_gpu_session_free(&k.code);
    }
    printf("TRAIN check modes_identical words_differing=%zu\n", diff); if (diff) fail++;
    printf("TRAIN slope read_once ns_per_step=%.2f (from the %u- and %u-step runs, launch cost removed)\n", (med[0] - med[1]) / (steps - steps / 4), steps, steps / 4);
    size_t bad = 0, checked = 0; double a0 = 0, a1 = 0;
    for (uint32_t g = 0; g < rows; g += 97) { float w[IN], lo; ref_row(g, rows, steps, x, y, w, &lo); checked++;
        for (uint32_t i = 0; i < IN; i++) bad += f2u(w_at(w0, rows, g, i)) != f2u(w[i]);
        bad += f2u(l0[g]) != f2u(lo);
        a0 += data_loss(NULL, rows, g, x, y); a1 += data_loss(w0, rows, g, x, y); }
    printf("TRAIN check host_reference rows=%zu words_wrong=%zu\n", checked, bad); if (bad) fail++;
    printf("TRAIN check data_loss before=%.6e after=%.6e ratio=%.3e\n", a0 / checked, a1 / checked, a1 / a0);
    if (!(a1 < a0 * 1e-2)) fail++;
    omega_gpu_pf_free(&X); omega_gpu_pf_free(&Y); omega_gpu_pf_free(&Wc); omega_gpu_session_free(&Wu); omega_gpu_session_free(&L);
    omega_gpu_session_close();
    printf("GPU_TRAIN_PROBE: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--host-only")) { int f = host_only(); printf("GPU_TRAIN_PROBE host: %s\n", f ? "FAIL" : "PASS"); return f ? 1 : 0; }
    uint32_t steps = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 16384u;
    if (steps < 4096 || steps % 4096) { printf("steps must be a multiple of 4096\n"); return 2; }
    return chip(steps);
}
