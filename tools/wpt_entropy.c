/*
 * wpt_entropy: how many bits per weight a LOSSLESS code needs for a checkpoint's FP8 and bf16 tensors (L274,
 * docs/speed/WORK_PER_TOKEN.md). Lossless means the decoder gives back the exact stored bytes, so the matmul inputs,
 * the arithmetic and the tokens are bit-identical; only the bytes read from DRAM change.
 *
 *   wpt_entropy <file.safetensors>...        one line per tensor + a total line, all on stdout
 *
 * Per tensor:
 *   h0     order-0 Shannon entropy of the stored byte (FP8 e4m3) or of the bf16 exponent byte, bits per weight:
 *          the floor for any code that maps one value to one codeword with a per-tensor table (Huffman lands within
 *          1 bit of it, usually much closer).
 *   blk    a fixed-width block code that a GEMV can unpack with shifts and adds, no table walk: per block of 32 weights
 *          the exponent field is stored as (e - e_min) in ceil(log2(e_max - e_min + 1)) bits, sign and mantissa raw,
 *          plus a 4-bit e_min and a 3-bit width per block. Exact: every block decodes to its stored bytes.
 * FP8 e4m3: s eeee mmm (exponent 0 = subnormal, its own value). bf16: s eeeeeeee mmmmmmm (h0 is sign + mantissa raw
 * 8 bits + exponent entropy, the DFloat11 view).
 * Reads with mmap; tensors run in parallel (pthreads, AIEN_THREADS, default 8).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BLK 32

typedef struct {
    char name[160];
    int kind;                 /* 1 = F8_E4M3, 2 = BF16 */
    const uint8_t *p;
    uint64_t n;               /* elements */
    double h0, blk, huff, esc2, esc3; /* bits per weight */
    uint64_t zeros;           /* exact zero values (+0 and -0) */
} Tensor;

static Tensor *T;
static int NT, next_t;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static double entropy(const uint64_t *hist, int k, uint64_t n) {
    double h = 0;
    for (int i = 0; i < k; i++)
        if (hist[i]) { double q = (double)hist[i] / (double)n; h -= q * log2(q); }
    return h;
}

static int width_of(int range) { int w = 0; while ((1 << w) < range + 1) w++; return w; }

static int cmp_u64_desc(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x < y) - (x > y);
}

/* Exponent-only codes (sign and mantissa stay raw, `raw` bits): exact Huffman over the per-tensor exponent histogram
 * (`k` symbols), and "top-3 / top-7 + escape": a 2- or 3-bit index into the tensor's most frequent exponents, the last
 * index value meaning "the exponent follows raw" (`ebits` more bits). */
static void exp_codes(Tensor *t, const uint64_t *eh, int k, int raw, int ebits) {
    uint64_t w[256], s[256];
    int n = 0;
    uint64_t tot = 0;
    for (int i = 0; i < k; i++) { s[i] = eh[i]; tot += eh[i]; if (eh[i]) w[n++] = eh[i]; }
    uint64_t cost = 0; /* Huffman: sum of merged weights = total code length */
    while (n > 1) {
        int a = 0, b = 1;
        if (w[b] < w[a]) { a = 1; b = 0; }
        for (int i = 2; i < n; i++) {
            if (w[i] < w[a]) { b = a; a = i; } else if (w[i] < w[b]) b = i;
        }
        uint64_t m = w[a] + w[b];
        cost += m;
        w[a] = m;
        w[b] = w[--n];
    }
    t->huff = raw + (double)cost / (double)tot;
    qsort(s, (size_t)k, sizeof(uint64_t), cmp_u64_desc);
    uint64_t top3 = s[0] + s[1] + s[2], top7 = top3 + s[3] + s[4] + s[5] + s[6];
    t->esc2 = raw + 2 + ebits * (double)(tot - top3) / (double)tot;
    t->esc3 = raw + 3 + ebits * (double)(tot - top7) / (double)tot;
}

static void do_tensor(Tensor *t) {
    static const int hdr = 4 + 3; /* e_min + width; the bf16 code below spends 8 + 4 */
    if (t->kind == 1) {
        uint64_t hist[256] = {0}, bits = 0, z = 0;
        for (uint64_t b = 0; b < t->n; b += BLK) {
            uint64_t e = b + BLK < t->n ? b + BLK : t->n;
            int lo = 15, hi = 0;
            for (uint64_t i = b; i < e; i++) {
                uint8_t v = t->p[i];
                hist[v]++;
                if ((v & 0x7f) == 0) z++;
                int ex = (v >> 3) & 15;
                if (ex < lo) lo = ex;
                if (ex > hi) hi = ex;
            }
            bits += (uint64_t)hdr + (e - b) * (uint64_t)(1 + 3 + width_of(hi - lo));
        }
        t->h0 = entropy(hist, 256, t->n);
        t->blk = (double)bits / (double)t->n;
        t->zeros = z;
        uint64_t eh[16] = {0};
        for (int v = 0; v < 256; v++) eh[(v >> 3) & 15] += hist[v];
        exp_codes(t, eh, 16, 4, 4);
    } else {
        uint64_t hist[256] = {0}, bits = 0, z = 0;
        const uint16_t *q = (const uint16_t *)t->p;
        for (uint64_t b = 0; b < t->n; b += BLK) {
            uint64_t e = b + BLK < t->n ? b + BLK : t->n;
            int lo = 255, hi = 0;
            for (uint64_t i = b; i < e; i++) {
                uint16_t v = q[i];
                int ex = (v >> 7) & 255;
                hist[ex]++;
                if ((v & 0x7fff) == 0) z++;
                if (ex < lo) lo = ex;
                if (ex > hi) hi = ex;
            }
            bits += (uint64_t)(8 + 4) + (e - b) * (uint64_t)(1 + 7 + width_of(hi - lo));
        }
        t->h0 = 8.0 + entropy(hist, 256, t->n);
        t->blk = (double)bits / (double)t->n;
        t->zeros = z;
        exp_codes(t, hist, 256, 8, 8);
    }
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        int i = next_t++;
        pthread_mutex_unlock(&mu);
        if (i >= NT) return NULL;
        do_tensor(&T[i]);
    }
}

