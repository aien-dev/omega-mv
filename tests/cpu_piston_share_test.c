/* cpu_piston_share_test.c (L321 E1 + E1b, 2026-10-21): the shared point and the second road.
 *
 * Question (Drake): load something once and let the CPU and the GPU both use it. NVIDIA's GB10 deck (Hot Chips 2025):
 * "16MB System Level Cache: Serves as L4 cache for CPU, enables power efficient data-sharing between engines" and
 * "Hardware-enabled bidirectional coherency between CPU and GPU". This probe measures whether a read (or write, or an NVMe
 * landing) by one side leaves the data where the other side gets it without a second DRAM trip, how big that place is
 * (the size knee) and how long it stays (the delay sweep).
 *
 * Instruments, all on one pinned core (--cpu, default 19 = X925 in cluster 1):
 *   chase  random pointer chase, one dependent load per 64-byte line, each line visited at most once: ns per line
 *          (the latency says where the line came from: own L2, cluster L3, system cache, DRAM);
 *   seq    one-core sequential read of the whole buffer: GB/s;
 *   core PMU counters around each measured region (needs perf_event_paranoid <= 2): LL_CACHE_RD (0x36),
 *          LL_CACHE_MISS_RD (0x37), L3D_CACHE_REFILL (0x2A); "counters off" when refused;
 *   gpu    the XOR stream kernel (omega_blackwell_codegen_tc_bw_prog), launch ns, every XOR result checked.
 *
 * Cold = the line is in no cache we can reach: DC CIVAC over the buffer, then the core streams a 96 MB evict buffer, and (chip
 * arms) the GPU streams a 64 MB GPU-cached evict buffer first. Every arm runs the same evictions; only the touch differs.
 *
 * Arms (one line "CELL ..." each, median/min/max over --reps):
 *  host (no chip): c-cold, c-self (core read it first), c-civac (core read it, then DC CIVAC: is the system cache beyond the
 *       point of coherency?), c-nvme-dN (O_DIRECT read landed it N us ago), c-nvme-ev (landed, then evicted: control).
 *  chip, buffers host (GPU-uncached) and dev (GPU-cached):
 *       g2c-dN (GPU streamed X, N us later the core reads X), g2c-ctl-dN (GPU streamed a different buffer Y instead);
 *       c2g-cold, c2g-read (core read X first), c2g-wsync / c2g-wnosync (core rewrote X, with / without the session sync;
 *       the XOR check is the coherence check), g2g (GPU streamed X just before), n2g / n2g-ev (NVMe landed X, then GPU).
 *  E1b chip: z-cold / z-hot: one launch reads [H | D] (H = h bytes, D = 64 MB): H cold vs H read by the core first
 *       (a second road shows as total GB/s above the cold launch); r-hot / r-ctl: the core reads GPU-hot X while a second
 *       thread keeps the GPU streaming a 256 MB buffer (the reverse road under a full pipe).
 * Verdict line last: "CPU_PISTON_SHARE: PASS|FAIL" (FAIL = a wrong XOR, a refused chip call or a broken chain). */
#include "omega_gpu_prefill.h"
#include "omega_blackwell_codegen_tc.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define LINE 64u
#define MAXR 15
#define EVICT_CPU (96ull << 20)
#define EVICT_GPU (64ull << 20)
#define ZD (64ull << 20)
#define RBIG (256ull << 20)
#define MAX_HOPS 131072u

