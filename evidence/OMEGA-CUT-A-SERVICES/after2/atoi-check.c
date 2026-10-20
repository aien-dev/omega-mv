#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int parse_int_like_atoi(const char *p) {
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int neg = *p == '-';
    if (*p == '-' || *p == '+') p++;
    const uint64_t lim = neg ? (uint64_t)1 << 63 : ((uint64_t)1 << 63) - 1u;
    uint64_t v = 0;
    int over = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        uint64_t d = (uint64_t)(*p - '0');
        if (over || v > (lim - d) / 10u) over = 1;
        else v = v * 10u + d;
    }
    if (over) v = lim;
    uint64_t bits = neg ? 0u - v : v; /* the two's complement long */
    return (int)(uint32_t)bits;      /* gcc: unsigned to int keeps the bits */
}
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
int main(void) {
    const char *fixed[] = { "", " ", "0", "7", "-0", "+5", "  \t\n\v\f\r12x", "-", "+", "--1", "+-1", "abc", "3 4",
        "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295", "4294967296",
        "9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
        "99999999999999999999999999", "-99999999999999999999999999", "000000000000000000000000000000012" };
    long n = 0, bad = 0; char buf[64];
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++, n++)
        if (parse_int_like_atoi(fixed[i]) != atoi(fixed[i])) { bad++; printf("FAIL '%s'\n", fixed[i]); }
    const char alpha[] = " \t-+0123456789x9";
    for (long k = 0; k < 20000000; k++, n++) {
        int len = (int)(rnd() % 30); for (int j = 0; j < len; j++) buf[j] = alpha[rnd() % 16]; buf[len] = 0;
        if (parse_int_like_atoi(buf) != atoi(buf)) { if (bad++ < 5) printf("FAIL '%s'\n", buf); }
    }
    printf("%ld strings compared with glibc atoi, %ld differ\n", n, bad); return bad != 0;
}
