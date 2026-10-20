/* mc_cpu: CPU data-path and arithmetic ceilings of this box (L151 machine ceiling map, 2026-10-19).
 *
 *   mc_cpu bw  CPULIST BYTES_PER_THREAD OP ROUNDS   OP = read | write | copy
 *       One thread pinned to each CPU in CPULIST (e.g. 5-9,15-19). Each thread owns BYTES_PER_THREAD (copy: two
 *       buffers of that size) and streams it repeatedly with 128-bit SIMD loads/stores (write = STNP non-temporal
 *       pairs, copy = loads + STNP). Small sizes stay in L1/L2/L3, large ones go to DRAM. Every round all threads
 *       start together; GB/s = bytes moved by all threads / slowest thread. Prints every round and the best.
 *   mc_cpu ops CPULIST OP SECONDS                    OP = f32 | f16 | i8dot | i8mm | bf16mm
 *       Independent SIMD multiply-add chains (12 accumulators) on each pinned thread; prints Gop/s for all threads.
 *       Counted per instruction: f32 FMLA 4 lanes x 2 = 8, f16 FMLA 16, SDOT 16 MACs = 32, SMMLA 32 MACs = 64,
 *       BFMMLA 16 MACs = 32 (a multiply-add is 2 operations).
 * Checks: read/copy fold every loaded word into a sum that is compared with a scalar recount on round 0; copy compares
 * destination with source after the run; ops results are printed so the loop cannot be removed.
 * Output lines: "MC_CPU bw ..." / "MC_CPU ops ...", then "MC_CPU: PASS|FAIL". */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arm_neon.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXT 64
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
/* MC_START_AT=EPOCH: sleep until that wall-clock second, so legs of the concurrent run start together */
static void start_at(void) { const char *s = getenv("MC_START_AT"); if (!s) return; double t = atof(s); struct timespec r; clock_gettime(CLOCK_REALTIME, &r);
    double d = t - (r.tv_sec + r.tv_nsec * 1e-9); if (d > 0) { struct timespec w = { (time_t)d, (long)((d - (time_t)d) * 1e9) }; nanosleep(&w, NULL); } }
static double epoch(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static int parse_cpus(const char *s, int *out) {
    int n = 0; char *dup = strdup(s), *save = NULL;
    for (char *tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        int a, b; if (sscanf(tok, "%d-%d", &a, &b) == 2) { for (int c = a; c <= b && n < MAXT; c++) out[n++] = c; }
        else if (n < MAXT) out[n++] = atoi(tok);
    }
    free(dup); return n;
}

typedef struct {
    int cpu, op; size_t bytes; int reps; double secs;
    uint8_t *a, *b; uint64_t sum; double t; pthread_barrier_t *bar; int mode;
    float32x4_t f; double ops;
} job;

static void pin(int cpu) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); pthread_setaffinity_np(pthread_self(), sizeof s, &s); }

static uint64_t read_pass(const uint8_t *p, size_t n) {
    uint64x2_t s0 = vdupq_n_u64(0), s1 = s0, s2 = s0, s3 = s0, s4 = s0, s5 = s0, s6 = s0, s7 = s0;
    for (size_t i = 0; i < n; i += 128) {
        uint64x2x4_t x = vld1q_u64_x4((const uint64_t *)(p + i)), y = vld1q_u64_x4((const uint64_t *)(p + i + 64));
        s0 = vaddq_u64(s0, x.val[0]); s1 = vaddq_u64(s1, x.val[1]); s2 = vaddq_u64(s2, x.val[2]); s3 = vaddq_u64(s3, x.val[3]);
        s4 = vaddq_u64(s4, y.val[0]); s5 = vaddq_u64(s5, y.val[1]); s6 = vaddq_u64(s6, y.val[2]); s7 = vaddq_u64(s7, y.val[3]);
    }
    s0 = vaddq_u64(vaddq_u64(vaddq_u64(s0, s1), vaddq_u64(s2, s3)), vaddq_u64(vaddq_u64(s4, s5), vaddq_u64(s6, s7)));
    return vgetq_lane_u64(s0, 0) + vgetq_lane_u64(s0, 1);
}
static void write_pass(uint8_t *p, size_t n, uint64_t v) {
    uint64x2_t q = vdupq_n_u64(v);
    for (size_t i = 0; i < n; i += 128)
        __asm__ volatile("stnp %q1, %q1, [%0]\n stnp %q1, %q1, [%0, #32]\n stnp %q1, %q1, [%0, #64]\n stnp %q1, %q1, [%0, #96]"
                         :: "r"(p + i), "w"(q) : "memory");
}
static uint64_t copy_pass(uint8_t *d, const uint8_t *s, size_t n) {
    uint64x2_t acc = vdupq_n_u64(0);
    for (size_t i = 0; i < n; i += 64) {
        uint64x2x4_t x = vld1q_u64_x4((const uint64_t *)(s + i));
        acc = vaddq_u64(acc, x.val[0]);
        __asm__ volatile("stnp %q1, %q2, [%0]\n stnp %q3, %q4, [%0, #32]" :: "r"(d + i), "w"(x.val[0]), "w"(x.val[1]), "w"(x.val[2]), "w"(x.val[3]) : "memory");
    }
    return vgetq_lane_u64(acc, 0) + vgetq_lane_u64(acc, 1);
}

