/* arith.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/arith.om: same loop, same output. */
#include <stdio.h>
#include <stdint.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
int main(void) {
    uint64_t h = 0, s = 0;
    for (uint64_t i = 0; i < 40000000u; i++) {
        h = h * 31u + i;
        uint64_t t = (i * i) ^ (i >> 3);
        s += t % 1000003u;
    }
    putnum(h); putnum(s);
    return 0;
}
