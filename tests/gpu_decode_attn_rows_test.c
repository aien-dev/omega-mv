/* W-L4d chip check: omega_gpu_decode_attn_rows (two launches for `rows` new tokens) against omega_gpu_decode_attn
 * called once per row in order. Bit-for-bit on the output and on the K/V each row appends to the cache.
 * Negative control: the rows call run with TEST mutant 2 (V one position late) must differ, or the comparison
 * cannot say no. Chip only; run under flock /tmp/aien-gb10.lock. Prints one line per case and a final verdict. */
#include "omega_gpu_decode_attn_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
#define MAXP 2048u
#define W OMEGA_GPU_DATTN_QKV_WORDS

/* history 0..pos0-1 random; positions pos0.. poisoned with a fixed pattern so the call under test must write them */
static int reset_cache(OmegaGpuDecodeAttnCache *c, uint32_t pos0, uint32_t rows, uint32_t seed) {
    uint32_t n = pos0 + rows, s = seed;
    uint16_t *k = malloc((size_t)n * 128 * 2), *v = malloc((size_t)n * 128 * 2);
    int rc = 0;
    for (unsigned g = 0; g < 4 && rc == 0; g++) {
        for (size_t i = 0; i < (size_t)n * 128; i++) {
            k[i] = i < (size_t)pos0 * 128 ? omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f)) : 0x7f7f;
            v[i] = i < (size_t)pos0 * 128 ? omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f)) : 0x7f7f;
        }
        rc = omega_gpu_decode_attn_cache_write(c, 0, g, 0, n, k, v);
    }
    free(k); free(v);
    return rc;
}

static void read_new(OmegaGpuDecodeAttnCache *c, uint32_t pos0, uint32_t rows, uint16_t *kv) {
    for (uint32_t r = 0; r < rows; r++) for (unsigned g = 0; g < 4; g++)
        (void)omega_gpu_decode_attn_cache_read(c, 0, g, pos0 + r, kv + ((size_t)r * 4 + g) * 256, kv + ((size_t)r * 4 + g) * 256 + 128);
}

int main(void) {
    OmegaGpuDecodeAttnCache *c = NULL;
    if (omega_gpu_decode_attn_cache_alloc(1, MAXP, 10000000.0, &c) != OMEGA_GPU_DATTN_OK) { printf("FAIL alloc: %s\n", omega_gpu_decode_attn_last_error()); return 1; }
    uint32_t s = 7; float qn[128], kn[128];
    for (unsigned i = 0; i < 128; i++) { qn[i] = frand(&s, 0.8f, 1.2f); kn[i] = frand(&s, 0.8f, 1.2f); }
    if (omega_gpu_decode_attn_set_norms(c, 0, qn, kn) != OMEGA_GPU_DATTN_OK) { printf("FAIL norms\n"); return 1; }
    static const uint32_t P0[] = { 0, 1, 37, 250, 255, 256, 509, 700, 1023, 1500, 2032 };
    static const uint32_t R[] = { 1, 2, 3, 5, 8, 16 };
    const uint32_t MR = OMEGA_GPU_DATTN_MAX_ROWS;
    float *qkv = malloc((size_t)MR * W * 4), *o_ref = malloc((size_t)MR * 4096 * 4), *o_got = malloc((size_t)MR * 4096 * 4);
    uint16_t *kv_ref = malloc((size_t)MR * 4 * 256 * 2), *kv_got = malloc((size_t)MR * 4 * 256 * 2);
    int cases = 0, bad = 0, neg_caught = 0, neg = 0;
    double ns_rows = 0, ns_seq = 0;
    for (size_t a = 0; a < sizeof P0 / sizeof P0[0]; a++) for (size_t b = 0; b < sizeof R / sizeof R[0]; b++) {
        uint32_t pos0 = P0[a], rows = R[b];
        if (pos0 + rows > MAXP) continue;
        uint32_t seed = 1000u + pos0 * 31u + rows;
        uint32_t qs = seed ^ 0x5a5a;
        for (size_t i = 0; i < (size_t)rows * W; i++) qkv[i] = frand(&qs, -1.0f, 1.0f) * ((i % W) < 4096 ? 3.0f : 1.0f);
        float scale = (a & 1) ? 1.0f : 18446744073709551616.0f;
        /* reference: one decode call per row, in order */
        int rc = reset_cache(c, pos0, rows, seed);
        OmegaGpuDecodeAttnInfo info;
        for (uint32_t r = 0; r < rows && rc == 0; r++) { rc = omega_gpu_decode_attn(c, 0, pos0 + r, qkv + (size_t)r * W, 1e-6f, scale, o_ref + (size_t)r * 4096, &info); ns_seq += (double)info.elapsed_ns; }
        if (rc) { printf("FAIL ref pos0=%u rows=%u rc=%d %s\n", pos0, rows, rc, omega_gpu_decode_attn_last_error()); bad++; continue; }
        read_new(c, pos0, rows, kv_ref);
        /* under test */
        rc = reset_cache(c, pos0, rows, seed);
        if (!rc) rc = omega_gpu_decode_attn_rows(c, 0, pos0, rows, qkv, 1e-6f, scale, o_got, &info);
        if (rc) { printf("FAIL rows pos0=%u rows=%u rc=%d %s\n", pos0, rows, rc, omega_gpu_decode_attn_last_error()); bad++; continue; }
        ns_rows += (double)info.elapsed_ns;
        read_new(c, pos0, rows, kv_got);
        size_t dw = 0, dk = 0;
        for (size_t i = 0; i < (size_t)rows * 4096; i++) { uint32_t x, y; memcpy(&x, o_ref + i, 4); memcpy(&y, o_got + i, 4); dw += x != y; }
        for (size_t i = 0; i < (size_t)rows * 4 * 256; i++) dk += kv_ref[i] != kv_got[i];
        cases++;
        if (dw || dk) bad++;
        printf("%s pos0=%u rows=%u splits=%u out_words_differ=%zu kv_differ=%zu rows_chip_us=%.1f\n", dw || dk ? "DIFF" : "SAME", pos0, rows, info.splits, dw, dk, info.elapsed_ns / 1e3);
        /* negative control on a subset: V one position late in the rows kernels only */
        if (rows >= 2 && (a % 3) == 0) {
            omega_gpu_decode_attn_test_set_mutant(2);
            rc = reset_cache(c, pos0, rows, seed);
            if (!rc) rc = omega_gpu_decode_attn_rows(c, 0, pos0, rows, qkv, 1e-6f, scale, o_got, &info);
            omega_gpu_decode_attn_test_set_mutant(0);
            size_t mw = 0;
            for (size_t i = 0; i < (size_t)rows * 4096; i++) { uint32_t x, y; memcpy(&x, o_ref + i, 4); memcpy(&y, o_got + i, 4); mw += x != y; }
            neg++; if (rc || mw) neg_caught++;
            printf("NEG mutant2 pos0=%u rows=%u rc=%d out_words_differ=%zu -> %s\n", pos0, rows, rc, mw, rc || mw ? "caught" : "MISSED");
        }
    }
    printf("chip time: rows path %.1f us total vs per-row path %.1f us total\n", ns_rows / 1e3, ns_seq / 1e3);
    int ok = cases > 0 && bad == 0 && neg > 0 && neg_caught == neg;
    printf("VERDICT %s cases=%d bad=%d negative=%d/%d caught\n", ok ? "PASS" : "FAIL", cases, bad, neg_caught, neg);
    omega_gpu_decode_attn_cache_free(c);
    return ok ? 0 : 1;
}
