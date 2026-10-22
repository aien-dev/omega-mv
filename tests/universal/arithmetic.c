/* Independent C17 development oracle. Not an Omega runtime. */
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
int main(void) {
    uint64_t a, b;
    while (scanf("%" SCNu64 " %" SCNu64, &a, &b) == 2) {
        if (!b) return 2;
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", a + b, a - b, a * b, a / b);
    }
    return ferror(stdin) ? 3 : 0;
}
