/* gpu_ceiling_test.c (L151 machine ceiling map, 2026-10-19): the GB10 units no other test pushes to the wall.
 *   --host-only  the three kernels of src/omega_blackwell_codegen_ceil.h in the warp simulator, every result checked
 *   (default)    the chip (caller holds flock /tmp/aien-gb10.lock, inside a spark-window):
 *     ffma  FFMA rate: sweep chains x stall x threads x CTAs; TFLOPS = 2 * FFMAs / time. Register traffic it implies is
 *           reported beside it (3 operand reads + 1 write of 4 bytes per FFMA per thread) as INFERRED, not measured.
 *     lds   shared-memory read rate: LDS.128, sweep threads x CTAs; TB/s = bytes loaded / time.
 *     stg   store stream to memory: GPU-uncached session buffer and GPU-cached device buffer; GB/s = bytes / time.
 *   Every point: 5 launches, median, every stored value checked (a wrong value fails the point and the verdict).
 *   Time is the session launch time (host clock around submit and completion), so each point runs >= 2 ms to keep
 *   the fixed launch cost (tens of us, gpu_launch_cost_test) under a few per cent.
 * Output: one "CEIL <unit> ..." line per point, "CEIL best <unit> ..." per unit, then "GPU_CEILING: PASS|FAIL". */
#include "omega_blackwell_codegen_ceil.h"
#include "omega_blackwell_codegen_tc.h"
#include "omega_gpu_session.h"
#include "omega_gpu_prefill.h"
#include "bw_warp_sim_tc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static uint32_t lds_expect(uint32_t tid, uint32_t threads, uint32_t iters) {
    uint32_t s = 0;
    for (uint32_t q = 0; q < 8; q++) for (uint32_t w = 0; w < 4; w++) s += tid * 4u + q * threads * 4u + w;
    return s * iters;
}

static int host_only(void) {
    static BlackwellIRProgram p; static WsSim s; static uint8_t smem[64 * 128]; int fail = 0;
    static uint32_t out[64 * 64 * 32];
    /* ffma */
    if (omega_blackwell_codegen_ffma_peak_prog(7, 8, 1, 64, &p) != 0) { printf("codegen refused ffma\n"); return 1; }
    memset(out, 0xff, sizeof out); memset(&s, 0, sizeof s); s.p = &p; s.threads = 64;
    uint64_t va = (uintptr_t)out; s.cbank[0x380 / 4] = (uint32_t)va; s.cbank[0x384 / 4] = (uint32_t)(va >> 32);
    s.ranges[0].lo = va; s.ranges[0].hi = va + 2u * 64u * 8u * 4u; s.nranges = 1;
    int err = ws_run(&s, 2, 1); size_t bad = 0; float f7 = 7.0f; uint32_t u7; memcpy(&u7, &f7, 4);
    for (size_t i = 0; i < 2u * 64u * 8u; i++) if (out[i] != u7) bad++;
    printf("sim ffma: %s, %u registers, %zu wrong\n", err ? s.msg : "ran", p.regalloc.peak_gpr_usage, bad); if (err || bad) fail++;
    /* lds */
    if (omega_blackwell_codegen_lds_bw_prog(3, 64, &p) != 0) { printf("codegen refused lds\n"); return 1; }
    memset(out, 0xff, sizeof out); memset(&s, 0, sizeof s); s.p = &p; s.threads = 64; s.smem = smem; s.smem_bytes = sizeof smem;
    s.cbank[0x380 / 4] = (uint32_t)va; s.cbank[0x384 / 4] = (uint32_t)(va >> 32); s.ranges[0].lo = va; s.ranges[0].hi = va + 2u * 64u * 4u; s.nranges = 1;
    err = ws_run(&s, 2, 1); bad = 0;
    for (uint32_t c = 0; c < 2; c++) for (uint32_t t = 0; t < 64; t++) if (out[c * 64 + t] != lds_expect(t, 64, 3)) bad++;
    printf("sim lds: %s, %u registers, %zu wrong\n", err ? s.msg : "ran", p.regalloc.peak_gpr_usage, bad); if (err || bad) fail++;
    /* stg */
    if (omega_blackwell_codegen_stg_stream_prog(3, 64, &p) != 0) { printf("codegen refused stg\n"); return 1; }
    memset(out, 0xff, sizeof out); memset(&s, 0, sizeof s); s.p = &p; s.threads = 64;
    s.cbank[0x380 / 4] = (uint32_t)va; s.cbank[0x384 / 4] = (uint32_t)(va >> 32); s.ranges[0].lo = va; s.ranges[0].hi = va + 2u * 3u * 64u * 32u; s.nranges = 1;
    err = ws_run(&s, 2, 1); bad = 0;
    for (uint32_t c = 0; c < 2; c++) for (uint32_t r = 0; r < 3; r++) for (uint32_t w = 0; w < 64u * 8u; w++) if (out[(c * 3u + r) * 512u + w] != 3u - r) bad++;
    printf("sim stg: %s, %u registers, %zu wrong\n", err ? s.msg : "ran", p.regalloc.peak_gpr_usage, bad); if (err || bad) fail++;
    return fail;
}

