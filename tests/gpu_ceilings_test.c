/* gpu_ceilings_test.c (L272, 2026-10-20): the GB10's own ceilings for one-token decode, measured with Omega kernels
 * (omega_blackwell_codegen_ceil.h). docs/speed/GB10_CEILINGS.md reads its output.
 *   --host-only   every generator in the warp simulator, results checked (stream XOR, chase end pointer, resid count,
 *                 grid barrier with one CTA). No chip.
 *   (default)     the chip (caller holds flock /tmp/aien-gb10.lock, under quiet-measure). Sections, in order:
 *     resid    co-resident CTAs per launch shape (threads x declared registers x shared bytes), max over CTAs
 *     launch   empty kernels in one command list: GPU-stamped cost per launch per barrier kind; host round trip of one launch
 *     dram     streaming read bandwidth at the decode shape, distinct bytes (1 GiB), uncached and GPU-cached mappings
 *     l2       the same kernel inside a 2^k-byte footprint, 512 MiB read per point: the L2 knee and L2 bandwidth
 *     chase    load-to-use latency, one warp following a random pointer chain: L2-resident and DRAM, cached and uncached
 *     boundary 256 MiB streamed as one launch, as P launches (WFI between), and as one launch with P grid barriers
 *   Every point checks every stored value against the host's own computation; a wrong value is a FAIL.
 *   --only NAME runs one section. Output: "CEIL <section> key=value ..." lines; verdict last "GPU_CEILINGS: PASS|FAIL". */
#include "omega_blackwell_codegen_ceil.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_mem_cached.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim_tc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define FAILF(...) do { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t xs64(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }

/* host model of the stream kernel: word XOR over thread gid's loads */
static uint32_t stream_want(const uint32_t *src, uint32_t gid, uint32_t iters, uint32_t threads, uint32_t grid, uint32_t mask, uint32_t phases, uint32_t base) {
    const uint32_t step = grid * threads * 16u; uint32_t off = base + gid * 16u, a = 0;
    for (uint32_t i = 0; i < iters * phases; i++) {
        for (uint32_t q = 0; q < 8; q++) { uint32_t o = (off + q * step) & mask; for (int s = 0; s < 4; s++) a ^= src[o / 4 + (uint32_t)s]; }
        off += 8u * step;
    }
    return a;
}

