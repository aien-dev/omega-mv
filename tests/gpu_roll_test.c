/*
 * tests/gpu_roll_test.c: omega-mv#580 "teach it more notes", steps 1 to 5, measured one against the other.
 *
 *   --host            no GPU: group validation refusals, order words (SERIAL, NONE, SEM, WAVE, skip, ignore-deps),
 *                     argument word layouts the roll patches (vecadd 7 words, elementwise and matmul 10 words).
 *   --chip [--out F]  caller holds the GB10 lock. Prints CSV rows "row,<section>,..." and a verdict line.
 *       note     omega_gpu_execute (persistent device, one job at a time): the one-note baseline
 *       roll     vecadd 65, depth 1, 4, 16, 64 (step 1)
 *       melody   vecadd, ex2, swiglu, rmsnorm, rope, matmul: each alone, each against its mutant or no-store control,
 *                each vs its session API, then a mixed roll (step 2)
 *       chord    K independent nodes in one submission (K = 1, 4, 16, 64), voices (channels) 1, 2, 4, the
 *                serial-vs-chord time of 16 medium grids, and the skipped-node negative control (step 3)
 *       harmony  64-node graphs (random DAG, chain, fan-in tree): SERIAL, WAVE, SEM correct; SEM with the
 *                ordering removed as the negative control (step 4)
 *   --joules SECS --what note|roll|chord  runs one configuration for SECS seconds (step 5, under spark-receipt).
 * Every job's output is compared with a host reference; every output word is poison-filled and scanned.
 */
#include "omega_gpu_roll.h"
#include "omega_gpu_engine.h"
#include "omega_blackwell_engine.h"
#include "omega_blackwell_encoder.h"
#include "omega_blackwell_qmd.h"
#include "omega_blackwell_submit.h"
#include "omega_blackwell_codegen.h"
#include "omega_blackwell_matmul.h"
#include "omega_gpu_elementwise_api.h"
#include "omega_gpu_matmul_api.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_failed;
static FILE *g_out;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define ROW(...) do { printf("row,"); printf(__VA_ARGS__); printf("\n"); if (g_out) { fprintf(g_out, __VA_ARGS__); fprintf(g_out, "\n"); fflush(g_out); } } while (0)

static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median(uint64_t *v, size_t n) { if (!n) return 0; qsort(v, n, 8, cmp_u64); return v[n / 2]; }
#define POISON 0xffbadbadu
static size_t compare_f(const float *got, const float *want, size_t n, double rel, double abs_tol) {
    size_t bad = 0;
    for (size_t i = 0; i < n; i++) { double d = fabs((double)got[i] - (double)want[i]); if (!(d <= rel * fabs((double)want[i]) + abs_tol)) bad++; }
    return bad;
}

/* ------------------------------------------------------------------ kernels */
typedef enum { K_VECADD, K_EX2, K_SWIGLU, K_RMSNORM, K_ROPE, K_MATMUL, K_COUNT } Kind;
static const char *KNAME[K_COUNT] = { "vecadd", "ex2", "swiglu", "rmsnorm", "rope", "matmul" };

typedef struct { uint8_t *code; size_t bytes; uint32_t gpr; } Prog;

static int prog_vecadd(Prog *p, int no_store) {
    static uint8_t buf[4096]; size_t len = 0;
    if (omega_blackwell_encode_vecadd(buf, sizeof buf, &len) != 0 || len < 0x140u) return -1;
    p->code = malloc(len); memcpy(p->code, buf, len); p->bytes = len; p->gpr = 64;
    if (no_store) memcpy(p->code + 0x120, p->code + 0x130, OMEGA_BW_INSTRUCTION_BYTES); /* the engine gate's no-store control */
    return 0;
}
static int prog_ew(Prog *p, OmegaGpuEwOp op, int mutant) {
    OmegaBlackwellKernel k; memset(&k, 0, sizeof k);
    if (omega_gpu_elementwise_codegen_ir(op, mutant, NULL, &k) != OMEGA_GPU_EW_OK) return -1;
    p->code = k.code; p->bytes = k.code_size; p->gpr = 64; /* the API declares 64 for every elementwise kernel */
    return 0;
}
/* matmul: m = 16, k = 128, n = 64 bf16 fragment kernel, the API's launch shape */
#define MM_M 16u
#define MM_K 128u
#define MM_N 64u
static uint32_t g_mm_grid_x, g_mm_grid_y, g_mm_rows;
static int prog_matmul(Prog *p, int mutant) {
    uint32_t rt = 1, ct = 1;
    omega_matmul_frag_block_shape(MM_M / 16u, MM_N / 8u, &rt, &ct);
    OmegaMatMulSpec spec;
    if (omega_matmul_spec_init(&spec, 16, MM_K, MM_N, OMEGA_MATMUL_PRECISION_BF16) != 0) return -1;
    const uint32_t rows = MM_M, nb = MM_N / 8u / ct;
    uint32_t gx = OMEGA_GPU_MATMUL_MAX_CTAS / (rows / 16u / rt); if (gx == 0) gx = 1; if (gx > nb) gx = nb;
    OmegaBlackwellKernel k; memset(&k, 0, sizeof k);
    int rc;
    if (rt > 1 || ct > 1) {
        uint32_t unroll = omega_matmul_frag_block_unroll(MM_K, rt, ct);
        if (mutant) while (unroll > 1 && MM_K / 16u / unroll < 2) unroll /= 2;
        rc = omega_blackwell_codegen_matmul_frag_block(&spec, gx, unroll, rt, ct, mutant, &k);
    } else {
        uint32_t unroll = omega_matmul_frag_unroll(MM_K);
        if (mutant) while (unroll > 1 && MM_K / 16u / unroll < 2) unroll /= 2;
        rc = omega_blackwell_codegen_matmul_frag(&spec, gx, unroll, mutant, &k);
    }
    if (rc != 0) return -1;
    g_mm_grid_x = gx; g_mm_grid_y = rows / 16u / rt; g_mm_rows = g_mm_grid_y * 16u * rt;
    p->code = k.code; p->bytes = k.code_size; p->gpr = k.gpr_count;
    return 0;
}

/* One test input set per kernel with its host reference. */
typedef struct {
    Kind kind;
    uint32_t n_in_a, n_in_b, n_out; /* words */
    void *a, *b; float *ref_f; uint32_t *ref_u;
    double rel, abs_tol;
    OmegaRollNode tmpl;            /* launch shape, args, buffers (in pointers set, out pointer set per job) */
} Case;

#define EW_N 8192u
#define RMS_ROWS 16u
#define RMS_DIM 1024u
#define ROPE_HEADS 32u
#define ROPE_HD 128u