static int g_reps = 5, g_fail = 0;
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static void spin_until(uint64_t t) { while (now_ns() < t) { } }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* ---- core counters ---- */
static int g_pfd[3] = { -1, -1, -1 };
static void pmu_open(int cpu) {
    char path[160], buf[64]; int type = -1;
    for (int k = 0; k < 4 && type < 0; k++) {
        snprintf(path, sizeof path, "/sys/bus/event_source/devices/armv8_pmuv3_%d/cpus", k);
        FILE *f = fopen(path, "r"); if (!f) continue;
        char list[128] = { 0 }; if (!fgets(list, sizeof list, f)) list[0] = 0; fclose(f);
        int in = 0; for (char *s = list; *s;) { int a = (int)strtol(s, &s, 10), b = a; if (*s == '-') b = (int)strtol(s + 1, &s, 10); if (cpu >= a && cpu <= b) in = 1; if (*s == ',') s++; else break; }
        if (!in) continue;
        snprintf(path, sizeof path, "/sys/bus/event_source/devices/armv8_pmuv3_%d/type", k);
        f = fopen(path, "r"); if (f && fgets(buf, sizeof buf, f)) type = atoi(buf); if (f) fclose(f);
    }
    const uint64_t ev[3] = { 0x36, 0x37, 0x2A };
    for (int i = 0; i < 3 && type >= 0; i++) {
        struct perf_event_attr a; memset(&a, 0, sizeof a); a.size = sizeof a; a.type = (uint32_t)type; a.config = ev[i];
        a.exclude_kernel = 1; a.exclude_hv = 1; a.disabled = 0;
        g_pfd[i] = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    }
    printf("counters: %s (pmu type %d; LL_CACHE_RD, LL_CACHE_MISS_RD, L3D_CACHE_REFILL)\n", g_pfd[0] >= 0 && g_pfd[1] >= 0 ? "on" : "off", type);
}
static void pmu_read(uint64_t v[3]) { for (int i = 0; i < 3; i++) { v[i] = 0; if (g_pfd[i] >= 0 && read(g_pfd[i], &v[i], 8) != 8) v[i] = 0; } }

/* ---- buffers ---- */
typedef struct { const char *name; uint8_t *cpu; OmegaGpuMem mem; int chip; size_t cap; } Buf;
static uint8_t *g_evc;
static void *alloc_host_huge(size_t n) { void *p = NULL; if (posix_memalign(&p, 2u << 20, n)) return NULL; madvise(p, n, MADV_HUGEPAGE); memset(p, 1, n); return p; }

/* chain: a random cyclic order of the S/64 lines; word 0..1 of a line = byte offset of the next line; words 2..15 = filler */
static uint32_t *g_perm;
static void fill_chain(uint8_t *x, size_t S, uint64_t seed) {
    const size_t n = S / LINE;
    for (size_t i = 0; i < n; i++) g_perm[i] = (uint32_t)i;
    uint64_t r = seed * 0x9e3779b97f4a7c15ull | 1;
    for (size_t i = n - 1; i > 0; i--) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; size_t j = r % (i + 1); uint32_t t = g_perm[i]; g_perm[i] = g_perm[j]; g_perm[j] = t; }
    for (size_t i = 0; i < n; i++) {
        uint64_t *l = (uint64_t *)(x + (size_t)g_perm[i] * LINE);
        l[0] = (uint64_t)g_perm[(i + 1) % n] * LINE;
        uint32_t *w = (uint32_t *)l;
        for (uint32_t k = 2; k < 16; k++) w[k] = (uint32_t)(((size_t)g_perm[i] * 16 + k) * 2654435761u) ^ 0x5bd1e995u;
    }
}
static int chain_ok(const uint8_t *x, size_t S) { /* the whole chain closes after exactly S/64 hops */
    const size_t n = S / LINE; uint64_t o = 0; for (size_t i = 0; i < n; i++) { o = *(const uint64_t *)(x + o); if (o >= S) return 0; if (o == 0 && i != n - 1) return 0; }
    return o == 0;
}
static void civac(const uint8_t *x, size_t S) { for (size_t o = 0; o < S; o += LINE) __asm__ volatile("dc civac, %0" ::"r"(x + o) : "memory"); __asm__ volatile("dsb sy" ::: "memory"); }
static uint64_t seq_sum(const uint8_t *x, size_t S) {
    const uint64_t *p = (const uint64_t *)x; uint64_t a = 0, b = 0, c = 0, d = 0;
    for (size_t i = 0; i < S / 8; i += 4) { a += p[i]; b ^= p[i + 1]; c += p[i + 2]; d ^= p[i + 3]; }
    return a + b + c + d;
}
static volatile uint64_t g_sink;
static void evict_cpu(const uint8_t *x, size_t S) { civac(x, S); g_sink += seq_sum(g_evc, EVICT_CPU); }