/* ---------------- host-only: the simulator ---------------- */
static int sim_stream(uint32_t phases, uint32_t mask) {
    /* the simulator runs CTAs one after another: a grid barrier can only complete with one CTA, so phases > 1 uses G = 1 */
    static BlackwellIRProgram p; const uint32_t T = 64, G = phases > 1 ? 1 : 2, it = 3;
    if (omega_blackwell_codegen_ceil_stream_prog(it, T, G, mask, phases, &p) != 0) { FAILF("stream codegen phases %u", phases); return 1; }
    const size_t bytes = (size_t)G * T * 128u * it * phases + 64;
    uint32_t *src = malloc(bytes), *out = calloc(T * G, 4), cnt = 0;
    for (size_t i = 0; i < bytes / 4; i++) src[i] = (uint32_t)(i * 2654435761u);
    static WsSim s; memset(&s, 0, sizeof s); s.p = &p; s.threads = T;
    uint64_t vx = (uintptr_t)src, vy = (uintptr_t)out, vc = (uintptr_t)&cnt;
    s.cbank[0x380 / 4] = (uint32_t)vx; s.cbank[0x384 / 4] = (uint32_t)(vx >> 32); s.cbank[0x388 / 4] = (uint32_t)vy; s.cbank[0x38c / 4] = (uint32_t)(vy >> 32);
    s.cbank[0x390 / 4] = (uint32_t)vc; s.cbank[0x394 / 4] = (uint32_t)(vc >> 32); s.cbank[0x398 / 4] = 0;
    s.ranges[0] = (WsRange){ vx, vx + bytes }; s.ranges[1] = (WsRange){ vy, vy + T * G * 4u }; s.ranges[2] = (WsRange){ vc, vc + 4 }; s.nranges = 3;
    const uint32_t grid_run = G;
    int err = ws_run(&s, grid_run, 1); size_t bad = 0;
    for (uint32_t g = 0; g < T * grid_run; g++) if (out[g] != stream_want(src, g, it, T, G, mask, phases, 0)) bad++;
    printf("sim stream phases %u mask %08x: %s, %u registers, %zu instructions, %zu wrong, counter %u\n", phases, mask, err ? s.msg : "ran", p.regalloc.peak_gpr_usage, p.count, bad, cnt);
    int f = err || bad || (phases > 1 && cnt != phases);
    if (f) FAILF("sim stream phases %u", phases);
    free(src); free(out); return f;
}
static int sim_chase(void) {
    static BlackwellIRProgram p; const uint32_t n = 64, it = 100;
    if (omega_blackwell_codegen_ceil_chase_prog(it, &p) != 0) { FAILF("chase codegen"); return 1; }
    uint64_t *chain = malloc(n * 8), out = 0;
    for (uint32_t i = 0; i < n; i++) chain[i] = (uintptr_t)&chain[(i * 5u + 3u) % n];
    static WsSim s; memset(&s, 0, sizeof s); s.p = &p; s.threads = 32;
    uint64_t v0 = (uintptr_t)&chain[0], vy = (uintptr_t)&out;
    s.cbank[0x380 / 4] = (uint32_t)v0; s.cbank[0x384 / 4] = (uint32_t)(v0 >> 32); s.cbank[0x388 / 4] = (uint32_t)vy; s.cbank[0x38c / 4] = (uint32_t)(vy >> 32);
    s.ranges[0] = (WsRange){ (uintptr_t)chain, (uintptr_t)(chain + n) }; s.ranges[1] = (WsRange){ vy, vy + 8 }; s.nranges = 2;
    int err = ws_run(&s, 1, 1);
    uint64_t want = v0; for (uint32_t i = 0; i < it; i++) want = *(uint64_t *)(uintptr_t)want;
    printf("sim chase: %s, end %s\n", err ? s.msg : "ran", out == want ? "right" : "WRONG");
    int f = err || out != want; if (f) FAILF("sim chase");
    free(chain); return f;
}
static int sim_resid(void) {
    static BlackwellIRProgram p;
    if (omega_blackwell_codegen_ceil_resid_prog(5, &p) != 0) { FAILF("resid codegen"); return 1; }
    uint32_t cnt = 0, out[3] = { 9, 9, 9 };
    static WsSim s; memset(&s, 0, sizeof s); s.p = &p; s.threads = 64;
    uint64_t vc = (uintptr_t)&cnt, vy = (uintptr_t)out;
    s.cbank[0x380 / 4] = (uint32_t)vc; s.cbank[0x384 / 4] = (uint32_t)(vc >> 32); s.cbank[0x388 / 4] = (uint32_t)vy; s.cbank[0x38c / 4] = (uint32_t)(vy >> 32);
    s.ranges[0] = (WsRange){ vc, vc + 4 }; s.ranges[1] = (WsRange){ vy, vy + 12 }; s.nranges = 2;
    int err = ws_run(&s, 3, 1); /* sequential CTAs: each sees only itself */
    printf("sim resid: %s, seen %u %u %u, counter after %u\n", err ? s.msg : "ran", out[0], out[1], out[2], cnt);
    int f = err || out[0] != 1 || out[1] != 1 || out[2] != 1 || cnt != 0; if (f) FAILF("sim resid");
    return f;
}
static int host_only(void) {
    int f = sim_stream(1, 0xffffffffu) + sim_stream(1, 0x3fffu) + sim_stream(3, 0xffffffffu) + sim_chase() + sim_resid();
    static BlackwellIRProgram p; if (omega_blackwell_codegen_ceil_empty_prog(&p) != 0) { FAILF("empty codegen"); f++; }
    return f;
}

