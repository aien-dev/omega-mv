/* leafcall.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/leafcall.om: same calls, same output.
   noinline keeps the call a call, as in the Omega kernel. */
#include <stdio.h>
#include <stdint.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
static __attribute__((noinline)) uint64_t mix(uint64_t x, uint64_t k) { return (x * k) ^ (x >> 5); }
int main(void) {
    uint64_t s = 1, t = 0;
    for (uint64_t i = 0; i < 30000000u; i++) {
        s += mix(i, 2654435761u);
        t ^= s;
    }
    putnum(s); putnum(t);
    return 0;
}
