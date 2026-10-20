/* gpu_engine_piston_test.c (L312, 2026-10-20): the chip piston of the engine (src/omega_blackwell_codegen_piston.h), alone and
 * with a host fill engine feeding it while it runs.
 *   --host-only        the generator in the warp simulator (one CTA, fill words preset), every stored word checked. No chip.
 *   --mode pre         power engine alone: every beat's fuel is in place before the launch (slots = images), flags preset.
 *   --mode live        both engines: a host thread copies beat b's image from host RAM into slot b % slots, then raises
 *                      fill[b]; it waits for done[b - slots] before reusing a slot. The chip fires as fuel lands.
 *   --beats N --slots B --iters I --grid G --threads T --images P --out DIR
 * The fuel is P images of one slot each (distinct random words); beat b carries image b % P, so a beat fired on a stale slot
 * reads the wrong image and its XOR fails: that is a misfire. Every thread's XOR is checked against the host's own sum.
 * Chip numbers come from the chip's own clock (%globaltimer stamps per beat); host numbers from CLOCK_MONOTONIC.
 * Output: DIR/beats.tsv (per beat: chip fire start/done, chip idle before the beat, host fill start/done, host slot wait),
 * DIR/summary.txt and "PISTON key=value" lines; verdict last "GPU_ENGINE_PISTON: PASS|FAIL".
 * Chip mode: caller holds flock /tmp/aien-gb10.lock under quiet-measure. */
#include "omega_blackwell_codegen_piston.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim_tc.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int g_fail;
#define FAILF(...) do { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static uint64_t xs64(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

/* XOR thread gid reads from one slot image (same walk as the kernel) */
static uint32_t slot_xor(const uint32_t *img, uint32_t gid, uint32_t iters, uint32_t threads, uint32_t grid) {
    const uint64_t step = (uint64_t)grid * threads * 16u; uint64_t off = (uint64_t)gid * 16u; uint32_t a = 0;
    for (uint32_t i = 0; i < iters; i++) {
        for (uint32_t q = 0; q < 8; q++) { const uint32_t *w = img + (off + q * step) / 4; a ^= w[0] ^ w[1] ^ w[2] ^ w[3]; }
        off += 8u * step;
    }
    return a;
}
static void fill_image(uint32_t *img, size_t words, uint64_t seed) { uint64_t s = seed * 0x9E3779B97F4A7C15ull + 1; for (size_t i = 0; i < words; i++) img[i] = (uint32_t)xs64(&s); }

