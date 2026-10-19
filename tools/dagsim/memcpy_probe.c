/*
 * memcpy_probe -- held-out measurement for the shared-bandwidth model
 * (omega-mv#583). CPU only, no GPU. Copies GB gigabytes once per rep using
 * THREADS threads, each on its own contiguous slice, and prints the same CSV
 * shape as M1 block4 (omega-mv#577) so the two can be compared directly.
 *
 *   memcpy_probe GB THREADS REPS
 *
 * Header lines carry load average and MemAvailable before and after, so a
 * loaded-box number is never quoted as a quiet-box number.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { char *dst; const char *src; size_t n; } slice;
static void *worker(void *p) { slice *s = p; memcpy(s->dst, s->src, s->n); return NULL; }

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static void sysline(const char *tag) {
    char load[128] = "?", mem[64] = "?";
    FILE *f = fopen("/proc/loadavg", "r"); if (f) { if (fgets(load, sizeof load, f)) load[strcspn(load, "\n")] = 0; fclose(f); }
    f = fopen("/proc/meminfo", "r");
    if (f) { char l[256]; while (fgets(l, sizeof l, f)) if (!strncmp(l, "MemAvailable:", 13)) { snprintf(mem, sizeof mem, "%s", l + 13); mem[strcspn(mem, "\n")] = 0; break; } fclose(f); }
    time_t t = time(NULL); char ts[64]; strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
    printf("# %s %s loadavg=%s MemAvailable=%s\n", tag, ts, load, mem);
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: memcpy_probe GB THREADS REPS\n"); return 2; }
    double gb = atof(argv[1]); int k = atoi(argv[2]), reps = atoi(argv[3]);
    if (gb <= 0 || k < 1 || k > 64 || reps < 1) return 2;
    size_t n = (size_t)(gb * 1e9);
    char *src = malloc(n), *dst = malloc(n);
    if (!src || !dst) { fprintf(stderr, "alloc failed\n"); return 3; }
    memset(src, 1, n); memset(dst, 2, n);   /* touch every page before timing */
    printf("# omega-mv#583 memcpy_probe; cmd=memcpy_probe %s %s %s; bytes_copied=%zu (read+write counted twice below, as in M1 block4)\n", argv[1], argv[2], argv[3], n);
    sysline("before");
    printf("gb,threads,rep,seconds,GBps_read_plus_write\n");
    for (int r = 0; r < reps; r++) {
        pthread_t th[64]; slice sl[64];
        size_t per = n / (size_t)k;
        double t0 = now();
        for (int i = 0; i < k; i++) { sl[i].dst = dst + (size_t)i * per; sl[i].src = src + (size_t)i * per; sl[i].n = (i == k - 1) ? n - (size_t)i * per : per; pthread_create(&th[i], NULL, worker, &sl[i]); }
        for (int i = 0; i < k; i++) pthread_join(th[i], NULL);
        double dt = now() - t0;
        printf("%g,%d,%d,%.4f,%.1f\n", gb, k, r, dt, 2.0 * n / dt / 1e9);
        fflush(stdout);
    }
    sysline("after");
    free(src); free(dst);
    return 0;
}