/* ---------------- chip ---------------- */
typedef struct { OmegaGpuMem code; uint32_t gpr; } Code;
static int upload(BlackwellIRProgram *p, Code *c) {
    OmegaBlackwellKernel k; size_t need = omega_bw_program_code_bytes(p);
    if (omega_gpu_session_alloc_code(need, &c->code) != 0 || omega_blackwell_kernel_encode_into(p, c->code.cpu, need, &k) != 0 || omega_gpu_session_sync_to_gpu(&c->code) != 0) return -1;
    c->gpr = omega_bw_gpr_declared(p->regalloc.peak_gpr_usage);
    return 0;
}
static OmegaGpuLaunch mk(const Code *c, uint32_t threads, uint32_t grid, const uint32_t *args, uint32_t nargs) {
    OmegaGpuLaunch L; memset(&L, 0, sizeof L);
    L.code_va = c->code.va; L.gpr_count = c->gpr; L.threads_x = threads; L.threads_y = 1; L.grid_x = grid; L.grid_y = 1;
    L.num_elements = threads * grid; L.args = args; L.n_args = nargs; L.timeout_ms = 20000;
    return L;
}
static void a64(uint32_t *w, uint64_t v) { w[0] = (uint32_t)v; w[1] = (uint32_t)(v >> 32); }

/* one command list, one GPU-stamped span over all its launches (ns) */
static int run_list(OmegaGpuCmdList *cl, const OmegaGpuLaunch *Ls, uint32_t n, OmegaGpuCmdListBarrier bar, uint64_t *span, uint64_t *host_elapsed) {
    omega_gpu_cmdlist_reset(cl);
    for (uint32_t i = 0; i < n; i++) if (omega_gpu_cmdlist_add(cl, &Ls[i]) < 0) { printf("cmdlist add %u: %s\n", i, omega_gpu_session_last_error()); return -1; }
    OmegaGpuCmdListOpts o; memset(&o, 0, sizeof o); o.mode = OMEGA_GPU_CMDLIST_PCAS; o.barrier = bar; o.stamps = 1;
    OmegaGpuCmdListStats st; memset(&st, 0, sizeof st);
    if (omega_gpu_cmdlist_submit(cl, &o, &st) != 0) { printf("submit: %s\n", omega_gpu_session_last_error()); return -1; }
    static uint64_t stamps[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1];
    int ns = omega_gpu_cmdlist_stamps(cl, stamps, OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1);
    if (ns != (int)n + 1) { printf("stamps %d\n", ns); return -1; }
    *span = stamps[n] - stamps[0];
    if (host_elapsed) *host_elapsed = st.elapsed_ns;
    return 0;
}

static int sec_resid(OmegaGpuCmdList *cl, uint32_t *cap256) {
    static BlackwellIRProgram p; Code c;
    if (omega_blackwell_codegen_ceil_resid_prog(4000, &p) != 0 || upload(&p, &c) != 0) { FAILF("resid setup: %s", omega_gpu_session_last_error()); return -1; }
    OmegaGpuMem cnt, out; const uint32_t G = 3072;
    if (omega_gpu_session_alloc(64, &cnt) != 0 || omega_gpu_session_alloc(G * 4u, &out) != 0) { FAILF("resid alloc"); return -1; }
    static const struct { uint32_t threads, gpr, smem; } pts[] = {
        { 32, 0, 0 }, { 32, 0, 16384 }, { 32, 0, 8192 }, { 128, 0, 0 }, { 256, 0, 0 }, { 256, 64, 0 }, { 256, 128, 0 }, { 256, 168, 0 }, { 256, 256, 0 },
        { 512, 64, 0 }, { 1024, 0, 0 }, { 1024, 64, 0 },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        memset(cnt.cpu, 0, 64); memset(out.cpu, 0, G * 4u);
        if (omega_gpu_session_sync_to_gpu(&cnt) != 0 || omega_gpu_session_sync_to_gpu(&out) != 0) return -1;
        uint32_t args[4]; a64(args, cnt.va); a64(args + 2, out.va);
        OmegaGpuLaunch L = mk(&c, pts[i].threads, G, args, 4);
        if (pts[i].gpr) L.gpr_count = pts[i].gpr;
        L.shared_bytes = pts[i].smem;
        uint64_t span;
        if (run_list(cl, &L, 1, OMEGA_GPU_CMDLIST_BARRIER_WFI, &span, NULL) != 0 || omega_gpu_session_sync_to_cpu(&out) != 0 || omega_gpu_session_sync_to_cpu(&cnt) != 0) { FAILF("resid launch %zu", i); return -1; }
        uint32_t mx = 0, *o = out.cpu, zero = 0; for (uint32_t g = 0; g < G; g++) { if (o[g] > mx) mx = o[g]; if (o[g] == 0) zero++; }
        uint32_t left = *(uint32_t *)cnt.cpu;
        printf("CEIL resid threads=%u gpr=%u smem=%u max_resident_ctas=%u per_sm=%.2f regs_per_sm_used=%u span_us=%.1f\n", pts[i].threads, L.gpr_count, pts[i].smem, mx, mx / 48.0,
               (uint32_t)(mx / 48.0 + 0.5) * pts[i].threads * L.gpr_count, span / 1e3);
        if (zero || left != 0 || mx == 0) FAILF("resid point %zu: %u CTAs stored nothing, counter left %u", i, zero, left);
        if (pts[i].threads == 256 && pts[i].gpr == 0 && cap256) *cap256 = mx;
    }
    omega_gpu_session_free(&c.code); omega_gpu_session_free(&cnt); omega_gpu_session_free(&out);
    return 0;
}

