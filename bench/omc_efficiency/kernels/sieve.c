/* sieve.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/sieve.om: same passes, same output. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
static uint8_t fl[2000001];
static __attribute__((noinline)) uint64_t pass(void) {
    for (uint64_t i = 0; i < 2000001u; i++) fl[i] = 0;
    for (uint64_t p = 2; p * p <= 2000000u; p++)
        if (fl[p] == 0)
            for (uint64_t m = p * p; m <= 2000000u; m += p) fl[m] = 1;
    uint64_t c = 0;
    for (uint64_t i = 0; i < 1999999u; i++) if (fl[i + 2] == 0) c++;
    return c;
}
int main(void) {
    uint64_t t0 = nowns();
    uint64_t sum = 0, last = 0;
    __asm__ volatile("" : "+r"(sum), "+r"(last) : : "memory");
    for (uint64_t r = 0; r < 50; r++) { last = pass(); sum += last; }
    __asm__ volatile("" : : "r"(last), "r"(sum) : "memory");
    putns(t0, nowns());
    putnum(last); putnum(sum);
    return 0;
}