/* metric 0 = chase ns/line, 1 = seq GB/s */
static double measure_cpu(int metric, const uint8_t *x, size_t S, uint64_t ctr[3]) {
    uint64_t c0[3], c1[3]; pmu_read(c0);
    uint64_t t0 = now_ns(), t1;
    double v;
    if (metric == 0) {
        size_t hops = S / LINE; if (hops > MAX_HOPS) hops = MAX_HOPS;
        uint64_t o = 0; for (size_t i = 0; i < hops; i++) o = *(const volatile uint64_t *)(x + o);
        t1 = now_ns(); g_sink += o; v = (double)(t1 - t0) / (double)hops;
    } else { g_sink += seq_sum(x, S); t1 = now_ns(); v = (double)S / (double)(t1 - t0); }
    pmu_read(c1); for (int i = 0; i < 3; i++) ctr[i] = c1[i] - c0[i];
    return v;
}

/* ---- GPU stream ---- */
static OmegaGpuMem g_out, g_evg;
static int gen_bw(const OmegaPfKey *k, void *prog) { return omega_blackwell_codegen_tc_bw_prog(k->words[0], k->words[1], k->words[2], prog); }
typedef struct { uint32_t T, iters, grid; } Shape;
static Shape shape_of(size_t S) {
    Shape s; s.T = S >= (8u << 20) ? 256u : 64u; s.iters = 1;
    const size_t chunk = (size_t)s.T * 128u;
    while (s.iters < 8 && S % (chunk * s.iters * 2) == 0 && S / (chunk * s.iters * 2) >= 384) s.iters *= 2;
    s.grid = (uint32_t)(S / (chunk * s.iters)); return s;
}
static int gpu_stream(const OmegaGpuMem *m, size_t off, size_t S, uint64_t *ns) {
    Shape s = shape_of(S);
    OmegaPfKey key; memset(&key, 0, sizeof key); key.kind = 0x100u; key.words[0] = s.iters; key.words[1] = s.grid; key.words[2] = s.T;
    const uint64_t va = m->va + off;
    uint32_t args[4] = { (uint32_t)va, (uint32_t)(va >> 32), (uint32_t)g_out.va, (uint32_t)(g_out.va >> 32) };
    uint64_t t = 0;
    omega_gpu_session_lock();
    OmegaPfKernel k; int rc = omega_gpu_pf_open_locked();
    if (!rc) rc = omega_gpu_pf_kernel_locked(&key, gen_bw, &k);
    if (!rc) rc = omega_gpu_pf_launch_locked(&k, s.T, s.grid, 1, 0, args, 4, &t);
    omega_gpu_session_unlock();
    if (ns) *ns = t;
    if (rc) { printf("gpu stream refused: %s (%s)\n", omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error()); g_fail++; }
    return rc;
}
/* the XOR each thread of the stream kernel must produce over x (S bytes); salt != 0: filler words w ^= salt * (idx | 1) */
static void want_of(const uint8_t *x, size_t S, uint32_t salt, uint32_t *want) {
    Shape s = shape_of(S); const size_t region = (size_t)s.iters * s.T * 128u;
    for (uint32_t c = 0; c < s.grid; c++) for (uint32_t t = 0; t < s.T; t++) {
        uint32_t acc = 0;
        for (uint32_t it = 0; it < s.iters; it++) for (uint32_t q = 0; q < 8; q++) for (uint32_t k = 0; k < 4; k++) {
            const size_t idx = ((size_t)c * region + (size_t)it * s.T * 128u + (size_t)q * s.T * 16u + (size_t)t * 16u) / 4 + k;
            acc ^= x ? ((const uint32_t *)x)[idx] : ((idx % 16) >= 2 ? salt * (uint32_t)(idx | 1) : 0u);
        }
        want[(size_t)c * s.T + t] = acc;
    }
}
static void salt_apply(uint8_t *x, size_t S, uint32_t salt) { uint32_t *w = (uint32_t *)x; for (size_t i = 0; i < S / 4; i++) if ((i % 16) >= 2) w[i] ^= salt * (uint32_t)(i | 1); }
static size_t check_out(const uint32_t *want, const uint32_t *delta, size_t S) {
    Shape s = shape_of(S); const size_t n = (size_t)s.grid * s.T; size_t bad = 0;
    if (omega_gpu_session_sync_to_cpu(&g_out) != 0) return n;
    const uint32_t *o = (const uint32_t *)g_out.cpu;
    for (size_t i = 0; i < n; i++) if (o[i] != (want[i] ^ (delta ? delta[i] : 0u))) bad++;
    return bad;
}
static void evict_gpu(void) { (void)gpu_stream(&g_evg, 0, EVICT_GPU, NULL); }