static uint64_t ctxsw(void) { /* this process's voluntary + involuntary context switches (/proc/self/status) */
    FILE *f = fopen("/proc/self/status", "r"); char line[256]; uint64_t n = 0, v;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "voluntary_ctxt_switches: %lu", &v) == 1 || sscanf(line, "nonvoluntary_ctxt_switches: %lu", &v) == 1) n += v;
    fclose(f); return n;
}
static int sec_launch(OmegaGpuCmdList *cl) {
    static BlackwellIRProgram p; Code c;
    if (omega_blackwell_codegen_ceil_empty_prog(&p) != 0 || upload(&p, &c) != 0) { FAILF("empty setup"); return -1; }
    static OmegaGpuLaunch Ls[1024];
    static const OmegaGpuCmdListBarrier bars[] = { OMEGA_GPU_CMDLIST_BARRIER_WFI, OMEGA_GPU_CMDLIST_BARRIER_WFI_ONLY, OMEGA_GPU_CMDLIST_BARRIER_WFI_DATA, OMEGA_GPU_CMDLIST_BARRIER_NONE };
    static const uint32_t grids[] = { 1, 48, 384 };
    for (size_t gi = 0; gi < 3; gi++) for (size_t b = 0; b < 4; b++) {
        uint64_t med[2];
        static const uint32_t ms[2] = { 1, 1024 };
        for (int mi = 0; mi < 2; mi++) {
            for (uint32_t i = 0; i < ms[mi]; i++) Ls[i] = mk(&c, 128, grids[gi], NULL, 0);
            uint64_t v[7];
            for (int r = 0; r < 7; r++) if (run_list(cl, Ls, ms[mi], bars[b], &v[r], NULL) != 0) { FAILF("launch list"); return -1; }
            qsort(v, 7, 8, cmp_u64); med[mi] = v[3];
        }
        printf("CEIL launch grid=%u barrier=%s span1_us=%.2f span1024_us=%.1f per_launch_us=%.3f\n", grids[gi], omega_gpu_cmdlist_barrier_name(bars[b]), med[0] / 1e3, med[1] / 1e3,
               (double)(med[1] - med[0]) / 1023.0 / 1e3);
    }
    /* host round trip: one launch through the session (submit, marker poll), as the decode loop does per token */
    static const uint32_t spins[] = { 0, 200 };
    for (int si = 0; si < 2; si++) {
        uint64_t v[21];
        const uint64_t cs0 = ctxsw();
        for (int r = 0; r < 21; r++) { OmegaGpuLaunch L = mk(&c, 128, 1, NULL, 0); L.spin_us = spins[si]; v[r] = 0; if (omega_gpu_session_launch(&L, &v[r], NULL) != 0) { FAILF("session launch"); return -1; } }
        const uint64_t cs1 = ctxsw();
        qsort(v, 21, 8, cmp_u64);
        printf("CEIL launch host_round_trip spin_us=%u median_us=%.1f min_us=%.1f max_us=%.1f ctx_switches_per_launch=%.1f\n", spins[si], v[10] / 1e3, v[0] / 1e3, v[20] / 1e3, (double)(cs1 - cs0) / 21.0);
    }
    /* host polling during a long kernel (one CTA re-reading a word 20000 times): context switches per ms of GPU wait */
    {   static BlackwellIRProgram rp; Code rc; OmegaGpuMem cnt, out;
        if (omega_blackwell_codegen_ceil_resid_prog(20000, &rp) != 0 || upload(&rp, &rc) != 0 || omega_gpu_session_alloc(64, &cnt) != 0 || omega_gpu_session_alloc(64, &out) != 0) { FAILF("long setup"); return -1; }
        for (int si = 0; si < 2; si++) {
            uint64_t tot = 0, cs0 = ctxsw();
            for (int r = 0; r < 5; r++) {
                memset(cnt.cpu, 0, 64); if (omega_gpu_session_sync_to_gpu(&cnt) != 0) return -1;
                uint32_t args[4]; a64(args, cnt.va); a64(args + 2, out.va);
                OmegaGpuLaunch L = mk(&rc, 32, 1, args, 4); L.spin_us = spins[si]; uint64_t el = 0;
                if (omega_gpu_session_launch(&L, &el, NULL) != 0) { FAILF("long launch"); return -1; }
                tot += el;
            }
            uint64_t cs1 = ctxsw();
            printf("CEIL launch host_wait_long spin_us=%u wait_ms=%.2f ctx_switches=%lu per_ms=%.1f\n", spins[si], tot / 1e6, (unsigned long)(cs1 - cs0), (double)(cs1 - cs0) / (tot / 1e6));
        }
        omega_gpu_session_free(&rc.code); omega_gpu_session_free(&cnt); omega_gpu_session_free(&out);
    }
    omega_gpu_session_free(&c.code);
    return 0;
}

