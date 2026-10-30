/* sort.c -- REFERENCE TWIN ONLY (gcc oracle) of seed/rung4/tests/sort.om: same generator, same sort. */
#include <stdio.h>
#include <stdint.h>
static uint64_t a[1000];
int main(void) {
    uint64_t x = 12345, s = 0, bad = 0;
    for (int i = 0; i < 1000; i++) { x = (x * 1103515245u + 12345u) % 2147483648u; a[i] = x % 100000; }
    for (int j = 0; j < 1000; j++) { uint64_t v = a[j]; int k = j;
        while (k > 0 && a[k-1] > v) { a[k] = a[k-1]; k--; } a[k] = v; }
    for (int i = 0; i < 1000; i++) s += a[i] * (uint64_t)(i + 1);
    for (int i = 0; i < 999; i++) if (a[i] > a[i+1]) bad++;
    printf("%llu\n%llu\n%llu\n%llu\n", (unsigned long long)s, (unsigned long long)bad,
           (unsigned long long)a[0], (unsigned long long)a[999]);
    return 0;
}
