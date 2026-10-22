/* bytecopy.c -- REFERENCE TWIN ONLY (gcc oracle) of seed/rung4/tests/bytecopy.om: stdin reversed. */
#include <stdio.h>
#include <unistd.h>
static unsigned char s[65536], d[65536];
int main(void) { size_t t = 0; ssize_t k;
    while (t < sizeof s && (k = read(0, s + t, sizeof s - t)) > 0) t += (size_t)k;
    for (size_t i = 0; i < t; i++) d[t - i - 1] = s[i];
    fwrite(d, 1, t, stdout); return 0; }