static void node_bufs3(OmegaRollNode *nd, size_t a_bytes, const void *a, size_t b_bytes, const void *b, size_t c_bytes) {
    nd->nbufs = 3;
    nd->buf[0] = (OmegaRollBuf){ .bytes = a_bytes, .in = a, .arg_lo = 0, .from_node = -1 };
    nd->buf[1] = (OmegaRollBuf){ .bytes = b_bytes, .in = b, .arg_lo = 2, .from_node = -1 };
    nd->buf[2] = (OmegaRollBuf){ .bytes = c_bytes, .poisoned = 1, .poison = POISON, .arg_lo = 4, .from_node = -1 };
}

static void case_build(Case *c, Kind kind, const OmegaRollKernel *k, uint32_t vec_n) {
    memset(c, 0, sizeof *c);
    c->kind = kind;
    uint32_t s = 1234u + (uint32_t)kind;
    OmegaRollNode *nd = &c->tmpl;
    nd->k = k; nd->threads_y = 1; nd->grid_y = 1;
    switch (kind) {
    case K_VECADD: {
        uint32_t n = vec_n; uint32_t *a = malloc(n * 4), *b = malloc(n * 4); c->ref_u = malloc(n * 4);
        for (uint32_t i = 0; i < n; i++) { a[i] = lcg(&s); b[i] = lcg(&s); c->ref_u[i] = a[i] + b[i]; }
        c->a = a; c->b = b; c->n_out = n;
        nd->threads_x = 64; nd->grid_x = (n + 63) / 64; nd->num_elements = n; nd->n_args = OMEGA_BW_CBANK_ARGS_WORDS;
        omega_blackwell_build_cbank_args(nd->args, 0, 0, 0, n);
        node_bufs3(nd, n * 4, a, n * 4, b, n * 4);
        break; }
    case K_EX2: case K_SWIGLU: {
        uint32_t n = EW_N; float *a = malloc(n * 4), *b = malloc(n * 4); c->ref_f = malloc(n * 4);
        for (uint32_t i = 0; i < n; i++) { a[i] = frand(&s, -8, 8); b[i] = frand(&s, -2, 2); }
        for (uint32_t i = 0; i < n; i++) {
            if (kind == K_EX2) c->ref_f[i] = (float)exp2((double)a[i]);
            else { double g = a[i]; c->ref_f[i] = (float)(g / (1.0 + exp(-g)) * (double)b[i]); }
        }
        c->rel = kind == K_EX2 ? 1e-6 : 1e-5; c->abs_tol = kind == K_EX2 ? 0 : 1e-6;
        c->a = a; c->b = b; c->n_out = n;
        nd->threads_x = OMEGA_GPU_EW_THREADS; nd->grid_x = (n + OMEGA_GPU_EW_THREADS - 1) / OMEGA_GPU_EW_THREADS; nd->num_elements = n;
        nd->n_args = OMEGA_BW_CBANK_MATMUL_ARGS_WORDS; nd->args[6] = n;
        node_bufs3(nd, n * 4, a, n * 4, kind == K_EX2 ? (const void *)a : (const void *)b, n * 4);
        break; }
    case K_RMSNORM: {
        uint32_t n = RMS_ROWS * RMS_DIM; float *x = malloc(n * 4), *w = malloc(RMS_DIM * 4); c->ref_f = malloc(n * 4);
        for (uint32_t i = 0; i < n; i++) x[i] = frand(&s, -2, 2);
        for (uint32_t i = 0; i < RMS_DIM; i++) w[i] = frand(&s, 0.5f, 1.5f);
        const float eps = 1e-6f;
        for (uint32_t r = 0; r < RMS_ROWS; r++) {
            double ss = 0; for (uint32_t i = 0; i < RMS_DIM; i++) ss += (double)x[r * RMS_DIM + i] * x[r * RMS_DIM + i];
            float scale = (float)(1.0 / sqrt(ss / RMS_DIM + eps));
            for (uint32_t i = 0; i < RMS_DIM; i++) { float t = x[r * RMS_DIM + i] * scale; c->ref_f[r * RMS_DIM + i] = t * w[i]; }
        }
        c->rel = 1e-5; c->abs_tol = 1e-6; c->a = x; c->b = w; c->n_out = n;
        nd->threads_x = OMEGA_GPU_EW_THREADS; nd->grid_x = RMS_ROWS; nd->num_elements = n; nd->n_args = OMEGA_BW_CBANK_MATMUL_ARGS_WORDS;
        nd->args[6] = RMS_DIM / OMEGA_GPU_EW_THREADS; nd->args[7] = f2u(1.0f / (float)RMS_DIM); nd->args[8] = f2u(eps); nd->args[9] = RMS_DIM;
        node_bufs3(nd, n * 4, x, RMS_DIM * 4, w, n * 4);
        break; }
    case K_ROPE: {
        const uint32_t half = ROPE_HD / 2, n = ROPE_HEADS * ROPE_HD;
        float *v = malloc(n * 4), *tab = malloc(n * 4), *cs = malloc(half * 4), *sn = malloc(half * 4); c->ref_f = malloc(n * 4);
        for (uint32_t i = 0; i < n; i++) v[i] = frand(&s, -1, 1);
        for (uint32_t i = 0; i < half; i++) { double f = 1.0 / pow(10000.0, (double)(2 * i) / ROPE_HD), rot = 17.0 * f; cs[i] = (float)cos(rot); sn[i] = (float)sin(rot); }
        for (uint32_t h = 0; h < ROPE_HEADS; h++) { memcpy(tab + h * ROPE_HD, cs, half * 4); memcpy(tab + h * ROPE_HD + half, sn, half * 4); }
        for (uint32_t h = 0; h < ROPE_HEADS; h++) for (uint32_t i = 0; i < half; i++) {
            uint32_t i0 = h * ROPE_HD + i, i1 = i0 + half; float q0 = v[i0], q1 = v[i1];
            float a = q0 * cs[i], b = q1 * sn[i]; c->ref_f[i0] = a - b; float d = q0 * sn[i], e = q1 * cs[i]; c->ref_f[i1] = d + e;
        }
        c->rel = 0; c->abs_tol = 0; c->a = v; c->b = tab; c->n_out = n;
        nd->threads_x = half; nd->grid_x = ROPE_HEADS; nd->num_elements = n; nd->n_args = OMEGA_BW_CBANK_MATMUL_ARGS_WORDS;
        nd->args[6] = ROPE_HD; nd->args[7] = half;
        node_bufs3(nd, n * 4, v, n * 4, tab, n * 4);
        break; }
    case K_MATMUL: {
        float *a = malloc(MM_M * MM_K * 4), *bf = malloc(MM_K * MM_N * 4); uint16_t *b16 = malloc(MM_K * MM_N * 2);
        uint16_t *ap = malloc(MM_M * MM_K * 2), *bp = malloc(MM_K * MM_N * 2); c->ref_f = malloc(MM_M * MM_N * 4);
        for (uint32_t i = 0; i < MM_M * MM_K; i++) a[i] = frand(&s, -1, 1);
        for (uint32_t i = 0; i < MM_K * MM_N; i++) { b16[i] = omega_fp32_to_bf16(frand(&s, -1, 1)); bf[i] = omega_bf16_to_fp32(b16[i]); }
        omega_matmul_frag_pack_a_f32(a, MM_M, MM_K, MM_M, MM_K, ap);
        omega_matmul_frag_pack_b(b16, MM_K, MM_N, MM_K, 0, MM_N / 8u, bp);
        for (uint32_t r = 0; r < MM_M; r++) for (uint32_t j = 0; j < MM_N; j++) {
            double acc = 0; for (uint32_t q = 0; q < MM_K; q++) acc += (double)omega_bf16_to_fp32(omega_fp32_to_bf16(a[r * MM_K + q])) * bf[q * MM_N + j];
            c->ref_f[r * MM_N + j] = (float)acc;
        }
        c->rel = 1e-3; c->abs_tol = 1e-3; c->a = ap; c->b = bp; c->n_out = MM_M * MM_N;
        nd->threads_x = 32; nd->grid_x = g_mm_grid_x; nd->grid_y = g_mm_grid_y; nd->num_elements = g_mm_grid_x * g_mm_grid_y * 128u;
        nd->n_args = OMEGA_BW_CBANK_MATMUL_ARGS_WORDS;
        omega_blackwell_build_cbank_args_matmul(nd->args, 0, 0, 0, g_mm_rows, MM_K, MM_N);
        node_bufs3(nd, MM_M * MM_K * 2, ap, MM_K * MM_N * 2, bp, MM_M * MM_N * 4);
        free(a); free(bf); free(b16);
        break; }
    default: break;
    }
}
/* bad words of one job's output against the case reference */
static size_t case_bad(const Case *c, const void *out) {
    if (c->kind == K_VECADD) { size_t bad = 0; const uint32_t *o = out; for (uint32_t i = 0; i < c->n_out; i++) bad += o[i] != c->ref_u[i]; return bad; }
    if (c->kind == K_ROPE) { size_t bad = 0; const uint32_t *o = out, *r = (const uint32_t *)c->ref_f; for (uint32_t i = 0; i < c->n_out; i++) bad += o[i] != r[i]; return bad; }
    return compare_f(out, c->ref_f, c->n_out, c->rel, c->abs_tol);
}

