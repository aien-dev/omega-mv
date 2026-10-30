/* TEMPORARY oracle-side harness (L838), outside the delivered closure: calls the S4-compiled
 * om/sha256/sha256-om2.om through the bridge-generated omega_api.h (ABI v2) and checks it against
 * the historical C (src/sha256.c @ a647b6a9, symbols renamed orc_*).
 *   t vec       stdin -> hex digest of sha256_hash; exit 4 if init/update(random splits)/final differ
 *   t receipts  section 7 / erratum E3 receipts and C parity; one line per check, exit 1 on any FAIL
 *   t big       one sha256_update of 2^32 + 4097 bytes, and sha256_hash of the same, against the C */
#include "omega_api.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
typedef struct { unsigned char b[104]; } orc_ctx;
void orc_sha256_init(orc_ctx *);
void orc_sha256_update(orc_ctx *, const uint8_t *, size_t);
void orc_sha256_final(orc_ctx *, uint8_t *);
void orc_sha256_hash(const uint8_t *, size_t, uint8_t *);
static uint64_t rs = 838;
static uint64_t rnd(void) { rs += 0x9E3779B97F4A7C15ull; uint64_t z = rs; z = (z ^ z >> 30) * 0xBF58476D1CE4E5B9ull; z = (z ^ z >> 27) * 0x94D049BB133111EBull; return z ^ z >> 31; }
static int fails;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }
static void split(const uint8_t *m, size_t n, uint8_t *out)
{
    static const size_t ch[] = {0, 1, 3, 55, 56, 63, 64, 65, 127, 128, 200, 4096};
    sha256_ctx c; sha256_init(&c);
    for (size_t p = 0; p < n;) { size_t k = ch[rnd() % 12]; if (k > n - p) k = n - p; sha256_update(&c, m + p, k); p += k; }
    sha256_final(&c, out);
}
int main(int argc, char **argv)
{
    uint8_t a[32], b[32], c[32];
    if (argc > 1 && !strcmp(argv[1], "vec")) {
        static uint8_t m[1 << 21]; size_t n = fread(m, 1, sizeof m, stdin);
        sha256_hash(m, n, a); split(m, n, b);
        for (int i = 0; i < 32; i++) printf("%02x", a[i]);
        printf("\n");
        return memcmp(a, b, 32) ? 4 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "big")) {
        size_t n = (1ull << 32) + 4097;
        uint8_t *m = mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { puts("NOT_RUN mmap"); return 3; }
        for (size_t i = 0; i < n; i++) m[i] = (uint8_t)(i % 251);
        sha256_ctx x; sha256_init(&x); sha256_update(&x, m, 3); sha256_update(&x, m + 3, n - 3); sha256_final(&x, a);
        sha256_hash(m, n, b); orc_sha256_hash(m, n, c);
        check(!memcmp(a, c, 32), "update of 2^32+4094 bytes (after 3) equals C");
        check(!memcmp(b, c, 32), "hash of 2^32+4097 bytes equals C");
        return fails != 0;
    }
    static uint8_t m[20000], d[64];
    for (size_t i = 0; i < sizeof m; i++) m[i] = (uint8_t)rnd();
    int bad = 0;
    for (size_t n = 0; n <= 9000; n++) {
        orc_sha256_hash(m, n, c); sha256_hash(m, n, a); split(m, n, b);
        if (memcmp(a, c, 32) || memcmp(b, c, 32)) bad++;
    }
    check(!bad, "lengths 0..9000: hash and split update equal the C");
    bad = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = rnd() % sizeof m; orc_sha256_hash(m, n, c); sha256_hash(m, n, a); split(m, n, b);
        if (memcmp(a, c, 32) || memcmp(b, c, 32)) bad++;
    }
    check(!bad, "2000 random lengths < 20000: hash and split update equal the C");
    /* E3 (2): hash(d, 32, d), iterated as tests/runtime/rx_sem_incremental.c:183 */
    memcpy(d, m, 32); memcpy(c, m, 32);
    for (int i = 0; i < 100; i++) { sha256_hash(d, 32, d); orc_sha256_hash(c, 32, c); }
    check(!memcmp(d, c, 32), "hash(d,32,d) x100 equals the C");
    /* src/omega_program.c:777 shape: digest inside the hashed range at offset 0 */
    memcpy(d, m, 64); orc_sha256_hash(d, 64, c); sha256_hash(d, 64, d);
    check(!memcmp(d, c, 32), "hash(d,64,d) (digest overlaps the first 32 bytes) equals the C");
    /* E3 (1): byte copy of a live context after k updates, both continued */
    bad = 0;
    for (size_t k = 0; k < 300; k++) {
        sha256_ctx x, y; sha256_init(&x);
        for (size_t i = 0; i < k; i++) sha256_update(&x, m + i * 7, 7);
        memcpy(&y, &x, sizeof x);
        sha256_update(&x, m + k * 7, 1000); sha256_update(&y, m + k * 7, 1000);
        sha256_final(&x, a); sha256_final(&y, b); orc_sha256_hash(m, k * 7 + 1000, c);
        if (memcmp(a, c, 32) || memcmp(b, c, 32)) bad++;
    }
    check(!bad, "copy of a live context after k = 0..299 updates of 7 bytes, both continued, equals the C");
    /* section 7: init/update/final after a final equals a fresh context */
    { sha256_ctx x; sha256_init(&x); sha256_update(&x, m, 777); sha256_final(&x, a);
      sha256_init(&x); sha256_update(&x, m + 5, 1234); sha256_final(&x, a);
      orc_sha256_hash(m + 5, 1234, c); check(!memcmp(a, c, 32), "init/update/final after a final equals a fresh context"); }
    /* section 7: hash(n) equals init/update(n)/final; empty updates */
    bad = 0;
    for (size_t n = 0; n < 300; n++) {
        sha256_ctx x; sha256_init(&x); sha256_update(&x, NULL, 0); sha256_update(&x, m, n); sha256_update(&x, m + n, 0);
        sha256_final(&x, a); sha256_hash(m, n, b); if (memcmp(a, b, 32)) bad++;
    }
    check(!bad, "hash(n) equals init/update(0, NULL)/update(n)/update(0)/final, n = 0..299");
    /* identity */
    const char *id = omega2_ident_sha256_om2();
    printf("ident %s\n", id);
    check(strstr(id, "profile=OM-2") && strstr(id, "unit=sha256_om2"), "omega2_ident_sha256_om2 names the unit and profile");
    return fails != 0;
}