/* Minimal safetensors header walk: "name":{"dtype":"X","shape":[...],"data_offsets":[a,b]} */
static int scan_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    struct stat st;
    if (fstat(fd, &st) != 0) { perror(path); close(fd); return -1; }
    const uint8_t *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { perror("mmap"); return -1; }
    uint64_t hn;
    memcpy(&hn, m, 8);
    const char *h = (const char *)m + 8, *end = h + hn;
    const uint8_t *data = m + 8 + hn;
    const char *s = h;
    while (s < end) {
        const char *k = memchr(s, '"', (size_t)(end - s));
        if (!k) break;
        const char *ke = memchr(k + 1, '"', (size_t)(end - k - 1));
        if (!ke) break;
        if (ke[1] != ':' || ke[2] != '{') { s = ke + 1; continue; }
        const char *obj_end = memchr(ke, '}', (size_t)(end - ke));
        if (!obj_end) break;
        const char *dt = strstr(ke, "\"dtype\":\"");
        const char *off = strstr(ke, "\"data_offsets\":[");
        int kind = 0;
        if (dt && dt < obj_end) {
            dt += 9;
            if (!strncmp(dt, "F8_E4M3", 7)) kind = 1;
            else if (!strncmp(dt, "BF16", 4)) kind = 2;
        }
        if (kind && off && off < obj_end) {
            unsigned long long a, b;
            if (sscanf(off + 16, "%llu,%llu", &a, &b) == 2) {
                uint64_t n = (b - a) / (kind == 1 ? 1 : 2);
                if (n >= 4096) { /* norms and other small vectors are not on the byte budget */
                    T = realloc(T, sizeof(Tensor) * (size_t)(NT + 1));
                    Tensor *t = &T[NT++];
                    memset(t, 0, sizeof *t);
                    size_t L = (size_t)(ke - k - 1) < sizeof t->name - 1 ? (size_t)(ke - k - 1) : sizeof t->name - 1;
                    memcpy(t->name, k + 1, L);
                    t->kind = kind;
                    t->p = data + a;
                    t->n = n;
                }
            }
        }
        s = obj_end + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: wpt_entropy <file.safetensors>...\n"); return 2; }
    for (int i = 1; i < argc; i++)
        if (scan_file(argv[i]) != 0) return 1;
    int nth = getenv("AIEN_THREADS") ? atoi(getenv("AIEN_THREADS")) : 8;
    if (nth < 1) nth = 1;
    pthread_t th[64];
    if (nth > 64) nth = 64;
    for (int i = 0; i < nth; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);
    double w[3] = {0}, h0b[3] = {0}, blkb[3] = {0}, hfb[3] = {0}, e2b[3] = {0}, e3b[3] = {0};
    uint64_t zr[3] = {0};
    printf("kind\tweights\th0_bits\tblk_bits\tzeros\tname\thuff_bits\tesc2_bits\tesc3_bits\n");
    for (int i = 0; i < NT; i++) {
        Tensor *t = &T[i];
        printf("%s\t%llu\t%.4f\t%.4f\t%llu\t%s\t%.4f\t%.4f\t%.4f\n", t->kind == 1 ? "fp8" : "bf16", (unsigned long long)t->n,
               t->h0, t->blk, (unsigned long long)t->zeros, t->name, t->huff, t->esc2, t->esc3);
        double n = (double)t->n;
        w[t->kind] += n;
        h0b[t->kind] += t->h0 * n;
        blkb[t->kind] += t->blk * n;
        hfb[t->kind] += t->huff * n;
        e2b[t->kind] += t->esc2 * n;
        e3b[t->kind] += t->esc3 * n;
        zr[t->kind] += t->zeros;
    }
    for (int k = 1; k <= 2; k++)
        if (w[k] > 0)
            printf("TOTAL %s weights %.0f stored_bits %d h0_bits %.4f blk_bits %.4f huff_bits %.4f esc2_bits %.4f esc3_bits %.4f "
                   "zeros %llu\n", k == 1 ? "fp8" : "bf16", w[k], k == 1 ? 8 : 16, h0b[k] / w[k], blkb[k] / w[k], hfb[k] / w[k],
                   e2b[k] / w[k], e3b[k] / w[k], (unsigned long long)zr[k]);
    return 0;
}