/* ------------------------------------------------------------------ host */
static int host_only(void) {
    /* argument layouts the roll patches: words 0/1, 2/3, 4/5 are the a, b, c addresses */
    uint32_t w7[OMEGA_BW_CBANK_ARGS_WORDS], w10[OMEGA_BW_CBANK_MATMUL_ARGS_WORDS];
    omega_blackwell_build_cbank_args(w7, 0x1111222233ull, 0x4444555566ull, 0x7777888899ull, 65);
    CHECK(w7[0] == 0x11222233u && w7[1] == 0x11u && w7[2] == 0x44555566u && w7[3] == 0x44u && w7[4] == 0x77888899u && w7[5] == 0x77u && w7[6] == 65u,
          "vecadd args: a, b, c addresses at words 0, 2, 4, n at 6");
    omega_blackwell_build_cbank_args_matmul(w10, 0x1111222233ull, 0x4444555566ull, 0x7777888899ull, 16, 128, 64);
    CHECK(w10[0] == 0x11222233u && w10[1] == 0x11u && w10[2] == 0x44555566u && w10[3] == 0x44u && w10[4] == 0x77888899u && w10[5] == 0x77u,
          "matmul args: a, b, c addresses at words 0, 2, 4");
    OmegaRollKernel k; memset(&k, 0, sizeof k); k.gpr = 64;
    OmegaRollConfig cfg = { .voices = 1, .depth = 4, .max_nodes = 64, .data_bytes = 1u << 20 };
    static OmegaRollNode nodes[64];
    memset(nodes, 0, sizeof nodes);
    for (int i = 0; i < 64; i++) {
        OmegaRollNode *nd = &nodes[i];
        nd->k = &k; nd->threads_x = 64; nd->grid_x = 1; nd->grid_y = 1; nd->n_args = 7; nd->nbufs = 1;
        nd->buf[0] = (OmegaRollBuf){ .bytes = 256, .poisoned = 1, .poison = POISON, .arg_lo = 4, .from_node = -1 };
    }
    OmegaRollGroup g = { .n = 8, .nodes = nodes, .order = OMEGA_ROLL_ORDER_NONE, .skip_node = -1 };
    const char *why = NULL;
    CHECK(omega_gpu_roll_validate(&cfg, &g, &why) == 0, "8 independent nodes, ORDER_NONE: accepted");
    nodes[3].deps = 1u << 5; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "dependency on a later node refused (%s)", why ? why : "");
    nodes[3].deps = 1u << 1; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "ORDER_NONE with a dependency refused (%s)", why ? why : "");
    g.order = OMEGA_ROLL_ORDER_SEM; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) == 0, "ORDER_SEM with an earlier dependency accepted");
    nodes[3].deps = 1u << 3; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "self dependency refused (%s)", why ? why : "");
    nodes[3].deps = 0;
    nodes[4].nbufs = 2; nodes[4].buf[1] = (OmegaRollBuf){ .bytes = 256, .arg_lo = 0, .from_node = 2, .from_buf = 0 };
    CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "data alias without its dependency refused (%s)", why ? why : "");
    nodes[4].deps = 1u << 2; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) == 0, "data alias with its dependency accepted");
    nodes[4].buf[1].from_node = 6; nodes[4].deps = 0;
    CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "alias to a later node refused (%s)", why ? why : "");
    nodes[4].nbufs = 1;
    nodes[1].buf[0].bytes = 2u << 20; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "buffers past data_bytes refused (%s)", why ? why : "");
    nodes[1].buf[0].bytes = 256;
    nodes[2].buf[0].arg_lo = 6; CHECK(omega_gpu_roll_validate(&cfg, &g, &why) != 0, "address argument past n_args refused (%s)", why ? why : "");
    nodes[2].buf[0].arg_lo = 4;

    /* order words */
    static uint32_t pb[65536];
    g.order = OMEGA_ROLL_ORDER_NONE; for (int i = 0; i < 64; i++) nodes[i].deps = 0;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 8 * 4, "NONE: 4 launch words per node, no barrier");
    g.order = OMEGA_ROLL_ORDER_SERIAL;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 8 * 4 + 7 * 4, "SERIAL: WFI + invalidate before nodes 1..7");
    /* chain 0 -> 1 -> ... -> 7 */
    for (int i = 1; i < 8; i++) nodes[i].deps = 1ull << (i - 1);
    g.order = OMEGA_ROLL_ORDER_SEM;
    int n = omega_gpu_roll_order_words(&g, pb, 65536);
    CHECK(n == 8 * 4 + 7 * 6, "SEM chain of 8: one 6-word ACQUIRE per edge (%d words)", n);
    /* the first acquire waits for node 0's semaphore (symbolic 0x200000) == DONE, operation ACQUIRE */
    CHECK(pb[4] == ((2u << 28) | (5u << 16) | (0u << 13) | (0x5cu >> 2)) && pb[5] == 0x200000u && pb[7] == OMEGA_BW_SEMAPHORE_INTERMEDIATE_DONE && pb[9] == 0u,
          "SEM: ACQUIRE words (SEM_ADDR_LO 0x5c x5, address of node 0's semaphore, payload DONE, execute 0)");
    g.order = OMEGA_ROLL_ORDER_WAVE;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 8 * 4 + 7 * 2, "WAVE chain: a WFI before every dependent node");
    /* two independent chains interleaved: 0,1 roots; 2 needs 0; 3 needs 1 -> WAVE: one WFI before 2, none before 3 */
    for (int i = 0; i < 8; i++) nodes[i].deps = 0;
    nodes[2].deps = 1; nodes[3].deps = 2; g.n = 4;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 4 * 4 + 2, "WAVE: 2 waves of 2 nodes, one WFI");
    g.test_ignore_deps = 1;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 4 * 4, "ignore-deps control: no ordering words at all");
    g.test_ignore_deps = 0; g.skip_node = 1; g.order = OMEGA_ROLL_ORDER_NONE; nodes[2].deps = nodes[3].deps = 0;
    CHECK(omega_gpu_roll_order_words(&g, pb, 65536) == 3 * 4, "skip_node drops exactly one launch");
    g.skip_node = -1; g.n = 64; g.order = OMEGA_ROLL_ORDER_SEM;
    for (int i = 1; i < 64; i++) nodes[i].deps = (1ull << i) - 1; /* complete DAG: 2016 edges */
    n = omega_gpu_roll_order_words(&g, pb, 65536);
    CHECK(n == 64 * 4 + 2016 * 6, "SEM complete 64-node DAG: 2016 acquires fit (%d words)", n);
    return g_failed ? 1 : 0;
}

