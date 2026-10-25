/* SPDX-License-Identifier: Apache-2.0
 * ORACLE (outside the delivered closure). OM-2 qualification, L836 (#748), task phaseB/sha-f5d14cb2.
 * Independent C driver for the OM-2 SHA unit om/sha256/sha256-om2.om bound through the omega-bridge
 * ABI v2 generator. It never computes SHA-256 itself: every expected digest comes from
 * /usr/bin/sha256sum (child process fed through a pipe), from the Phase A fixture file (external
 * oracles), or is a self-consistency relation (copy == original, hash == stream).
 * Calls go through the raw omega2_* symbols so a nonzero status is observed (TRAP line), never an
 * abort; the bridge-generated omega_api.h supplies sha256_ctx only.
 *
 * Build (see PROVENANCE.txt):
 *   gcc -std=gnu11 -O2 -Wall -Wextra -Werror -I<bind> sha_qual.c -L<bind> -lomega_native \
 *       -Wl,-rpath,<bind> -o sha_qual
 * Modes (each prints PASS/FAIL/TRAP/NOT_RUN lines, then a TOTAL line; exit 0 iff no FAIL/TRAP):
 *   vec FILE ROOT        fixtures/sha256-vectors.txt rows, three call paths each
 *   rcpt                 section 7 / E3 receipts, empty updates, refusals
 *   ident EXPECTED       omega2_ident_sha256_om2() against the harness-computed string
 *   rand SEED COUNT      seeded random messages, random chunkings, vs sha256sum; prints RAND lines
 *   part                 messages around the 2^25-byte run boundaries of blocks()
 *   big                  >= 2^32-byte single update, hash, > 2^32 total via many updates
 *   bigzero              2^32+3 zero bytes from a NORESERVE mapping (no resident memory)
 * Line format of a failure: FAIL <id> first-diff-byte=<i> got=<hex> want=<hex> [detail] */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "omega_api.h"

static long npass, nfail, ntrap;
static uint32_t sq; /* sequenced status accumulator: (sq = a, sq |= b, ..., sq) */
static const char *SHA256SUM = "/usr/bin/sha256sum";

static void hex(const uint8_t *d, char *h)
{
    static const char x[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        h[2 * i] = x[d[i] >> 4];
        h[2 * i + 1] = x[d[i] & 15];
    }
    h[64] = 0;
}

static int unhex(const char *h, uint8_t *d)
{
    for (int i = 0; i < 32; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1)
            return -1;
        d[i] = (uint8_t)v;
    }
    return 0;
}

static void pass(const char *id, const char *what)
{
    npass++;
    printf("PASS %s %s\n", id, what);
}

/* compare got against want (both binary); report the first differing byte */
static int cmpd(const char *id, const uint8_t *got, const uint8_t *want, const char *what)
{
    char hg[65], hw[65];
    for (int i = 0; i < 32; i++)
        if (got[i] != want[i]) {
            hex(got, hg);
            hex(want, hw);
            nfail++;
            printf("FAIL %s first-diff-byte=%d got=%s want=%s %s\n", id, i, hg, hw, what);
            return 0;
        }
    return 1;
}

static int trapped(const char *id, uint32_t s, const char *what)
{
    if (s == 0)
        return 0;
    ntrap++;
    printf("TRAP %s status=%u %s\n", id, s, what);
    return 1;
}

/* raw calls; return status */
static uint32_t I(sha256_ctx *c)
{
    uint64_t r;
    return omega2_sha256_init(&r, (uint8_t *)c);
}
static uint32_t U(sha256_ctx *c, const void *p, uint64_t n)
{
    uint64_t r;
    return omega2_sha256_update(&r, (uint8_t *)c, p, n);
}
static uint32_t F(sha256_ctx *c, uint8_t *o)
{
    uint64_t r;
    return omega2_sha256_final(&r, (uint8_t *)c, o);
}
static uint32_t H(const void *p, uint64_t n, uint8_t *o)
{
    uint64_t r;
    return omega2_sha256_hash(&r, p, n, o);
}