/* ---------------- host-only: the simulator ---------------- */
static int sim_piston(void) {
    static BlackwellIRProgram p; const uint32_t T = 64, G = 1, it = 2, beats = 5, slots = 2, P = 3;
    if (omega_blackwell_codegen_piston_prog(it, T, G, beats, slots, &p) != 0) { FAILF("piston codegen"); return 1; }
    const size_t slot = omega_piston_slot_bytes(it, T, G), words = slot / 4;
    /* the simulator runs the beats back to back with no host in between, so each slot holds one image for every beat it
     * serves: beat b reads slot b % slots, preset to image (b % slots) % P */
    uint32_t *src = malloc(slot * slots + 64), *out = calloc(T * G, 4), cnt = 0, fill[5], done[5], stamp[10];
    for (uint32_t s = 0; s < slots; s++) fill_image(src + s * words, words, s % P);
    for (uint32_t b = 0; b < beats; b++) { fill[b] = b + 1; done[b] = 0; }
    memset(stamp, 0, sizeof stamp);
    static WsSim s; memset(&s, 0, sizeof s); s.p = &p; s.threads = T;
    uint64_t v[6] = { (uintptr_t)src, (uintptr_t)out, (uintptr_t)&cnt, (uintptr_t)fill, (uintptr_t)done, (uintptr_t)stamp };
    for (int i = 0; i < 6; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)v[i]; s.cbank[(0x384 + 8 * i) / 4] = (uint32_t)(v[i] >> 32); }
    s.ranges[0] = (WsRange){ v[0], v[0] + slot * slots }; s.ranges[1] = (WsRange){ v[1], v[1] + T * G * 4u }; s.ranges[2] = (WsRange){ v[2], v[2] + 4 };
    s.ranges[3] = (WsRange){ v[3], v[3] + sizeof fill }; s.ranges[4] = (WsRange){ v[4], v[4] + sizeof done }; s.ranges[5] = (WsRange){ v[5], v[5] + sizeof stamp };
    s.nranges = 6;
    int err = ws_run(&s, G, 1); size_t bad = 0, dbad = 0;
    for (uint32_t g = 0; g < T * G; g++) {
        uint32_t want = 0;
        for (uint32_t b = 0; b < beats; b++) want ^= slot_xor(src + (b % slots) * words, g, it, T, G);
        if (out[g] != want) bad++;
    }
    for (uint32_t b = 0; b < beats; b++) if (done[b] != b + 1) dbad++;
    printf("sim piston beats %u slots %u: %s, %u registers, %zu instructions, %zu wrong xor, %zu wrong done, counter %u\n", beats, slots,
           err ? s.msg : "ran", p.regalloc.peak_gpr_usage, p.count, bad, dbad, cnt);
    int f = err || bad || dbad || cnt != beats * G;
    if (f) FAILF("sim piston");
    /* negative control: a beat whose slot holds the wrong image must fail the XOR */
    fill_image(src, words, 7); memset(out, 0, T * G * 4u); cnt = 0; for (uint32_t b = 0; b < beats; b++) done[b] = 0;
    memset(&s, 0, sizeof s); s.p = &p; s.threads = T;
    for (int i = 0; i < 6; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)v[i]; s.cbank[(0x384 + 8 * i) / 4] = (uint32_t)(v[i] >> 32); }
    s.ranges[0] = (WsRange){ v[0], v[0] + slot * slots }; s.ranges[1] = (WsRange){ v[1], v[1] + T * G * 4u }; s.ranges[2] = (WsRange){ v[2], v[2] + 4 };
    s.ranges[3] = (WsRange){ v[3], v[3] + sizeof fill }; s.ranges[4] = (WsRange){ v[4], v[4] + sizeof done }; s.ranges[5] = (WsRange){ v[5], v[5] + sizeof stamp };
    s.nranges = 6;
    uint32_t *keep = malloc(slot); fill_image(keep, words, 0);
    err = ws_run(&s, G, 1); size_t nbad = 0;
    for (uint32_t g = 0; g < T * G; g++) {
        uint32_t want = 0;
        for (uint32_t b = 0; b < beats; b++) want ^= slot_xor(b % slots ? src + words : keep, g, it, T, G);
        if (out[g] != want) nbad++;
    }
    printf("sim piston negative control (slot 0 stale): %zu of %u threads caught\n", nbad, T * G);
    if (err || nbad == 0) { FAILF("negative control not caught"); f = 1; }
    free(keep); free(src); free(out); return f;
}

/* ---------------- chip ---------------- */
typedef struct {
    uint32_t beats, slots, images; size_t slot;
    uint32_t **img;                  /* host RAM fuel, one image per index */
    uint8_t *cyl;                    /* slot 0 (cpu view of the cylinder memory) */
    volatile uint32_t *fill, *done;
    uint64_t *t_fill0, *t_fill1, *t_wait; /* host clock per beat: fill start, fill done, time spent waiting for a free slot */
    volatile int abort;
} Filler;
static void *filler_main(void *arg) {
    Filler *F = arg;
    for (uint32_t b = 0; b < F->beats && !F->abort; b++) {
        uint64_t w0 = now_ns();
        if (b >= F->slots) { const uint32_t need = b - F->slots + 1; while (F->done[b - F->slots] < need) { if (F->abort) return NULL; __asm__ volatile("yield"); } }
        F->t_wait[b] = now_ns() - w0;
        F->t_fill0[b] = now_ns();
        memcpy(F->cyl + (size_t)(b % F->slots) * F->slot, F->img[b % F->images], F->slot);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        __atomic_store_n(&F->fill[b], b + 1, __ATOMIC_RELEASE);
        F->t_fill1[b] = now_ns();
    }
    return NULL;
}
typedef struct { OmegaGpuLaunch L; uint64_t elapsed; int rc; } Shot;
static void *launch_main(void *arg) { Shot *s = arg; s->rc = omega_gpu_session_launch(&s->L, &s->elapsed, NULL); return NULL; }

