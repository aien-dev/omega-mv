/* sieve.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/sieve.om: same passes, same output. */
#include <stdio.h>
#include <stdint.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
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
    uint64_t sum = 0, last = 0;
    for (uint64_t r = 0; r < 50; r++) { last = pass(); sum += last; }
    putnum(last); putnum(sum);
    return 0;
}