/* /usr/bin/sha256sum of n bytes at p, via a pipe; exits 3 if the oracle cannot run */
static void ref(const void *p, size_t n, uint8_t *d)
{
    int in[2], out[2];
    if (pipe(in) || pipe(out)) {
        perror("pipe");
        exit(3);
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(3);
    }
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        close(in[0]);
        close(in[1]);
        close(out[0]);
        close(out[1]);
        execl(SHA256SUM, SHA256SUM, "-b", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    const uint8_t *q = p;
    while (n > 0) {
        size_t k = n > (1u << 24) ? (1u << 24) : n;
        ssize_t w = write(in[1], q, k);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("write sha256sum");
            exit(3);
        }
        q += w;
        n -= (size_t)w;
    }
    close(in[1]);
    char buf[200];
    size_t got = 0;
    ssize_t r;
    while ((r = read(out[0], buf + got, sizeof buf - 1 - got)) > 0)
        got += (size_t)r;
    close(out[0]);
    int st;
    waitpid(pid, &st, 0);
    buf[got] = 0;
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0 || got < 64 || unhex(buf, d)) {
        fprintf(stderr, "sha256sum oracle failed\n");
        exit(3);
    }
}

/* splitmix64, the generator of fixtures/PROVENANCE.txt */
static uint64_t sm_s;
static uint64_t sm(void)
{
    uint64_t z = (sm_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static void rndfill(uint8_t *b, size_t n, uint64_t seed)
{
    sm_s = seed;
    size_t i = 0;
    while (i < n) {
        uint64_t z = sm();
        for (int k = 0; k < 8 && i < n; k++, i++)
            b[i] = (uint8_t)(z >> (8 * k));
    }
}

/* stream with the given chunk plan; returns status */
static uint32_t stream(const uint8_t *m, size_t n, const size_t *plan, size_t np, uint8_t *d)
{
    sha256_ctx c;
    uint32_t s = I(&c);
    size_t off = 0, i = 0;
    while (!s && off < n) {
        size_t k = np ? plan[i++ % np] : n;
        if (k > n - off)
            k = n - off;
        s = U(&c, m + off, k);
        off += k;
    }
    if (!s)
        s = F(&c, d);
    return s;
}

/* ---- vec ---------------------------------------------------------------------------------------- */
static uint8_t *genmsg(const char *spec, const char *root, size_t n)
{
    uint8_t *m = malloc(n ? n : 1);
    if (!m)
        exit(3);
    if (strcmp(spec, "-") == 0)
        return m;
    if (strncmp(spec, "seq:", 4) == 0) {
        for (size_t i = 0; i < n; i++)
            m[i] = (uint8_t)(i % 251);
    } else if (strncmp(spec, "rep:", 4) == 0) {
        unsigned b = (unsigned)strtoul(spec + 4, NULL, 10);
        memset(m, (int)b, n);
    } else if (strncmp(spec, "rnd:", 4) == 0) {
        rndfill(m, n, strtoull(spec + 4, NULL, 10));
    } else if (strncmp(spec, "file:", 5) == 0) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", root, spec + 5);
        FILE *f = fopen(path, "rb");
        if (!f || fread(m, 1, n, f) != n) {
            fprintf(stderr, "cannot read %s\n", path);
            exit(3);
        }
        fclose(f);
    } else {
        fprintf(stderr, "bad spec %s\n", spec);
        exit(3);
    }
    return m;
}

static const size_t plan_a[] = {1, 63, 64, 65, 0, 127, 3, 4096, 55};
static const size_t plan_b[] = {7};

static int mode_vec(const char *file, const char *root)
{
    FILE *f = fopen(file, "r");
    if (!f)
        return 3;
    char name[128], spec[512], dig[80];
    unsigned long long n;
    long rows = 0;
    while (fscanf(f, "%127s %511s %llu %79s", name, spec, &n, dig) == 4) {
        uint8_t want[32], d[32];
        char id[200];
        rows++;
        unhex(dig, want);
        uint8_t *m = genmsg(spec, root, (size_t)n);
        snprintf(id, sizeof id, "vec-%s-hash", name);
        if (!trapped(id, H(m, n, d), "") && cmpd(id, d, want, "sha256_hash"))
            npass++;
        snprintf(id, sizeof id, "vec-%s-one", name);
        if (!trapped(id, stream(m, n, NULL, 0, d), "") && cmpd(id, d, want, "init/update(n)/final"))
            npass++;
        snprintf(id, sizeof id, "vec-%s-chunkA", name);
        if (!trapped(id, stream(m, n, plan_a, 9, d), "") && cmpd(id, d, want, "chunks 1,63,64,65,0,127,3,4096,55"))
            npass++;
        snprintf(id, sizeof id, "vec-%s-chunk7", name);
        if (n <= 200000 && !trapped(id, stream(m, n, plan_b, 1, d), "") && cmpd(id, d, want, "chunks of 7"))
            npass++;
        free(m);
    }
    fclose(f);
    printf("INFO vec rows=%ld\n", rows);
    return 0;
}

/* ---- rcpt --------------------------------------------------------------------------------------- */
static uint8_t msg[2000];

static void mode_rcpt(void)
{
    sha256_ctx c, c2;
    uint8_t d[32], d2[32], w[32];
    char id[160];
    int ok;
    for (size_t i = 0; i < sizeof msg; i++)
        msg[i] = (uint8_t)(i % 251);

    /* hash(n) == init/update(n)/final == sha256sum, n = 0..300 and 1000, 2000 */
    ok = 1;
    for (size_t n = 0; n <= 2000 && ok; n = (n < 300 ? n + 1 : (n == 300 ? 1000 : n + 1000))) {
        ref(msg, n, w);
        snprintf(id, sizeof id, "rcpt-hash-eq-stream-n%zu", n);
        if (trapped(id, H(msg, n, d), "hash") || trapped(id, stream(msg, n, NULL, 0, d2), "stream")) {
            ok = 0;
            break;
        }
        ok = cmpd(id, d, w, "hash vs sha256sum") && cmpd(id, d2, d, "init/update/final vs hash");
    }
    if (ok)
        pass("rcpt-hash-eq-stream", "hash(n) == init/update(n)/final == sha256sum, n = 0..300, 1000, 2000");

    /* context byte copy after k updates (E3): continue both, equal digests, == sha256sum */
    ref(msg, 2000, w);
    ok = 1;
    for (size_t k = 0; k <= 2000 && ok; k += (k < 200 ? 1 : 61)) {
        static uint8_t raw[104 + 16];
        sha256_ctx *c3 = (sha256_ctx *)(void *)(raw + 1 + (k % 7)); /* misaligned copy: layout alignment 1 */
        snprintf(id, sizeof id, "rcpt-ctx-copy-k%zu", k);
        size_t a = k / 3, b = k - a;
        if (trapped(id, (sq = I(&c), sq |= U(&c, msg, a), sq |= U(&c, msg + a, b), sq), "prefix"))
            break;
        memcpy(&c2, &c, sizeof c);
        memcpy(c3, &c, sizeof c);
        uint32_t s = U(&c, msg + k, 2000 - k);
        s |= U(&c2, msg + k, (2000 - k) / 2);
        s |= U(c3, msg + k, 2000 - k);
        s |= U(&c2, msg + k + (2000 - k) / 2, 2000 - k - (2000 - k) / 2);
        uint8_t d3[32];
        s |= F(&c, d);
        s |= F(&c2, d2);
        s |= F(c3, d3);
        if (trapped(id, s, "continue"))
            break;
        ok = cmpd(id, d, w, "original") && cmpd(id, d2, w, "aligned copy") && cmpd(id, d3, w, "misaligned copy");
    }
    if (ok)
        pass("rcpt-ctx-copy", "copy after k updates (k = 0..200, then every 61 to 2000), both copies continued, digests equal == sha256sum");

    /* hash(d, n, d + j): output overlapping input, j = -31..31, n in {32, 64, 100} */
    ok = 1;
    for (int ni = 0; ni < 3 && ok; ni++) {
        size_t n = ni == 0 ? 32 : ni == 1 ? 64 : 100;
        ref(msg, n, w);
        for (int j = -31; j <= 31 && ok; j++) {
            static uint8_t ov[400];
            memset(ov, 0xa5, sizeof ov);
            memcpy(ov + 100, msg, n);
            snprintf(id, sizeof id, "rcpt-hash-overlap-n%zu-j%d", n, j);
            if (trapped(id, H(ov + 100, n, ov + 100 + j), "")) {
                ok = 0;
                break;
            }
            ok = cmpd(id, ov + 100 + j, w, "digest written over input");
        }
    }
    if (ok)
        pass("rcpt-hash-overlap", "hash(d, n, d+j) == sha256sum(original d) for n in {32,64,100}, j = -31..31 (incl. hash(d,32,d))");

    /* reuse after final */
    ok = 1;
    for (size_t n = 0; n <= 130 && ok; n++) {
        ref(msg, n, w);
        snprintf(id, sizeof id, "rcpt-post-final-n%zu", n);
        uint32_t s = (sq = I(&c), sq |= U(&c, msg + 7, 77), sq |= F(&c, d2), sq);
        s |= (sq = I(&c), sq |= U(&c, msg, n), sq |= F(&c, d), sq);
        if (trapped(id, s, ""))
            break;
        ok = cmpd(id, d, w, "init/update/final after a final");
        /* no init after a final: memory safety only (digest unspecified) */
        s = (sq = U(&c, msg, n), sq |= F(&c, d2), sq |= F(&c, d2), sq);
        if (ok && trapped(id, s, "update/final without init"))
            ok = 0;
    }
    if (ok)
        pass("rcpt-post-final", "init/update(n)/final after a final == sha256sum, n = 0..130; update/final without init returned status 0");

    /* empty updates (NULL and non-NULL, including a bogus pointer) at every position of seq:200 */
    ref(msg, 200, w);
    ok = 1;
    static const void *const ep[3] = {NULL, msg, (const void *)(uintptr_t)0x10};
    for (size_t p = 0; p <= 200 && ok; p++) {
        for (int e = 0; e < 3 && ok; e++) {
            snprintf(id, sizeof id, "rcpt-empty-p%zu-%s", p, e == 0 ? "null" : e == 1 ? "ptr" : "bogus");
            uint32_t s = (sq = I(&c), sq |= U(&c, ep[e], 0), sq |= U(&c, msg, p), sq |= U(&c, ep[e], 0),
                          sq |= U(&c, ep[(e + 1) % 3], 0), sq |= U(&c, msg + p, 200 - p), sq |= U(&c, ep[e], 0),
                          sq |= F(&c, d), sq);
            if (trapped(id, s, ""))
                ok = 0;
            else
                ok = cmpd(id, d, w, "empty updates around a split");
        }
    }
    if (ok && !trapped("rcpt-empty-only", (sq = I(&c), sq |= U(&c, NULL, 0), sq |= U(&c, msg, 0), sq |= F(&c, d), sq), "")) {
        ref(msg, 0, w);
        ok = cmpd("rcpt-empty-only", d, w, "init, update(NULL,0), update(p,0), final");
        if (ok && !trapped("rcpt-empty-hash", (sq = H(NULL, 0, d), sq |= H((const void *)(uintptr_t)0x10, 0, d2), sq), ""))
            ok = cmpd("rcpt-empty-hash", d, w, "hash(NULL,0)") && cmpd("rcpt-empty-hash", d2, w, "hash(bogus,0)");
    }
    if (ok)
        pass("rcpt-empty", "len-0 updates (NULL, valid, bogus 0x10 pointer) at every split 0..200, before first data, before final; hash(NULL,0)");

    /* status-32 refusals through the real API; nothing written */
    {
        uint8_t o[32], o0[32];
        sha256_ctx s0;
        uint64_t r = 7;
        const uint8_t *wrap = (const uint8_t *)(UINTPTR_MAX - 15);
        int rok = 1;
        I(&c);
        U(&c, msg, 10);
        memcpy(&s0, &c, sizeof c);
        memset(o, 0x5a, 32);
        memcpy(o0, o, 32);
        struct {
            const char *n;
            uint32_t s;
        } t[] = {
            {"update-null-len1", omega2_sha256_update(&r, (uint8_t *)&c, NULL, 1)},
            {"update-null-len2^40", omega2_sha256_update(&r, (uint8_t *)&c, NULL, 1ull << 40)},
            {"update-wrap", omega2_sha256_update(&r, (uint8_t *)&c, wrap, 32)},
            {"update-wrap-end2^64", omega2_sha256_update(&r, (uint8_t *)&c, wrap, 16)},
            {"update-len-max", omega2_sha256_update(&r, (uint8_t *)&c, msg, UINT64_MAX)},
            {"update-ctx-null", omega2_sha256_update(&r, NULL, msg, 3)},
            {"update-ctx-wrap", omega2_sha256_update(&r, (uint8_t *)(UINTPTR_MAX - 50), msg, 3)},
            {"update-view-overlaps-ctx", omega2_sha256_update(&r, (uint8_t *)&c, (uint8_t *)&c + 100, 8)},
            {"update-result-in-ctx", omega2_sha256_update((uint64_t *)(void *)&c, (uint8_t *)&c, msg, 3)},
            {"update-result-null", omega2_sha256_update(NULL, (uint8_t *)&c, msg, 3)},
            {"init-null", omega2_sha256_init(&r, NULL)},
            {"final-out-null", omega2_sha256_final(&r, (uint8_t *)&c, NULL)},
            {"final-ctx-null", omega2_sha256_final(&r, NULL, o)},
            {"final-out-overlaps-ctx", omega2_sha256_final(&r, (uint8_t *)&c, (uint8_t *)&c + 90)},
            {"final-out-wrap", omega2_sha256_final(&r, (uint8_t *)&c, (uint8_t *)(UINTPTR_MAX - 20))},
            {"hash-null-len5", omega2_sha256_hash(&r, NULL, 5, o)},
            {"hash-wrap", omega2_sha256_hash(&r, wrap, 64, o)},
            {"hash-out-null", omega2_sha256_hash(&r, msg, 5, NULL)},
            {"hash-result-overlaps-out", omega2_sha256_hash((uint64_t *)(void *)o, msg, 5, o)},
        };
        for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
            snprintf(id, sizeof id, "rcpt-refuse-%s", t[i].n);
            if (t[i].s != 32) {
                nfail++;
                rok = 0;
                printf("FAIL %s status=%u want=32\n", id, t[i].s);
            } else
                printf("INFO %s status=32\n", id);
        }
        if (memcmp(&s0, &c, sizeof c) || memcmp(o, o0, 32) || r != 7) {
            nfail++;
            rok = 0;
            printf("FAIL rcpt-refuse-nowrite context, out or result changed by a refused call\n");
        }
        if (rok)
            pass("rcpt-refuse", "19 refusals returned status 32; context, out and result untouched");
    }
}

