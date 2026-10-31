/* isort.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/isort.om: same generator, same sort. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
static uint64_t a[20000];
int main(void) {
    uint64_t t0 = nowns();
    uint64_t x = 12345, s = 0, bad = 0;
    __asm__ volatile("" : "+r"(x), "+r"(s), "+r"(bad) : : "memory");
    for (uint64_t i = 0; i < 20000u; i++) { x = (x * 1103515245u + 12345u) % 2147483648u; a[i] = x % 100000u; }
    for (uint64_t j = 0; j < 20000u; j++) {
        uint64_t v = a[j], k = j;
        int moving = 1;
        while (moving && k > 0) {
            uint64_t p = a[k - 1];
            if (p > v) { a[k] = p; k--; } else moving = 0;
        }
        a[k] = v;
    }
    for (uint64_t i = 0; i < 20000u; i++) s += a[i] * (i + 1);
    for (uint64_t i = 0; i < 19999u; i++) if (a[i] > a[i + 1]) bad++;
    __asm__ volatile("" : : "r"(s), "r"(bad) : "memory");
    putns(t0, nowns());
    putnum(s); putnum(bad); putnum(a[0]); putnum(a[19999]);
    return 0;
}
