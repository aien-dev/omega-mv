/* branch.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/branch.om: same loop, same output. */
#include <stdio.h>
#include <stdint.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
int main(void) {
    uint64_t tot = 0, mx = 0;
    for (uint64_t n = 0; n < 1000000u; n++) {
        uint64_t x = n + 1, k = 0;
        while (x != 1) {
            if ((x & 1) == 0) x = x >> 1; else x = x * 3 + 1;
            k++;
        }
        tot += k;
        if (k > mx) mx = k;
    }
    putnum(tot); putnum(mx);
    return 0;
}