typedef struct { OmegaGpuMem code; uint32_t decl; } Kern;
static int upload(BlackwellIRProgram *p, Kern *k) {
    OmegaBlackwellKernel bk; size_t need = omega_bw_program_code_bytes(p);
    k->decl = omega_bw_gpr_declared(p->regalloc.peak_gpr_usage);
    return omega_gpu_session_alloc_code(need, &k->code) != 0 || omega_blackwell_kernel_encode_into(p, k->code.cpu, need, &bk) != 0 ||
           omega_gpu_session_sync_to_gpu(&k->code) != 0;
}
static int launch(const Kern *k, uint64_t va, uint32_t threads, uint32_t ctas, uint32_t shared, size_t elems, uint64_t *ns) {
    uint32_t args[2] = { (uint32_t)va, (uint32_t)(va >> 32) };
    OmegaGpuLaunch L = { .code_va = k->code.va, .gpr_count = k->decl, .threads_x = threads, .threads_y = 1, .grid_x = ctas, .grid_y = 1,
                         .num_elements = elems, .args = args, .n_args = 2, .timeout_ms = 30000, .shared_bytes = shared };
    *ns = 0;
    return omega_gpu_session_launch(&L, ns, NULL);
}

/* --stream-read SECONDS [cached]: the whole-machine run's GPU leg. Back-to-back launches of the tc_bw read kernel
 * (src/omega_blackwell_codegen_tc.h) over 1.5 GB of distinct bytes for SECONDS; every launch's 98,304 XOR results are
 * checked. Prints wall-clock start/end (epoch seconds) so the CPU, disk and GPU legs can be lined up, and the GB/s
 * over the whole interval (bytes / wall time, launch gaps included) and over kernel time alone. */
