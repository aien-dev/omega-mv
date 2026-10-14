/*
 * gpu_decode_verify_test: OWN-30B W-L4f, the batched exact verify (omega_gpu_decode_graph_verify, omega_gpu_decode_graph.h).
 *   --host-only   the vrows row-copy kernel on the host warp simulator (plain gather, GEMM lane layout 8 and 16 against
 *                 omega_fp8gemm_permute_x bit for bit, the mutant caught) and the new entry points' argument refusals.
 *   (default)     chip, caller holds the GB10 lock. Synthetic Qwen3-30B-A3B-shaped weights (as gpu_decode_graph_test:
 *                 hidden 2048, 128 experts top-8, 32/4 heads, --layers N (3), --vocab V (4096), random KV prefix of 300):
 *                 for every rows count m in {1, 3, 5, 9, 16}: m one-list tokens at positions 300 .. 300 + m - 1 (launched,
 *                 then resident), then ONE verify call at the same positions. Checks: every row's hidden state and the
 *                 K/V the call stored are bit-identical to the tokens'; the lm_head host call on a verify row equals the
 *                 token's own logits bit for bit; the two test mutants and the numerics hook (x' of row 0 nudged by one
 *                 ulp per value) each change some row; after them the clean call is identical again. Timing: medians
 *                 over --reps (5) of one verify call vs m tokens.
 *   --red         mutant 1 left on for the identity checks: this run MUST FAIL (red-before-green receipt).
 * Verdict: exit 0 when every CHECK passed.
 */
#include "omega_gpu_decode_graph.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_fp8gemm_api.h"
#include "omega_blackwell_codegen_vrows.h"
#include "omega_gpu_session.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bw_warp_sim_k.h"

