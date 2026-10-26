/* fib.c -- REFERENCE TWIN ONLY (gcc oracle) of seed/rung4/tests/fib.om. */
#include <stdio.h>
static unsigned long long fib(unsigned long long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { for (unsigned long long i = 0; i < 26; i++) printf("%llu\n", fib(i)); return 0; }
