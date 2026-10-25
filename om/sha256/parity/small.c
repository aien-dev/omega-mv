/* Short-input timing, C vs Omega SHA-256, interleaved in one process: for each length, 11 rounds of
 * (C batch, Omega batch), each batch 200,000 calls; prints median and min..max ns per call. */
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
void orc_sha256_hash(const uint8_t *, size_t, uint8_t *);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
int main(void)
{
    static uint8_t b[4096];
    uint8_t h[32];
    const int N = 200000, R = 11;
    size_t L[] = {0, 32, 55, 64, 100, 200, 1000};
    for (int j = 0; j < 7; j++) {
        double c[11], o[11];
        for (int r = 0; r < R; r++) {
            double t = now();
            for (int i = 0; i < N; i++) { orc_sha256_hash(b, L[j], h); b[0] ^= h[0]; }
            c[r] = (now() - t) / N * 1e9;
            t = now();
            for (int i = 0; i < N; i++) { sha256_hash(b, L[j], h); b[0] ^= h[0]; }
            o[r] = (now() - t) / N * 1e9;
        }
        qsort(c, R, sizeof c[0], cmp);
        qsort(o, R, sizeof o[0], cmp);
        printf("MEASURED len %4zu: omega median %.0f ns (%.0f..%.0f)  C median %.0f ns (%.0f..%.0f)  omega/C %.3f\n",
               L[j], o[5], o[0], o[10], c[5], c[0], c[10], o[5] / c[5]);
    }
    return 0;
}