/* stream one point: returns the median GPU span (ns) of 5 runs, values checked against want[] */
static int stream_point(OmegaGpuCmdList *cl, const Code *c, const OmegaGpuMem *src, const OmegaGpuMem *out, const OmegaGpuMem *cnt, uint32_t threads, uint32_t grid,
                        uint32_t nlaunch, uint32_t base_step, const uint32_t *want, uint64_t *med) {
    static OmegaGpuLaunch Ls[256]; static uint32_t args[256][8];
    uint64_t v[5];
    for (int r = 0; r < 5; r++) {
        memset(out->cpu, 0xee, (size_t)threads * grid * 4u); if (omega_gpu_session_sync_to_gpu(out) != 0) return -1;
        if (cnt) { memset(cnt->cpu, 0, 64); if (omega_gpu_session_sync_to_gpu(cnt) != 0) return -1; }
        for (uint32_t i = 0; i < nlaunch; i++) {
            a64(args[i], src->va); a64(args[i] + 2, out->va); a64(args[i] + 4, cnt ? cnt->va : 0); args[i][6] = i * base_step; args[i][7] = 0;
            Ls[i] = mk(c, threads, grid, args[i], 8);
        }
        if (run_list(cl, Ls, nlaunch, OMEGA_GPU_CMDLIST_BARRIER_WFI, &v[r], NULL) != 0 || omega_gpu_session_sync_to_cpu(out) != 0) return -1;
        size_t bad = 0; const uint32_t *o = out->cpu; for (size_t g = 0; g < (size_t)threads * grid; g++) if (o[g] != want[g]) bad++;
        if (bad) { FAILF("stream threads %u grid %u launches %u: %zu wrong values", threads, grid, nlaunch, bad); return -1; }
    }
    qsort(v, 5, 8, cmp_u64); *med = v[2];
    return 0;
}