static void *bw_thread(void *v) {
    job *j = v; pin(j->cpu);
    pthread_barrier_wait(j->bar);
    double t0 = now(); uint64_t s = 0;
    for (int r = 0; r < j->reps; r++) {
        if (j->op == 0) s += read_pass(j->a, j->bytes);
        else if (j->op == 1) write_pass(j->a, j->bytes, (uint64_t)r + 1);
        else s += copy_pass(j->b, j->a, j->bytes);
    }
    j->t = now() - t0; j->sum = s; return NULL;
}

#define CHAINS(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11)
static void *ops_thread(void *v) {
    job *j = v; pin(j->cpu);
    pthread_barrier_wait(j->bar);
    double t0 = now(), per = 0; uint64_t iters = 0; const uint64_t blk = 1u << 16;
    float sink = 0;
    if (j->mode == 0) {
        float32x4_t b = vdupq_n_f32(1.0000001f), c = vdupq_n_f32(1e-7f);
#define D(i) float32x4_t a##i = vdupq_n_f32((float)(i));
        CHAINS(D)
        do { for (uint64_t k = 0; k < blk; k++) {
#define F(i) a##i = vfmaq_f32(a##i, b, c);
            CHAINS(F)
        } iters += blk; } while (now() - t0 < j->secs);
#define S(i) sink += vgetq_lane_f32(a##i, 0);
        CHAINS(S) per = 12.0 * 8;
    } else if (j->mode == 1) {
        float16x8_t b = vdupq_n_f16((float16_t)1.0f), c = vdupq_n_f16((float16_t)0.0009765625f);
#define D16(i) float16x8_t h##i = vdupq_n_f16((float16_t)(i));
        CHAINS(D16)
        do { for (uint64_t k = 0; k < blk; k++) {
#define F16(i) h##i = vfmaq_f16(h##i, b, c);
            CHAINS(F16)
        } iters += blk; } while (now() - t0 < j->secs);
#define S16(i) sink += (float)vgetq_lane_f16(h##i, 0);
        CHAINS(S16) per = 12.0 * 16;
    } else if (j->mode == 2 || j->mode == 3) {
        int8x16_t b = vdupq_n_s8(1), c = vdupq_n_s8(1);
#define DI(i) int32x4_t q##i = vdupq_n_s32(i);
        CHAINS(DI)
        if (j->mode == 2) {
            do { for (uint64_t k = 0; k < blk; k++) {
#define FI(i) q##i = vdotq_s32(q##i, b, c);
                CHAINS(FI)
            } iters += blk; } while (now() - t0 < j->secs);
            per = 12.0 * 32;
        } else {
            do { for (uint64_t k = 0; k < blk; k++) {
#define FM(i) q##i = vmmlaq_s32(q##i, b, c);
                CHAINS(FM)
            } iters += blk; } while (now() - t0 < j->secs);
            per = 12.0 * 64;
        }
#define SI(i) sink += (float)vgetq_lane_s32(q##i, 0);
        CHAINS(SI)
    } else {
        bfloat16x8_t b = vreinterpretq_bf16_u16(vdupq_n_u16(0x3f80)), c = vreinterpretq_bf16_u16(vdupq_n_u16(0x3380));
        CHAINS(D)
        do { for (uint64_t k = 0; k < blk; k++) {
#define FB(i) a##i = vbfmmlaq_f32(a##i, b, c);
            CHAINS(FB)
        } iters += blk; } while (now() - t0 < j->secs);
        CHAINS(S) per = 12.0 * 32;
    }
    j->t = now() - t0; j->ops = per * (double)iters; j->sum = (uint64_t)(sink != sink); j->f = vdupq_n_f32(sink);
    return NULL;
}

