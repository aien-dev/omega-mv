/* mc_disk: NVMe sequential read and write ceilings through the file system with the page cache bypassed (L151, 2026-10-19).
 *
 *   mc_disk write FILE GB THREADS BLOCK_KB   create/overwrite FILE with GB gigabytes (O_DIRECT, each thread its own slice)
 *   mc_disk read  FILE THREADS BLOCK_KB [SECONDS]   read FILE (O_DIRECT) slice per thread; with SECONDS, loop until then
 * Every block carries its offset in its first 8 bytes and a pattern after it; read checks both, so a short or wrong read
 * fails. Output: "MC_DISK op=... start=EPOCH end=EPOCH GB=... GBps=..." then "MC_DISK: PASS|FAIL". fsync before the
 * write clock stops, so the write number is bytes on the drive, not in a queue. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int fd, wr; size_t blk; off_t lo, hi; double until; uint64_t bytes, bad; } job;
static double now(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
/* MC_START_AT=EPOCH: wait until that wall-clock second (concurrent legs start together) */
static void start_at(void) { const char *s = getenv("MC_START_AT"); if (!s) return; double d = atof(s) - now(); if (d > 0) usleep((useconds_t)(d * 1e6)); }
static void fill(uint8_t *b, size_t n, off_t off) { for (size_t i = 0; i < n; i += 8) { uint64_t v = (uint64_t)off + i; memcpy(b + i, &v, 8); } }

static void *run(void *v) {
    job *j = v; uint8_t *b;
    if (posix_memalign((void **)&b, 4096, j->blk)) { j->bad++; return NULL; }
    do {
        for (off_t o = j->lo; o < j->hi; o += (off_t)j->blk) {
            if (j->wr) { fill(b, j->blk, o); if (pwrite(j->fd, b, j->blk, o) != (ssize_t)j->blk) { j->bad++; goto out; } }
            else {
                if (pread(j->fd, b, j->blk, o) != (ssize_t)j->blk) { j->bad++; goto out; }
                uint64_t f, l; memcpy(&f, b, 8); memcpy(&l, b + j->blk - 8, 8);
                if (f != (uint64_t)o || l != (uint64_t)o + j->blk - 8) j->bad++;
            }
            j->bytes += j->blk;
            if (j->until > 0 && now() >= j->until) goto out;
        }
    } while (j->until > 0 && now() < j->until);
out:
    free(b); return NULL;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: mc_disk write FILE GB THREADS BLOCK_KB | mc_disk read FILE THREADS BLOCK_KB [SECONDS]\n"); return 2; }
    int wr = !strcmp(argv[1], "write"); const char *path = argv[2];
    size_t gb = wr ? strtoull(argv[3], NULL, 0) : 0; int nt = atoi(argv[wr ? 4 : 3]); size_t blk = strtoull(argv[wr ? 5 : 4], NULL, 0) * 1024u;
    double secs = (!wr && argc > 5) ? atof(argv[5]) : 0;
    if (nt < 1 || nt > 64 || blk < 4096 || blk % 4096) { fprintf(stderr, "bad args\n"); return 2; }
    int fd = open(path, (wr ? O_WRONLY | O_CREAT : O_RDONLY) | O_DIRECT, 0600);
    if (fd < 0) { perror("open"); return 1; }
    off_t total = wr ? (off_t)(gb << 30) : lseek(fd, 0, SEEK_END);
    off_t slice = total / nt / (off_t)blk * (off_t)blk;
    if (slice <= 0) { fprintf(stderr, "file too small\n"); return 1; }
    job jb[64]; pthread_t th[64];
    start_at();
    double t0 = now();
    for (int i = 0; i < nt; i++) { jb[i] = (job){ .fd = fd, .wr = wr, .blk = blk, .lo = slice * i, .hi = slice * (i + 1), .until = secs > 0 ? t0 + secs : 0 }; pthread_create(&th[i], NULL, run, &jb[i]); }
    uint64_t bytes = 0, bad = 0;
    for (int i = 0; i < nt; i++) { pthread_join(th[i], NULL); bytes += jb[i].bytes; bad += jb[i].bad; }
    if (wr && fsync(fd) != 0) bad++;
    double t1 = now(); close(fd);
    printf("MC_DISK op=%s file=%s threads=%d block_KB=%zu start=%.3f end=%.3f GB=%.2f GBps=%.3f bad=%llu\n", argv[1], path, nt, blk / 1024, t0, t1, bytes / 1e9,
           bytes / (t1 - t0) / 1e9, (unsigned long long)bad);
    printf("MC_DISK: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