/* ---- ident -------------------------------------------------------------------------------------- */
static void mode_ident(const char *want)
{
    const char *got = omega2_ident_sha256_om2();
    if (strcmp(got, want) == 0 && strcmp(got, OMEGA2_IDENT_sha256_om2) == 0)
        pass("ident", got);
    else {
        nfail++;
        printf("FAIL ident got=\"%s\" want=\"%s\" header=\"%s\"\n", got, want, OMEGA2_IDENT_sha256_om2);
    }
}

/* ---- rand --------------------------------------------------------------------------------------- */
static void mode_rand(uint64_t seed, long count)
{
    uint8_t d[32], d2[32], w[32];
    char id[160], h[65];
    long ok = 0;
    uint64_t tot = 0, nupd = 0, nempty = 0;
    for (long i = 0; i < count; i++) {
        sm_s = seed * 1000003ull + (uint64_t)i;
        uint64_t r = sm(), cls = r % 16, len;
        if (cls < 7)
            len = sm() % 300;
        else if (cls < 11)
            len = sm() % 5000;
        else if (cls < 14)
            len = sm() % 70000;
        else if (cls < 15)
        {
            uint64_t kb = 1 + sm() % 64;
            len = 64 * kb + sm() % 3 - 1; /* block multiples and neighbours */
        }
        else
            len = sm() % 1100000;
        uint64_t plan_seed = sm();
        uint8_t *m = malloc(len ? len : 1);
        rndfill(m, len, seed ^ ((uint64_t)i << 20) ^ 0x5eedull);
        /* random chunking with empty updates (NULL and non-NULL) */
        sha256_ctx c;
        snprintf(id, sizeof id, "rand-%llu-%ld-len%llu", (unsigned long long)seed, i, (unsigned long long)len);
        sm_s = plan_seed;
        uint32_t s = I(&c);
        uint64_t off = 0;
        while (!s && off < len) {
            uint64_t q = sm(), k;
            switch (q % 6) {
            case 0:
                k = 0;
                break;
            case 1:
                k = 1 + (q >> 8) % 64;
                break;
            case 2:
                k = 1 + (q >> 8) % 300;
                break;
            case 3:
                k = 64 - (off % 64);
                break;
            case 4:
                k = 1 + (q >> 8) % 70000;
                break;
            default:
                k = len - off;
                break;
            }
            if (k > len - off)
                k = len - off;
            nupd++;
            if (k == 0) {
                nempty++;
                s = U(&c, (q >> 3) & 1 ? NULL : m, 0);
            } else
                s = U(&c, m + off, k);
            off += k;
        }
        if (!s)
            s = (sq = U(&c, NULL, 0), sq |= F(&c, d), sq);
        if (trapped(id, s, "stream") || trapped(id, H(m, len, d2), "hash")) {
            free(m);
            continue;
        }
        ref(m, len, w);
        tot += len;
        if (cmpd(id, d, w, "random chunking vs sha256sum") && cmpd(id, d2, w, "hash vs sha256sum"))
            ok++;
        hex(d, h);
        printf("RAND %llu %ld %llu %s\n", (unsigned long long)seed, i, (unsigned long long)len, h);
        free(m);
    }
    printf("INFO rand bytes=%llu updates=%llu empty-updates=%llu\n", (unsigned long long)tot,
           (unsigned long long)nupd, (unsigned long long)nempty);
    if (ok == count) {
        snprintf(id, sizeof id, "%ld seeded messages (seed %llu): random chunkings incl. empty updates and hash == sha256sum",
                 count, (unsigned long long)seed);
        pass("rand", id);
    }
}