static void series(const char *name, uint64_t *v, uint32_t n, FILE *sum) {
    if (!n) return;
    uint64_t *c = malloc(n * sizeof *c); memcpy(c, v, n * sizeof *c); qsort(c, n, sizeof *c, cmp_u64);
    double md = c[n / 2] / 1e3, p10 = c[n / 10] / 1e3, p90 = c[(n * 9) / 10] / 1e3, mx = c[n - 1] / 1e3, mn = c[0] / 1e3;
    char line[256];
    snprintf(line, sizeof line, "PISTON %s_us n=%u min=%.2f p10=%.2f median=%.2f p90=%.2f max=%.2f spread_p90_p10=%.3f\n", name, n, mn, p10, md, p90, mx, p10 > 0 ? p90 / p10 : 0);
    fputs(line, stdout); if (sum) fputs(line, sum);
    free(c);
}

static int chip(const char *mode, uint32_t beats, uint32_t slots, uint32_t iters, uint32_t grid, uint32_t threads, uint32_t images, const char *out) {
    const int live = !strcmp(mode, "live");
    if (!live) slots = images; /* power engine alone: every image sits in its own slot before the launch */
    if (live && slots >= images) { FAILF("live needs images > slots (stale slot must read a different image)"); return -1; }
    static BlackwellIRProgram p;
    if (omega_blackwell_codegen_piston_prog(iters, threads, grid, beats, slots, &p) != 0) { FAILF("piston codegen"); return -1; }
    const size_t slot = omega_piston_slot_bytes(iters, threads, grid), words = slot / 4, nt = (size_t)grid * threads;
    OmegaGpuMem code, cyl, outm, cnt, fill, done, stamp;
    OmegaBlackwellKernel k; size_t need = omega_bw_program_code_bytes(&p);
    if (omega_gpu_session_alloc_code(need, &code) != 0 || omega_blackwell_kernel_encode_into(&p, code.cpu, need, &k) != 0 || omega_gpu_session_sync_to_gpu(&code) != 0) { FAILF("upload: %s", omega_gpu_session_last_error()); return -1; }
    if (omega_gpu_session_alloc(slot * slots + 4096, &cyl) != 0 || omega_gpu_session_alloc(nt * 4u, &outm) != 0 || omega_gpu_session_alloc(64, &cnt) != 0 ||
        omega_gpu_session_alloc((size_t)beats * 4u, &fill) != 0 || omega_gpu_session_alloc((size_t)beats * 4u, &done) != 0 || omega_gpu_session_alloc((size_t)beats * 8u, &stamp) != 0) { FAILF("alloc: %s", omega_gpu_session_last_error()); return -1; }
    uint32_t **img = calloc(images, sizeof *img);
    uint32_t **want_img = calloc(images, sizeof *want_img);
    for (uint32_t i = 0; i < images; i++) {
        img[i] = aligned_alloc(4096, (slot + 4095) & ~(size_t)4095); fill_image(img[i], words, 1000 + i);
        want_img[i] = malloc(nt * 4u); for (size_t g = 0; g < nt; g++) want_img[i][g] = slot_xor(img[i], (uint32_t)g, iters, threads, grid);
    }
    memset(outm.cpu, 0xee, nt * 4u); memset(cnt.cpu, 0, 64); memset(done.cpu, 0, (size_t)beats * 4u); memset(stamp.cpu, 0, (size_t)beats * 8u);
    uint32_t *fw = fill.cpu;
    if (live) memset(fill.cpu, 0, (size_t)beats * 4u);
    else { for (uint32_t i = 0; i < slots; i++) memcpy((uint8_t *)cyl.cpu + (size_t)i * slot, img[i], slot); for (uint32_t b = 0; b < beats; b++) fw[b] = b + 1; }
    if (omega_gpu_session_sync_to_gpu(&cyl) != 0 || omega_gpu_session_sync_to_gpu(&outm) != 0 || omega_gpu_session_sync_to_gpu(&cnt) != 0 ||
        omega_gpu_session_sync_to_gpu(&fill) != 0 || omega_gpu_session_sync_to_gpu(&done) != 0 || omega_gpu_session_sync_to_gpu(&stamp) != 0) { FAILF("sync"); return -1; }
    uint32_t args[12]; uint64_t va[6] = { cyl.va, outm.va, cnt.va, fill.va, done.va, stamp.va };
    for (int i = 0; i < 6; i++) { args[2 * i] = (uint32_t)va[i]; args[2 * i + 1] = (uint32_t)(va[i] >> 32); }
    Shot shot; memset(&shot, 0, sizeof shot);
    shot.L.code_va = code.va; shot.L.gpr_count = omega_bw_gpr_declared(p.regalloc.peak_gpr_usage); shot.L.threads_x = threads; shot.L.threads_y = 1;
    shot.L.grid_x = grid; shot.L.grid_y = 1; shot.L.num_elements = threads * grid; shot.L.args = args; shot.L.n_args = 12;
    shot.L.timeout_ms = 30000u + beats * 100u;
    Filler F; memset(&F, 0, sizeof F);
    F.beats = beats; F.slots = slots; F.images = images; F.slot = slot; F.img = img; F.cyl = cyl.cpu; F.fill = fill.cpu; F.done = done.cpu;
    F.t_fill0 = calloc(beats, 8); F.t_fill1 = calloc(beats, 8); F.t_wait = calloc(beats, 8);
    pthread_t tf, tl; const uint64_t t0 = now_ns();
    if (pthread_create(&tl, NULL, launch_main, &shot) != 0) { FAILF("launch thread"); return -1; }
    if (live && pthread_create(&tf, NULL, filler_main, &F) != 0) { F.abort = 1; FAILF("filler thread"); }
    pthread_join(tl, NULL);
    if (shot.rc != 0) { F.abort = 1; FAILF("launch rc %d: %s", shot.rc, omega_gpu_session_last_error()); }
    if (live) pthread_join(tf, NULL);
    const uint64_t t1 = now_ns();
    if (omega_gpu_session_sync_to_cpu(&outm) != 0 || omega_gpu_session_sync_to_cpu(&done) != 0 || omega_gpu_session_sync_to_cpu(&stamp) != 0 || omega_gpu_session_sync_to_cpu(&cnt) != 0) { FAILF("sync back"); return -1; }
    /* checks: every thread's XOR over all beats, every done word, the barrier count */
    const uint32_t *o = outm.cpu, *dn = done.cpu, *st = stamp.cpu; size_t bad = 0, dbad = 0;
    for (size_t g = 0; g < nt; g++) { uint32_t w = 0; for (uint32_t b = 0; b < beats; b++) w ^= want_img[b % images][g]; if (o[g] != w) bad++; }
    for (uint32_t b = 0; b < beats; b++) if (dn[b] != b + 1) dbad++;
    const uint32_t cw = *(const uint32_t *)cnt.cpu;
    /* per-beat series (chip clock deltas are taken mod 2^32: consecutive stamps are far less than 4.29 s apart) */
    uint64_t *fire = calloc(beats, 8), *idle = calloc(beats, 8), *beat = calloc(beats, 8), *hfill = calloc(beats, 8), *hwait = calloc(beats, 8);
    uint32_t ni = 0, nb = 0;
    mkdir(out, 0775);
    char path[512]; snprintf(path, sizeof path, "%s/beats.tsv", out); FILE *tsv = fopen(path, "w");
    if (tsv) fprintf(tsv, "beat\tchip_fire_start_ns\tchip_fire_done_ns\tchip_fire_ns\tchip_idle_before_ns\thost_fill_start_ns\thost_fill_done_ns\thost_slot_wait_ns\n");
    for (uint32_t b = 0; b < beats; b++) {
        fire[b] = (uint32_t)(st[2 * b + 1] - st[2 * b]);
        if (b) { idle[ni++] = (uint32_t)(st[2 * b] - st[2 * b - 1]); beat[nb++] = (uint32_t)(st[2 * b] - st[2 * b - 2]); }
        if (live) { hfill[b] = F.t_fill1[b] - F.t_fill0[b]; hwait[b] = F.t_wait[b]; }
        if (tsv) fprintf(tsv, "%u\t%u\t%u\t%llu\t%llu\t%llu\t%llu\t%llu\n", b, st[2 * b], st[2 * b + 1], (unsigned long long)fire[b], (unsigned long long)(b ? idle[ni - 1] : 0),
                         (unsigned long long)(live ? F.t_fill0[b] - t0 : 0), (unsigned long long)(live ? F.t_fill1[b] - t0 : 0), (unsigned long long)(live ? hwait[b] : 0));
    }
    if (tsv) fclose(tsv);
    snprintf(path, sizeof path, "%s/summary.txt", out); FILE *sum = fopen(path, "w");
    char head[512];
    snprintf(head, sizeof head, "PISTON mode=%s beats=%u slots=%u images=%u iters=%u grid=%u threads=%u slot_bytes=%zu wall_ms=%.1f launch_elapsed_ms=%.1f wrong_xor_threads=%zu wrong_done=%zu barrier_count=%u want=%u\n",
             mode, beats, slots, images, iters, grid, threads, slot, (t1 - t0) / 1e6, shot.elapsed / 1e6, bad, dbad, cw, beats * grid);
    fputs(head, stdout); if (sum) fputs(head, sum);
    series("chip_fire", fire, beats, sum); series("chip_idle_before_beat", idle, ni, sum); series("chip_beat", beat, nb, sum);
    if (live) { series("host_fill", hfill, beats, sum); series("host_slot_wait", hwait, beats, sum); }
    /* fire bandwidth: slot bytes over the median fire time */
    uint64_t *c = malloc(beats * 8); memcpy(c, fire, beats * 8); qsort(c, beats, 8, cmp_u64);
    snprintf(head, sizeof head, "PISTON fire_gbps_median=%.1f\n", c[beats / 2] ? (double)slot / (double)c[beats / 2] : 0); fputs(head, stdout); if (sum) fputs(head, sum);
    free(c);
    if (sum) fclose(sum);
    if (bad || dbad || cw != beats * grid) FAILF("misfire: %zu threads wrong xor, %zu done words wrong, barrier %u of %u", bad, dbad, cw, beats * grid);
    for (uint32_t i = 0; i < images; i++) { free(img[i]); free(want_img[i]); }
    free(img); free(want_img); free(fire); free(idle); free(beat); free(hfill); free(hwait); free(F.t_fill0); free(F.t_fill1); free(F.t_wait);
    omega_gpu_session_free(&code); omega_gpu_session_free(&cyl); omega_gpu_session_free(&outm); omega_gpu_session_free(&cnt);
    omega_gpu_session_free(&fill); omega_gpu_session_free(&done); omega_gpu_session_free(&stamp);
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = "pre", *out = "piston-out"; uint32_t beats = 2000, slots = 2, iters = 24, grid = 48, threads = 256, images = 8; int host_only = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host_only = 1;
        else if (i + 1 < argc && !strcmp(argv[i], "--mode")) mode = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--out")) out = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--beats")) beats = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (i + 1 < argc && !strcmp(argv[i], "--slots")) slots = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (i + 1 < argc && !strcmp(argv[i], "--iters")) iters = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (i + 1 < argc && !strcmp(argv[i], "--grid")) grid = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (i + 1 < argc && !strcmp(argv[i], "--threads")) threads = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (i + 1 < argc && !strcmp(argv[i], "--images")) images = (uint32_t)strtoul(argv[++i], NULL, 0);
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (host_only) { sim_piston(); printf("GPU_ENGINE_PISTON: %s\n", g_fail ? "FAIL" : "PASS"); return g_fail ? 1 : 0; }
    if (strcmp(mode, "pre") && strcmp(mode, "live")) { printf("--mode pre|live\n"); return 2; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open failed: %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    chip(mode, beats, slots, iters, grid, threads, images, out);
    omega_gpu_session_close();
    omega_gpu_session_unlock();
    printf("GPU_ENGINE_PISTON: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
