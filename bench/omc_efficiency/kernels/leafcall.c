/* leafcall.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/leafcall.om: same calls, same output.
   noinline keeps the call a call, as in the Omega kernel. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
static __attribute__((noinline)) uint64_t mix(uint64_t x, uint64_t k) { return (x * k) ^ (x >> 5); }
int main(void) {
    uint64_t t0 = nowns();
    uint64_t s = 1, t = 0;
    __asm__ volatile("" : "+r"(s), "+r"(t));
    for (uint64_t i = 0; i < 30000000u; i++) {
        s += mix(i, 2654435761u);
        t ^= s;
    }
    __asm__ volatile("" : : "r"(s), "r"(t) : "memory");
    putns(t0, nowns());
    putnum(s); putnum(t);
    return 0;
}