/* ---- part: 2^25-byte run boundaries of blocks() ------------------------------------------------- */
static void mode_part(void)
{
    const uint64_t R = 1ull << 25;
    const uint64_t lens[] = {R - 64, R - 1, R, R + 1, R + 64, 2 * R, 2 * R + 77, 3 * R + 4095};
    uint64_t max = 3 * R + 4095;
    uint8_t *m = malloc(max);
    uint8_t d[32], d2[32], d3[32], w[32];
    char id[160];
    rndfill(m, max, 0x25);
    int ok = 1;
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        uint64_t n = lens[i];
        static const size_t plan[] = {33, (1u << 25) + 5, 1u << 24};
        snprintf(id, sizeof id, "part-len%llu", (unsigned long long)n);
        ref(m, n, w);
        if (trapped(id, H(m, n, d), "hash") || trapped(id, stream(m, n, NULL, 0, d2), "one") ||
            trapped(id, stream(m, n, plan, 3, d3), "chunks")) {
            ok = 0;
            continue;
        }
        if (!(cmpd(id, d, w, "hash") && cmpd(id, d2, w, "one update") && cmpd(id, d3, w, "chunks 33,2^25+5,2^24")))
            ok = 0;
    }
    free(m);
    if (ok)
        pass("part", "lengths 2^25-64 .. 3*2^25+4095 (one update, hash, chunked) == sha256sum");
}