/* ------------------------------------------------------------------ chip */
static OmegaRoll *roll_open(uint32_t voices, uint32_t depth, uint32_t max_nodes, size_t data) {
    OmegaRollConfig cfg = { .voices = voices, .depth = depth, .max_nodes = max_nodes, .data_bytes = data, .timeout_ms = 20000 };
    OmegaRoll *r = NULL;
    if (omega_gpu_roll_open(&cfg, &r) != OMEGA_ROLL_OK) { printf("roll open failed (v=%u d=%u n=%u data=%zu)\n", voices, depth, max_nodes, data); return NULL; }
    return r;
}
static int upload(OmegaRoll *r, const Prog *p, OmegaRollKernel *k) {
    int rc = omega_gpu_roll_kernel(r, p->code, p->bytes, p->gpr, k);
    if (rc != OMEGA_ROLL_OK) printf("kernel upload: %s\n", omega_gpu_roll_error(r));
    return rc;
}

/* the one-note baseline: omega_gpu_execute, persistent device */
static double run_note(uint32_t jobs, uint64_t until_ns, uint64_t *done_jobs, int *bad_jobs) {
    static uint8_t code[4096]; size_t len = 0;
    if (omega_blackwell_encode_vecadd(code, sizeof code, &len) != 0) return -1;
    uint32_t a[65], b[65], out[65], poison[65];
    omega_gpu_engine_set_backend(omega_blackwell_engine_backend());
    uint64_t t0 = now_ns(); uint32_t j = 0; *bad_jobs = 0;
    for (; until_ns ? now_ns() < until_ns : j < jobs; j++) {
        for (uint32_t i = 0; i < 65; i++) { a[i] = i + j; b[i] = 3u * i + 7u * j; poison[i] = ~(a[i] + b[i]); }
        OmegaGpuJob job; memset(&job, 0, sizeof job);
        job.program = code; job.program_len = len; job.layout = OMEGA_GPU_LAYOUT_VECTOR_1D; job.element_count = 65;
        job.input_a = a; job.input_a_len = sizeof a; job.input_b = b; job.input_b_len = sizeof b; job.output = out; job.output_len = sizeof out;
        job.poison = poison; job.poison_count = 65; job.timeouts.marker_ms = job.timeouts.release_semaphore_ms = job.timeouts.marker2_ms = job.timeouts.visibility_ms = 20000;
        OmegaGpuResult res;
        int rc = omega_gpu_execute(&job, &res);
        int bad = rc != 0; for (uint32_t i = 0; i < 65 && !bad; i++) bad = out[i] != a[i] + b[i];
        if (bad) { (*bad_jobs)++; if (rc != 0) { printf("note job %u: rc %d (%s)\n", j, rc, omega_gpu_engine_failure_name(rc)); break; } }
    }
    *done_jobs = j;
    return (double)j / ((now_ns() - t0) / 1e9);
}