static int stream_read(double secs, int cached) {
    const uint32_t T = 256, iters = 128, grid = 384, groups = grid;
    static BlackwellIRProgram p; OmegaGpuMem src = { 0 }, out = { 0 }; Kern k; int fail = 0;
    if (omega_blackwell_codegen_tc_bw_prog(iters, groups, T, &p) != 0) { printf("bw codegen refused\n"); return 1; }
    size_t region = (size_t)iters * T * 128u, sb = region * groups, ob = (size_t)grid * T * 4u;
    uint32_t *sw = malloc(sb), *want = calloc((size_t)grid * T, 4);
    if (!sw || !want) return 1;
    for (size_t q = 0; q < sb / 4; q++) sw[q] = (uint32_t)(q * 2654435761u);
    for (uint32_t c = 0; c < grid; c++) for (uint32_t t = 0; t < T; t++) { uint32_t a = 0; const uint32_t *r = sw + (size_t)c * region / 4;
        for (uint32_t it = 0; it < iters; it++) for (uint32_t q = 0; q < 8; q++) for (uint32_t s = 0; s < 4; s++) a ^= r[((size_t)it * T * 128u + (size_t)q * T * 16u + t * 16u) / 4 + s];
        want[(size_t)c * T + t] = a; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    if (upload(&p, &k) || omega_gpu_session_alloc(ob, &out) != 0 || (cached ? omega_gpu_pf_alloc_dev(sb, &src) : omega_gpu_session_alloc(sb, &src)) != 0) {
        printf("alloc failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    if (cached) { if (omega_gpu_pf_upload(&src, 0, sw, sb) != 0) { printf("upload failed\n"); omega_gpu_session_unlock(); return 1; } }
    else { memcpy(src.cpu, sw, sb); if (omega_gpu_session_sync_to_gpu(&src) != 0) { omega_gpu_session_unlock(); return 1; } }
    {   const char *sa = getenv("MC_START_AT"); /* concurrent legs start together */
        if (sa) { struct timespec r; clock_gettime(CLOCK_REALTIME, &r); double d = atof(sa) - (r.tv_sec + r.tv_nsec * 1e-9);
                  if (d > 0) { struct timespec w = { (time_t)d, (long)((d - (time_t)d) * 1e9) }; nanosleep(&w, NULL); } } }
    struct timespec t0, t1; clock_gettime(CLOCK_REALTIME, &t0);
    double kern_ns = 0, moved = 0; uint64_t n = 0; size_t bad = 0;
    for (;;) {
        clock_gettime(CLOCK_REALTIME, &t1);
        if ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9 >= secs) break;
        uint32_t args[4] = { (uint32_t)src.va, (uint32_t)(src.va >> 32), (uint32_t)out.va, (uint32_t)(out.va >> 32) };
        OmegaGpuLaunch L = { .code_va = k.code.va, .gpr_count = k.decl, .threads_x = T, .threads_y = 1, .grid_x = grid, .grid_y = 1,
                             .num_elements = ob / 4, .args = args, .n_args = 4, .timeout_ms = 20000 };
        uint64_t ns = 0; memset(out.cpu, 0xee, ob);
        if (omega_gpu_session_sync_to_gpu(&out) || omega_gpu_session_launch(&L, &ns, NULL) || omega_gpu_session_sync_to_cpu(&out)) { printf("launch failed: %s\n", omega_gpu_session_last_error()); fail++; break; }
        for (size_t q = 0; q < (size_t)grid * T; q++) if (((uint32_t *)out.cpu)[q] != want[q]) bad++;
        kern_ns += (double)ns; moved += (double)sb; n++;
    }
    double wall = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("CEIL stream_read %s start=%ld.%09ld end=%ld.%09ld launches=%llu GB=%.1f wall_GBps=%.1f kernel_GBps=%.1f wrong=%zu\n", cached ? "gpu_cached" : "gpu_uncached",
           (long)t0.tv_sec, t0.tv_nsec, (long)t1.tv_sec, t1.tv_nsec, (unsigned long long)n, moved / 1e9, moved / wall / 1e9, kern_ns > 0 ? moved / kern_ns : 0, bad);
    if (bad) fail++;
    omega_gpu_session_free(&k.code); omega_gpu_session_free(&out); if (cached) omega_gpu_pf_free(&src); else omega_gpu_session_free(&src);
    omega_gpu_session_close(); omega_gpu_session_unlock(); free(sw); free(want);
    printf("GPU_CEILING: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--stream-read")) return stream_read(atof(argv[2]), argc > 3 && !strcmp(argv[3], "cached"));
    int fail = host_only();
    if (argc > 1 && !strcmp(argv[1], "--host-only")) { printf("GPU_CEILING: %s\n", fail ? "FAIL" : "PASS"); return fail ? 1 : 0; }
    if (fail) { printf("GPU_CEILING: FAIL\n"); return 1; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    static BlackwellIRProgram p;
    /* ---- FFMA ---- */
    static const struct { uint32_t chains, stall, threads, ctas; } fp[] = {
        { 8, 1, 128, 192 }, { 8, 2, 128, 192 }, { 16, 1, 128, 192 }, { 16, 1, 256, 192 }, { 8, 1, 256, 384 },
        { 16, 1, 512, 96 }, { 16, 1, 1024, 48 }, { 32, 1, 256, 192 }, { 4, 1, 256, 384 },
    };
    double best_ffma = 0; const uint32_t fit = 1u << 16;
    for (size_t i = 0; i < sizeof fp / sizeof fp[0]; i++) {
        Kern k; OmegaGpuMem out = { 0 }; size_t ob = (size_t)fp[i].ctas * fp[i].threads * 4u * fp[i].chains;
        if (omega_blackwell_codegen_ffma_peak_prog(fit, fp[i].chains, fp[i].stall, fp[i].threads, &p) != 0) { printf("codegen refused\n"); fail++; continue; }
        if (upload(&p, &k) || omega_gpu_session_alloc(ob, &out) != 0) { printf("upload failed: %s\n", omega_gpu_session_last_error()); fail++; break; }
        uint64_t ns[5]; size_t bad = 0; int ok = 1; float fe = (float)fit; uint32_t ue; memcpy(&ue, &fe, 4);
        for (int r = 0; r < 5 && ok; r++) {
            memset(out.cpu, 0xff, ob);
            if (omega_gpu_session_sync_to_gpu(&out) || launch(&k, out.va, fp[i].threads, fp[i].ctas, 0, ob / 4, &ns[r]) || omega_gpu_session_sync_to_cpu(&out)) { printf("launch failed: %s\n", omega_gpu_session_last_error()); ok = 0; break; }
            for (size_t q = 0; q < ob / 4; q++) if (((uint32_t *)out.cpu)[q] != ue) bad++;
        }
        if (ok) {
            qsort(ns, 5, 8, cmp_u64); double ffmas = (double)fp[i].ctas * fp[i].threads * fp[i].chains * fit, tf = 2.0 * ffmas / ns[2] / 1e3;
            printf("CEIL ffma chains=%u stall=%u threads=%u ctas=%u regs=%u median_us=%.1f TFLOPS=%.2f reg_TBps_inferred=%.1f wrong=%zu\n", fp[i].chains, fp[i].stall,
                   fp[i].threads, fp[i].ctas, k.decl, ns[2] / 1e3, tf, 16.0 * ffmas / ns[2] / 1e3, bad);
            if (bad) fail++; else if (tf > best_ffma) best_ffma = tf;
        } else fail++;
        omega_gpu_session_free(&k.code); omega_gpu_session_free(&out);
        if (!ok) break;
    }
    printf("CEIL best ffma TFLOPS=%.2f\n", best_ffma);
    /* ---- LDS.128 ---- */
    static const struct { uint32_t threads, ctas; } lp[] = { { 256, 48 }, { 256, 96 }, { 256, 144 }, { 512, 48 }, { 512, 96 }, { 128, 288 }, { 256, 384 } };
    double best_lds = 0; const uint32_t lit = 4096;
    for (size_t i = 0; i < sizeof lp / sizeof lp[0]; i++) {
        Kern k; OmegaGpuMem out = { 0 }; size_t ob = (size_t)lp[i].ctas * lp[i].threads * 4u;
        if (omega_blackwell_codegen_lds_bw_prog(lit, lp[i].threads, &p) != 0) { printf("codegen refused\n"); fail++; continue; }
        if (upload(&p, &k) || omega_gpu_session_alloc(ob, &out) != 0) { printf("upload failed: %s\n", omega_gpu_session_last_error()); fail++; break; }
        uint64_t ns[5]; size_t bad = 0; int ok = 1;
        for (int r = 0; r < 5 && ok; r++) {
            memset(out.cpu, 0xff, ob);
            if (omega_gpu_session_sync_to_gpu(&out) || launch(&k, out.va, lp[i].threads, lp[i].ctas, lp[i].threads * 128u, ob / 4, &ns[r]) || omega_gpu_session_sync_to_cpu(&out)) { printf("launch failed: %s\n", omega_gpu_session_last_error()); ok = 0; break; }
            for (uint32_t c = 0; c < lp[i].ctas; c++) for (uint32_t t = 0; t < lp[i].threads; t++) if (((uint32_t *)out.cpu)[c * lp[i].threads + t] != lds_expect(t, lp[i].threads, lit)) bad++;
        }
        if (ok) {
            qsort(ns, 5, 8, cmp_u64); double bytes = (double)lp[i].ctas * lp[i].threads * 128.0 * lit, tb = bytes / ns[2] / 1e3;
            printf("CEIL lds threads=%u ctas=%u shared_B=%u regs=%u median_us=%.1f TBps=%.3f wrong=%zu\n", lp[i].threads, lp[i].ctas, lp[i].threads * 128u, k.decl, ns[2] / 1e3, tb, bad);
            if (bad) fail++; else if (tb > best_lds) best_lds = tb;
        } else fail++;
        omega_gpu_session_free(&k.code); omega_gpu_session_free(&out);
        if (!ok) break;
    }
    printf("CEIL best lds TBps=%.3f\n", best_lds);
    /* ---- store stream: uncached session buffer, then GPU-cached device buffer ---- */
    static const struct { uint32_t threads, ctas, iters; } sp[] = { { 256, 192, 512 }, { 512, 96, 512 }, { 1024, 96, 256 }, { 256, 384, 256 } };
    double best_stg[2] = { 0, 0 };
    for (int cached = 0; cached < 2; cached++) for (size_t i = 0; i < sizeof sp / sizeof sp[0]; i++) {
        Kern k; OmegaGpuMem out = { 0 }; size_t ob = (size_t)sp[i].ctas * sp[i].iters * sp[i].threads * 32u;
        if (omega_blackwell_codegen_stg_stream_prog(sp[i].iters, sp[i].threads, &p) != 0) { printf("codegen refused\n"); fail++; continue; }
        if (upload(&p, &k) || (cached ? omega_gpu_pf_alloc_dev(ob, &out) : omega_gpu_session_alloc(ob, &out)) != 0) { printf("alloc failed: %s\n", omega_gpu_session_last_error()); fail++; break; }
        uint32_t *host = malloc(ob); uint64_t ns[5]; size_t bad = 0; int ok = host != NULL;
        for (int r = 0; r < 5 && ok; r++) {
            if (!cached) { memset(out.cpu, 0xff, ob); if (omega_gpu_session_sync_to_gpu(&out)) { ok = 0; break; } }
            if (launch(&k, out.va, sp[i].threads, sp[i].ctas, 0, ob / 4, &ns[r])) { printf("launch failed: %s\n", omega_gpu_session_last_error()); ok = 0; break; }
            if (r == 4) {
                memset(host, 0xff, ob);
                if (cached ? omega_gpu_pf_download(&out, 0, host, ob) : (omega_gpu_session_sync_to_cpu(&out) || (memcpy(host, out.cpu, ob), 0))) { printf("readback failed: %s\n", omega_gpu_session_last_error()); ok = 0; break; }
                size_t per_round = (size_t)sp[i].threads * 8u;
                for (size_t w = 0; w < ob / 4; w++) if (host[w] != sp[i].iters - (uint32_t)((w / per_round) % sp[i].iters)) bad++;
            }
        }
        if (ok) {
            qsort(ns, 5, 8, cmp_u64); double gbs = (double)ob / ns[2];
            printf("CEIL stg %s threads=%u ctas=%u MB=%.0f median_us=%.1f GBps=%.1f wrong=%zu\n", cached ? "gpu_cached" : "gpu_uncached", sp[i].threads, sp[i].ctas,
                   ob / 1e6, ns[2] / 1e3, gbs, bad);
            if (bad) fail++; else if (gbs > best_stg[cached]) best_stg[cached] = gbs;
        } else fail++;
        free(host); omega_gpu_session_free(&k.code); if (cached) omega_gpu_pf_free(&out); else omega_gpu_session_free(&out);
        if (!ok) break;
    }
    printf("CEIL best stg gpu_uncached GBps=%.1f gpu_cached GBps=%.1f\n", best_stg[0], best_stg[1]);
    omega_gpu_session_close();
    omega_gpu_session_unlock();
    printf("GPU_CEILING: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