static int sec_dram(OmegaGpuCmdList *cl) {
    const size_t F = (size_t)1 << 30;
    static const struct { uint32_t threads, grid; } pts[] = { { 256, 96 }, { 256, 192 }, { 256, 384 }, { 256, 768 }, { 512, 192 }, { 512, 384 }, { 1024, 48 }, { 1024, 96 }, { 1024, 192 } };
    for (int cached = 0; cached < 2; cached++) {
        OmegaGpuMem src, out;
        if ((cached ? omega_gpu_session_alloc_cached(F + 4096, &src) : omega_gpu_session_alloc(F + 4096, &src)) != 0 || omega_gpu_session_alloc(1024u * 768u * 4u, &out) != 0) { FAILF("dram alloc"); return -1; }
        uint32_t *sw = src.cpu; for (size_t i = 0; i < (F + 4096) / 4; i++) sw[i] = (uint32_t)(i * 2654435761u);
        if (omega_gpu_session_sync_to_gpu(&src) != 0) return -1;
        for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
            const uint32_t T = pts[i].threads, G = pts[i].grid, it = (uint32_t)(F / ((size_t)G * T * 128u));
            static BlackwellIRProgram p; Code c;
            if (omega_blackwell_codegen_ceil_stream_prog(it, T, G, 0xffffffffu, 1, &p) != 0 || upload(&p, &c) != 0) { FAILF("dram codegen"); return -1; }
            uint32_t *want = malloc((size_t)T * G * 4u); for (uint32_t g = 0; g < T * G; g++) want[g] = stream_want(sw, g, it, T, G, 0xffffffffu, 1, 0);
            uint64_t med; if (stream_point(cl, &c, &src, &out, NULL, T, G, 1, 0, want, &med) != 0) return -1;
            double bytes = (double)G * T * 128.0 * it;
            printf("CEIL dram mapping=%s threads=%u grid=%u gpr=%u bytes=%.0f span_us=%.1f GBps=%.1f\n", cached ? "gpu-cached" : "uncached", T, G, c.gpr, bytes, med / 1e3, bytes / med);
            free(want); omega_gpu_session_free(&c.code);
        }
        omega_gpu_session_free(&src); omega_gpu_session_free(&out);
    }
    return 0;
}

static int sec_l2(OmegaGpuCmdList *cl) {
    const size_t F = (size_t)128 << 20; const uint32_t T = 256, G = 384, it = (uint32_t)(((size_t)512 << 20) / ((size_t)G * T * 128u));
    for (int cached = 1; cached >= 0; cached--) {
        OmegaGpuMem src, out;
        if ((cached ? omega_gpu_session_alloc_cached(F, &src) : omega_gpu_session_alloc(F, &src)) != 0 || omega_gpu_session_alloc((size_t)T * G * 4u, &out) != 0) { FAILF("l2 alloc"); return -1; }
        uint32_t *sw = src.cpu; for (size_t i = 0; i < F / 4; i++) sw[i] = (uint32_t)(i * 2246822519u + 7u);
        if (omega_gpu_session_sync_to_gpu(&src) != 0) return -1;
        for (uint32_t lg = 20; lg <= 27; lg++) {
            const uint32_t mask = (1u << lg) - 1u;
            static BlackwellIRProgram p; Code c;
            if (omega_blackwell_codegen_ceil_stream_prog(it, T, G, mask, 1, &p) != 0 || upload(&p, &c) != 0) { FAILF("l2 codegen"); return -1; }
            uint32_t *want = malloc((size_t)T * G * 4u); for (uint32_t g = 0; g < T * G; g++) want[g] = stream_want(sw, g, it, T, G, mask, 1, 0);
            uint64_t med; if (stream_point(cl, &c, &src, &out, NULL, T, G, 1, 0, want, &med) != 0) return -1;
            double bytes = (double)G * T * 128.0 * it;
            printf("CEIL l2 mapping=%s footprint_MiB=%u bytes=%.0f span_us=%.1f GBps=%.1f\n", cached ? "gpu-cached" : "uncached", 1u << (lg - 20), bytes, med / 1e3, bytes / med);
            free(want); omega_gpu_session_free(&c.code);
        }
        omega_gpu_session_free(&src); omega_gpu_session_free(&out);
    }
    return 0;
}