/* A roll of single-node groups of one case at `depth`; jobs/s. Inputs fixed per case; every output checked. */
typedef struct { double jps; uint64_t jobs, bad_jobs, check_fail, unwritten; uint64_t gpu_p50_ns; } RollStat;
static int roll_cases(OmegaRoll *r, Case **cs, uint32_t ncs, uint32_t depth, uint32_t jobs, uint64_t until_ns, RollStat *st) {
    memset(st, 0, sizeof *st);
    size_t maxout = 0; for (uint32_t c = 0; c < ncs; c++) if (cs[c]->n_out > maxout) maxout = cs[c]->n_out;
    uint8_t *outs = malloc((size_t)depth * maxout * 4);
    uint64_t *gpu = calloc(jobs ? jobs : 1u << 22, 8); size_t ngpu = 0, gcap = jobs ? jobs : 1u << 22;
    uint64_t t0 = now_ns(), sub = 0, ret = 0;
    int rc = 0;
    while (until_ns ? now_ns() < until_ns : sub < jobs) {
        Case *c = cs[sub % ncs];
        OmegaRollNode nd = c->tmpl;
        nd.buf[2].out = outs + (size_t)(sub % depth) * maxout * 4;
        OmegaRollGroup g = { .n = 1, .nodes = &nd, .order = OMEGA_ROLL_ORDER_SERIAL, .skip_node = -1, .tag = sub };
        int s = omega_gpu_roll_submit(r, 0, &g);
        if (s == OMEGA_ROLL_FULL) {
            OmegaRollResult res; int x = omega_gpu_roll_retire(r, 0, &res);
            if (x == OMEGA_ROLL_LATCHED || x == OMEGA_ROLL_ERR) { printf("retire: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
            Case *rc_ = cs[res.tag % ncs];
            if (x == OMEGA_ROLL_CHECK) st->check_fail++;
            st->unwritten += res.unwritten_words;
            if (case_bad(rc_, outs + (size_t)(res.tag % depth) * maxout * 4)) st->bad_jobs++;
            if (ngpu < gcap) gpu[ngpu++] = res.done_ns - res.submit_ns;
            ret++;
            continue;
        }
        if (s != OMEGA_ROLL_OK) { printf("submit: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        sub++;
    }
    while (rc == 0 && omega_gpu_roll_in_flight(r, 0)) {
        OmegaRollResult res; int x = omega_gpu_roll_retire(r, 0, &res);
        if (x == OMEGA_ROLL_LATCHED || x == OMEGA_ROLL_ERR) { printf("retire: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        if (x == OMEGA_ROLL_CHECK) st->check_fail++;
        st->unwritten += res.unwritten_words;
        if (case_bad(cs[res.tag % ncs], outs + (size_t)(res.tag % depth) * maxout * 4)) st->bad_jobs++;
        if (ngpu < gcap) gpu[ngpu++] = res.done_ns - res.submit_ns;
        ret++;
    }
    st->jobs = ret; st->jps = ret / ((now_ns() - t0) / 1e9); st->gpu_p50_ns = median(gpu, ngpu);
    free(outs); free(gpu);
    return rc;
}

/* Chord: groups of K independent copies of a case (own buffers each), ORDER_NONE or SERIAL, voice round robin. */
static int chord_run(OmegaRoll *r, uint32_t voices, Case *c, uint32_t K, int order, uint32_t depth, uint32_t groups, uint64_t until_ns, RollStat *st, int skip) {
    memset(st, 0, sizeof *st);
    const size_t ob = (size_t)c->n_out * 4;
    uint8_t *outs = malloc((size_t)voices * depth * K * ob);
    OmegaRollNode *nodes = malloc(K * sizeof *nodes);
    uint64_t *gpu = calloc(groups ? groups : 1u << 20, 8); size_t ngpu = 0, gcap = groups ? groups : 1u << 20;
    uint32_t *slot_of = calloc(voices, 4);
    uint64_t t0 = now_ns(), sub = 0, jobs = 0; int rc = 0;
    /* per-voice ring position of the next submit, to pick the host out area */
    uint64_t *vsub = calloc(voices, 8), *vret = calloc(voices, 8);
    while (until_ns ? now_ns() < until_ns : sub < groups) {
        uint32_t v = (uint32_t)(sub % voices);
        for (uint32_t i = 0; i < K; i++) {
            nodes[i] = c->tmpl;
            nodes[i].buf[2].out = outs + (((size_t)v * depth + vsub[v] % depth) * K + i) * ob;
        }
        OmegaRollGroup g = { .n = K, .nodes = nodes, .order = order, .skip_node = skip, .tag = sub };
        int s = omega_gpu_roll_submit(r, v, &g);
        if (s == OMEGA_ROLL_FULL) {
            OmegaRollResult res; int x = omega_gpu_roll_retire(r, v, &res);
            if (x == OMEGA_ROLL_LATCHED || x == OMEGA_ROLL_ERR) { printf("retire: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
            if (x == OMEGA_ROLL_CHECK) { st->check_fail++; if (skip >= 0 && st->check_fail == 1) printf("skip control: first_missing %d first_unwritten %d unwritten %" PRIu64 "\n", res.first_missing, res.first_unwritten, res.unwritten_words); }
            st->unwritten += res.unwritten_words;
            for (uint32_t i = 0; i < K; i++) if (case_bad(c, outs + (((size_t)v * depth + vret[v] % depth) * K + i) * ob)) st->bad_jobs++;
            vret[v]++; jobs += K;
            if (ngpu < gcap) gpu[ngpu++] = res.done_ns - res.submit_ns;
            continue;
        }
        if (s != OMEGA_ROLL_OK) { printf("submit: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        vsub[v]++; sub++;
    }
    for (uint32_t v = 0; rc == 0 && v < voices; v++) while (omega_gpu_roll_in_flight(r, v)) {
        OmegaRollResult res; int x = omega_gpu_roll_retire(r, v, &res);
        if (x == OMEGA_ROLL_LATCHED || x == OMEGA_ROLL_ERR) { printf("retire: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        if (x == OMEGA_ROLL_CHECK) { st->check_fail++; if (skip >= 0 && st->check_fail == 1) printf("skip control: first_missing %d first_unwritten %d unwritten %" PRIu64 "\n", res.first_missing, res.first_unwritten, res.unwritten_words); }
        st->unwritten += res.unwritten_words;
        for (uint32_t i = 0; i < K; i++) if (case_bad(c, outs + (((size_t)v * depth + vret[v] % depth) * K + i) * ob)) st->bad_jobs++;
        vret[v]++; jobs += K;
        if (ngpu < gcap) gpu[ngpu++] = res.done_ns - res.submit_ns;
    }
    st->jobs = jobs; st->jps = jobs / ((now_ns() - t0) / 1e9); st->gpu_p50_ns = median(gpu, ngpu);
    free(outs); free(nodes); free(gpu); free(slot_of); free(vsub); free(vret);
    return rc;
}

/* ---- harmony: 64-node graphs of u32 adds, out_j = in1 + in2 where inputs are parents' outputs or host data ---- */
#define H_NODES 64u
#define H_ROOTS 8u
typedef struct { int p[2]; } HNode; /* parent node index per input, -1 = host input */
static void graph_shape(int kind, uint32_t seed, HNode *hn) {
    uint32_t s = seed;
    for (uint32_t j = 0; j < H_NODES; j++) {
        hn[j].p[0] = hn[j].p[1] = -1;
        if (kind == 0) { /* random DAG: 8 roots, then two parents among the previous 12 nodes */
            if (j < H_ROOTS) continue;
            uint32_t lo = j > 12 ? j - 12 : 0;
            hn[j].p[0] = (int)(lo + lcg(&s) % (j - lo));
            hn[j].p[1] = (int)(lo + lcg(&s) % (j - lo));
        } else if (kind == 1) { /* chain of 64 (rx_plan reverse_tower shape, one long dependency line) */
            if (j == 0) continue;
            hn[j].p[0] = (int)j - 1;
        } else { /* fan-in tree: 32 leaves, 16, 8, 4, 2, 1 (rx_plan merge_two_towers shape, generalized); node 63 spare root */
            static const uint32_t lvl[] = { 0, 32, 48, 56, 60, 62, 63 };
            for (int L = 1; L < 6; L++) if (j >= lvl[L] && j < lvl[L + 1]) { uint32_t k = j - lvl[L]; hn[j].p[0] = (int)(lvl[L - 1] + 2 * k); hn[j].p[1] = (int)(lvl[L - 1] + 2 * k + 1); }
        }
    }
}
typedef struct { uint64_t wrong_words, check_fail, unwritten; uint64_t gpu_p50_ns; double graphs_per_s; uint32_t graphs; } HStat;
static int harmony_run(OmegaRoll *r, const OmegaRollKernel *k, int kind, int order, int ignore, uint32_t n_el, uint32_t graphs, uint32_t depth, HStat *st) {
    memset(st, 0, sizeof *st);
    HNode hn[H_NODES]; graph_shape(kind, 77u + (uint32_t)kind, hn);
    const size_t bytes = (size_t)n_el * 4;
    /* host inputs: two per node (only used where p = -1); reference per node */
    uint32_t *hin = malloc(H_NODES * 2 * bytes), *ref = malloc(H_NODES * bytes), *outs = malloc((size_t)depth * H_NODES * bytes);
    uint32_t s = 99u + (uint32_t)kind;
    for (size_t i = 0; i < (size_t)H_NODES * 2 * n_el; i++) hin[i] = lcg(&s);
    for (uint32_t j = 0; j < H_NODES; j++) for (uint32_t i = 0; i < n_el; i++) {
        uint32_t x = hn[j].p[0] >= 0 ? ref[(size_t)hn[j].p[0] * n_el + i] : hin[((size_t)j * 2) * n_el + i];
        uint32_t y = hn[j].p[1] >= 0 ? ref[(size_t)hn[j].p[1] * n_el + i] : hin[((size_t)j * 2 + 1) * n_el + i];
        ref[(size_t)j * n_el + i] = x + y;
    }
    static OmegaRollNode nodes[H_NODES];
    uint64_t *gpu = calloc(graphs, 8); size_t ngpu = 0;
    uint64_t t0 = now_ns(), sub = 0, ret = 0; int rc = 0;
    while (ret < graphs) {
        if (sub < graphs) {
            for (uint32_t j = 0; j < H_NODES; j++) {
                OmegaRollNode *nd = &nodes[j]; memset(nd, 0, sizeof *nd);
                nd->k = k; nd->threads_x = 64; nd->threads_y = 1; nd->grid_x = (n_el + 63) / 64; nd->grid_y = 1; nd->num_elements = n_el;
                nd->n_args = OMEGA_BW_CBANK_ARGS_WORDS; omega_blackwell_build_cbank_args(nd->args, 0, 0, 0, n_el);
                nd->nbufs = 3; nd->deps = 0;
                for (int q = 0; q < 2; q++) {
                    if (hn[j].p[q] >= 0) { nd->buf[q] = (OmegaRollBuf){ .bytes = bytes, .arg_lo = (uint8_t)(2 * q), .from_node = (int8_t)hn[j].p[q], .from_buf = 2 }; nd->deps |= 1ull << hn[j].p[q]; }
                    else nd->buf[q] = (OmegaRollBuf){ .bytes = bytes, .in = hin + ((size_t)j * 2 + q) * n_el, .arg_lo = (uint8_t)(2 * q), .from_node = -1 };
                }
                nd->buf[2] = (OmegaRollBuf){ .bytes = bytes, .out = outs + ((size_t)(sub % depth) * H_NODES + j) * n_el, .poisoned = 1, .poison = POISON, .arg_lo = 4, .from_node = -1 };
            }
            OmegaRollGroup g = { .n = H_NODES, .nodes = nodes, .order = order, .skip_node = -1, .test_ignore_deps = ignore, .tag = sub };
            int x = omega_gpu_roll_submit(r, 0, &g);
            if (x == OMEGA_ROLL_OK) { sub++; continue; }
            if (x != OMEGA_ROLL_FULL) { printf("harmony submit: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        }
        OmegaRollResult res; int x = omega_gpu_roll_retire(r, 0, &res);
        if (x == OMEGA_ROLL_LATCHED || x == OMEGA_ROLL_ERR) { printf("harmony retire: %s\n", omega_gpu_roll_error(r)); rc = -1; break; }
        if (x == OMEGA_ROLL_CHECK) st->check_fail++;
        st->unwritten += res.unwritten_words;
        const uint32_t *o = outs + (size_t)(res.tag % depth) * H_NODES * n_el;
        for (size_t i = 0; i < (size_t)H_NODES * n_el; i++) st->wrong_words += o[i] != ref[i];
        gpu[ngpu++] = res.done_ns - res.submit_ns;
        ret++;
    }
    st->graphs = (uint32_t)ret; st->graphs_per_s = ret / ((now_ns() - t0) / 1e9); st->gpu_p50_ns = median(gpu, ngpu);
    free(hin); free(ref); free(outs); free(gpu);
    return rc;
}

static int chip_run(void) {
    int rc = 0;
    printf("# omega-mv#580 gpu_roll_test chip\n");
    ROW("section,config,jobs,jobs_per_s,bad_jobs,check_fail,unwritten_words,gpu_p50_us,note");

    /* ---- 0. one note: omega_gpu_execute ---- */
    { uint64_t done = 0; int bad = 0; double jps = run_note(600, 0, &done, &bad);
      ROW("note,engine vecadd65 one job at a time,%" PRIu64 ",%.1f,%d,0,0,,omega_gpu_execute persistent", done, jps, bad);
      CHECK(bad == 0 && done == 600, "note: 600 omega_gpu_execute vecadd jobs, every output exact (%.1f jobs/s)", jps);
      (void)omega_blackwell_engine_shutdown(); }

    Prog pv, pvns, pe[K_COUNT], pm[K_COUNT];
    memset(pe, 0, sizeof pe); memset(pm, 0, sizeof pm);
    if (prog_vecadd(&pv, 0) || prog_vecadd(&pvns, 1)) { printf("FAIL: vecadd encode\n"); return 1; }
    pe[K_VECADD] = pv; pm[K_VECADD] = pvns;
    const OmegaGpuEwOp ew[K_COUNT] = { 0, OMEGA_GPU_EW_EX2, OMEGA_GPU_EW_SWIGLU, OMEGA_GPU_EW_RMSNORM, OMEGA_GPU_EW_ROPE, 0 };
    for (int kd = K_EX2; kd <= K_ROPE; kd++) if (prog_ew(&pe[kd], ew[kd], 0) || prog_ew(&pm[kd], ew[kd], 1)) { printf("FAIL: codegen %s\n", KNAME[kd]); return 1; }
    if (prog_matmul(&pm[K_MATMUL], 1) || prog_matmul(&pe[K_MATMUL], 0)) { printf("FAIL: codegen matmul\n"); return 1; }

    /* ---- 1. roll depth ---- */
    {
        OmegaRoll *r = roll_open(1, 64, 1, 1u << 16);
        if (!r) return 1;
        OmegaRollKernel kv; if (upload(r, &pv, &kv)) return 1;
        Case cv; case_build(&cv, K_VECADD, &kv, 65); Case *cs[1] = { &cv };
        const uint32_t depths[] = { 1, 4, 16, 64 };
        for (unsigned i = 0; i < 4; i++) {
            /* a fresh roll per depth so slot count = depth exactly */
            RollStat st;
            OmegaRoll *rd = i == 3 ? r : roll_open(1, depths[i], 1, 1u << 16);
            OmegaRollKernel kd; if (rd != r && upload(rd, &pv, &kd)) return 1;
            Case cd = cv; cd.tmpl.k = rd == r ? &kv : &kd; Case *cds[1] = { &cd };
            if (roll_cases(rd, cds, 1, depths[i], 3000, 0, &st)) rc = 1;
            ROW("roll,vecadd65 depth %u,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,", depths[i], st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
            CHECK(st.jobs == 3000 && st.bad_jobs == 0 && st.check_fail == 0 && st.unwritten == 0, "roll depth %u: 3000 jobs, every output exact, every semaphore DONE, no poison left", depths[i]);
            if (rd != r) omega_gpu_roll_close(rd);
        }
        (void)cs;
        omega_gpu_roll_close(r);
    }

    /* ---- 2. melody ---- */
    {
        OmegaRoll *r = roll_open(1, 16, 1, 1u << 20);
        if (!r) return 1;
        OmegaRollKernel kk[K_COUNT], km[K_COUNT];
        for (int kd = 0; kd < K_COUNT; kd++) if (upload(r, &pe[kd], &kk[kd]) || upload(r, &pm[kd], &km[kd])) return 1;
        Case cs[K_COUNT], cm[K_COUNT];
        for (int kd = 0; kd < K_COUNT; kd++) { case_build(&cs[kd], (Kind)kd, &kk[kd], 4096); cm[kd] = cs[kd]; cm[kd].tmpl.k = &km[kd]; }
        for (int kd = 0; kd < K_COUNT; kd++) {
            RollStat st, sm; Case *one[1] = { &cs[kd] }, *mut[1] = { &cm[kd] };
            if (roll_cases(r, one, 1, 16, 400, 0, &st)) rc = 1;
            ROW("melody,%s alone depth 16,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,", KNAME[kd], st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
            CHECK(st.jobs == 400 && st.bad_jobs == 0 && st.check_fail == 0 && st.unwritten == 0, "melody %s: 400 jobs, every output within tolerance, no poison left", KNAME[kd]);
            if (roll_cases(r, mut, 1, 16, 16, 0, &sm)) rc = 1;
            const char *what = kd == K_VECADD ? "no-store program" : "math mutant";
            ROW("melody,%s %s control,%" PRIu64 ",,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",,", KNAME[kd], what, sm.jobs, sm.bad_jobs, sm.check_fail, sm.unwritten);
            if (kd == K_VECADD) CHECK(sm.check_fail == 16 && sm.unwritten == 16u * 4096u, "vecadd no-store control: caught by the unwritten-word scan in all 16 jobs");
            else CHECK(sm.bad_jobs == 16, "%s math mutant: caught by the reference in all 16 jobs (%" PRIu64 ")", KNAME[kd], sm.bad_jobs);
        }
        Case *mix[K_COUNT]; for (int kd = 0; kd < K_COUNT; kd++) mix[kd] = &cs[kd];
        RollStat st;
        if (roll_cases(r, mix, K_COUNT, 16, 1200, 0, &st)) rc = 1;
        ROW("melody,mixed 6 kernels depth 16,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,vecadd ex2 swiglu rmsnorm rope matmul round robin", st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
        CHECK(st.jobs == 1200 && st.bad_jobs == 0 && st.check_fail == 0 && st.unwritten == 0, "melody mixed roll: 1200 jobs over 6 kernels, all correct");
        omega_gpu_roll_close(r);
        /* the session APIs, same inputs, one call per job */
        float *o = malloc(RMS_ROWS * RMS_DIM * 4); OmegaGpuEwInfo info;
        struct { const char *name; Kind kd; } api[] = { { "ex2", K_EX2 }, { "swiglu", K_SWIGLU }, { "rmsnorm", K_RMSNORM }, { "rope", K_ROPE } };
        for (unsigned a = 0; a < 4; a++) {
            Case *c = &cs[api[a].kd]; uint64_t t0 = now_ns(); int bad = 0; const int N = 200;
            for (int j = 0; j < N; j++) {
                int x;
                if (api[a].kd == K_EX2) x = omega_gpu_ex2_f32(EW_N, c->a, o, &info);
                else if (api[a].kd == K_SWIGLU) x = omega_gpu_swiglu_f32(EW_N, c->a, c->b, o, &info);
                else if (api[a].kd == K_RMSNORM) x = omega_gpu_rmsnorm_f32(RMS_ROWS, RMS_DIM, c->a, c->b, 1e-6f, o, &info);
                else { const float *t = c->b; x = omega_gpu_rope_f32(ROPE_HEADS, ROPE_HD, c->a, t, t + ROPE_HD / 2, o, &info); }
                if (x != 0 || case_bad(c, o)) bad++;
            }
            double jps = N / ((now_ns() - t0) / 1e9);
            ROW("melody,%s session API one call per job,%d,%.1f,%d,,,,omega_gpu_%s_f32", api[a].name, N, jps, bad, api[a].name);
            CHECK(bad == 0, "session API %s: %d calls agree with the same reference", api[a].name, N);
        }
        free(o);
    }

    /* ---- 3. chord ---- */
    {
        OmegaRoll *r = roll_open(1, 4, 64, 4u << 20);
        if (!r) return 1;
        OmegaRollKernel kv; if (upload(r, &pv, &kv)) return 1;
        Case cv; case_build(&cv, K_VECADD, &kv, 65);
        const uint32_t Ks[] = { 1, 4, 16, 64 };
        for (unsigned i = 0; i < 4; i++) {
            RollStat st;
            if (chord_run(r, 1, &cv, Ks[i], OMEGA_ROLL_ORDER_NONE, 4, 4096 / Ks[i] < 64 ? 64 : 4096 / Ks[i], 0, &st, -1)) rc = 1;
            ROW("chord,vecadd65 K=%u per submission depth 4,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,ORDER_NONE", Ks[i], st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
            CHECK(st.bad_jobs == 0 && st.check_fail == 0 && st.unwritten == 0, "chord K=%u: %" PRIu64 " jobs, all exact", Ks[i], st.jobs);
        }
        /* concurrency: 16 medium grids (swiglu 8192 = 64 CTAs each), serial vs chord, depth 1 */
        OmegaRollKernel ks; if (upload(r, &pe[K_SWIGLU], &ks)) return 1;
        Case csw; case_build(&csw, K_SWIGLU, &ks, 0);
        RollStat ser, cho;
        if (chord_run(r, 1, &csw, 16, OMEGA_ROLL_ORDER_SERIAL, 4, 200, 0, &ser, -1)) rc = 1;
        if (chord_run(r, 1, &csw, 16, OMEGA_ROLL_ORDER_NONE, 4, 200, 0, &cho, -1)) rc = 1;
        ROW("chord,swiglu8192 x16 SERIAL,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,WFI between grids", ser.jobs, ser.jps, ser.bad_jobs, ser.check_fail, ser.unwritten, ser.gpu_p50_ns / 1e3);
        ROW("chord,swiglu8192 x16 NONE,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,grids may overlap", cho.jobs, cho.jps, cho.bad_jobs, cho.check_fail, cho.unwritten, cho.gpu_p50_ns / 1e3);
        CHECK(ser.bad_jobs == 0 && cho.bad_jobs == 0 && ser.unwritten == 0 && cho.unwritten == 0, "chord swiglu x16: serial and chord both correct");
        /* negative control: one node dropped from a chord of 16 */
        RollStat sk;
        if (chord_run(r, 1, &cv, 16, OMEGA_ROLL_ORDER_NONE, 4, 8, 0, &sk, 5)) rc = 1;
        CHECK(sk.check_fail == 8 && sk.unwritten == 8u * 65u && sk.bad_jobs == 8, "skip control: the dropped node is caught in all 8 chords (sems + 65 unwritten words each)");
        omega_gpu_roll_close(r);
        /* voices: several channels, one node per submission, depth 16 each */
        const uint32_t Vs[] = { 1, 2, 4 };
        for (unsigned i = 0; i < 3; i++) {
            OmegaRoll *rv = roll_open(Vs[i], 16, 1, 1u << 16);
            if (!rv) { rc = 1; continue; }
            OmegaRollKernel kk; if (upload(rv, &pv, &kk)) return 1;
            Case cc = cv; cc.tmpl.k = &kk;
            RollStat st;
            if (chord_run(rv, Vs[i], &cc, 1, OMEGA_ROLL_ORDER_SERIAL, 16, 3000, 0, &st, -1)) rc = 1;
            ROW("chord,vecadd65 voices=%u depth 16 each,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,channels round robin", Vs[i], st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
            CHECK(st.bad_jobs == 0 && st.check_fail == 0 && st.unwritten == 0, "voices=%u: all exact", Vs[i]);
            omega_gpu_roll_close(rv);
        }
    }

    /* ---- 4. harmony ---- */
    {
        const uint32_t n_el = 262144; /* 1 MiB per buffer: grids long enough to overlap when the order is missing */
        OmegaRoll *r = roll_open(1, 2, 64, (size_t)64 * 3 * n_el * 4 + (1u << 20));
        if (!r) return 1;
        OmegaRollKernel kv; if (upload(r, &pv, &kv)) return 1;
        const char *shape[] = { "random DAG", "chain 64", "fan-in tree" };
        const char *oname[] = { "SERIAL", "NONE", "SEM", "WAVE" };
        for (int kind = 0; kind < 3; kind++) {
            const int orders[] = { OMEGA_ROLL_ORDER_SERIAL, OMEGA_ROLL_ORDER_WAVE, OMEGA_ROLL_ORDER_SEM };
            for (int o = 0; o < 3; o++) {
                HStat st;
                if (harmony_run(r, &kv, kind, orders[o], 0, n_el, 6, 2, &st)) rc = 1;
                ROW("harmony,%s %s,%u,%.2f graphs/s,%" PRIu64 " wrong words,%" PRIu64 ",%" PRIu64 ",%.1f,64 nodes x 256K u32 adds", shape[kind], oname[orders[o]], st.graphs, st.graphs_per_s, st.wrong_words, st.check_fail, st.unwritten, st.gpu_p50_ns / 1e3);
                CHECK(st.graphs == 6 && st.wrong_words == 0 && st.check_fail == 0 && st.unwritten == 0, "harmony %s %s: 6 graphs, every node exact", shape[kind], oname[orders[o]]);
            }
            HStat neg;
            if (harmony_run(r, &kv, kind, OMEGA_ROLL_ORDER_SEM, 1, n_el, 6, 2, &neg)) rc = 1;
            ROW("harmony,%s SEM with ordering removed (control),%u,%.2f graphs/s,%" PRIu64 " wrong words,%" PRIu64 ",%" PRIu64 ",%.1f,informational: wrong words mean the order is load-bearing", shape[kind], neg.graphs, neg.graphs_per_s, neg.wrong_words, neg.check_fail, neg.unwritten, neg.gpu_p50_ns / 1e3);
            printf("harmony %s control: %" PRIu64 " wrong words without ordering (informational)\n", shape[kind], neg.wrong_words);
        }
        omega_gpu_roll_close(r);
    }
    if (omega_gpu_roll_is_latched()) { printf("FAIL: roll latched\n"); rc = 1; }
    return rc || g_failed;
}

static int joules(double secs, const char *what) {
    uint64_t until = now_ns() + (uint64_t)(secs * 1e9);
    if (!strcmp(what, "note")) {
        uint64_t done = 0; int bad = 0; double jps = run_note(0, until, &done, &bad);
        printf("joules,note,%" PRIu64 ",%.1f,%d\n", done, jps, bad);
        return bad != 0;
    }
    Prog pv; if (prog_vecadd(&pv, 0)) return 1;
    if (!strcmp(what, "roll")) {
        OmegaRoll *r = roll_open(1, 16, 1, 1u << 16); if (!r) return 1;
        OmegaRollKernel kv; if (upload(r, &pv, &kv)) return 1;
        Case cv; case_build(&cv, K_VECADD, &kv, 65); Case *cs[1] = { &cv }; RollStat st;
        int rc = roll_cases(r, cs, 1, 16, 0, until, &st);
        printf("joules,roll depth16,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten);
        omega_gpu_roll_close(r);
        return rc || st.bad_jobs || st.check_fail;
    }
    if (!strcmp(what, "chord")) {
        OmegaRoll *r = roll_open(1, 4, 64, 1u << 20); if (!r) return 1;
        OmegaRollKernel kv; if (upload(r, &pv, &kv)) return 1;
        Case cv; case_build(&cv, K_VECADD, &kv, 65); RollStat st;
        int rc = chord_run(r, 1, &cv, 64, OMEGA_ROLL_ORDER_NONE, 4, 0, until, &st, -1);
        printf("joules,chord K64 depth4,%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", st.jobs, st.jps, st.bad_jobs, st.check_fail, st.unwritten);
        omega_gpu_roll_close(r);
        return rc || st.bad_jobs || st.check_fail;
    }
    return 2;
}

int main(int argc, char **argv) {
    int host = 0, chip = 0; double jsec = 0; const char *what = "roll", *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host")) host = 1;
        else if (!strcmp(argv[i], "--chip")) chip = 1;
        else if (!strcmp(argv[i], "--joules") && i + 1 < argc) jsec = atof(argv[++i]);
        else if (!strcmp(argv[i], "--what") && i + 1 < argc) what = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }
    if (out) g_out = fopen(out, "w");
    int rc;
    if (host) rc = host_only();
    else if (jsec > 0) return joules(jsec, what);
    else if (chip) rc = chip_run();
    else { puts("usage: gpu_roll_test --host | --chip [--out csv] | --joules SECS --what note|roll|chord"); return 2; }
    printf("%s: %d checks, %d failed: %s\n", host ? "host" : "chip", g_checks, g_failed, rc || g_failed ? "FAIL" : "PASS");
    if (g_out) fclose(g_out);
    return rc || g_failed ? 1 : 0;
}
