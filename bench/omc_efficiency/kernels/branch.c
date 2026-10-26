/* branch.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/branch.om: same loop, same output. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
int main(void) {
    uint64_t t0 = nowns();
    uint64_t tot = 0, mx = 0;
    __asm__ volatile("" : "+r"(tot), "+r"(mx));
    for (uint64_t n = 0; n < 1000000u; n++) {
        uint64_t x = n + 1, k = 0;
        while (x != 1) {
            if ((x & 1) == 0) x = x >> 1; else x = x * 3 + 1;
            k++;
        }
        tot += k;
        if (k > mx) mx = k;
    }
    __asm__ volatile("" : : "r"(tot), "r"(mx) : "memory");
    putns(t0, nowns());
    putnum(tot); putnum(mx);
    return 0;
}
