/* gpu_fx1_test: FX1 lossless FP8 weights (L274, src/omega_gpu_fx1_api.h).
 *   ./gpu_fx1_test --host-only     encoder/decoder round trip on synthetic tensors, plus a corrupted stream that must fail
 *   ./gpu_fx1_test --sim           the FX1 kernel on tests/bw_warp_sim.h against the raw FP8 kernel (moe, lane_bytes 16):
 *                                  y must be bit-identical; mutants 1 and 2 must differ
 *   ./gpu_fx1_test --chip FILE.safetensors [max_tensors [reps]]
 *                                  real checkpoint tensors on the chip: y of omega_gpu_fx1_gemv_f32 bit-identical to
 *                                  omega_gpu_fp8_gemv_f32 (lane_bytes 16, 1 row, 8 warps: the live graph's shape), median
 *                                  chip time of each, mutants caught on the first tensor. Run under quiet-measure and
 *                                  flock /tmp/aien-gb10.lock for published numbers.
 * Exit 0 only when every check passes. */
#include "bw_warp_sim.h"
#include "omega_blackwell_codegen_fx1.h"
#include "omega_blackwell_codegen_moe.h"
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_fx1_api.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s) { return ((float)(lcg(s) >> 8) + 0.5f) / 16777216.0f; }
static float gauss(uint32_t *s) { return sqrtf(-2.0f * logf(frand(s))) * cosf(6.2831853f * frand(s)); }
/* e4m3 byte with a checkpoint-like exponent spread (most weights near one scale, a long thin tail); never NaN */
static uint8_t synth_e4m3(uint32_t *s) {
    float v = fabsf(gauss(s)) * 24.0f;
    int e = v < 1e-9f ? 0 : (int)floorf(log2f(v)) + 7;
    if (e < 0) e = 0;
    if (e > 15) e = 15;
    uint32_t m = lcg(s) >> 29, sg = lcg(s) >> 31;
    if (e == 15 && m == 7) m = 6;
    return (uint8_t)((sg << 7) | ((uint32_t)e << 3) | m);
}
static uint16_t bf16_of(float f) { uint32_t u; memcpy(&u, &f, 4); return (uint16_t)((u + 0x7fffu + ((u >> 16) & 1u)) >> 16); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

/* ---------------------------------------------------------------- host */

static void host_only(void) {
    static const uint32_t shapes[][2] = { { 1, 512 }, { 128, 1024 }, { 256, 5120 }, { 64, 17408 } };
    for (unsigned t = 0; t < sizeof shapes / sizeof shapes[0]; t++) {
        uint32_t n = shapes[t][0], k = shapes[t][1], seed = 7u + t;
        uint8_t *w = malloc((size_t)n * k);
        for (size_t i = 0; i < (size_t)n * k; i++) w[i] = synth_e4m3(&seed);
        OmegaFx1Host h;
        int rc = omega_fx1_encode(n, k, w, &h);
        CHECK(rc == 0, "encode %ux%u rc %d", n, k, rc);
        size_t bad = 0;
        CHECK(rc == 0 && omega_fx1_decode_check(&h, w, &bad) == 0, "round trip %ux%u first bad %zu", n, k, bad);
        double bpw = 8.0 * (double)h.coded_bytes / ((double)n * k);
        printf("host %ux%u: coded %llu B of %llu raw (%.4f bits per weight)\n", n, k, (unsigned long long)h.coded_bytes,
               (unsigned long long)n * k, bpw);
        /* the per-row stream padding dominates a single 512-wide row; the size claim is for real tensor widths */
        if (k >= 1024) CHECK(bpw < 8.0, "%ux%u codes no smaller than raw", n, k);
        /* corrupt one exponent word in row 0: the decode must notice */
        if (rc == 0) { h.exp[h.rows[0]] ^= 0x80000000u; CHECK(omega_fx1_decode_check(&h, w, NULL) != 0, "corrupted stream %ux%u passed", n, k); }
        omega_fx1_host_free(&h);
        free(w);
    }
    OmegaFx1Host h;
    uint8_t one[512] = {0};
    CHECK(omega_fx1_encode(1, 256, one, &h) != 0, "k 256 accepted (needs a multiple of 512)");
    CHECK(omega_fx1_encode(1, 512, one, &h) == 0 && omega_fx1_decode_check(&h, one, NULL) == 0, "all-zero row (one symbol)");
    omega_fx1_host_free(&h);
}

/* ---------------------------------------------------------------- sim */

static void *al(size_t bytes) { return aligned_alloc(64, (bytes + 63) & ~(size_t)63); }

/* raw FP8 moe kernel, lane_bytes 16, depth 0, one group: the reference y */
static int sim_raw(uint32_t n, uint32_t k, uint32_t rows, uint32_t warps, uint32_t ctas, const uint8_t *w, const float *sf,
                   const float *xf, float *y, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    OmegaFp8GemvSpec spec; memset(&spec, 0, sizeof spec);
    spec.k = k; spec.lane_bytes = 16; spec.rows = rows; spec.warps = warps;
    if (omega_blackwell_codegen_fp8_gemv_prog(&spec, &prog) != 0) { snprintf(msg, ml, "raw codegen refused"); return -1; }
    uint64_t *tab = al(16);
    tab[0] = (uintptr_t)w; tab[1] = (uintptr_t)sf;
    static uint8_t smem[1 << 17];
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32 * warps; s.smem = smem; s.smem_bytes = omega_fp8_gemv_shared_bytes(&spec);
    uint64_t ta = (uintptr_t)tab, xa = (uintptr_t)xf, ya = (uintptr_t)y;
    s.cbank[OMEGA_FP8_ARG_TAB / 4] = (uint32_t)ta; s.cbank[OMEGA_FP8_ARG_TAB / 4 + 1] = (uint32_t)(ta >> 32);
    s.cbank[OMEGA_FP8_ARG_X / 4] = (uint32_t)xa; s.cbank[OMEGA_FP8_ARG_X / 4 + 1] = (uint32_t)(xa >> 32);
    s.cbank[OMEGA_FP8_ARG_Y / 4] = (uint32_t)ya; s.cbank[OMEGA_FP8_ARG_Y / 4 + 1] = (uint32_t)(ya >> 32);
    s.cbank[OMEGA_FP8_ARG_RG / 4] = n / rows; s.cbank[OMEGA_FP8_ARG_XSTRIDE / 4] = 0;
    s.cbank[OMEGA_FP8_ARG_YSTRIDE / 4] = n * 4; s.cbank[OMEGA_FP8_ARG_STRIDE / 4] = ctas * warps;
    for (uint32_t i = 0; i < n; i++) y[i] = -999.0f;
    int err = ws_run(&s, ctas, 1);
    if (err) snprintf(msg, ml, "raw: %s", s.msg);
    free(tab);
    return err ? -1 : 0;
}

static uint64_t g_issues_raw, g_issues_fx;

static int sim_fx1(const OmegaFx1Host *h, uint32_t rows, uint32_t warps, uint32_t ctas, int mutant, const float *sf, size_t scount,
                   const float *xf, float *y, int order, char *msg, size_t ml) {
    static BlackwellIRProgram prog;
    const uint32_t n = h->n, k = h->k;
    OmegaFx1GemvSpec spec = { .k = k, .rows = rows, .warps = warps, .mutant = mutant };
    if (omega_blackwell_codegen_fx1_gemv_prog(&spec, &prog) != 0) { snprintf(msg, ml, "fx1 codegen refused"); return -1; }
    const uint32_t lw = omega_fx1_lut_words(warps);
    uint32_t *lut = al(lw * 4u); memset(lut, 0, lw * 4u); memcpy(lut, h->lut, 1024);
    static uint8_t smem[1 << 17];
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 32 * warps; s.smem = smem; s.smem_bytes = omega_fx1_shared_bytes(&spec); s.order = order;
    const uint64_t va[7] = { (uintptr_t)h->nib, (uintptr_t)h->exp, (uintptr_t)h->rows, (uintptr_t)lut, (uintptr_t)sf, (uintptr_t)xf, (uintptr_t)y };
    const size_t len[7] = { (size_t)n * k / 2, h->exp_words * 4, (size_t)n * 4, lw * 4u, scount * 4, (size_t)k * 4, (size_t)n * 4 };
    for (int i = 0; i < 7; i++) {
        s.cbank[OMEGA_FX1_ARG_NIB / 4 + 2 * i] = (uint32_t)va[i]; s.cbank[OMEGA_FX1_ARG_NIB / 4 + 2 * i + 1] = (uint32_t)(va[i] >> 32);
        s.ranges[i].lo = (uintptr_t)va[i]; s.ranges[i].hi = (uintptr_t)va[i] + len[i];
    }
    s.nranges = 7;
    s.cbank[OMEGA_FX1_ARG_RG / 4] = n / rows; s.cbank[OMEGA_FX1_ARG_STRIDE / 4] = ctas * warps;
    for (uint32_t i = 0; i < n; i++) y[i] = -999.0f;
    int err = ws_run(&s, ctas, 1);
    if (err) snprintf(msg, ml, "fx1: %s", s.msg);
    g_issues_fx += s.warp_issues;
    free(lut);
    return err ? -1 : 0;
}

static void sim_battery(void) {
    typedef struct { uint32_t n, k, rows, warps, ctas; } P;
    static const P pts[] = { { 128, 512, 1, 4, 2 }, { 128, 1024, 2, 4, 3 }, { 256, 1024, 1, 8, 4 }, { 256, 2048, 2, 8, 2 }, { 64, 5120, 1, 2, 5 } };
    for (unsigned t = 0; t < sizeof pts / sizeof pts[0]; t++) {
        const P p = pts[t];
        uint32_t seed = 99u + t, n = p.n, k = p.k;
        uint8_t *w = al((size_t)n * k);
        for (size_t i = 0; i < (size_t)n * k; i++) w[i] = synth_e4m3(&seed);
        const size_t scount = (size_t)(n / 128 ? n / 128 : 1) * (k / 128);
        uint16_t *sb = malloc(scount * 2);
        for (size_t i = 0; i < scount; i++) sb[i] = bf16_of(0.001f + 0.01f * frand(&seed));
        float *sf = al(scount * 4), *x = malloc((size_t)k * 4), *xf = al((size_t)k * 4);
        omega_fp8_pack_scales(sb, scount, sf, 0);
        for (uint32_t i = 0; i < k; i++) x[i] = gauss(&seed);
        omega_fp8_pack_x_f32(x, k, xf);
        float *yr = al((size_t)n * 4), *yf = al((size_t)n * 4), *y1 = al((size_t)n * 4);
        char msg[256] = "";
        OmegaFx1Host h;
        int ok = omega_fx1_encode(n, k, w, &h) == 0;
        CHECK(ok, "sim encode %ux%u", n, k);
        if (!ok) continue;
        int rr = sim_raw(n, k, p.rows, p.warps, p.ctas, w, sf, xf, yr, msg, sizeof msg);
        CHECK(rr == 0, "sim raw %ux%u: %s", n, k, msg);
        for (int order = 0; order < 2 && rr == 0; order++) {
            int rf = sim_fx1(&h, p.rows, p.warps, p.ctas, 0, sf, scount, xf, order ? y1 : yf, order, msg, sizeof msg);
            CHECK(rf == 0, "sim fx1 %ux%u R%u W%u order %d: %s", n, k, p.rows, p.warps, order, msg);
            if (rf) break;
            size_t diff = 0, first = 0;
            for (uint32_t i = 0; i < n; i++) if (memcmp(&(order ? y1 : yf)[i], &yr[i], 4) != 0) { if (!diff) first = i; diff++; }
            CHECK(diff == 0, "sim %ux%u R%u W%u order %d: %zu of %u outputs differ from raw (first %zu: %a vs %a)", n, k, p.rows,
                  p.warps, order, diff, n, first, (double)(order ? y1 : yf)[first], (double)yr[first]);
        }
        for (int m = 1; m <= 2 && rr == 0; m++) {
            int rf = sim_fx1(&h, p.rows, p.warps, p.ctas, m, sf, scount, xf, y1, 0, msg, sizeof msg);
            size_t diff = 0;
            for (uint32_t i = 0; rf == 0 && i < n; i++) diff += memcmp(&y1[i], &yr[i], 4) != 0;
            CHECK(rf != 0 || diff > 0, "sim mutant %d not caught on %ux%u", m, n, k);
        }
        printf("sim %ux%u R%u W%u ctas %u: %s\n", n, k, p.rows, p.warps, p.ctas, g_failed ? "see failures" : "bit-identical to raw, mutants caught");
        omega_fx1_host_free(&h);
        free(w); free(sb); free(sf); free(x); free(xf); free(yr); free(yf); free(y1);
    }
    (void)g_issues_raw;
}

/* ---------------------------------------------------------------- chip */

typedef struct { char name[160]; uint32_t n, k; const uint8_t *w; const uint16_t *s; } Tensor;

static const uint8_t *g_map; static uint64_t g_hn;
/* finds "name":{...} in the safetensors header; returns the object span */
static int find_tensor(const char *name, const char **dtype, uint64_t *n, uint64_t *k, uint64_t *a, uint64_t *b) {
    char key[200]; snprintf(key, sizeof key, "\"%s\":{", name);
    const char *h = (const char *)g_map + 8, *p = memmem(h, g_hn, key, strlen(key));
    if (!p) return -1;
    const char *oe = memchr(p, '}', g_hn - (size_t)(p - h));
    const char *dt = strstr(p, "\"dtype\":\""), *sh = strstr(p, "\"shape\":["), *of = strstr(p, "\"data_offsets\":[");
    unsigned long long nn, kk, aa, bb;
    if (!oe || !dt || dt > oe || !sh || sh > oe || !of || of > oe) return -1;
    if (sscanf(sh + 9, "%llu,%llu]", &nn, &kk) != 2 || sscanf(of + 16, "%llu,%llu", &aa, &bb) != 2) return -1;
    *dtype = dt + 9; *n = nn; *k = kk; *a = aa; *b = bb;
    return 0;
}

static int list_tensors(Tensor *out, int max) {
    const char *h = (const char *)g_map + 8, *end = h + g_hn, *s = h;
    int cnt = 0;
    while (s < end && cnt < max) {
        const char *q = memmem(s, (size_t)(end - s), ".weight\":{\"dtype\":\"F8_E4M3\"", 27);
        if (!q) break;
        const char *k0 = q; while (k0 > h && k0[-1] != '"') k0--;
        Tensor *t = &out[cnt];
        size_t L = (size_t)(q + 7 - k0); if (L >= sizeof t->name) L = sizeof t->name - 1;
        memcpy(t->name, k0, L); t->name[L] = 0;
        const char *dt; uint64_t n, k, a, b, sn, sk, sa, sbb;
        char sname[200]; snprintf(sname, sizeof sname, "%s_scale_inv", t->name);
        if (find_tensor(t->name, &dt, &n, &k, &a, &b) == 0 && n * k == b - a && k % 512 == 0 && n % 128 == 0 &&
            find_tensor(sname, &dt, &sn, &sk, &sa, &sbb) == 0 && strncmp(dt, "BF16", 4) == 0 && sn == n / 128 && sk == k / 128) {
            t->n = (uint32_t)n; t->k = (uint32_t)k; t->w = g_map + 8 + g_hn + a; t->s = (const uint16_t *)(g_map + 8 + g_hn + sa);
            cnt++;
        }
        s = q + 27;
    }
    return cnt;
}

static uint64_t median(uint64_t *v, int n) { qsort(v, (size_t)n, sizeof *v, cmp_u64); return v[n / 2]; }

static int chip_run(const char *path, int max_tensors, int reps) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return 1; }
    struct stat st; fstat(fd, &st);
    g_map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (g_map == MAP_FAILED) return 1;
    memcpy(&g_hn, g_map, 8);
    Tensor *ts = calloc(4096, sizeof *ts);
    int nt = list_tensors(ts, max_tensors < 4096 ? max_tensors : 4096);
    printf("chip: %d FP8 tensors from %s\n", nt, path);
    if (nt == 0) return 1;
    omega_gpu_fp8_set_variant(1);
    omega_gpu_fp8_set_shape(16, 1, 8);      /* the live graph's shape for every Qwen3.8-27B projection (k % 512 == 0) */
    omega_gpu_fp8_set_weight_cache(1);
    {   /* shape sweep for the timing: FX1_ROWS (1|2 rows interleaved per warp) and FX1_WARPS; default 1 row, 8 warps */
        const char *er = getenv("FX1_ROWS"), *ew = getenv("FX1_WARPS");
        uint32_t rows = er ? (uint32_t)atoi(er) : 1u, warps = ew ? (uint32_t)atoi(ew) : 8u;
        omega_gpu_fx1_set_shape(rows, warps, 0);
        printf("fx1 shape rows %u warps %u\n", rows, warps);
    }
    printf("tensor\tn\tk\traw_bytes\tfx1_bytes\tbits_per_weight\traw_us\tfx1_us\tfx1_over_raw\tidentical\n");
    uint64_t sum_raw_b = 0, sum_fx_b = 0, sum_raw_ns = 0, sum_fx_ns = 0;
    uint32_t seed = 12345;
    for (int i = 0; i < nt; i++) {
        Tensor *t = &ts[i];
        OmegaFx1Host h;
        if (omega_fx1_encode(t->n, t->k, t->w, &h) != 0) { CHECK(0, "%s encode", t->name); continue; }
        CHECK(omega_fx1_decode_check(&h, t->w, NULL) == 0, "%s host round trip", t->name);
        OmegaGpuFp8Tensor *rt = NULL; OmegaGpuFx1Tensor *ft = NULL;
        int r1 = omega_gpu_fp8_tensor_upload(1, t->n, t->k, t->w, t->s, &rt), r2 = omega_gpu_fx1_tensor_upload(&h, t->s, &ft);
        if (r1 == OMEGA_GPU_FP8_TOO_LARGE && r2 == 0) { /* the raw GEMV refuses k past OMEGA_FP8_MAX_K: nothing to compare against, say so */
            printf("SKIP %s: raw path refuses k=%u (TOO_LARGE), fx1 upload ok\n", t->name, t->k);
            omega_gpu_fx1_tensor_free(ft); omega_fx1_host_free(&h); continue;
        }
        CHECK(r1 == 0 && r2 == 0, "%s upload raw %d fx1 %d (%s)", t->name, r1, r2, omega_gpu_fp8_last_error());
        if (r1 || r2) { omega_gpu_fp8_tensor_free(rt); omega_gpu_fx1_tensor_free(ft); omega_fx1_host_free(&h); continue; }
        float *x = malloc((size_t)t->k * 4), *yr = malloc((size_t)t->n * 4), *yf = malloc((size_t)t->n * 4);
        for (uint32_t j = 0; j < t->k; j++) x[j] = gauss(&seed);
        uint64_t *tr = calloc((size_t)reps, 8), *tf = calloc((size_t)reps, 8);
        int ok = 1;
        for (int r = 0; r < reps && ok; r++) {
            OmegaGpuFp8Info ir; OmegaGpuFx1Info inf;
            int a = omega_gpu_fp8_gemv_f32(rt, 0, x, yr, &ir), b = omega_gpu_fx1_gemv_f32(ft, x, yf, &inf);
            if (a || b) { CHECK(0, "%s gemv raw %d fx1 %d (%s)", t->name, a, b, omega_gpu_fp8_last_error()); ok = 0; break; }
            tr[r] = ir.elapsed_ns; tf[r] = inf.elapsed_ns;
            size_t diff = 0;
            for (uint32_t j = 0; j < t->n; j++) diff += memcmp(&yr[j], &yf[j], 4) != 0;
            if (diff) { CHECK(0, "%s rep %d: %zu of %u outputs differ", t->name, r, diff, t->n); ok = 0; }
        }
        if (ok) {
            uint64_t mr = median(tr, reps), mf = median(tf, reps);
            sum_raw_b += (uint64_t)t->n * t->k; sum_fx_b += h.coded_bytes; sum_raw_ns += mr; sum_fx_ns += mf;
            printf("%s\t%u\t%u\t%llu\t%llu\t%.4f\t%.1f\t%.1f\t%.4f\tyes\n", t->name, t->n, t->k, (unsigned long long)t->n * t->k,
                   (unsigned long long)h.coded_bytes, 8.0 * (double)h.coded_bytes / ((double)t->n * t->k), mr / 1e3, mf / 1e3, (double)mf / (double)mr);
        }
        if (i == 0 && ok) {
            for (int m = 1; m <= 2; m++) {
                omega_gpu_fx1_test_set_mutant(m);
                int b = omega_gpu_fx1_gemv_f32(ft, x, yf, NULL);
                size_t diff = 0;
                for (uint32_t j = 0; b == 0 && j < t->n; j++) diff += memcmp(&yr[j], &yf[j], 4) != 0;
                CHECK(b != 0 || diff > 0, "chip mutant %d not caught on %s", m, t->name);
                printf("chip mutant %d on %s: rc %d, %zu outputs differ (must be caught)\n", m, t->name, b, diff);
            }
            omega_gpu_fx1_test_set_mutant(0);
        }
        free(x); free(yr); free(yf); free(tr); free(tf);
        omega_gpu_fp8_tensor_free(rt); omega_gpu_fx1_tensor_free(ft); omega_fx1_host_free(&h);
    }
    if (sum_raw_b)
        printf("TOTAL\traw_bytes %llu\tfx1_bytes %llu\tbits_per_weight %.4f\traw_us %.1f\tfx1_us %.1f\tfx1_over_raw %.4f\n",
               (unsigned long long)sum_raw_b, (unsigned long long)sum_fx_b, 8.0 * (double)sum_fx_b / (double)sum_raw_b,
               sum_raw_ns / 1e3, sum_fx_ns / 1e3, (double)sum_fx_ns / (double)sum_raw_ns);
    free(ts);
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0;
    const char *chip = NULL; int max_t = 8, reps = 9;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--chip") == 0 && i + 1 < argc) {
            chip = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') max_t = atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-') reps = atoi(argv[++i]);
        } else { printf("usage: %s --host-only | --sim | --chip FILE.safetensors [max_tensors [reps]]\n", argv[0]); return 2; }
    }
    if (!host && !sim && !chip) host = sim = 1;
    if (host) host_only();
    if (sim) sim_battery();
    if (chip) CHECK(chip_run(chip, max_t, reps > 0 ? reps : 1) == 0, "chip run did not complete on %s", chip);
    if (g_checks == 0) { printf("0 checks ran: that is a failure, not a pass\n"); return 1; }
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