#define H 2048u
#define QKV 5120u
#define ATT 4096u
#define NE 128u
#define TK 8u
#define I 768u
#define EPS 1e-6f
#define POS 300u
#define MAX_POS 512u
#define MAXR OMEGA_GPU_DGRAPH_VERIFY_MAX_ROWS

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("CHECK FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static uint16_t bf16_rne(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t r = x + 0x7fffu + ((x >> 16) & 1u); return (uint16_t)(r >> 16); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median_u64(uint64_t *v, unsigned n) { qsort(v, n, sizeof *v, cmp_u64); return v[n / 2]; }
static void put64(uint32_t *a, unsigned off, uint64_t v) { a[(off - 0x380) / 4] = (uint32_t)v; a[(off - 0x380) / 4 + 1] = (uint32_t)(v >> 32); }

/* ---- host-only: the vrows kernel on the simulator ---- */
static int sim_vrows(uint32_t lb, int mutant, uint32_t rows, uint32_t k, uint32_t sstride_f, uint32_t src_rows, uint32_t seed) {
    static BlackwellIRProgram prog;
    OmegaVrowsSpec sp = { lb, mutant };
    if (omega_blackwell_codegen_vrows_prog(&sp, &prog) != 0) { CHECK(0, "vrows codegen lb %u mutant %d", lb, mutant); return -1; }
    float *src = malloc((size_t)src_rows * sstride_f * 4), *dst = malloc((size_t)rows * k * 4), *ref = malloc((size_t)rows * k * 4), *tmp = malloc((size_t)k * 4);
    uint32_t *idx = malloc((size_t)rows * 4);
    if (!src || !dst || !ref || !tmp || !idx) return -1;
    uint32_t s = seed;
    for (size_t i = 0; i < (size_t)src_rows * sstride_f; i++) src[i] = frand(&s, -1, 1);
    for (uint32_t r = 0; r < rows; r++) idx[r] = lcg(&s) % src_rows;
    memset(dst, 0xff, (size_t)rows * k * 4);
    /* reference: the GEMM's own permutation (x * 2^64) applied to src / 2^64... compared on the 2^64-scaled source instead */
    for (uint32_t r = 0; r < rows; r++) {
        const float *row = src + (size_t)idx[r] * sstride_f;
        if (lb) {
            for (uint32_t c = 0; c < k; c++) tmp[c] = row[c];
            omega_fp8gemm_permute_x(tmp, k, lb, ref + (size_t)r * k); /* scales by 2^64 */
            for (uint32_t c = 0; c < k; c++) ref[(size_t)r * k + c] /= 18446744073709551616.0f; /* exact: power of two, no underflow at |v| ~ 1 */
        } else memcpy(ref + (size_t)r * k, row, (size_t)k * 4);
    }
    uint32_t args[OMEGA_VROWS_WORDS] = { 0 };
    put64(args, OMEGA_VROWS_DST, (uintptr_t)dst); put64(args, OMEGA_VROWS_SRC, (uintptr_t)src); put64(args, OMEGA_VROWS_IDX, (uintptr_t)idx);
    args[(OMEGA_VROWS_UNITS - 0x380) / 4] = k / 4; args[(OMEGA_VROWS_SSTRIDE - 0x380) / 4] = sstride_f * 4;
    static uint8_t smem[64];
    WsSim sim; memset(&sim, 0, sizeof sim);
    sim.p = &prog; sim.threads = OMEGA_VROWS_THREADS; sim.smem = smem; sim.smem_bytes = sizeof smem;
    memcpy(&sim.cbank[0x380 / 4], args, sizeof args);
    sim.nranges = 3;
    sim.ranges[0] = (WsRange){ (uintptr_t)src, (uintptr_t)(src + (size_t)src_rows * sstride_f) };
    sim.ranges[1] = (WsRange){ (uintptr_t)dst, (uintptr_t)(dst + (size_t)rows * k) };
    sim.ranges[2] = (WsRange){ (uintptr_t)idx, (uintptr_t)(idx + rows) };
    int err = ws_run(&sim, (k / 4 + OMEGA_VROWS_THREADS - 1) / OMEGA_VROWS_THREADS, rows);
    int same = memcmp(dst, ref, (size_t)rows * k * 4) == 0;
    if (!mutant) CHECK(err == 0 && same, "sim vrows lb %u rows %u k %u sstride %u: %s (%s)", lb, rows, k, sstride_f, same ? "bit-identical to the reference" : "DIFFERS", err ? sim.msg : "no sim error");
    else CHECK(err == 0 && !same, "sim vrows mutant lb %u rows %u k %u: caught (%s)", lb, rows, k, same ? "NOT caught" : "differs");
    free(src); free(dst); free(ref); free(tmp); free(idx);
    return 0;
}

static int host_only(void) {
    sim_vrows(0, 0, 9, 2072, 2072, 1, 1);       /* router copy: one source row to row 8 */
    sim_vrows(0, 0, 40, 2048, 2048, 40, 2);     /* the down outputs back to row order */
    sim_vrows(16, 0, 9, 2048, 2048, 9, 3);      /* xq -> GEMM layout */
    sim_vrows(16, 0, 9, 4096, 4096, 9, 4);      /* attention -> GEMM layout */
    sim_vrows(16, 0, 72, 2048, 2072, 9, 5);     /* router rows gathered in expert order, GEMM layout */
    sim_vrows(8, 0, 72, 768, 768, 72, 6);       /* SWIGLU rows -> GEMM layout (k 768: 8 lane bytes) */
    sim_vrows(0, 0, 3, 8, 8, 3, 7);             /* units below one CTA */
    sim_vrows(16, 1, 9, 2048, 2048, 9, 8);
    sim_vrows(8, 1, 16, 768, 768, 16, 9);
    for (uint32_t q = 0; q < 512; q++) { uint32_t s = omega_vrows_src_unit(16, q); if (s >= 512) { CHECK(0, "src unit range"); break; } }
    OmegaVrowsSpec bad = { 4, 0 }; static BlackwellIRProgram p;
    CHECK(omega_blackwell_codegen_vrows_prog(&bad, &p) != 0, "vrows refuses lane bytes 4");
    float h[H]; uint32_t ids[2] = { 1, 0 };
    CHECK(omega_gpu_decode_graph_verify(NULL, 1, h, 0, h, NULL) == OMEGA_GPU_DGRAPH_BAD_ARGS, "verify refuses a NULL graph");
    CHECK(omega_gpu_fp8gemm_record(NULL, NULL, 1, ids, h, 1, 1, 1, NULL) == OMEGA_GPU_FP8GEMM_BAD_ARGS, "gemm record refuses a NULL list");
    return g_failed ? 1 : 0;
}

/* ---- chip: the synthetic model (gpu_decode_graph_test.c) ---- */
typedef struct {
    uint32_t layers, vocab;
    OmegaGpuFp8Tensor *qkv, *o, *gu, *dn;
    OmegaGpuRouter *router;
    OmegaGpuDecodeAttnCache *attn;
    OmegaGpuBf16Tensor *lm;
    float *in_norm, *fnorm;
} Model;

static int fp8_random(uint32_t seed, uint32_t e, uint32_t n, uint32_t k, OmegaGpuFp8Tensor **out) {
    size_t wb = (size_t)e * n * k, sb = (size_t)e * (n / 128) * (k / 128);
    uint8_t *w = malloc(wb); uint16_t *s = malloc(sb * 2);
    if (!w || !s) { free(w); free(s); return -1; }
    uint32_t st = seed;
    for (size_t i = 0; i < wb; i += 4) { uint32_t r = lcg(&st); for (unsigned j = 0; j < 4 && i + j < wb; j++) w[i + j] = (uint8_t)((r >> (8 * j)) & 0xbfu); }
    for (size_t i = 0; i < sb; i++) s[i] = bf16_rne(frand(&st, 0.02f, 0.04f));
    int rc = omega_gpu_fp8_tensor_upload(e, n, k, w, s, out);
    free(w); free(s);
    return rc;
}

static int model_build(Model *m, uint32_t layers, uint32_t vocab) {
    memset(m, 0, sizeof *m); m->layers = layers; m->vocab = vocab;
    uint32_t s = 1234;
    if (fp8_random(11, 1, QKV, H, &m->qkv) || fp8_random(12, 1, H, ATT, &m->o) || fp8_random(13, NE, 2 * I, H, &m->gu) || fp8_random(14, NE, H, I, &m->dn)) { printf("fp8 upload: %s\n", omega_gpu_fp8_last_error()); return -1; }
    if (omega_gpu_router_alloc(layers, &m->router) != 0) { printf("router alloc: %s\n", omega_gpu_router_last_error()); return -1; }
    uint16_t *gate = malloc((size_t)NE * H * 2); float *nw = malloc(H * 4);
    m->in_norm = malloc((size_t)layers * H * 4); m->fnorm = malloc(H * 4);
    if (!gate || !nw || !m->in_norm || !m->fnorm) return -1;
    for (uint32_t l = 0; l < layers; l++) {
        for (size_t i = 0; i < (size_t)NE * H; i++) gate[i] = bf16_rne(frand(&s, -0.05f, 0.05f));
        for (unsigned i = 0; i < H; i++) nw[i] = frand(&s, 0.8f, 1.2f);
        if (omega_gpu_router_set_layer(m->router, l, gate, nw) != 0 || omega_gpu_router_set_directory_fp8(m->router, l, m->gu, m->dn) != 0) { printf("router layer: %s\n", omega_gpu_router_last_error()); return -1; }
        for (unsigned i = 0; i < H; i++) m->in_norm[(size_t)l * H + i] = frand(&s, 0.8f, 1.2f);
    }
    free(gate); free(nw);
    for (unsigned i = 0; i < H; i++) m->fnorm[i] = frand(&s, 0.8f, 1.2f);
    if (omega_gpu_decode_attn_cache_alloc(layers, MAX_POS, 1000000.0, &m->attn) != 0) { printf("attn alloc: %s\n", omega_gpu_decode_attn_last_error()); return -1; }
    float qn[128], kn[128]; uint16_t *kr = malloc((size_t)POS * 128 * 2), *vr = malloc((size_t)POS * 128 * 2);
    if (!kr || !vr) return -1;
    for (uint32_t l = 0; l < layers; l++) {
        for (unsigned i = 0; i < 128; i++) { qn[i] = frand(&s, 0.8f, 1.2f); kn[i] = frand(&s, 0.8f, 1.2f); }
        if (omega_gpu_decode_attn_set_norms(m->attn, l, qn, kn) != 0) return -1;
        for (uint32_t h = 0; h < 4; h++) {
            for (size_t i = 0; i < (size_t)POS * 128; i++) { kr[i] = bf16_rne(frand(&s, -1, 1)); vr[i] = bf16_rne(frand(&s, -1, 1)); }
            if (omega_gpu_decode_attn_cache_write(m->attn, l, h, 0, POS, kr, vr) != 0) { printf("kv write: %s\n", omega_gpu_decode_attn_last_error()); return -1; }
        }
    }
    free(kr); free(vr);
    uint16_t *lw = malloc((size_t)vocab * H * 2);
    if (!lw) return -1;
    for (size_t i = 0; i < (size_t)vocab * H; i++) lw[i] = bf16_rne(frand(&s, -0.05f, 0.05f));
    int rc = omega_gpu_bf16_tensor_upload(vocab, H, lw, &m->lm);
    free(lw);
    if (rc != 0) { printf("lm_head upload: %s\n", omega_gpu_bf16_last_error()); return -1; }
    return 0;
}

/* the K and V rows every layer and head stored at positions POS .. POS + m - 1 */
static int kv_snap(const Model *m, uint32_t rows, uint16_t *out) {
    for (uint32_t l = 0; l < m->layers; l++) for (uint32_t h = 0; h < 4; h++) for (uint32_t r = 0; r < rows; r++) {
        uint16_t *p = out + (((size_t)l * 4 + h) * rows + r) * 256;
        if (omega_gpu_decode_attn_cache_read(m->attn, l, h, POS + r, p, p + 128) != 0) return -1;
    }
    return 0;
}

/* numerics hook: row 0's x' moved by one ulp per value (the control: an exact-looking verify that is not) */
static void nudge(void *ctx, uint32_t layer, uint32_t rows, float *xr) {
    (void)ctx; (void)layer; (void)rows;
    for (unsigned i = 0; i < H; i++) xr[i] = nextafterf(xr[i], INFINITY);
}

static size_t words_differ(const float *a, const float *b, size_t n) { size_t d = 0; for (size_t i = 0; i < n; i++) d += memcmp(&a[i], &b[i], 4) != 0; return d; }

static int chip_run(uint32_t layers, uint32_t vocab, uint32_t reps, int red) {
    Model m;
    if (model_build(&m, layers, vocab)) return 1;
    OmegaGpuDecodeGraphConfig cfg = { .layers = layers, .vocab = vocab, .eps = EPS, .norm_topk = 1, .router = m.router, .attn = m.attn, .lm_head = m.lm };
    OmegaGpuDecodeGraph *gr = NULL;
    if (omega_gpu_decode_graph_create(&cfg, &gr) != 0) { printf("graph create: %s\n", omega_gpu_session_last_error()); return 1; }
    for (uint32_t l = 0; l < layers; l++) if (omega_gpu_decode_graph_set_layer(gr, l, m.qkv, m.o, m.gu, m.dn, m.in_norm + (size_t)l * H) != 0) { printf("set layer\n"); return 1; }
    if (omega_gpu_decode_graph_set_final_norm(gr, m.fnorm) != 0) return 1;
    /* warm every kernel shape the records use with a plain call (the record paths refuse evicting cache misses) */
    {
        static float x[H], q[QKV], a[ATT], y[H]; uint32_t zero = 0, ids[TK]; float w[TK];
        uint32_t s = 5; for (unsigned i = 0; i < H; i++) x[i] = frand(&s, -1, 1);
        if (omega_gpu_fp8_gemv_grouped_f32(m.qkv, 1, &zero, x, 1, q, NULL) || omega_gpu_decode_attn(m.attn, 0, POS, q, EPS, 1.0f, a, NULL) ||
            omega_gpu_fp8_gemv_grouped_f32(m.o, 1, &zero, a, 1, y, NULL) || omega_gpu_router(m.router, 0, x, EPS, 1, 1.0f, ids, w, NULL)) { printf("warm: %s\n", omega_gpu_session_last_error()); return 1; }
        float *lg = malloc((size_t)vocab * 4); if (!lg || omega_gpu_bf16_gemv_f32(m.lm, x, lg, NULL)) { printf("warm lm\n"); return 1; } free(lg);
    }
    static const uint32_t ROWS[] = { 1, 3, 5, 9, 16 };
    float *X = malloc((size_t)MAXR * H * 4), *ref = malloc((size_t)MAXR * H * 4), *hid = malloc((size_t)MAXR * H * 4), *ref2 = malloc((size_t)MAXR * H * 4);
    float *lg_tok = malloc((size_t)MAXR * vocab * 4), *lg = malloc((size_t)vocab * 4);
    const size_t kvn = (size_t)layers * 4 * MAXR * 256;
    uint16_t *kv_ref = malloc(kvn * 2), *kv_ver = malloc(kvn * 2);
    if (!X || !ref || !hid || !ref2 || !lg_tok || !lg || !kv_ref || !kv_ver) return 1;
    uint32_t s = 77;
    for (size_t i = 0; i < (size_t)MAXR * H; i++) X[i] = frand(&s, -1, 1);
    if (red) omega_gpu_decode_graph_verify_test_set_mutant(gr, 1);
    printf("TIMING (median of %u, %u layers, vocab %u):\n", reps, layers, vocab);
    for (unsigned ri = 0; ri < sizeof ROWS / sizeof ROWS[0]; ri++) {
        const uint32_t R = ROWS[ri];
        uint64_t t_tok[32], t_ver[32], c_ver[32], h_ver[32], sub[32];
        for (int resident = 0; resident < 2; resident++) {
            omega_gpu_decode_graph_set_resident(resident);
            OmegaGpuDecodeGraphStats st;
            for (uint32_t r = 0; r < R; r++) {
                uint32_t tok = 0;
                if (omega_gpu_decode_graph_token(gr, X + (size_t)r * H, POS + r, &tok, &st) != 0) { printf("token: %s\n", omega_gpu_session_last_error()); return 1; }
                if (omega_gpu_decode_graph_read(gr, NULL, (resident ? ref2 : ref) + (size_t)r * H, lg_tok + (size_t)r * vocab) != 0) return 1;
            }
            if (resident) { CHECK(memcmp(ref, ref2, (size_t)R * H * 4) == 0, "m=%u resident tokens == launched tokens (hidden)", R); continue; }
            if (kv_snap(&m, R, kv_ref)) return 1;
        }
        omega_gpu_decode_graph_set_resident(0);
        OmegaGpuDecodeGraphStats vs;
        int rc = omega_gpu_decode_graph_verify(gr, R, X, POS, hid, &vs);
        if (rc != 0) { CHECK(0, "m=%u verify rc %d: %s", R, rc, omega_gpu_session_last_error()); return 1; }
        if (kv_snap(&m, R, kv_ver)) return 1;
        size_t dw = words_differ(ref, hid, (size_t)R * H);
        unsigned rows_eq = 0; for (uint32_t r = 0; r < R; r++) rows_eq += words_differ(ref + (size_t)r * H, hid + (size_t)r * H, H) == 0;
        CHECK(dw == 0, "IDENTITY m=%u rows_equal=%u/%u words_differ=%zu (verify vs one-list tokens, hidden)", R, rows_eq, R, dw);
        CHECK(memcmp(kv_ref, kv_ver, (size_t)layers * 4 * R * 256 * 2) == 0, "m=%u K/V stored by the verify == by the tokens (every layer, head, row)", R);
        CHECK(vs.submits == layers, "m=%u one submission per layer (%u)", R, vs.submits);
        if (R == 5 || R == 1) {
            int lm_same = 1;
            for (uint32_t r = 0; r < R; r++) { if (omega_gpu_bf16_gemv_f32(m.lm, hid + (size_t)r * H, lg, NULL) != 0) return 1; lm_same &= memcmp(lg, lg_tok + (size_t)r * vocab, (size_t)vocab * 4) == 0; }
            CHECK(lm_same, "m=%u lm_head host call on each verify row == the token's own logits (bit for bit)", R);
        }
        if (R == 5 && !red) {
            for (int mut = 1; mut <= 3; mut++) {
                if (mut < 3) omega_gpu_decode_graph_verify_test_set_mutant(gr, mut); else omega_gpu_decode_graph_verify_set_hook(gr, nudge, NULL);
                if (omega_gpu_decode_graph_verify(gr, R, X, POS, ref2, NULL) != 0) { CHECK(0, "mutant %d run: %s", mut, omega_gpu_session_last_error()); return 1; }
                omega_gpu_decode_graph_verify_test_set_mutant(gr, 0); omega_gpu_decode_graph_verify_set_hook(gr, NULL, NULL);
                size_t d = words_differ(ref, ref2, (size_t)R * H);
                CHECK(d > 0, "NEGATIVE CONTROL %s caught: %zu words differ", mut == 1 ? "mutant 1 (row copies without gather/layout)" : mut == 2 ? "mutant 2 (combine slots 0/1 of row 0 swapped)" : "numerics hook (row 0 x' +1 ulp)", d);
            }
            if (omega_gpu_decode_graph_verify(gr, R, X, POS, ref2, NULL) != 0) return 1;
            CHECK(memcmp(ref, ref2, (size_t)R * H * 4) == 0, "m=%u clean verify after the controls identical again", R);
        }
        /* timing: one verify call vs R launched-off resident tokens (the server's plain step is the resident token) */
        omega_gpu_decode_graph_set_resident(1);
        for (unsigned k = 0; k < reps && k < 32; k++) {
            OmegaGpuDecodeGraphStats st; uint64_t acc = 0; uint32_t tok;
            for (uint32_t r = 0; r < R; r++) { if (omega_gpu_decode_graph_token(gr, X + (size_t)r * H, POS + r, &tok, &st) != 0) return 1; acc += st.total_ns; }
            t_tok[k] = acc;
            if (omega_gpu_decode_graph_verify(gr, R, X, POS, hid, &vs) != 0) return 1;
            t_ver[k] = vs.total_ns; c_ver[k] = vs.chip_ns; h_ver[k] = vs.record_ns; sub[k] = vs.launches;
        }
        omega_gpu_decode_graph_set_resident(0);
        const unsigned n = reps < 32 ? reps : 32;
        const uint64_t mt = median_u64(t_tok, n), mv = median_u64(t_ver, n), mc = median_u64(c_ver, n), mh = median_u64(h_ver, n), ml = median_u64(sub, n);
        printf("TIMING m=%u tokens(resident) %.3f ms | verify %.3f ms (chip %.3f, host outside submits %.3f, launches %" PRIu64 ") | ratio %.2f\n",
               R, mt / 1e6, mv / 1e6, mc / 1e6, mh / 1e6, ml, mt ? (double)mv / (double)mt : 0.0);
    }
    omega_gpu_decode_graph_free(gr);
    return g_failed ? 1 : 0;
}

int main(int argc, char **argv) {
    int host = 0, red = 0; uint32_t layers = 3, vocab = 4096, reps = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--red")) red = 1;
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vocab") && i + 1 < argc) vocab = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else { printf("usage: %s [--host-only] [--red] [--layers N] [--vocab V] [--reps R]\n", argv[0]); return 2; }
    }
    int rc = host ? host_only() : chip_run(layers, vocab, reps, red);
    printf("%s: %d checks, %d failed: %s\n", host ? "host-only" : red ? "chip-red" : "chip", g_checks, g_failed, rc ? "FAIL" : "PASS");
    return rc;
}