static int sec_chase(OmegaGpuCmdList *cl) {
    static const size_t foot[] = { (size_t)1 << 20, (size_t)8 << 20, (size_t)256 << 20 };
    const uint32_t stride = 4096u + 128u;
    for (int cached = 1; cached >= 0; cached--) for (size_t fi = 0; fi < 3; fi++) {
        const size_t F = foot[fi]; const uint32_t n = (uint32_t)(F / stride);
        OmegaGpuMem ch, out;
        if ((cached ? omega_gpu_session_alloc_cached(F + stride, &ch) : omega_gpu_session_alloc(F + stride, &ch)) != 0 || omega_gpu_session_alloc(64, &out) != 0) { FAILF("chase alloc"); return -1; }
        uint32_t *perm = malloc((size_t)n * 4); for (uint32_t i = 0; i < n; i++) perm[i] = i;
        uint64_t rs = 0x9e3779b97f4a7c15ull ^ F; for (uint32_t i = n - 1; i > 0; i--) { uint32_t j = (uint32_t)(xs64(&rs) % (i + 1)); uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
        uint8_t *b = ch.cpu; uint64_t *hostnext = malloc((size_t)n * 8);
        for (uint32_t i = 0; i < n; i++) { uint32_t from = perm[i], to = perm[(i + 1) % n]; uint64_t va = ch.va + (uint64_t)to * stride; memcpy(b + (size_t)from * stride, &va, 8); hostnext[from] = to; }
        if (omega_gpu_session_sync_to_gpu(&ch) != 0) return -1;
        static const uint32_t its[2] = { 4000, 20000 }; uint64_t med[2];
        for (int k = 0; k < 2; k++) {
            static BlackwellIRProgram p; Code c;
            if (omega_blackwell_codegen_ceil_chase_prog(its[k], &p) != 0 || upload(&p, &c) != 0) { FAILF("chase codegen"); return -1; }
            uint32_t want = perm[0]; for (uint32_t i = 0; i < its[k]; i++) want = (uint32_t)hostnext[want];
            uint64_t v[5];
            for (int r = 0; r < 5; r++) {
                memset(out.cpu, 0, 64); if (omega_gpu_session_sync_to_gpu(&out) != 0) return -1;
                uint32_t args[4]; a64(args, ch.va + (uint64_t)perm[0] * stride); a64(args + 2, out.va);
                OmegaGpuLaunch L = mk(&c, 32, 1, args, 4);
                if (run_list(cl, &L, 1, OMEGA_GPU_CMDLIST_BARRIER_WFI, &v[r], NULL) != 0 || omega_gpu_session_sync_to_cpu(&out) != 0) return -1;
                uint64_t got; memcpy(&got, out.cpu, 8);
                if (got != ch.va + (uint64_t)want * stride) { FAILF("chase end pointer wrong (F %zu, iters %u)", F, its[k]); return -1; }
            }
            qsort(v, 5, 8, cmp_u64); med[k] = v[2];
            omega_gpu_session_free(&c.code);
        }
        printf("CEIL chase mapping=%s footprint_MiB=%.0f nodes=%u ns_per_load=%.1f\n", cached ? "gpu-cached" : "uncached", F / 1048576.0, n, (double)(med[1] - med[0]) / (its[1] - its[0]));
        free(perm); free(hostnext); omega_gpu_session_free(&ch); omega_gpu_session_free(&out);
    }
    return 0;
}

static int sec_boundary(OmegaGpuCmdList *cl, uint32_t cap256) {
    const size_t B = (size_t)256 << 20; const uint32_t T = 256;
    uint32_t G = cap256 >= 384 ? 192 : (cap256 / 2u / 48u) * 48u; /* half the measured co-residency: the barrier needs every CTA resident */
    if (G == 0) { FAILF("boundary: no residency figure (run resid first)"); return -1; }
    OmegaGpuMem src, out, cnt;
    if (omega_gpu_session_alloc(B + 4096, &src) != 0 || omega_gpu_session_alloc((size_t)T * G * 4u, &out) != 0 || omega_gpu_session_alloc(64, &cnt) != 0) { FAILF("boundary alloc"); return -1; }
    uint32_t *sw = src.cpu; for (size_t i = 0; i < (B + 4096) / 4; i++) sw[i] = (uint32_t)(i * 2654435761u + 1u);
    if (omega_gpu_session_sync_to_gpu(&src) != 0) return -1;
    const uint32_t total_it = (uint32_t)(B / ((size_t)G * T * 128u));
    static const uint32_t Ps[] = { 16, 64 };
    uint64_t one_med;
    {   static BlackwellIRProgram p; Code c;
        if (omega_blackwell_codegen_ceil_stream_prog(total_it, T, G, 0xffffffffu, 1, &p) != 0 || upload(&p, &c) != 0) { FAILF("boundary codegen"); return -1; }
        uint32_t *want = malloc((size_t)T * G * 4u); for (uint32_t g = 0; g < T * G; g++) want[g] = stream_want(sw, g, total_it, T, G, 0xffffffffu, 1, 0);
        if (stream_point(cl, &c, &src, &out, NULL, T, G, 1, 0, want, &one_med) != 0) return -1;
        printf("CEIL boundary kind=one_launch grid=%u threads=%u bytes=%zu span_us=%.1f GBps=%.1f\n", G, T, (size_t)G * T * 128u * total_it, one_med / 1e3, (double)G * T * 128.0 * total_it / one_med);
        free(want); omega_gpu_session_free(&c.code);
    }
    for (size_t pi = 0; pi < 2; pi++) {
        const uint32_t P = Ps[pi], it = total_it / P; if (it == 0) continue;
        const double bytes = (double)G * T * 128.0 * it * P;
        /* P launches, each its own slice (base = i * slice), WFI between: what the graph does today */
        {   static BlackwellIRProgram p; Code c;
            if (omega_blackwell_codegen_ceil_stream_prog(it, T, G, 0xffffffffu, 1, &p) != 0 || upload(&p, &c) != 0) { FAILF("boundary codegen"); return -1; }
            /* every launch writes py[gid]; the last launch's values remain */
            const uint32_t slice = G * T * 128u * it;
            uint32_t *want = malloc((size_t)T * G * 4u); for (uint32_t g = 0; g < T * G; g++) want[g] = stream_want(sw, g, it, T, G, 0xffffffffu, 1, (P - 1) * slice);
            uint64_t med; if (stream_point(cl, &c, &src, &out, NULL, T, G, P, slice, want, &med) != 0) return -1;
            printf("CEIL boundary kind=launches P=%u grid=%u span_us=%.1f GBps=%.1f per_boundary_us=%.2f\n", P, G, med / 1e3, bytes / med, ((double)med - one_med * (bytes / ((double)G * T * 128.0 * total_it))) / (P - 1) / 1e3);
            free(want); omega_gpu_session_free(&c.code);
        }
        /* one persistent launch, P phases joined by the software grid barrier */
        {   static BlackwellIRProgram p; Code c;
            if (omega_blackwell_codegen_ceil_stream_prog(it, T, G, 0xffffffffu, P, &p) != 0 || upload(&p, &c) != 0) { FAILF("boundary codegen"); return -1; }
            uint32_t *want = malloc((size_t)T * G * 4u); for (uint32_t g = 0; g < T * G; g++) want[g] = stream_want(sw, g, it, T, G, 0xffffffffu, P, 0);
            uint64_t med; if (stream_point(cl, &c, &src, &out, &cnt, T, G, 1, 0, want, &med) != 0) return -1;
            if (omega_gpu_session_sync_to_cpu(&cnt) != 0) return -1;
            uint32_t got = *(uint32_t *)cnt.cpu; if (got != P * G) FAILF("grid barrier counter %u, want %u", got, P * G);
            printf("CEIL boundary kind=grid_barrier P=%u grid=%u span_us=%.1f GBps=%.1f per_boundary_us=%.2f counter=%u\n", P, G, med / 1e3, bytes / med, ((double)med - one_med * (bytes / ((double)G * T * 128.0 * total_it))) / P / 1e3, got);
            free(want); omega_gpu_session_free(&c.code);
        }
    }
    omega_gpu_session_free(&src); omega_gpu_session_free(&out); omega_gpu_session_free(&cnt);
    return 0;
}

int main(int argc, char **argv) {
    const char *only = NULL; int host = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else { printf("usage: %s [--host-only] [--only resid|launch|dram|l2|chase|boundary]\n", argv[0]); return 2; }
    }
    int hf = host_only();
    if (host || hf) { printf("GPU_CEILINGS: %s\n", hf ? "FAIL" : "PASS"); return hf ? 1 : 0; }
    OmegaGpuCmdList *cl = NULL;
    if (omega_gpu_cmdlist_create(1024, &cl) != 0) { printf("cmdlist create: %s\n", omega_gpu_session_last_error()); return 1; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    uint32_t cap256 = 0;
#define RUN(name, call) do { if (!only || !strcmp(only, name) || (!strcmp(name, "resid") && !strcmp(only, "boundary"))) { if ((call) != 0) g_fail++; } } while (0)
    RUN("resid", sec_resid(cl, &cap256));
    RUN("launch", sec_launch(cl));
    RUN("dram", sec_dram(cl));
    RUN("l2", sec_l2(cl));
    RUN("chase", sec_chase(cl));
    RUN("boundary", sec_boundary(cl, cap256));
    omega_gpu_session_unlock();
    printf("GPU_CEILINGS: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