/* ---- big ---------------------------------------------------------------------------------------- */
static void mode_big(void)
{
    const uint64_t L = (1ull << 32) + 1000003ull;
    uint8_t *m = malloc(L);
    uint8_t d[32], w[32], wl[32];
    sha256_ctx c;
    if (!m) {
        printf("NOT_RUN big malloc(%llu) failed\n", (unsigned long long)L);
        return;
    }
    rndfill(m, L, 0xb16);
    ref(m, L, w);
    ref(m, (1ull << 32) - 1, wl);
    printf("INFO big L=%llu sha256sum=", (unsigned long long)L);
    for (int i = 0; i < 32; i++)
        printf("%02x", w[i]);
    printf("\n");
    if (!trapped("big-1", (sq = I(&c), sq |= U(&c, m, L), sq |= F(&c, d), sq), "") && cmpd("big-1", d, w, "one update of 2^32+1000003 bytes"))
        pass("big-1", "one update of 2^32+1000003 random bytes == sha256sum");
    if (!trapped("big-hash", H(m, L, d), "") && cmpd("big-hash", d, w, "hash"))
        pass("big-hash", "sha256_hash of 2^32+1000003 bytes == sha256sum");
    if (!trapped("big-split32", (sq = I(&c), sq |= U(&c, m, (1ull << 32) - 1), sq |= U(&c, m + (1ull << 32) - 1, L - ((1ull << 32) - 1)), sq |= F(&c, d), sq), "") &&
        cmpd("big-split32", d, w, "updates of 2^32-1 and the rest"))
        pass("big-split32", "updates of 2^32-1 and 1000004 bytes == sha256sum");
    if (!trapped("big-below", H(m, (1ull << 32) - 1, d), "") && cmpd("big-below", d, wl, "hash of 2^32-1 bytes"))
        pass("big-below", "hash of 2^32-1 bytes == sha256sum");
    /* total > 2^32 through many updates of random sizes (count crosses 2^32 inside an update) */
    sm_s = 0x3a7;
    uint64_t off = 0, n = 0;
    uint32_t s = I(&c);
    while (!s && off < L) {
        uint64_t k = sm() % (1ull << 24) + 1;
        if (n % 97 == 5)
            k = sm() % 64;
        if (k > L - off)
            k = L - off;
        s = U(&c, k ? m + off : NULL, k);
        off += k;
        n++;
    }
    if (!s)
        s = F(&c, d);
    char what[120];
    snprintf(what, sizeof what, "%llu updates totalling 2^32+1000003 bytes == sha256sum", (unsigned long long)n);
    if (!trapped("big-many", s, "") && cmpd("big-many", d, w, what))
        pass("big-many", what);
    free(m);
}

