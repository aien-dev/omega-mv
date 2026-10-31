/* matmul.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/matmul.om: same matrices, same output. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
/* OMC-EFF-2: the computation is timed in process with CLOCK_MONOTONIC and the nanoseconds go to stderr.
   The empty asm statements are compiler barriers: they keep the timed work between the two clock reads. */
static uint64_t nowns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void putns(uint64_t t0, uint64_t t1) { fprintf(stderr, "%llu\n", (unsigned long long)(t1 - t0)); }
static uint64_t ma[90000], mb[90000], mc[90000];
int main(void) {
    uint64_t t0 = nowns();
    __asm__ volatile("" : : : "memory");
    for (uint64_t i = 0; i < 300; i++)
        for (uint64_t j = 0; j < 300; j++) {
            ma[i * 300 + j] = (i + j * 2) % 13;
            mb[i * 300 + j] = (i * 3 + j) % 11;
        }
    for (uint64_t i = 0; i < 300; i++) {
        uint64_t r = i * 300;
        for (uint64_t j = 0; j < 300; j++) {
            uint64_t s = 0, q = j;
            for (uint64_t k = 0; k < 300; k++) { s += ma[r + k] * mb[q]; q += 300; }
            mc[r + j] = s;
        }
    }
    uint64_t tot = 0, tr = 0;
    for (uint64_t i = 0; i < 300; i++) {
        for (uint64_t j = 0; j < 300; j++) tot += mc[i * 300 + j];
        tr += mc[i * 301];
    }
    __asm__ volatile("" : : "r"(tot), "r"(tr) : "memory");
    putns(t0, nowns());
    putnum(tot); putnum(tr);
    return 0;
}