/* ---- cells ---- */
static void cell(const char *arm, const char *buf, size_t S, int delay, const char *metric, double *v, uint64_t (*c)[3], int n, size_t wrong) {
    double s[MAXR]; memcpy(s, v, sizeof(double) * n); qsort(s, n, sizeof(double), cmp_d);
    uint64_t m[3]; for (int i = 0; i < 3; i++) { double t[MAXR]; for (int r = 0; r < n; r++) t[r] = (double)c[r][i]; qsort(t, n, sizeof(double), cmp_d); m[i] = (uint64_t)t[n / 2]; }
    double la[3] = { 0 }; FILE *f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf %lf %lf", &la[0], &la[1], &la[2]) != 3) la[0] = -1; fclose(f); }
    printf("CELL arm=%s buf=%s size=%zu delay_us=%d metric=%s med=%.3f min=%.3f max=%.3f llrd=%llu llmiss=%llu l3refill=%llu lines=%zu wrong=%zu reps=%d load1=%.1f\n",
           arm, buf, S, delay, metric, s[n / 2], s[0], s[n - 1], (unsigned long long)m[0], (unsigned long long)m[1], (unsigned long long)m[2],
           metric[0] == 'c' ? (S / LINE > MAX_HOPS ? (size_t)MAX_HOPS : S / LINE) : S / LINE, wrong, n, la[0]);
    fflush(stdout);
}

static const int DELAYS[] = { 0, 1, 10, 100, 1000 };
enum { ND = 5 };
static int g_nvfd = -1;

static int nvme_land(uint8_t *x, size_t S) { /* O_DIRECT read of the file's first S bytes into x */
    size_t done = 0;
    while (done < S) { ssize_t r = pread(g_nvfd, x + done, S - done, (off_t)done); if (r <= 0) return r < 0 ? -errno : -EIO; done += (size_t)r; }
    return 0;
}
static int nvme_store(const char *path, const uint8_t *x, size_t S) {
    int fd = open(path, O_WRONLY | O_CREAT, 0644); if (fd < 0) return -errno;
    size_t done = 0; while (done < S) { ssize_t w = pwrite(fd, x + done, S - done, (off_t)done); if (w <= 0) { close(fd); return -EIO; } done += (size_t)w; }
    fdatasync(fd); posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED); close(fd); return 0;
}

