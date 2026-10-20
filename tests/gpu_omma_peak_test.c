/* gpu_omma_peak_test.c (L284 own-fp4, 2026-10-20): dense rate of the GB10's 4-bit e2m1 tensor-core ops beside the FP8 control.
 *   --host-only  encoder self-test against the ptxas oracle bits (omega_blackwell_codegen_omma.h)
 *   (default)    chip (caller holds flock /tmp/aien-gb10.lock): per variant a chain of MMAs with constant A = B and a constant
 *                scale; every stored value must equal per_mma * iters (a scale the chip ignored or misread shows up as wrong
 *                values), the scale register must read back, then TFLOPS = flop per op * ops / median time of 5.
 * Verdict last: "GPU_OMMA_PEAK: PASS|FAIL". */
#include "omega_blackwell_codegen_omma.h"
#include "omega_blackwell_codegen_qmma.h"
#include "omega_gpu_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static int load_code(BlackwellIRProgram *prog, int svreg, OmegaGpuMem *code) {
    size_t need = omega_bw_program_code_bytes(prog); OmegaBlackwellKernel k;
    if (omega_gpu_session_alloc_code(need, code) != 0 || omega_blackwell_kernel_encode_into(prog, code->cpu, need, &k) != 0) return -1;
    int nq = omega_qmma_patch_code(prog, code->cpu, need), no = omega_omma_patch_code(prog, svreg, code->cpu, need);
    if (nq < 0 || no < 0 || nq + no < 1) { printf("patch failed (qmma %d omma %d)\n", nq, no); return -1; }
    return omega_gpu_session_sync_to_gpu(code);
}

/* name, variant, A/B data word, scale word, value added per op per output, flop per op (2*M*N*K) */
static const struct { const char *name; int v; uint32_t data, scale; float per; double flop; } VAR[] = {
    { "FP8 QMMA.16832.E4M3 (control)",        OMMA_V_E4M3, 0x38383838u, 0x00000000u,  32.0f,  8192.0 },
    { "FP4 QMMA.16832.E2M1 (no block scale)", OMMA_V_E2M1, 0x08080808u, 0x00000000u,  32.0f,  8192.0 },
    { "FP4 QMMA.SF.16832.E2M1 E8 x1 (k32)",   OMMA_V_SF8,  0x08080808u, 0x80808080u, 128.0f,  8192.0 },
    { "MXFP4 OMMA.SF.16864 E8 2X (32/scale)", OMMA_V_MX,   0x22222222u, 0x80808080u, 256.0f, 16384.0 },
    { "MXFP4 OMMA.SF.16864 E8 2X scale 1.0",  OMMA_V_MX,   0x22222222u, 0x7f7f7f7fu,  64.0f, 16384.0 },
    { "NVFP4 OMMA.SF.16864 UE4M3 4X (16/sc)", OMMA_V_NV,   0x22222222u, 0x40404040u, 256.0f, 16384.0 },
};

static int peak(void) {
    static const struct { uint32_t stall, chains, threads, ctas; } pts[] = {
        { 8, 16, 128, 192 }, { 2, 16, 256, 384 }, { 4, 8, 256, 384 }, { 1, 16, 256, 384 },
    };
    const uint32_t iters = 4096; int fail = 0;
    double best[sizeof VAR / sizeof VAR[0]] = { 0 };
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    for (size_t vi = 0; vi < sizeof VAR / sizeof VAR[0]; vi++)
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        static BlackwellIRProgram p; OmegaGpuMem code = { 0 }, out = { 0 }; int sv = -1;
        if (omega_blackwell_codegen_omma_peak_prog(VAR[vi].v, VAR[vi].data, VAR[vi].scale, iters, pts[i].chains, pts[i].stall, pts[i].threads, &p, &sv) != 0) { printf("codegen refused\n"); fail++; continue; }
        size_t per_t = 16u * pts[i].chains + 16u, ob = (size_t)pts[i].ctas * pts[i].threads * per_t;
        uint32_t decl = omega_bw_gpr_declared(p.regalloc.peak_gpr_usage);
        if (load_code(&p, sv, &code) != 0 || omega_gpu_session_alloc(ob, &out) != 0) { printf("upload failed: %s\n", omega_gpu_session_last_error()); fail++; break; }
        uint64_t ns[5]; int ok = 1; size_t bad = 0, badscale = 0; float first_bad = 0;
        for (int r = 0; r < 5 && ok; r++) {
            ns[r] = 0;
            float *o = out.cpu; for (size_t q = 0; q < ob / 4; q++) o[q] = -1.0f;
            uint32_t args[2] = { (uint32_t)out.va, (uint32_t)(out.va >> 32) };
            OmegaGpuLaunch L = { .code_va = code.va, .gpr_count = decl, .threads_x = pts[i].threads, .threads_y = 1, .grid_x = pts[i].ctas, .grid_y = 1,
                                 .num_elements = ob / 4, .args = args, .n_args = 2, .timeout_ms = 20000 };
            if (omega_gpu_session_sync_to_gpu(&out) != 0 || omega_gpu_session_launch(&L, &ns[r], NULL) != 0 || omega_gpu_session_sync_to_cpu(&out) != 0) { printf("launch failed: %s\n", omega_gpu_session_last_error()); ok = 0; break; }
            for (size_t t = 0; t < ob / per_t; t++) {
                const float *ot = (const float *)((const uint8_t *)out.cpu + t * per_t);
                for (uint32_t q = 0; q < 4u * pts[i].chains; q++) if (ot[q] != VAR[vi].per * (float)iters) { if (!bad) first_bad = ot[q]; bad++; }
                uint32_t sw; memcpy(&sw, ot + 4u * pts[i].chains, 4); if (sw != VAR[vi].scale) badscale++;
            }
        }
        if (!ok) { fail++; omega_gpu_session_free(&code); omega_gpu_session_free(&out); break; }
        qsort(ns, 5, 8, cmp_u64);
        double ops = (double)pts[i].ctas * (pts[i].threads / 32u) * iters * pts[i].chains, tf = ops * VAR[vi].flop / ns[2] / 1e3;
        printf("%-38s stall %u chains %2u threads %u ctas %u (declared %u): median %.1f us, %.2f TFLOPS, %zu wrong values (first %g, want %g), %zu bad scale\n",
               VAR[vi].name, pts[i].stall, pts[i].chains, pts[i].threads, pts[i].ctas, decl, ns[2] / 1e3, tf, bad, first_bad, VAR[vi].per * (float)iters, badscale);
        if (bad || badscale) fail++; else if (tf > best[vi]) best[vi] = tf;
        omega_gpu_session_free(&code); omega_gpu_session_free(&out);
    }
    omega_gpu_session_unlock();
    for (size_t vi = 0; vi < sizeof VAR / sizeof VAR[0]; vi++) printf("BEST %-38s %.2f TFLOPS (%.3f of FP8 control)\n", VAR[vi].name, best[vi], best[0] > 0 ? best[vi] / best[0] : 0.0);
    return fail;
}

int main(int argc, char **argv) {
    int host = argc > 1 && !strcmp(argv[1], "--host-only");
    char msg[256]; int fail = 0;
    if (omega_omma_patch_selftest(msg, sizeof msg) != 0) { printf("OMMA selftest FAIL: %s\n", msg); fail++; } else printf("OMMA selftest: %s\n", msg);
    if (omega_qmma_patch_selftest(msg, sizeof msg) != 0) { printf("QMMA selftest FAIL: %s\n", msg); fail++; } else printf("QMMA selftest: %s\n", msg);
    if (!host && !fail) fail += peak();
    printf("GPU_OMMA_PEAK: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
