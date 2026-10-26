/* Throwaway measurement driver (not delivered code). Links one arm's libomega_gpu.a and times
 * sha256_hash / sha256_init+update+final per size. One process per arm per round. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sha256.h"

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec*1e9 + (double)t.tv_nsec; }
static int cmp(const void*a,const void*b){ double x=*(const double*)a, y=*(const double*)b; return x<y?-1:(x>y); }

static const size_t sizes[] = {0,1,31,32,55,56,63,64,65,127,128,1024,4096,65536,1048576,16777216};
static const size_t chunk[] = {1,7,64,4096};

int main(int argc, char** argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 31;
    static uint8_t *big = NULL;
    size_t bigcap = 1u<<24;
    big = malloc(bigcap);
    for (size_t i = 0; i < bigcap; i++) big[i] = (uint8_t)(i*31u + 7u);
    uint8_t d[SHA256_DIGEST_SIZE];
    int bad = 0;
    for (unsigned s = 0; s < sizeof sizes/sizeof *sizes; s++) {
        size_t n = sizes[s];
        for (int w = 0; w < 3; w++) (void)sha256_hash(big, n, d);
        double *v = malloc((size_t)reps * sizeof(double));
        for (int r = 0; r < reps; r++) { double t0 = now_ns(); (void)sha256_hash(big, n, d); v[r] = now_ns() - t0; }
        qsort(v, (size_t)reps, sizeof(double), cmp);
        printf("hash %zu %.1f %.1f %.1f %.1f %.1f %02x\n", n, v[0], v[reps/2], v[(reps*9)/10], v[reps-1],
               v[reps/2]/ (n ? (double)n : 1.0), d[0]);
        free(v);
    }
    for (unsigned c = 0; c < sizeof chunk/sizeof *chunk; c++) {
        size_t cs = chunk[c], total = 1u<<20;
        sha256_ctx ctx;
        for (int w = 0; w < 3; w++) { sha256_init(&ctx); for (size_t o = 0; o < total; o += cs) sha256_update(&ctx, big+o, cs); sha256_final(&ctx, d); }
        double *v = malloc((size_t)reps * sizeof(double));
        for (int r = 0; r < reps; r++) {
            double t0 = now_ns();
            sha256_init(&ctx);
            for (size_t o = 0; o < total; o += cs) sha256_update(&ctx, big+o, cs);
            sha256_final(&ctx, d);
            v[r] = now_ns() - t0;
        }
        qsort(v, (size_t)reps, sizeof(double), cmp);
        printf("stream %zu %zu %.1f %.1f %.1f %.1f %.1f %02x\n", cs, total, v[0], v[reps/2], v[(reps*9)/10], v[reps-1],
               (total/1048576.0)/(v[reps/2]/1e9), d[0]);
        free(v);
    }
    return bad;
}