static void host_arms(Buf *b, size_t S) {
    const char *mn[2] = { "chase_ns", "seq_gbs" };
    double v[MAXR]; uint64_t c[MAXR][3]; char arm[32];
    for (int m = 0; m < 2; m++) {
        for (int r = 0; r < g_reps; r++) { evict_cpu(b->cpu, S); v[r] = measure_cpu(m, b->cpu, S, c[r]); }
        cell("c-cold", b->name, S, -1, mn[m], v, c, g_reps, 0);
        for (int r = 0; r < g_reps; r++) { evict_cpu(b->cpu, S); g_sink += seq_sum(b->cpu, S); v[r] = measure_cpu(m, b->cpu, S, c[r]); }
        cell("c-self", b->name, S, -1, mn[m], v, c, g_reps, 0);
        for (int r = 0; r < g_reps; r++) { evict_cpu(b->cpu, S); g_sink += seq_sum(b->cpu, S); civac(b->cpu, S); v[r] = measure_cpu(m, b->cpu, S, c[r]); }
        cell("c-civac", b->name, S, -1, mn[m], v, c, g_reps, 0);
        if (g_nvfd < 0) continue;
        for (int d = 0; d < ND; d++) {
            int bad = 0;
            for (int r = 0; r < g_reps; r++) { evict_cpu(b->cpu, S); if (nvme_land(b->cpu, S)) bad = 1; spin_until(now_ns() + (uint64_t)DELAYS[d] * 1000u); v[r] = measure_cpu(m, b->cpu, S, c[r]); }
            snprintf(arm, sizeof arm, "c-nvme-d%d", DELAYS[d]); cell(arm, b->name, S, DELAYS[d], mn[m], v, c, g_reps, (size_t)bad);
            if (bad) { printf("nvme landing refused on %s (errno)\n", b->name); g_fail++; break; }
        }
        for (int r = 0; r < g_reps; r++) { if (nvme_land(b->cpu, S)) g_fail++; evict_cpu(b->cpu, S); v[r] = measure_cpu(m, b->cpu, S, c[r]); }
        cell("c-nvme-ev", b->name, S, -1, mn[m], v, c, g_reps, 0);
    }
}

static void chip_arms(Buf *x, Buf *y, size_t S, const uint32_t *want, uint32_t *delta) {
    const char *mn[2] = { "chase_ns", "seq_gbs" };
    double v[MAXR]; uint64_t c[MAXR][3]; char arm[32]; uint64_t ns;
    /* GPU first, then the core */
    for (int m = 0; m < 2; m++) for (int d = 0; d < ND; d++) for (int ctl = 0; ctl < 2; ctl++) {
        for (int r = 0; r < g_reps; r++) {
            evict_gpu(); evict_cpu(x->cpu, S);
            gpu_stream(ctl ? &y->mem : &x->mem, 0, S, &ns);
            spin_until(now_ns() + (uint64_t)DELAYS[d] * 1000u);
            v[r] = measure_cpu(m, x->cpu, S, c[r]);
        }
        snprintf(arm, sizeof arm, ctl ? "g2c-ctl-d%d" : "g2c-d%d", DELAYS[d]); cell(arm, x->name, S, DELAYS[d], mn[m], v, c, g_reps, 0);
    }
    /* the core first (or GPU, or NVMe), then the GPU: metric = GPU GB/s from launch ns */
    static const char *carm[] = { "c2g-cold", "c2g-read", "c2g-wsync", "c2g-wnosync", "g2g", "n2g", "n2g-ev", "c2g-wstale" };
    for (int a = 0; a < 8; a++) {
        if ((a == 5 || a == 6) && g_nvfd < 0) continue;
        size_t wrong = 0; int refused = 0;
        for (int r = 0; r < g_reps; r++) {
            const uint32_t salt = (a == 2 || a == 3 || a == 7) ? 0x01000193u * (uint32_t)(r + 1) : 0u;
            if (salt) want_of(NULL, S, salt, delta);
            /* wstale: the GPU reads X first (lines in its L2), the core rewrites X, the GPU reads again with no sync: coherence */
            if (a == 7) { evict_gpu(); gpu_stream(&x->mem, 0, S, NULL); evict_cpu(x->cpu, S); } else { evict_gpu(); evict_cpu(x->cpu, S); }
            if (a == 1) g_sink += seq_sum(x->cpu, S);
            if (salt) salt_apply(x->cpu, S, salt);
            if (a == 2 && omega_gpu_session_sync_to_gpu(&x->mem) != 0) { refused = 1; }
            if (a == 4) gpu_stream(&x->mem, 0, S, NULL);
            if (a == 5 || a == 6) { if (nvme_land(x->cpu, S)) { refused = 1; break; } if (a == 6) { evict_gpu(); evict_cpu(x->cpu, S); } }
            memset(g_out.cpu, 0, g_out.size); omega_gpu_session_sync_to_gpu(&g_out);
            if (gpu_stream(&x->mem, 0, S, &ns)) { refused = 1; break; }
            v[r] = (double)S / (double)ns; c[r][0] = ns; c[r][1] = c[r][2] = 0;
            wrong += check_out(want, salt ? delta : NULL, S);
            if (salt) salt_apply(x->cpu, S, salt); /* restore */
        }
        if (refused) { printf("arm %s on %s refused (%s)\n", carm[a], x->name, omega_gpu_session_last_error()); if (a != 5 && a != 6) g_fail++; continue; }
        cell(carm[a], x->name, S, -1, "gpu_gbs(llrd=ns)", v, c, g_reps, wrong);
        if (wrong) { printf("WRONG: %s %s size %zu: %zu wrong XOR words\n", carm[a], x->name, S, wrong); g_fail++; }
    }
}

