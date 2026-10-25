/* SPDX-License-Identifier: Apache-2.0 */
/*
 * origsel_indep: independent recomputer (external referee, C) of the ORIGSEL v1.1 report.
 * Written from native/turing/REPORT-CONTRACT.md, Turing-profile-ORIGSEL-v1.1.toml,
 * ORIG1-CONTRACT.md s.2-3, ORIG1-CONTRACT-v2.md s.2-3 and CODER_SPEC.md s.8.
 * Interpretations and .om consultations are listed in README.md.
 *
 * usage: origsel_indep REC ALLOW V1 IMG_M1 IMG_SH IMG_UN SELECT_OM OMEXP_OM KAPPA_OM
 *        origsel_indep --selftest
 *        origsel_indep --fnv FILE   (FNV-1a 64 of every byte of FILE, unsigned decimal)
 * exit: 0 report written; 2 contract refusal (single report line); 1 usage or input error.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef unsigned __int128 u128;

#define NARMS 5
#define NX 10
#define REPS 10000
#define LB_IDX 166
#define UB_IDX 9833
#define IMG_BYTES 11560
#define SB_OFF 5912
#define NPAR 705
#define SEED 20261025ULL

enum { XU, XO0, XFR, XS, XR, XM1, XM2, XA, XCSH, XCUN };
static const char *const XNAME[NX] = {"U", "O0", "FR", "S", "R", "M1", "M2", "A", "CSH", "CUN"};

/* ------------------------------------------------------------------ files */

typedef struct {
    unsigned char *b;
    size_t n;
} buf_t;

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "origsel_indep: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(1);
}

static buf_t slurp(const char *path)
{
    buf_t r = {NULL, 0};
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open", path);
    size_t cap = 1 << 16;
    r.b = malloc(cap);
    if (!r.b)
        die("out of memory", NULL);
    for (;;) {
        if (r.n == cap) {
            cap *= 2;
            r.b = realloc(r.b, cap);
            if (!r.b)
                die("out of memory", NULL);
        }
        size_t k = fread(r.b + r.n, 1, cap - r.n, f);
        r.n += k;
        if (k == 0) {
            if (ferror(f))
                die("read error", path);
            break;
        }
    }
    fclose(f);
    return r;
}