static void mode_bigzero(void)
{
    const uint64_t L = (1ull << 32) + 3;
    uint8_t *z = mmap(NULL, L, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    uint8_t d[32], w[32];
    sha256_ctx c;
    if (z == MAP_FAILED) {
        printf("NOT_RUN bigzero mmap failed\n");
        return;
    }
    ref(z, L, w);
    if (!trapped("bigzero-1", (sq = I(&c), sq |= U(&c, z, L), sq |= F(&c, d), sq), "") && cmpd("bigzero-1", d, w, ""))
        pass("bigzero-1", "one update of 2^32+3 zero bytes == sha256sum");
    if (!trapped("bigzero-hash", H(z, L, d), "") && cmpd("bigzero-hash", d, w, ""))
        pass("bigzero-hash", "hash of 2^32+3 zero bytes == sha256sum");
    munmap(z, L);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2)
        return 90;
    const char *m = argv[1];
    if (!strcmp(m, "vec") && argc == 4)
        mode_vec(argv[2], argv[3]);
    else if (!strcmp(m, "rcpt"))
        mode_rcpt();
    else if (!strcmp(m, "ident") && argc == 3)
        mode_ident(argv[2]);
    else if (!strcmp(m, "rand") && argc == 4)
        mode_rand(strtoull(argv[2], NULL, 10), strtol(argv[3], NULL, 10));
    else if (!strcmp(m, "part"))
        mode_part();
    else if (!strcmp(m, "big"))
        mode_big();
    else if (!strcmp(m, "bigzero"))
        mode_bigzero();
    else
        return 90;
    printf("TOTAL %s pass=%ld fail=%ld trap=%ld\n", m, npass, nfail, ntrap);
    return (nfail || ntrap) ? 1 : 0;
}
