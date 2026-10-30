/* SHA-256 through the generated C API (src/sha256.h -> src/omega_api_decl.h), one source built twice by
 * mk/omega-gpu-engine.mk: against the hosted binding src/omega_om_units.c and against the freestanding
 * binding src/omega_om_units_fs.c compiled with GPU_FS_CFLAGS. Both print the same lines; the make
 * target compares them byte for byte and checks the FIPS 180-2 digests.
 *   fips <i> <digest>   empty, "abc", the 448-bit message, one million 'a' (in uneven update calls)
 *   len <n> <digest>    n = 0..300 bytes from a fixed-seed xorshift64 stream, one-shot; the same bytes
 *                       through init/update/final split at a seeded point must give the same digest
 *                       (else "split-mismatch <n>" and exit 1). */
#include "sha256.h"
#include <stdio.h>
#include <string.h>

static void put(const char *tag, unsigned i, const uint8_t d[SHA256_DIGEST_SIZE])
{
    printf("%s %u ", tag, i);
    for (int k = 0; k < SHA256_DIGEST_SIZE; k++) printf("%02x", d[k]);
    printf("\n");
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t next(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

int main(void)
{
    static const char *const fips[] = { "", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" };
    static uint8_t buf[1000000];
    uint8_t d[SHA256_DIGEST_SIZE], e[SHA256_DIGEST_SIZE];
    sha256_ctx c;
    unsigned i;
    for (i = 0; i < 3; i++) { sha256_hash((const uint8_t *)fips[i], strlen(fips[i]), d); put("fips", i, d); }
    memset(buf, 'a', sizeof buf);
    sha256_init(&c);
    for (size_t off = 0, step = 1; off < sizeof buf; off += step, step = step * 3 + 7) {
        size_t n = sizeof buf - off < step ? sizeof buf - off : step;
        sha256_update(&c, buf + off, n);
    }
    sha256_final(&c, d);
    put("fips", 3, d);
    for (unsigned n = 0; n <= 300; n++) {
        for (unsigned k = 0; k < n; k++) buf[k] = (uint8_t)next();
        sha256_hash(buf, n, d);
        const unsigned cut = n ? (unsigned)(next() % (n + 1)) : 0;
        sha256_init(&c);
        sha256_update(&c, buf, cut);
        sha256_update(&c, buf + cut, n - cut);
        sha256_final(&c, e);
        if (memcmp(d, e, sizeof d)) { printf("split-mismatch %u\n", n); return 1; }
        put("len", n, d);
    }
    return 0;
}