int main(int argc, char **argv) {
    int cpus[MAXT], fail = 0;
    if (argc < 5) { fprintf(stderr, "usage: mc_cpu bw CPULIST BYTES OP ROUNDS | mc_cpu ops CPULIST OP SECONDS\n"); return 2; }
    int nt = parse_cpus(argv[2], cpus);
    pthread_barrier_t bar; pthread_t th[MAXT]; static job jb[MAXT];
    if (!strcmp(argv[1], "bw")) {
        size_t bytes = strtoull(argv[3], NULL, 0) & ~(size_t)127; int rounds = atoi(argv[5]);
        int op = !strcmp(argv[4], "read") ? 0 : !strcmp(argv[4], "write") ? 1 : !strcmp(argv[4], "copy") ? 2 : -1;
        if (op < 0 || bytes < 4096 || rounds < 1) { fprintf(stderr, "bad args\n"); return 2; }
        /* aim each round at about 0.25 s of streaming per thread at 20 GB/s, at least one pass */
        int reps = (int)(5e9 / (double)bytes); if (reps < 1) reps = 1; if (reps > 1000000) reps = 1000000;
        for (int i = 0; i < nt; i++) {
            jb[i].cpu = cpus[i]; jb[i].op = op; jb[i].bytes = bytes; jb[i].reps = reps; jb[i].bar = &bar;
            if (posix_memalign((void **)&jb[i].a, 1 << 21, bytes) || (op == 2 && posix_memalign((void **)&jb[i].b, 1 << 21, bytes))) { fprintf(stderr, "alloc\n"); return 1; }
            for (size_t k = 0; k < bytes / 8; k++) ((uint64_t *)jb[i].a)[k] = k * 2654435761u + (uint64_t)i;
            if (op == 2) memset(jb[i].b, 0, bytes);
        }
        double best = 0; start_at(); printf("MC_CPU start=%.3f\n", epoch());
        for (int r = 0; r < rounds; r++) {
            pthread_barrier_init(&bar, NULL, (unsigned)nt);
            for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, bw_thread, &jb[i]);
            double tmax = 0;
            for (int i = 0; i < nt; i++) { pthread_join(th[i], NULL); if (jb[i].t > tmax) tmax = jb[i].t; }
            pthread_barrier_destroy(&bar);
            double moved = (double)bytes * reps * nt * (op == 2 ? 2 : 1), gbs = moved / tmax / 1e9;
            if (gbs > best) best = gbs;
            if (r == 0 && op != 1) for (int i = 0; i < nt; i++) {
                uint64_t ref = 0; const uint64_t *w = (const uint64_t *)jb[i].a;
                if (op == 0) { for (size_t k = 0; k < bytes / 8; k++) ref += w[k]; ref *= (uint64_t)reps;
                    if (jb[i].sum != ref) { printf("FAIL: thread %d sum mismatch\n", i); fail++; } }
                if (op == 2 && memcmp(jb[i].a, jb[i].b, bytes)) { printf("FAIL: thread %d copy mismatch\n", i); fail++; }
            }
            printf("MC_CPU bw op=%s cpus=%s threads=%d bytes_per_thread=%zu round=%d secs=%.4f GBps=%.2f t_end=%.3f\n", argv[4], argv[2], nt, bytes, r, tmax, gbs, epoch());
        }
        printf("MC_CPU bw best op=%s cpus=%s threads=%d bytes_per_thread=%zu GBps=%.2f\n", argv[4], argv[2], nt, bytes, best);
    } else if (!strcmp(argv[1], "ops")) {
        const char *ops[] = { "f32", "f16", "i8dot", "i8mm", "bf16mm" }; int mode = -1;
        for (int m = 0; m < 5; m++) if (!strcmp(argv[3], ops[m])) mode = m;
        double secs = atof(argv[4]);
        if (mode < 0 || secs <= 0) { fprintf(stderr, "bad args\n"); return 2; }
        pthread_barrier_init(&bar, NULL, (unsigned)nt);
        for (int i = 0; i < nt; i++) { jb[i].cpu = cpus[i]; jb[i].mode = mode; jb[i].secs = secs; jb[i].bar = &bar; pthread_create(&th[i], NULL, ops_thread, &jb[i]); }
        double total = 0, tmax = 0;
        for (int i = 0; i < nt; i++) { pthread_join(th[i], NULL); total += jb[i].ops; if (jb[i].t > tmax) tmax = jb[i].t; if (jb[i].sum) { printf("FAIL: NaN result\n"); fail++; } }
        printf("MC_CPU ops op=%s cpus=%s threads=%d secs=%.3f Gops=%.2f per_thread_Gops=%.2f sink=%g\n", argv[3], argv[2], nt, tmax, total / tmax / 1e9,
               total / tmax / 1e9 / nt, (double)vgetq_lane_f32(jb[0].f, 0));
    } else { fprintf(stderr, "unknown mode\n"); return 2; }
    printf("MC_CPU: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
