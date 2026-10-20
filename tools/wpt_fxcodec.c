/*
 * wpt_fxcodec: the FX1 lossless code for FP8 e4m3 weight matrices, encoder + decoder + whole-checkpoint round trip
 * (L274, docs/adr "mathematics before compute", docs/speed/WORK_PER_TOKEN.md "lossless weights").
 *
 *   wpt_fxcodec <file.safetensors>...     encode every F8_E4M3 matrix, decode it again, compare every byte;
 *                                          print bytes before / after per tensor and in total. Exit 1 on any mismatch.
 *
 * Why: the decode GEMV is memory bound, so a weight read is the work. The e4m3 byte is s eeee mmm; sign and mantissa
 * carry ~4 bits of information, the exponent only ~2.6 (wpt_entropy: 6.55 bits per weight in all). Coding the exponent
 * with a per-tensor Huffman code reads fewer bytes and gives back the identical byte, so every product, every sum and
 * every token stays bit-identical.
 *
 * Layout, chosen for the FP8 GEMV's lane mapping (omega_blackwell_codegen_moe.h: per K step lane L of a warp owns
 * bytes [L * 16, +16) of a 512-byte step of each row, lane_bytes 16):
 *   nibble plane   (s << 3 | mmm) of every weight, two per byte, low nibble first, row-major like the bytes:
 *                  n * k / 2 bytes, fixed addressing (lane L, step s reads 8 bytes at row * k/2 + s * 256 + L * 8).
 *   exponent plane per (row, lane) one MSB-first bit stream of the canonical code of that lane's k / 32 exponents in
 *                  consumption order (step 0 bytes 0..15, step 1 ...), padded to a 32-bit word.
 *                  The row region interleaves the 32 lane streams word by word (word j of lane L at j * 32 + L), each
 *                  padded to the row's longest stream LS: a warp reading its j-th words touches one 128-byte line.
 *   offsets        per row: u32 word offset of the region + u16 LS + u16 spare (8 bytes).
 *   table          per tensor: 16 code lengths (length-limited to FX_LMAX = 8 bits, so the decoder is one 256-entry
 *                  lookup: peek 8 bits, read symbol and length, shift).
 * Requirements: k % 512 == 0 (every 27B projection: 5120, 6144, 17408).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define FX_LMAX 8
#define FX_LANES 32
#define FX_LB 16 /* weight bytes per lane per K step */

typedef struct {
    uint8_t len[16];   /* code length per exponent value, 0 = unused */
    uint32_t code[16]; /* canonical code, right-aligned */
    uint8_t lut_sym[1 << FX_LMAX], lut_len[1 << FX_LMAX];
} FxTable;

/* Huffman code lengths for up to 16 symbols, then the JPEG (ITU T.81 Annex K.3) adjustment to at most FX_LMAX. */
static void fx_lengths(const uint64_t *hist, uint8_t *len) {
    int n = 0, sym[16];
    uint64_t w[32];
    int parent[32];
    for (int i = 0; i < 16; i++) { len[i] = 0; if (hist[i]) { sym[n] = i; w[n] = hist[i]; n++; } }
    if (n == 1) { len[sym[0]] = 1; return; }
    int nodes = n, alive[32], na = n;
    for (int i = 0; i < n; i++) alive[i] = i;
    while (na > 1) {
        int a = 0, b = 1;
        if (w[alive[b]] < w[alive[a]]) { a = 1; b = 0; }
        for (int i = 2; i < na; i++) {
            if (w[alive[i]] < w[alive[a]]) { b = a; a = i; } else if (w[alive[i]] < w[alive[b]]) b = i;
        }
        int x = alive[a], y = alive[b];
        w[nodes] = w[x] + w[y];
        parent[x] = nodes; parent[y] = nodes;
        alive[a] = nodes;
        alive[b] = alive[--na];
        nodes++;
    }
    parent[nodes - 1] = -1;
    int bits[64] = {0};
    for (int i = 0; i < n; i++) { int d = 0; for (int v = i; parent[v] >= 0; v = parent[v]) d++; len[sym[i]] = (uint8_t)d; bits[d]++; }
    for (int i = 63; i > FX_LMAX; i--) {
        while (bits[i] > 0) {
            int j = i - 2;
            while (bits[j] == 0) j--;
            bits[i] -= 2; bits[i - 1]++; bits[j + 1] += 2; bits[j]--;
        }
    }
    /* re-assign lengths: most frequent symbols get the shortest codes */
    int ord[16];
    for (int i = 0; i < n; i++) ord[i] = sym[i];
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (hist[ord[j]] > hist[ord[i]]) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int at = 0;
    for (int l = 1; l <= FX_LMAX; l++) for (int c = 0; c < bits[l]; c++) len[ord[at++]] = (uint8_t)l;
}

