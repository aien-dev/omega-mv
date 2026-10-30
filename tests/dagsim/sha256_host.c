/* Host link for dagsim when the seeded SHA-256 unit cannot be compiled.
 * src/omega_om_units.c is AArch64 machine code. rx_costmodel.o references
 * sha256_hash from serialize/digest only; the analytical dagsim tests do not
 * call it. On AArch64 the Makefile links src/omega_om_units.c instead and
 * this file is not built. */
#if !defined(__aarch64__)
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

void sha256_hash(const uint8_t *d, size_t n, uint8_t out[32]) {
    (void)d; (void)n; (void)out;
    fprintf(stderr, "sha256_hash: not linked (seeded unit is AArch64); dagsim --model is NOT_RUN on this host\n");
    abort();
}
#endif
