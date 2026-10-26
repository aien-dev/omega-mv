/* fib.c -- REFERENCE TWIN ONLY (gcc oracle) of kernels/fib.om: same recursion, same output. */
#include <stdio.h>
#include <stdint.h>
static void putnum(uint64_t v) { printf("%llu\n", (unsigned long long)v); }
static uint64_t fib(uint64_t n) {
    if (n < 2) return n;
    uint64_t a = fib(n - 1);
    uint64_t b = fib(n - 2);
    return a + b;
}
int main(void) { putnum(fib(38)); return 0; }