static void fx_table(FxTable *t, const uint64_t *hist) {
    fx_lengths(hist, t->len);
    uint32_t code = 0;
    for (int l = 1; l <= FX_LMAX; l++) {
        for (int s = 0; s < 16; s++) if (t->len[s] == l) { t->code[s] = code++; }
        code <<= 1;
    }
    memset(t->lut_len, 0, sizeof t->lut_len);
    for (int s = 0; s < 16; s++) {
        int l = t->len[s];
        if (!l) continue;
        uint32_t first = t->code[s] << (FX_LMAX - l), cnt = 1u << (FX_LMAX - l);
        for (uint32_t i = 0; i < cnt; i++) { t->lut_sym[first + i] = (uint8_t)s; t->lut_len[first + i] = (uint8_t)l; }
    }
}

typedef struct {
    char name[160];
    const uint8_t *w;
    uint32_t n, k;
    uint64_t bytes_in, bytes_out, nib, expw, offs;
    int ok;
} Job;

static Job *J;
static int NJ, nextj;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

/* Encode one tensor, decode it, compare. Returns 1 when every byte round-trips. */
static int fx_roundtrip(Job *j) {
    const uint32_t n = j->n, k = j->k, per = k / FX_LANES, steps = k / (FX_LANES * FX_LB);
    uint64_t hist[16] = {0};
    for (uint64_t i = 0; i < (uint64_t)n * k; i++) hist[(j->w[i] >> 3) & 15]++;
    FxTable t;
    fx_table(&t, hist);
    /* worst case per stream: per * FX_LMAX bits */
    const uint32_t maxw = (per * FX_LMAX + 31) / 32;
    uint32_t *words = malloc(sizeof(uint32_t) * maxw * FX_LANES);
    uint8_t *nib = malloc(k / 2);
    uint8_t *back = malloc(k);
    uint32_t *region = malloc(sizeof(uint32_t) * (maxw + 1) * FX_LANES);
    uint64_t expw = 0;
    int ok = 1;
    for (uint32_t r = 0; r < n && ok; r++) {
        const uint8_t *row = j->w + (uint64_t)r * k;
        uint32_t wl[FX_LANES];
        for (uint32_t L = 0; L < FX_LANES; L++) {
            uint32_t *out = words + L * maxw, nw = 0;
            uint64_t acc = 0;
            int nb = 0;
            for (uint32_t s = 0; s < steps; s++)
                for (uint32_t b = 0; b < FX_LB; b++) {
                    uint8_t v = row[s * FX_LANES * FX_LB + L * FX_LB + b];
                    int e = (v >> 3) & 15;
                    acc = (acc << t.len[e]) | t.code[e];
                    nb += t.len[e];
                    if (nb >= 32) { out[nw++] = (uint32_t)(acc >> (nb - 32)); nb -= 32; acc &= (1ull << nb) - 1; }
                }
            if (nb) out[nw++] = (uint32_t)(acc << (32 - nb));
            wl[L] = nw;
            expw += nw;
        }
        for (uint32_t i = 0; i < k; i += 2) nib[i / 2] = (uint8_t)(((row[i] >> 4) & 8) | (row[i] & 7)) | (uint8_t)((((row[i + 1] >> 4) & 8) | (row[i + 1] & 7)) << 4);
        /* The row's exponent region as the GPU stores it: LS = the longest lane stream of the row, word j of lane L at
         * j * 32 + L (a warp reading "its j-th word" touches one 128-byte line). Shorter streams are zero-padded. */
        uint32_t LS = 0;
        for (uint32_t L = 0; L < FX_LANES; L++) if (wl[L] > LS) LS = wl[L];
        expw += (uint64_t)LS * FX_LANES;
        for (uint32_t jw = 0; jw < LS + 1; jw++)
            for (uint32_t L = 0; L < FX_LANES; L++) region[jw * FX_LANES + L] = jw < wl[L] ? words[L * maxw + jw] : 0;
        /* decode exactly as a lane of the kernel does: per group of 4 codes (at most 32 bits), word index i = p >> 5 and
         * bit offset o = p & 31 of the bit position p; window = w[i] << o | w[i + 1] >> (32 - o); then per code: peek the
         * top FX_LMAX bits, look up symbol and length, shift the window left by the length, p += length. */
        for (uint32_t L = 0; L < FX_LANES; L++) {
            uint32_t p = 0;
            for (uint32_t s = 0; s < steps; s++)
                for (uint32_t g = 0; g < FX_LB; g += 4) {
                    uint32_t i = p >> 5, o = p & 31;
                    uint32_t w0 = region[i * FX_LANES + L], w1 = region[(i + 1) * FX_LANES + L];
                    uint32_t win = (w0 << o) | (uint32_t)(((uint64_t)w1 << o) >> 32); /* the IMAD.HI form */
                    for (uint32_t b = g; b < g + 4; b++) {
                        uint32_t peek = win >> (32 - FX_LMAX);
                        int e = t.lut_sym[peek], l = t.lut_len[peek];
                        win <<= l; p += (uint32_t)l;
                        uint32_t c = s * FX_LANES * FX_LB + L * FX_LB + b;
                        uint8_t q = (uint8_t)((nib[c / 2] >> ((c & 1) * 4)) & 15);
                        back[c] = (uint8_t)(((q & 8) << 4) | (e << 3) | (q & 7));
                    }
                }
        }
        if (memcmp(back, row, k) != 0) ok = 0;
    }
    j->nib = (uint64_t)n * k / 2;
    j->expw = expw * 4 + FX_LANES * 4; /* + one word row of tail padding, so the last w[i + 1] load stays inside */
    j->offs = (uint64_t)n * 8 + 16;    /* per row: u32 region word offset, u16 LS, u16 spare; table: 16 code lengths */
    j->bytes_in = (uint64_t)n * k;
    j->bytes_out = j->nib + j->expw + j->offs;
    free(words); free(nib); free(back); free(region);
    return ok;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        int i = nextj++;
        pthread_mutex_unlock(&mu);
        if (i >= NJ) return NULL;
        J[i].ok = fx_roundtrip(&J[i]);
    }
}

