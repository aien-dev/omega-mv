/* arith.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/arith.om: same loop, same output. */
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
    uint64_t h = 0, s = 0;
    __asm__ volatile("" : "+r"(h), "+r"(s));
    for (uint64_t i = 0; i < 40000000u; i++) {
        h = h * 31u + i;
        uint64_t t = (i * i) ^ (i >> 3);
        s += t % 1000003u;
    }
    __asm__ volatile("" : : "r"(h), "r"(s) : "memory");
    putns(t0, nowns());
    putnum(h); putnum(s);
    return 0;
}