/* E1b (2): one launch reads [H | D]; H hot in the core's caches or cold */
static void zarms(Buf *z, size_t h, const uint32_t *want) {
    double v[MAXR]; uint64_t c[MAXR][3]; uint64_t ns;
    for (int hot = 0; hot < 2; hot++) {
        size_t wrong = 0;
        for (int r = 0; r < g_reps; r++) {
            evict_gpu(); evict_cpu(z->cpu, h + ZD);
            if (hot) g_sink += seq_sum(z->cpu, h);
            if (gpu_stream(&z->mem, 0, h + ZD, &ns)) return;
            v[r] = (double)(h + ZD) / (double)ns; c[r][0] = ns; c[r][1] = c[r][2] = 0;
            wrong += check_out(want, NULL, h + ZD);
        }
        cell(hot ? "z-hot" : "z-cold", z->name, h, -1, "gpu_gbs_total(llrd=ns)", v, c, g_reps, wrong);
        if (wrong) g_fail++;
    }
}

/* E1b (3): the core reads GPU-hot X while a second thread keeps the GPU streaming RBIG bytes */
static OmegaGpuMem g_rbig;
static atomic_int g_go, g_stop, g_launching; static atomic_ullong g_rb_bytes, g_rb_ns;
static int g_bg_cpu = 15;
static void *bg_stream(void *arg) {
    (void)arg; cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(g_bg_cpu, &cs); sched_setaffinity(0, sizeof cs, &cs);
    for (;;) {
        while (!atomic_load(&g_go)) { if (atomic_load(&g_stop) == 2) return NULL; usleep(50); } /* idle: sleep, do not load the cluster */
        uint64_t ns = 0; atomic_store(&g_launching, 1); if (gpu_stream(&g_rbig, 0, RBIG, &ns)) { atomic_store(&g_go, 0); continue; }
        atomic_fetch_add(&g_rb_bytes, RBIG); atomic_fetch_add(&g_rb_ns, ns);
        if (atomic_load(&g_stop) == 1) { atomic_store(&g_go, 0); atomic_store(&g_stop, 0); }
    }
}
static void rarms(Buf *x, Buf *y, size_t S) {
    double v[MAXR], gv[MAXR]; uint64_t c[MAXR][3], gc[MAXR][3];
    for (int ctl = 0; ctl < 2; ctl++) {
        for (int r = 0; r < g_reps; r++) {
            evict_gpu(); evict_cpu(x->cpu, S);
            gpu_stream(ctl ? &y->mem : &x->mem, 0, S, NULL);
            atomic_store(&g_rb_bytes, 0); atomic_store(&g_rb_ns, 0); atomic_store(&g_launching, 0); atomic_store(&g_go, 1);
            while (!atomic_load(&g_launching)) { } spin_until(now_ns() + 20000u); /* the stream is submitted and running */
            v[r] = measure_cpu(1, x->cpu, S, c[r]);
            atomic_store(&g_stop, 1); while (atomic_load(&g_go)) { }
            const uint64_t b = atomic_load(&g_rb_bytes), t = atomic_load(&g_rb_ns);
            gv[r] = t ? (double)b / (double)t : 0; gc[r][0] = b; gc[r][1] = gc[r][2] = 0;
        }
        cell(ctl ? "r-ctl" : "r-hot", x->name, S, 0, "seq_gbs", v, c, g_reps, 0);
        cell(ctl ? "r-ctl-gpu" : "r-hot-gpu", x->name, S, 0, "gpu_gbs(llrd=bytes)", gv, gc, g_reps, 0);
    }
}