static int scan(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    struct stat st;
    fstat(fd, &st);
    const uint8_t *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return -1;
    uint64_t hn;
    memcpy(&hn, m, 8);
    const char *h = (const char *)m + 8, *end = h + hn, *s = h;
    while (s < end) {
        const char *k = memchr(s, '"', (size_t)(end - s));
        if (!k) break;
        const char *ke = memchr(k + 1, '"', (size_t)(end - k - 1));
        if (!ke) break;
        if (ke[1] != ':' || ke[2] != '{') { s = ke + 1; continue; }
        const char *oe = memchr(ke, '}', (size_t)(end - ke));
        if (!oe) break;
        const char *dt = strstr(ke, "\"dtype\":\"F8_E4M3\""), *sh = strstr(ke, "\"shape\":["), *of = strstr(ke, "\"data_offsets\":[");
        unsigned long long a, b, n, kk;
        if (dt && dt < oe && sh && sh < oe && of && of < oe && sscanf(sh + 9, "%llu,%llu]", &n, &kk) == 2 &&
            sscanf(of + 16, "%llu,%llu", &a, &b) == 2 && n * kk == b - a) {
            if (kk % (FX_LANES * FX_LB) != 0) { fprintf(stderr, "skip %.*s: k %llu %% 512\n", (int)(ke - k - 1), k + 1, kk); }
            else {
                J = realloc(J, sizeof(Job) * (size_t)(NJ + 1));
                Job *j = &J[NJ++];
                memset(j, 0, sizeof *j);
                size_t L = (size_t)(ke - k - 1) < sizeof j->name - 1 ? (size_t)(ke - k - 1) : sizeof j->name - 1;
                memcpy(j->name, k + 1, L);
                j->w = m + 8 + hn + a;
                j->n = (uint32_t)n;
                j->k = (uint32_t)kk;
            }
        }
        s = oe + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: wpt_fxcodec <file.safetensors>...\n"); return 2; }
    for (int i = 1; i < argc; i++) if (scan(argv[i]) != 0) return 1;
    int nth = getenv("AIEN_THREADS") ? atoi(getenv("AIEN_THREADS")) : 8;
    if (nth < 1 || nth > 64) nth = 8;
    pthread_t th[64];
    for (int i = 0; i < nth; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);
    uint64_t in = 0, out = 0, nib = 0, ex = 0, of = 0;
    int bad = 0;
    printf("ok\tn\tk\tbytes_in\tbytes_out\tbits_per_weight\tname\n");
    for (int i = 0; i < NJ; i++) {
        Job *j = &J[i];
        printf("%s\t%u\t%u\t%llu\t%llu\t%.4f\t%s\n", j->ok ? "ok" : "MISMATCH", j->n, j->k, (unsigned long long)j->bytes_in,
               (unsigned long long)j->bytes_out, 8.0 * (double)j->bytes_out / (double)j->bytes_in, j->name);
        in += j->bytes_in; out += j->bytes_out; nib += j->nib; ex += j->expw; of += j->offs;
        if (!j->ok) bad++;
    }
    printf("TOTAL tensors %d mismatches %d bytes_in %llu bytes_out %llu (nibbles %llu exponent %llu offsets+tables %llu) "
           "bits_per_weight %.4f\n", NJ, bad, (unsigned long long)in, (unsigned long long)out, (unsigned long long)nib,
           (unsigned long long)ex, (unsigned long long)of, 8.0 * (double)out / (double)in);
    return bad ? 1 : 0;
}