static u64 le64(const unsigned char *p)
{
    u64 v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static u64 fnv1a(const unsigned char *p, size_t n)
{
    u64 h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* ------------------------------------------------------------------ integer helpers */

static unsigned fl2(u64 v) /* floor(log2 v), 0 for v <= 1 */
{
    unsigned e = 0;
    while (v > 1) {
        v >>= 1;
        e++;
    }
    return e;
}

static u64 lg16(u64 v) /* ORIG1-CONTRACT s.3, v >= 1 */
{
    if (v == 0)
        return 0;
    unsigned e = fl2(v);
    return ((v << 4) >> e) + 16 * (u64)e - 16;
}

static u64 ceil_log2(u64 v) /* smallest k with 2^k >= v; ceil(log2 1) = 0 */
{
    u64 k = 0;
    while (k < 64 && ((u64)1 << k) < v)
        k++;
    return k;
}

static u64 gamma_len(u64 v) { return 2 * (u64)fl2(v) + 1; }

static u64 binom4(u64 n) /* C(n + 4, 4), exact */
{
    u64 c = 1;
    for (u64 i = 1; i <= 4; i++)
        c = c * (n + i) / i;
    return c;
}

static u64 row_len(u64 n) { return gamma_len(n + 1) + ceil_log2(binom4(n)); }

/* CODER_SPEC s.8 */
static u64 ub_q(u64 q)
{
    unsigned ip = 63 - (unsigned)__builtin_clzll(q);
    u64 m = q << (62 - ip);
    u64 frac = 0;
    for (int i = 1; i <= 32; i++) {
        m = (u64)(((u128)m * (u128)m) >> 62);
        if (m >= ((u64)1 << 63)) {
            frac |= (u64)1 << (32 - i);
            m >>= 1;
        }
    }
    u64 lg = ((u64)ip << 32) | frac;
    u64 x = ((u64)16 << 32) - lg;
    return (x * 1000000ULL + ((u64)1 << 31)) >> 32;
}

static u64 sm_state;
static u64 splitmix64(void)
{
    sm_state += 0x9E3779B97F4A7C15ULL;
    u64 z = sm_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static int selftest(void)
{
    static const u64 gq[] = {1, 32768, 43481, 46819, 13107, 13108, 65536};
    static const u64 gv[] = {16000000, 1000000, 591903, 485194, 2321950, 2321840, 0};
    int bad = 0;
    for (size_t i = 0; i < sizeof gq / sizeof gq[0]; i++) {
        u64 v = ub_q(gq[i]);
        printf("ub(%llu)=%llu expect=%llu %s\n", (unsigned long long)gq[i], (unsigned long long)v,
               (unsigned long long)gv[i], v == gv[i] ? "ok" : "FAIL");
        bad |= v != gv[i];
    }
    sm_state = SEED;
    for (int i = 0; i < 5; i++)
        printf("splitmix64[%d]=%llu\n", i, (unsigned long long)splitmix64());
    volatile float a = 0x1.000002p0f, b = 0x1.000002p0f, c = -0x1.000004p0f;
    float p = a * b;
    float s = p + c;
    uint32_t sb;
    memcpy(&sb, &s, 4);
    printf("fma_check=%08x %s\n", sb, sb == 0x00000000u ? "ok(unfused)" : "FAIL(fused)");
    bad |= sb != 0;
    printf("selftest %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------------ records */

typedef struct {
    u64 w[32];
    int code;      /* oxadm2 code, 0 admitted */
    int split;     /* 0 TRAIN, 1 VALIDATION, 2 HELD-OUT, 3 refused */
    unsigned cell; /* 16 fl(M) + fl(N) */
    int oracle;    /* cheapest ps over arms 0..4, lowest index on tie */
    long feat[12];
    int ord[NX][NARMS]; /* order per arm */
    int rank[NX][NARMS];
    int ctx[NX];
} rec_t;

static rec_t *R;
static size_t NR;

static u64 cache_of(u64 core) { return core == 0 ? 2097152 : core == 1 ? 524288 : 1048576; }

static int fmt2(const u64 *w)
{
    if (w[0] != 3553143591248351811ULL || w[1] != 1)
        return -2;
    if (w[2] > 5 || w[3] > 1 || w[4] > 1 || w[13] != 6)
        return -3;
    u64 m = w[5], n = w[6];
    if (m < 1 || m > 8192 || n < 1 || n > 8192 || m * n > 524288 || w[7] < 1 || w[7] > 1048576)
        return -4;
    if (w[10] != cache_of(w[3]) || w[9] != (m * n + m + n) * 8)
        return -9;
    return 0;
}

static int val2(const u64 *w)
{
    int paired = w[12] == 31;
    for (int a = 0; a < 5 && paired; a++)
        if (w[16 + a] == 0 || w[24 + a] != 1)
            paired = 0;
    if (paired && (w[21] != 0 || w[29] != 0 || w[22] != 0 || w[23] != 0))
        paired = 0;
    if (!paired)
        return -14;
    for (int a = 0; a < 5; a++)
        if (w[16 + a] >= ((u64)1 << 40))
            return -10;
    if (w[31] != 0)
        return -7;
    return 0;
}

static int hspl(unsigned cm, unsigned cn)
{
    u64 h = 14695981039346656037ULL;
    h ^= (unsigned char)cm;
    h *= 1099511628211ULL;
    h ^= (unsigned char)cn;
    h *= 1099511628211ULL;
    u64 k = (h >> 32) % 10;
    return k <= 5 ? 0 : k <= 7 ? 1 : 2;
}

/* ------------------------------------------------------------------ MLP */

static float P[3][NPAR];

static void load_image(int which, const char *path)
{
    buf_t b = slurp(path);
    if (b.n != IMG_BYTES)
        die("image is not 11560 bytes", path);
    if (memcmp(b.b, "SI19SLCK", 8) != 0 || le64(b.b + 8) != 1 || le64(b.b + 16) != 2 ||
        le64(b.b + 24) != 32 || le64(b.b + 136) != NPAR)
        die("image header is not SI19SLCK v1 kind 2 H 32 705 params", path);
    if (fnv1a(b.b, b.n - 8) != le64(b.b + b.n - 8))
        die("image trailer FNV-1a mismatch", path);
    for (int k = 0; k < NPAR; k++) {
        uint32_t bits = (uint32_t)le64(b.b + SB_OFF + 8 * (size_t)k);
        memcpy(&P[which][k], &bits, 4);
    }
    free(b.b);
}

static i64 mlp_key(int which, const long *feat, int arm)
{
    const float *p = P[which];
    float z[20];
    for (int j = 0; j < 12; j++)
        z[j] = (float)feat[j] / 16.0f;
    for (int j = 0; j < 8; j++)
        z[12 + j] = j == arm ? 1.0f : 0.0f;
    float act[32];
    for (int h = 0; h < 32; h++) {
        float acc = p[640 + h];
        for (int i = 0; i < 20; i++) {
            float prod = p[32 * i + h] * z[i];
            acc = acc + prod;
        }
        act[h] = acc > 0.0f ? acc : 0.0f;
    }
    float out = p[704];
    for (int h = 0; h < 32; h++) {
        float prod = p[672 + h] * act[h];
        out = out + prod;
    }
    uint32_t b;
    memcpy(&b, &out, 4);
    if (b & 0x80000000u)
        return -(i64)(b & 0x7fffffffu);
    return (i64)b;
}

/* stable ascending order of 5 keys, ties to the lower arm index */
static void order_by_keys(const i64 *key, int *ord)
{
    for (int a = 0; a < NARMS; a++)
        ord[a] = a;
    for (int i = 1; i < NARMS; i++) {
        int v = ord[i], j = i;
        while (j > 0 && key[ord[j - 1]] > key[v]) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = v;
    }
}

/* ------------------------------------------------------------------ units (P2) */

static int starts(const unsigned char *s, size_t n, const char *pre)
{
    size_t k = strlen(pre);
    return n >= k && memcmp(s, pre, k) == 0;
}

/* canonical bytes of lines [lo, hi) of the text; returns count */
static u64 canon(const buf_t *t, size_t lo, size_t hi)
{
    u64 c = 0;
    size_t p = lo;
    while (p < hi) {
        size_t e = p;
        while (e < hi && t->b[e] != '\n')
            e++;
        size_t len = e - p;
        if (len > 0 && !starts(t->b + p, len, "note"))
            c += len + (e < hi ? 1 : 0);
        p = e < hi ? e + 1 : e;
    }
    return c;
}

static u64 canon_fn(const buf_t *t, const char *name, const char *path)
{
    char head[128];
    snprintf(head, sizeof head, "fn %s ", name);
    size_t p = 0, lo = (size_t)-1;
    while (p < t->n) {
        size_t e = p;
        while (e < t->n && t->b[e] != '\n')
            e++;
        if (lo == (size_t)-1) {
            if (starts(t->b + p, e - p, head))
                lo = p;
        } else if (starts(t->b + p, e - p, "fn ") || starts(t->b + p, e - p, "global ")) {
            return canon(t, lo, p);
        }
        p = e < t->n ? e + 1 : e;
    }
    if (lo == (size_t)-1) {
        fprintf(stderr, "origsel_indep: function %s not found in %s\n", name, path);
        exit(1);
    }
    return canon(t, lo, t->n);
}

/* ------------------------------------------------------------------ main */

static int cmp_i64(const void *a, const void *b)
{
    i64 x = *(const i64 *)a, y = *(const i64 *)b;
    return x < y ? -1 : x > y;
}

typedef struct {
    i64 total, lb, ub;
} stat_t;

static stat_t finish(i64 total, i64 *v)
{
    qsort(v, REPS, sizeof v[0], cmp_i64);
    stat_t s = {total, v[LB_IDX], v[UB_IDX]};
    return s;
}

static void pstat(const char *head, stat_t s)
{
    printf("%s total=%lld lb=%lld ub=%lld\n", head, (long long)s.total, (long long)s.lb, (long long)s.ub);
}

/* ub per record and arm, computed once all rows exist */
static u64 (*UBX)[NX];

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return selftest();
    if (argc == 3 && strcmp(argv[1], "--fnv") == 0) {
        buf_t f = slurp(argv[2]);
        printf("%llu\n", (unsigned long long)fnv1a(f.b, f.n));
        free(f.b);
        if (fflush(stdout) != 0)
            die("write error", NULL);
        return 0;
    }
    if (argc != 10) {
        fprintf(stderr, "usage: origsel_indep REC ALLOW V1 IMG_M1 IMG_SH IMG_UN SELECT_OM OMEXP_OM KAPPA_OM\n"
                        "       origsel_indep --selftest\n"
                        "       origsel_indep --fnv FILE\n");
        return 1;
    }
    buf_t rec = slurp(argv[1]);
    buf_t allow = slurp(argv[2]);
    buf_t v1 = slurp(argv[3]);
    buf_t u_sel = slurp(argv[7]);
    buf_t u_omx = slurp(argv[8]);
    buf_t u_kap = slurp(argv[9]);
    if (v1.n % 256 != 0)
        die("V1 is not a whole number of 256-byte records", argv[3]);

    /* ---- allow-list: first line whose decimal fnv equals the file's decides */
    u64 h = fnv1a(rec.b, rec.n);
    int allowed = 0;
    {
        size_t p = 0;
        while (p < allow.n) {
            size_t e = p;
            while (e < allow.n && allow.b[e] != '\n')
                e++;
            size_t q = p;
            u64 v = 0;
            int ok = 1;
            while (q < e && allow.b[q] >= '0' && allow.b[q] <= '9') {
                u64 d = (u64)(allow.b[q] - '0');
                if (v > (UINT64_MAX - d) / 10)
                    ok = 0;
                v = v * 10 + d;
                q++;
            }
            if (ok && q > p && v == h) {
                if (q < e && allow.b[q] == ' ') {
                    size_t s = q + 1, t = s;
                    while (t < e && allow.b[t] != ' ')
                        t++;
                    if ((t - s == 10 && memcmp(allow.b + s, "AUTHORIZED", 10) == 0) ||
                        (t - s == 9 && memcmp(allow.b + s, "SYNTHETIC", 9) == 0))
                        allowed = 1;
                }
                break;
            }
            p = e < allow.n ? e + 1 : e;
        }
    }
    if (!allowed) {
        printf("refused allow\n");
        return 2;
    }

    /* ---- admission v2 */
    if (rec.n == 0 || rec.n % 256 != 0 || rec.n > 524288) {
        printf("refused admission -1\n");
        return 2;
    }
    NR = rec.n / 256;
    R = calloc(NR, sizeof *R);
    UBX = calloc(NR, sizeof *UBX);
    if (!R || !UBX)
        die("out of memory", NULL);
    for (size_t r = 0; r < NR; r++)
        for (int i = 0; i < 32; i++)
            R[r].w[i] = le64(rec.b + 256 * r + 8 * (size_t)i);
    size_t nsplit[3] = {0, 0, 0}, nadm = 0;
    for (size_t r = 0; r < NR; r++) {
        u64 *w = R[r].w;
        int c = fmt2(w);
        if (c == 0)
            c = val2(w);
        if (c == 0 && r > 0 && R[r - 1].w[14] >= w[14])
            c = -8;
        if (c == 0)
            for (size_t q = 0; q < r; q++)
                if (R[q].code == 0 && R[q].w[3] == w[3] && R[q].w[4] == w[4] && R[q].w[5] == w[5] &&
                    R[q].w[6] == w[6]) {
                    c = -15;
                    break;
                }
        R[r].code = c;
        R[r].split = 3;
        if (c == 0) {
            unsigned fm = fl2(w[5]), fn = fl2(w[6]);
            R[r].cell = 16 * fm + fn;
            R[r].split = hspl(fm, fn);
            nsplit[R[r].split]++;
            nadm++;
        }
    }
    for (size_t r = 0; r < NR; r++) {
        if (R[r].code != 0)
            continue;
        for (size_t q = 0; q < r; q++) {
            if (R[q].code != 0 || R[q].split == R[r].split)
                continue;
            if (R[q].cell == R[r].cell || (R[q].w[5] == R[r].w[5] && R[q].w[6] == R[r].w[6])) {
                printf("refused admission -11\n");
                return 2;
            }
        }
    }

    load_image(0, argv[4]);
    load_image(1, argv[5]);
    load_image(2, argv[6]);

    /* ---- per admitted record: features, oracle, R-independent orders */
    for (size_t r = 0; r < NR; r++) {
        rec_t *x = &R[r];
        if (x->code != 0)
            continue;
        const u64 *w = x->w;
        u64 lm = lg16(w[5]), ln = lg16(w[6]);
        x->feat[0] = 16;
        x->feat[1] = (long)lm;
        x->feat[2] = (long)ln;
        x->feat[3] = (long)((lm * ln) >> 8);
        x->feat[4] = (long)((ln * ln) >> 8);
        x->feat[5] = w[6] <= 3 ? 16 : 0;
        x->feat[6] = w[6] % 4 != 0 ? 16 : 0;
        i64 d = (i64)lg16(w[9]) - (i64)lg16(w[10]);
        x->feat[7] = d < 0 ? 0 : d > 256 ? 256 : (long)d;
        x->feat[8] = w[3] == 0 ? 16 : 0;
        x->feat[9] = w[3] == 1 ? 16 : 0;
        x->feat[10] = w[3] == 2 ? 16 : 0;
        x->feat[11] = 16 * (long)(w[4] < 15 ? w[4] : 15);
        int b = 0;
        for (int a = 1; a < NARMS; a++)
            if (w[16 + a] < w[16 + b])
                b = a;
        x->oracle = b;
    }

    /* S: per core class, totals of ps over admitted TRAIN (both pressures) */
    int sord[2][NARMS];
    for (int c = 0; c < 2; c++) {
        u64 tot[NARMS] = {0};
        for (size_t r = 0; r < NR; r++)
            if (R[r].code == 0 && R[r].split == 0 && R[r].w[3] == (u64)c)
                for (int a = 0; a < NARMS; a++)
                    tot[a] += R[r].w[16 + a];
        int *o = sord[c];
        for (int a = 0; a < NARMS; a++)
            o[a] = a;
        for (int i = 1; i < NARMS; i++) {
            int v = o[i], j = i;
            while (j > 0 && tot[o[j - 1]] > tot[v]) {
                o[j] = o[j - 1];
                j--;
            }
            o[j] = v;
        }
    }

    u64 ntrain = nsplit[0];
    for (size_t r = 0; r < NR; r++) {
        rec_t *x = &R[r];
        if (x->code != 0)
            continue;
        const u64 *w = x->w;
        static const int ident[NARMS] = {0, 1, 2, 3, 4};
        memcpy(x->ord[XU], ident, sizeof ident);
        memcpy(x->ord[XO0], ident, sizeof ident);
        memcpy(x->ord[XA], ident, sizeof ident);
        int ra = w[6] <= 8 ? 2 : 3, k = 1;
        x->ord[XFR][0] = ra;
        for (int a = 0; a < NARMS; a++)
            if (a != ra)
                x->ord[XFR][k++] = a;
        memcpy(x->ord[XS], sord[w[3]], sizeof ident);
        /* R: nearest admitted TRAIN record, same core and pressure */
        i64 lm = (i64)lg16(w[5]), ln = (i64)lg16(w[6]);
        long best = -1;
        i64 bd = 0;
        for (size_t q = 0; q < NR; q++) {
            const rec_t *y = &R[q];
            if (y->code != 0 || y->split != 0 || y->w[3] != w[3] || y->w[4] != w[4])
                continue;
            i64 dm = (i64)lg16(y->w[5]) - lm, dn = (i64)lg16(y->w[6]) - ln;
            i64 dd = (dm < 0 ? -dm : dm) + (dn < 0 ? -dn : dn);
            if (best < 0 || dd < bd) {
                best = (long)q;
                bd = dd;
            }
        }
        if (best < 0) {
            memcpy(x->ord[XR], ident, sizeof ident);
        } else {
            i64 key[NARMS];
            for (int a = 0; a < NARMS; a++)
                key[a] = (i64)R[best].w[16 + a];
            order_by_keys(key, x->ord[XR]);
        }
        for (int im = 0; im < 3; im++) {
            i64 key[NARMS];
            for (int a = 0; a < NARMS; a++)
                key[a] = mlp_key(im, x->feat, a);
            int *o = x->ord[im == 0 ? XM1 : im == 1 ? XCSH : XCUN];
            order_by_keys(key, o);
        }
        memcpy(x->ord[XM2], x->ord[XM1], sizeof ident);
        int kap = (int)(2 * w[3] + (w[6] <= 8 ? 1 : 0));
        for (int X = 0; X < NX; X++) {
            x->ctx[X] = (X == XM2 || X == XA) ? kap : 0;
            for (int i = 0; i < NARMS; i++)
                x->rank[X][x->ord[X][i]] = i;
        }
    }

    /* ---- F1 rows over VALIDATION pressure-0 records */
    u64 cnt[NX][4][NARMS];
    u64 Q[NX][4][NARMS];
    memset(cnt, 0, sizeof cnt);
    for (size_t r = 0; r < NR; r++) {
        const rec_t *x = &R[r];
        if (x->code != 0 || x->split != 1 || x->w[4] != 0 || x->w[3] > 1)
            continue;
        for (int X = 0; X < NX; X++)
            cnt[X][x->ctx[X]][x->rank[X][x->oracle]]++;
    }
    for (int X = 0; X < NX; X++)
        for (int c = 0; c < 4; c++) {
            if (X == XU) {
                Q[X][c][0] = 13108;
                for (int i = 1; i < NARMS; i++)
                    Q[X][c][i] = 13107;
                continue;
            }
            u64 N = 0, sum = 0;
            for (int i = 0; i < NARMS; i++)
                N += cnt[X][c][i];
            int big = 0;
            for (int i = 0; i < NARMS; i++) {
                Q[X][c][i] = 1 + 65531 * (2 * cnt[X][c][i] + 1) / (2 * N + 5);
                sum += Q[X][c][i];
                if (cnt[X][c][i] > cnt[X][c][big])
                    big = i;
            }
            Q[X][c][big] += 65536 - sum;
        }
    int nctx[NX];
    for (int X = 0; X < NX; X++)
        nctx[X] = (X == XM2 || X == XA) ? 4 : 1;

    /* ---- model code lengths */
    u64 rows_len[NX];
    for (int X = 0; X < NX; X++) {
        rows_len[X] = 0;
        for (int c = 0; c < nctx[X]; c++) {
            u64 N = 0;
            for (int i = 0; i < NARMS; i++)
                N += cnt[X][c][i];
            rows_len[X] += row_len(N);
        }
    }
    u64 P1[NX], P2[NX];
    const u64 mlpw = 32 + 22560;
    P1[XU] = 0;
    P1[XO0] = rows_len[XO0];
    P1[XFR] = 20 + rows_len[XFR];
    P1[XS] = 14 + rows_len[XS];
    P1[XR] = 12 + 26 * ntrain + rows_len[XR];
    P1[XM1] = mlpw + rows_len[XM1];
    P1[XCSH] = mlpw + rows_len[XCSH];
    P1[XCUN] = mlpw + rows_len[XCUN];
    P1[XM2] = mlpw + 21 + rows_len[XM2];
    P1[XA] = 21 + rows_len[XA];
    u64 b_sel = canon(&u_sel, 0, u_sel.n), b_kap = canon(&u_kap, 0, u_kap.n);
    u64 b_rule = canon_fn(&u_omx, "oxrule", argv[8]);
    u64 b_stat = canon_fn(&u_omx, "oxstatic", argv[8]);
    u64 b_near = canon_fn(&u_omx, "oxnearok", argv[8]) + canon_fn(&u_omx, "oxl1", argv[8]) +
                 canon_fn(&u_omx, "oxnear", argv[8]);
    u64 ubytes[NX] = {0};
    ubytes[XFR] = b_rule;
    ubytes[XS] = b_stat;
    ubytes[XR] = b_near;
    ubytes[XM1] = ubytes[XCSH] = ubytes[XCUN] = b_sel;
    ubytes[XM2] = b_sel + b_kap;
    ubytes[XA] = b_kap;
    for (int X = 0; X < NX; X++)
        P2[X] = P1[X] + 8 * ubytes[X];

    /* ---- D, burned, families, P1R */
    size_t nv1 = v1.n / 256;
    unsigned char v1cell[16 * 64 + 64] = {0};
    for (size_t i = 0; i < nv1; i++) {
        u64 m = le64(v1.b + 256 * i + 40), n = le64(v1.b + 256 * i + 48);
        v1cell[16 * fl2(m) + fl2(n)] = 1;
    }
    size_t *D = malloc(NR * sizeof *D), *PR = malloc(NR * sizeof *PR);
    if (!D || !PR)
        die("out of memory", NULL);
    size_t nD = 0, nPR = 0, burned = 0, nfresh = 0, nfam = 0;
    for (size_t r = 0; r < NR; r++) {
        const rec_t *x = &R[r];
        if (x->code != 0 || x->split != 2 || x->w[3] > 1 || x->w[4] > 1)
            continue;
        int inv1 = 0;
        for (size_t i = 0; i < nv1 && !inv1; i++)
            if (le64(v1.b + 256 * i + 40) == x->w[5] && le64(v1.b + 256 * i + 48) == x->w[6])
                inv1 = 1;
        if (x->w[4] == 0) {
            if (inv1) {
                burned++;
                continue;
            }
            D[nD++] = r;
            if (v1cell[x->cell])
                nfam++;
            else
                nfresh++;
        } else if (!inv1) {
            PR[nPR++] = r;
        }
    }

    u64 sumD[NX] = {0}, sumF[NX] = {0}, sumV[NX] = {0}, sumP[NX] = {0};
    for (size_t r = 0; r < NR; r++) {
        const rec_t *x = &R[r];
        if (x->code != 0)
            continue;
        for (int X = 0; X < NX; X++)
            UBX[r][X] = ub_q(Q[X][x->ctx[X]][x->rank[X][x->oracle]]);
    }
    for (size_t i = 0; i < nD; i++) {
        size_t r = D[i];
        for (int X = 0; X < NX; X++) {
            sumD[X] += UBX[r][X];
            if (v1cell[R[r].cell])
                sumV[X] += UBX[r][X];
            else
                sumF[X] += UBX[r][X];
        }
    }
    for (size_t i = 0; i < nPR; i++)
        for (int X = 0; X < NX; X++)
            sumP[X] += UBX[PR[i]][X];

    /* ---- blocks: cells (pooled) and (cell, core) */
    int blk1[512], blk2[512];
    for (int i = 0; i < 512; i++)
        blk1[i] = blk2[i] = -1;
    for (size_t i = 0; i < nD; i++) {
        blk1[R[D[i]].cell] = 0;
        blk2[2 * R[D[i]].cell + (unsigned)R[D[i]].w[3]] = 0;
    }
    size_t F = 0, F2 = 0;
    for (int i = 0; i < 512; i++) {
        if (blk1[i] == 0)
            blk1[i] = (int)F++;
        if (blk2[i] == 0)
            blk2[i] = (int)F2++;
    }
    i64(*bs1)[NX] = calloc(F ? F : 1, sizeof *bs1);
    i64(*bs2)[NX] = calloc(F2 ? F2 : 1, sizeof *bs2);
    if (!bs1 || !bs2)
        die("out of memory", NULL);
    for (size_t i = 0; i < nD; i++) {
        const rec_t *x = &R[D[i]];
        int j1 = blk1[x->cell], j2 = blk2[2 * x->cell + (unsigned)x->w[3]];
        for (int X = 0; X < NX; X++) {
            bs1[j1][X] += (i64)UBX[D[i]][X];
            bs2[j2][X] += (i64)UBX[D[i]][X];
        }
    }

    /* ---- statistics */
    static const int TM[6] = {XM1, XM2, XA, XR, XCSH, XCUN};
    static const int TY[4] = {XFR, XU, XO0, XS};
    static const int CM[3] = {XM1, XM2, XA};
    enum { NT = 24, IDL1 = 24, IDL2 = 25, IDEL = 26, NST = 27 };
    i64 *rep = malloc(sizeof(i64) * NST * REPS), *rep2 = malloc(sizeof(i64) * 3 * REPS);
    if (!rep || !rep2)
        die("out of memory", NULL);
    i64 tot[NST], tot2[3];
    i64 SD[NX];
    for (int X = 0; X < NX; X++)
        SD[X] = (i64)sumD[X];
#define TSTAT(S, M, Y) ((S)[Y] - (S)[M] - 1000000 * ((i64)P1[M] - (i64)P1[Y]))
#define DLSTAT(S, M, C) ((S)[C] - (S)[M])
#define DELSTAT(S) ((S)[XM1] - (S)[XM2] - 1000000 * ((i64)P1[XM2] - (i64)P1[XM1]))
    for (int m = 0; m < 6; m++)
        for (int y = 0; y < 4; y++)
            tot[m * 4 + y] = TSTAT(SD, TM[m], TY[y]);
    tot[IDL1] = DLSTAT(SD, XM1, XCSH);
    tot[IDL2] = DLSTAT(SD, XM1, XCUN);
    tot[IDEL] = DELSTAT(SD);
    for (int m = 0; m < 3; m++)
        tot2[m] = TSTAT(SD, CM[m], XFR);

    sm_state = SEED;
    for (size_t i = 0; i < REPS; i++) {
        i64 S[NX] = {0};
        for (size_t k = 0; k < F; k++) {
            size_t j = (size_t)(splitmix64() % F);
            for (int X = 0; X < NX; X++)
                S[X] += bs1[j][X];
        }
        for (int m = 0; m < 6; m++)
            for (int y = 0; y < 4; y++)
                rep[(size_t)(m * 4 + y) * REPS + i] = TSTAT(S, TM[m], TY[y]);
        rep[(size_t)IDL1 * REPS + i] = DLSTAT(S, XM1, XCSH);
        rep[(size_t)IDL2 * REPS + i] = DLSTAT(S, XM1, XCUN);
        rep[(size_t)IDEL * REPS + i] = DELSTAT(S);
    }
    sm_state = SEED;
    for (size_t i = 0; i < REPS; i++) {
        i64 S[NX] = {0};
        for (size_t k = 0; k < F2; k++) {
            size_t j = (size_t)(splitmix64() % F2);
            for (int X = 0; X < NX; X++)
                S[X] += bs2[j][X];
        }
        for (int m = 0; m < 3; m++)
            rep2[(size_t)m * REPS + i] = TSTAT(S, CM[m], XFR);
    }
    stat_t st[NST], st2[3];
    for (int s = 0; s < NST; s++)
        st[s] = finish(tot[s], rep + (size_t)s * REPS);
    for (int m = 0; m < 3; m++)
        st2[m] = finish(tot2[m], rep2 + (size_t)m * REPS);

    /* ---- verdicts */
    u64 nmin[3];
    const char *verd[3];
    int anyq = 0, allno = 1;
    for (int m = 0; m < 3; m++) {
        int M = CM[m];
        nmin[m] = (P1[M] * 1000000 + 2321950 - 1) / 2321950;
        const stat_t *t = &st[m * 4]; /* rows FR U O0 S of candidate m (TM[m] == CM[m]) */
        if (F < 20 || nD < 100) {
            verd[m] = "INCONCLUSIVE(UNDERPOWERED)";
        } else if (t[0].total > 0 && t[0].lb > 0 && t[1].lb > 0 && t[2].lb > 0 && t[3].lb > 0 &&
                   (M == XA || (st[IDL1].lb > 0 && st[IDL2].lb > 0))) {
            verd[m] = "QUALIFIED_IMPROVEMENT";
        } else if (t[0].ub <= 0 && nD < nmin[m]) {
            verd[m] = "NO_IMPROVEMENT(COST_BOUND)";
        } else if (t[0].ub <= 0) {
            verd[m] = "NO_IMPROVEMENT";
        } else {
            verd[m] = "INCONCLUSIVE";
        }
        if (strcmp(verd[m], "QUALIFIED_IMPROVEMENT") == 0)
            anyq = 1;
        if (strncmp(verd[m], "NO_IMPROVEMENT", 14) != 0)
            allno = 0;
    }

    /* ---- report */
    printf("ORIGSEL v1.1\n");
    printf("admit records=%zu admitted=%zu train=%zu validation=%zu heldout=%zu\n", NR, nadm, nsplit[0],
           nsplit[1], nsplit[2]);
    printf("d n=%zu burned=%zu F=%zu fresh=%zu v1fam=%zu p1r=%zu\n", nD, burned, F, nfresh, nfam, nPR);
    for (int X = 0; X < NX; X++) {
        printf("arm %s p1=%llu p2=%llu dl=%llu fresh=%llu v1fam=%llu p1r=%llu rows=", XNAME[X],
               (unsigned long long)P1[X], (unsigned long long)P2[X], (unsigned long long)sumD[X],
               (unsigned long long)sumF[X], (unsigned long long)sumV[X], (unsigned long long)sumP[X]);
        if (X == XU) {
            printf("-\n");
            continue;
        }
        for (int c = 0; c < nctx[X]; c++)
            printf("%s%llu,%llu,%llu,%llu,%llu", c ? ";" : "", (unsigned long long)cnt[X][c][0],
                   (unsigned long long)cnt[X][c][1], (unsigned long long)cnt[X][c][2],
                   (unsigned long long)cnt[X][c][3], (unsigned long long)cnt[X][c][4]);
        printf("\n");
    }
    char head[64];
    for (int m = 0; m < 6; m++)
        for (int y = 0; y < 4; y++) {
            snprintf(head, sizeof head, "t %s %s", XNAME[TM[m]], XNAME[TY[y]]);
            pstat(head, st[m * 4 + y]);
        }
    pstat("dl M1 CSH", st[IDL1]);
    pstat("dl M1 CUN", st[IDL2]);
    pstat("delta M2 M1", st[IDEL]);
    for (int m = 0; m < 3; m++) {
        snprintf(head, sizeof head, "tcf %s FR", XNAME[CM[m]]);
        pstat(head, st2[m]);
    }
    for (int m = 0; m < 3; m++)
        printf("nmin %s %llu\n", XNAME[CM[m]], (unsigned long long)nmin[m]);
    for (int m = 0; m < 3; m++)
        printf("verdict %s %s\n", XNAME[CM[m]], verd[m]);
    printf("experiment %s\n", anyq ? "QUALIFIED_IMPROVEMENT(UNISOLATED)" : allno ? "NO_IMPROVEMENT" : "INCONCLUSIVE");
    if (fflush(stdout) != 0)
        die("write error", NULL);
    return 0;
}