int main(int argc, char **argv) {
    int chip = 1, cpu = 19; const char *nvme = NULL; size_t only = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) chip = 0;
        else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bg-cpu") && i + 1 < argc) g_bg_cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) { g_reps = atoi(argv[++i]); if (g_reps < 1) g_reps = 1; if (g_reps > MAXR) g_reps = MAXR; }
        else if (!strcmp(argv[i], "--nvme") && i + 1 < argc) nvme = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) only = (size_t)strtoull(argv[++i], NULL, 0);
        else { fprintf(stderr, "usage: %s [--host-only] [--cpu N] [--bg-cpu N] [--reps R] [--nvme FILE] [--size BYTES]\n", argv[0]); return 2; }
    }
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(cpu, &cs);
    if (sched_setaffinity(0, sizeof cs, &cs)) { printf("pin to cpu %d refused\n", cpu); return 1; }
    printf("cpu_piston_share: core %d, bg core %d, reps %d, chip %d, nvme %s\n", cpu, g_bg_cpu, g_reps, chip, nvme ? nvme : "off");
    pmu_open(cpu);
    static const size_t SZ[] = { 256u << 10, 512u << 10, 1u << 20, 2u << 20, 4u << 20, 8u << 20, 12u << 20, 16u << 20, 24u << 20, 32u << 20, 48u << 20, 64u << 20, 128u << 20 };
    const size_t NS = sizeof SZ / sizeof SZ[0], SMAX = 128u << 20;
    g_evc = alloc_host_huge(EVICT_CPU); g_perm = malloc(sizeof(uint32_t) * (SMAX / LINE));
    Buf hb = { .name = "malloc", .cpu = alloc_host_huge(SMAX), .cap = SMAX };
    if (!g_evc || !g_perm || !hb.cpu) { printf("host memory refused\n"); return 1; }
    char nvpath[512] = { 0 };
    if (nvme) { snprintf(nvpath, sizeof nvpath, "%s", nvme); }

    Buf xh = { .name = "host" }, xd = { .name = "dev" }, yh = { .name = "host" }, yd = { .name = "dev" }, zh = { .name = "host" }, zd = { .name = "dev" };
    uint32_t *want = NULL, *delta = NULL;
    pthread_t bg;
    if (chip) {
        OmegaGpuMem *ms[9] = { &xh.mem, &xd.mem, &yh.mem, &yd.mem, &zh.mem, &zd.mem, &g_evg, &g_rbig, &g_out };
        const size_t mb[9] = { SMAX, SMAX, SMAX, SMAX, ZD + (32u << 20), ZD + (32u << 20), EVICT_GPU, RBIG, 8u << 20 };
        const int dev[9] = { 0, 1, 0, 1, 0, 1, 1, 1, 0 };
        int rc = 0;
        for (int k = 0; k < 9 && !rc; k++) rc = dev[k] ? omega_gpu_pf_alloc_dev(mb[k], ms[k]) : omega_gpu_pf_alloc_host(mb[k], ms[k]);
        if (rc) { printf("chip alloc refused: %s (%s)\nCPU_PISTON_SHARE: FAIL\n", omega_gpu_pf_rc_name(rc), omega_gpu_session_last_error()); return 1; }
        xh.cpu = xh.mem.cpu; xd.cpu = xd.mem.cpu; yh.cpu = yh.mem.cpu; yd.cpu = yd.mem.cpu; zh.cpu = zh.mem.cpu; zd.cpu = zd.mem.cpu;
        memset(g_evg.cpu, 7, EVICT_GPU); memset(g_rbig.cpu, 9, RBIG); omega_gpu_session_sync_to_gpu(&g_evg); omega_gpu_session_sync_to_gpu(&g_rbig);
        want = malloc(8u << 20); delta = malloc(8u << 20);
        if (pthread_create(&bg, NULL, bg_stream, NULL)) { printf("bg thread refused\n"); return 1; }
    }
    for (size_t si = 0; si < NS; si++) {
        const size_t S = SZ[si];
        if (only && S != only) continue;
        fill_chain(hb.cpu, S, 1000 + si);
        if (!chain_ok(hb.cpu, S)) { printf("chain broken at size %zu\n", S); g_fail++; continue; }
        if (nvme) { int e = nvme_store(nvpath, hb.cpu, S); if (e) { printf("nvme store refused (%d)\n", e); nvme = NULL; }
                    if (nvme) { if (g_nvfd >= 0) close(g_nvfd); g_nvfd = open(nvpath, O_RDONLY | O_DIRECT); if (g_nvfd < 0) { printf("O_DIRECT open refused (%d)\n", errno); nvme = NULL; } } }
        host_arms(&hb, S);
        if (!chip) continue;
        Buf *xs[2] = { &xh, &xd }, *ys[2] = { &yh, &yd };
        for (int k = 0; k < 2; k++) {
            memcpy(xs[k]->cpu, hb.cpu, S); memcpy(ys[k]->cpu, hb.cpu, S);
            omega_gpu_session_sync_to_gpu(&xs[k]->mem); omega_gpu_session_sync_to_gpu(&ys[k]->mem);
            want_of(xs[k]->cpu, S, 0, want);
            chip_arms(xs[k], ys[k], S, want, delta);
            if (S <= (64u << 20)) rarms(xs[k], ys[k], S);
        }
        if (S >= (2u << 20) && S <= (32u << 20) && (S & (S - 1)) == 0) {
            Buf *zs[2] = { &zh, &zd };
            for (int k = 0; k < 2; k++) {
                for (size_t o = 0; o < S + ZD; o += S) memcpy(zs[k]->cpu + o, hb.cpu, S); /* ZD is a multiple of S */
                omega_gpu_session_sync_to_gpu(&zs[k]->mem);
                want_of(zs[k]->cpu, S + ZD, 0, want);
                zarms(zs[k], S, want);
            }
        }
    }
    if (chip) {
        atomic_store(&g_stop, 2); pthread_join(bg, NULL);
        Buf *all[6] = { &xh, &xd, &yh, &yd, &zh, &zd }; for (int k = 0; k < 6; k++) omega_gpu_pf_free(&all[k]->mem);
        omega_gpu_pf_free(&g_evg); omega_gpu_pf_free(&g_rbig); omega_gpu_pf_free(&g_out);
    }
    if (nvme) unlink(nvpath);
    printf("sink %llu\nCPU_PISTON_SHARE: %s\n", (unsigned long long)g_sink, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
