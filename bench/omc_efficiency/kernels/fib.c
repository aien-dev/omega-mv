/* fib.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/fib.om: same recursion, same output. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
static uint64_t fib(uint64_t n) {
    if (n < 2) return n;
    uint64_t a = fib(n - 1);
    uint64_t b = fib(n - 2);
    return a + b;
}
int main(void) {
    uint64_t t0 = nowns();
    uint64_t n = 38;
    __asm__ volatile("" : "+r"(n));
    uint64_t r = fib(n);
    __asm__ volatile("" : : "r"(r) : "memory");
    putns(t0, nowns());
    putnum(r);
    return 0;
}
