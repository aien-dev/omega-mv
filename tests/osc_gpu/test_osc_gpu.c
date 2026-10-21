/* test_osc_gpu: the Omegese GPU back end (src/omega_osc_gpu.c, L386) on the host. No device.
 *
 *   test_osc_gpu <ir-dir>
 *
 * <ir-dir> holds kernel_<name>.ir for the accepted kernels of tests/checker2/kernels (made by omega_emit;
 * see mk/omega-osc-gpu.mk). For each kernel: decode, build, then run the machine program on the SIMT warp
 * simulator (tests/bw_warp_sim.h: divergence, BSSY/BSYNC reconvergence, SHFL and BAR.SYNC participation)
 * under both fragment orders, against a host reference. Every comparison is bit-exact: the simulator's
 * EX2 and RCP are exact functions and the reference uses the same ones (the chip's are approximate; the
 * chip run states its own tolerance). Also: the guard-predicate encoder words, the division sequence
 * against n / d, the trap path (status word, poison, no stores after a trap) and the refusals. */
#include "omega_osc_gpu.h"
#include "bw_warp_sim.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed, g_quiet;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; if (!g_quiet) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s) { return (float)((int32_t)(lcg(s) >> 8) - (1 << 23)) / (float)(1 << 21); } /* about -4..4 */
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static OscUnit g_unit;
static const char *g_dir;

static int load_kernel(const char *name, OscGpuKernel *k) {
    char path[512];
    snprintf(path, sizeof path, "%s/kernel_%s.ir", g_dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) { CHECK(0, "%s: cannot open %s", name, path); return -1; }
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    int rc = osc_ir_decode(buf, n, &g_unit);
    CHECK(rc == 0, "%s: decode refused (%s)", name, osc_ir_refusal_name(rc));
    if (rc) return -1;
    char err[256];
    rc = osc_gpu_build(&g_unit, 0, k, err, sizeof err);
    CHECK(rc == 0, "%s: build refused: %s", name, err);
    if (rc) return -1;
    if (!g_quiet) printf("  %-7s %3u instructions, %3u registers, %u threads, %u shared bytes, %u regions on %u barriers, %u argument words\n",
           name, k->insn_count, k->gpr_count, k->threads, k->shared_bytes, k->regions, k->region_bars, k->arg_words);
    return 0;
}

/* simulator set-up: argument words, status buffer, memory ranges */
typedef struct { WsSim s; uint32_t status[OSC_GPU_STATUS_WORDS]; uint8_t smem[OSC_MAX_SHARED_BYTES]; } Run;
static void run_init(Run *r, const OscGpuKernel *k, uint32_t blocks, uint32_t batches) {
    memset(r, 0, sizeof *r);
    r->s.p = k->prog; r->s.threads = k->threads; r->s.smem = r->smem; r->s.smem_bytes = k->shared_bytes;
    uint64_t st = (uint64_t)(uintptr_t)r->status;
    uint32_t w = (OSC_GPU_ARG_BASE + k->status_off) / 4;
    r->s.cbank[w] = (uint32_t)st; r->s.cbank[w + 1] = (uint32_t)(st >> 32);
    r->s.cbank[(OSC_GPU_ARG_BASE + k->blocks_off) / 4] = blocks;
    r->s.cbank[(OSC_GPU_ARG_BASE + k->batches_off) / 4] = batches;
    r->s.ranges[r->s.nranges++] = (WsRange){ (uintptr_t)r->status, (uintptr_t)(r->status + OSC_GPU_STATUS_WORDS) };
}
static void run_buf(Run *r, const OscGpuKernel *k, unsigned param, void *p, uint32_t elems) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    uint32_t w = (OSC_GPU_ARG_BASE + k->params[param].off) / 4;
    r->s.cbank[w] = (uint32_t)a; r->s.cbank[w + 1] = (uint32_t)(a >> 32); r->s.cbank[w + 2] = elems;
    r->s.ranges[r->s.nranges++] = (WsRange){ (uintptr_t)p, (uintptr_t)p + 4u * (uintptr_t)elems };
}
static int run_go(Run *r, int order, uint32_t gx, uint32_t gy, const char *what) {
    r->s.order = order;
    int e = ws_run(&r->s, gx, gy);
    CHECK(e == 0, "%s (order %d): simulator: %s", what, order, r->s.msg);
    if (e) memset(r->status, 0, sizeof r->status);
    return e;
}
static int status_code(const Run *r) { for (unsigned i = 1; i < OSC_GPU_STATUS_WORDS; i++) if (r->status[i]) return (int)i; return 0; }

/* ---------------------------------------------------------------- kernels */

static void test_vadd(void) {
    OscGpuKernel k;
    if (load_kernel("vadd", &k)) return;
    enum { N = 1000 };
    static float a[N], b[N], c[N];
    uint32_t seed = 1;
    for (int i = 0; i < N; i++) { a[i] = frand(&seed); b[i] = frand(&seed); }
    for (int order = 0; order < 2; order++) {
        for (int i = 0; i < N; i++) c[i] = bitsf(0xdeadbeefu);
        Run r; run_init(&r, &k, 4, 1);
        run_buf(&r, &k, 0, a, N); run_buf(&r, &k, 1, b, N); run_buf(&r, &k, 2, c, N);
        if (run_go(&r, order, 4, 1, "vadd")) continue;
        int bad = 0;
        for (int i = 0; i < N; i++) if (fbits(c[i]) != fbits(a[i] + b[i])) bad++;
        CHECK(bad == 0 && status_code(&r) == 0, "vadd order %d: %d wrong words, status %d", order, bad, status_code(&r));
        /* the trap path: a and b shorter than c. Threads 900..999 trap 3 on the load, take 0, and store nothing. */
        for (int i = 0; i < N; i++) c[i] = bitsf(0xdeadbeefu);
        run_init(&r, &k, 4, 1);
        run_buf(&r, &k, 0, a, 900); run_buf(&r, &k, 1, b, 900); run_buf(&r, &k, 2, c, N);
        if (run_go(&r, order, 4, 1, "vadd short inputs")) continue;
        bad = 0;
        for (int i = 0; i < N; i++) if (fbits(c[i]) != (i < 900 ? fbits(a[i] + b[i]) : 0xdeadbeefu)) bad++;
        CHECK(bad == 0, "vadd short inputs order %d: %d words wrong (a trapped thread stored, or a good one did not)", order, bad);
        CHECK(r.status[3] == 1 && status_code(&r) == 3, "vadd short inputs: status code %d, want 3", status_code(&r));
        unsigned others = 0; for (unsigned i = 0; i < OSC_GPU_STATUS_WORDS; i++) if (i != 3) others += r.status[i];
        CHECK(others == 0, "vadd short inputs: other status words set");
    }
    osc_gpu_free(&k);
}

static void test_gate(void) {
    OscGpuKernel k;
    if (load_kernel("gate", &k)) return;
    enum { N = 1000 };
    static float a0[N], a[N], gg[N];
    uint32_t seed = 7;
    for (int i = 0; i < N; i++) { a0[i] = frand(&seed); gg[i] = frand(&seed) * 4.0f; }
    for (int order = 0; order < 2; order++) {
        memcpy(a, a0, sizeof a);
        Run r; run_init(&r, &k, 4, 1);
        run_buf(&r, &k, 0, a, N); run_buf(&r, &k, 1, gg, N);
        if (run_go(&r, order, 4, 1, "gate")) continue;
        int bad = 0;
        for (int i = 0; i < N; i++) {
            float m = -gg[i] * bitsf(0x3fb8aa3bu);          /* the FMUL before EX2: one rounding */
            float want = a0[i] * (1.0f / (exp2f(m) + 1.0f)); /* simulator EX2 = exp2f, RCP = 1/x */
            if (fbits(a[i]) != fbits(want)) bad++;
        }
        CHECK(bad == 0 && status_code(&r) == 0, "gate order %d: %d wrong words", order, bad);
    }
    osc_gpu_free(&k);
}

static void test_gemm(void) {
    OscGpuKernel k;
    if (load_kernel("gemm", &k)) return;
    enum { M = 64 };
    static float A[M * M], B[M * M], C[M * M];
    uint32_t seed = 3;
    for (int i = 0; i < M * M; i++) { A[i] = frand(&seed); B[i] = frand(&seed); }
    for (int order = 0; order < 2; order++) {
        for (int i = 0; i < M * M; i++) C[i] = bitsf(0xdeadbeefu);
        Run r; run_init(&r, &k, 4, 4);
        run_buf(&r, &k, 0, A, M * M); run_buf(&r, &k, 1, B, M * M); run_buf(&r, &k, 2, C, M * M);
        if (run_go(&r, order, 4, 4, "gemm")) continue;
        int bad = 0;
        for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) {
            float acc = 0.0f;
            for (int kk = 0; kk < M; kk++) acc = fmaf(A[i * M + kk], B[kk * M + j], acc);
            if (fbits(C[i * M + j]) != fbits(acc)) bad++;
        }
        CHECK(bad == 0 && status_code(&r) == 0, "gemm order %d: %d wrong words, status %d", order, bad, status_code(&r));
    }
    osc_gpu_free(&k);
}

static void test_reduce(void) {
    OscGpuKernel k;
    if (load_kernel("reduce", &k)) return;
    enum { BL = 4, N = BL * 256 };
    static float x[N], out[BL];
    uint32_t seed = 11;
    for (int i = 0; i < N; i++) x[i] = frand(&seed);
    float want[BL];
    for (int b = 0; b < BL; b++) {
        float part[8];
        for (int w = 0; w < 8; w++) {
            float v[32];
            for (int l = 0; l < 32; l++) v[l] = x[b * 256 + w * 32 + l];
            for (int d = 16; d >= 1; d /= 2) {
                float n[32];
                for (int l = 0; l < 32; l++) n[l] = v[l] + (l + d < 32 ? v[l + d] : v[l]);
                memcpy(v, n, sizeof v);
            }
            part[w] = v[0];
        }
        float acc = 0.0f;
        for (int w = 0; w < 8; w++) acc = acc + part[w];
        want[b] = acc;
    }
    for (int order = 0; order < 2; order++) {
        for (int b = 0; b < BL; b++) out[b] = bitsf(0xdeadbeefu);
        Run r; run_init(&r, &k, BL, 1);
        run_buf(&r, &k, 0, x, N); run_buf(&r, &k, 1, out, BL);
        if (run_go(&r, order, BL, 1, "reduce")) continue;
        int bad = 0;
        for (int b = 0; b < BL; b++) if (fbits(out[b]) != fbits(want[b])) bad++;
        CHECK(bad == 0 && status_code(&r) == 0, "reduce order %d: %d wrong sums, status %d", order, bad, status_code(&r));
    }
    osc_gpu_free(&k);
}

static void test_nest16(void) {
    OscGpuKernel k;
    if (load_kernel("nest16", &k)) return;
    static uint32_t buf[32];
    for (int order = 0; order < 2; order++) {
        Run r; run_init(&r, &k, 1, 1);
        run_buf(&r, &k, 0, buf, 32);
        if (run_go(&r, order, 1, 1, "nest16")) continue;
        CHECK(status_code(&r) == 0, "nest16: status %d", status_code(&r));
    }
    osc_gpu_free(&k);
}

/* the float equality the references use: NaN results compare equal whatever their payload (GPU.md section 4) */
static bool same_f(float x, float y) { return (x != x && y != y) || fbits(x) == fbits(y); }

/* float inputs: random values, then the special ones (zeros, infinities, NaN, denormals, the u32 range edges) */
static void float_inputs(float *v, int n, uint32_t seed) {
    static const uint32_t special[] = { 0x00000000u, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7fa00001u,
        0x00000001u, 0x807fffffu, 0x3f800000u, 0xbf800000u, 0xbf000000u, 0x4f000000u, 0x4f7fffffu, 0x4f800000u,
        0x3effffffu, 0x7f7fffffu, 0x4b800001u, 0x3f7fffffu, 0xbf7fffffu, 0x42c80000u };
    for (int i = 0; i < n; i++) v[i] = frand(&seed) * (i % 3 == 0 ? 1e6f : 1.0f);
    for (unsigned i = 0; i < sizeof special / sizeof special[0] && (int)(2 * i + 1) < n; i++) v[2 * i + 1] = bitsf(special[i]);
}

/* ops_u: one launch per op code; r = 0 before the dispatch; a trapping thread stores nothing */
static uint32_t ref_u(int op, uint32_t a, uint32_t b, float fa, float fb, int *trap) {
    *trap = 0;
    switch (op) {
    case 0: if (a + b < a) *trap = 1; return a + b;
    case 1: if (a < b) *trap = 1; return a - b;
    case 2: if ((uint64_t)a * b >> 32) *trap = 1; return a * b;
    case 3: return a / 7; case 4: return a % 7; case 5: return a / 16; case 6: return a % 1000;
    case 7: return a & b; case 8: return a | b; case 9: return a ^ b;
    case 10: if (b >= 32) { *trap = 7; return 0; } return a << b;
    case 11: if (b >= 32) { *trap = 7; return 0; } return a >> b;
    case 12: return a < b ? a : b; case 13: return a > b ? a : b;
    case 14: return 0u - a; case 15: return ~a;
    case 16: return a < b; case 17: return a <= b; case 18: return a > b; case 19: return a >= b; case 20: return a == b; case 21: return a != b;
    case 22: return fa < fb; case 23: return fa <= fb; case 24: return fa > fb; case 25: return fa >= fb; case 26: return fa == fb; case 27: return !(fa == fb);
    case 28: if (!(fa > -1.0f && fa < 4294967296.0f)) { *trap = 5; return 0; } return (uint32_t)fa;
    case 29: return a != b; case 30: return a <= b; case 31: return a != b; case 32: return 0;
    default: return 0;
    }
}
#define OPS_U 33
static void test_ops_u(void) {
    OscGpuKernel k;
    if (load_kernel("ops_u", &k)) return;
    enum { N = 256 };
    static uint32_t x[N], y[N], ou[N];
    static float f[N], h[N];
    uint32_t seed = 21;
    for (int t = 0; t < N; t++) {
        switch (t % 4) {
        case 0: x[t] = lcg(&seed); y[t] = lcg(&seed) % 40; break;          /* shift amounts, some >= 32 */
        case 1: x[t] = lcg(&seed); y[t] = lcg(&seed); break;               /* overflows */
        case 2: x[t] = lcg(&seed) % 100; y[t] = lcg(&seed) % 100; break;
        default: x[t] = lcg(&seed); y[t] = t % 8 ? lcg(&seed) % 1000 : x[t]; break;
        }
    }
    x[3] = 0xffffffffu; y[3] = 1; x[7] = 0; y[7] = 0xffffffffu; x[11] = 0x10000u; y[11] = 0x10000u; x[15] = 0xffffu; y[15] = 0x10001u;
    float_inputs(f, N, 31); float_inputs(h, N, 41);
    h[0] = f[0]; h[2] = f[2]; h[4] = bitsf(0x80000000u); f[4] = 0.0f; /* equal pairs, -0 == +0 */
    int caught[OSC_GPU_STATUS_WORDS] = { 0 };
    for (int op = 0; op < OPS_U; op++) {
        uint32_t want[N]; int traps = 0; bool code_seen[OSC_GPU_STATUS_WORDS] = { false };
        for (int t = 0; t < N; t++) { int tr; want[t] = ref_u(op, x[t], y[t], f[t], h[t], &tr); if (tr) { traps++; code_seen[tr] = true; want[t] = 0xdeadbeefu; } }
        for (int order = 0; order < 2; order++) {
            for (int t = 0; t < N; t++) ou[t] = 0xdeadbeefu;
            Run r; run_init(&r, &k, 1, 1);
            run_buf(&r, &k, 0, x, N); run_buf(&r, &k, 1, y, N); run_buf(&r, &k, 2, f, N); run_buf(&r, &k, 3, h, N); run_buf(&r, &k, 4, ou, N);
            r.s.cbank[(OSC_GPU_ARG_BASE + k.params[5].off) / 4] = (uint32_t)op;
            if (run_go(&r, order, 1, 1, "ops_u")) continue;
            int bad = 0, first = -1;
            for (int t = 0; t < N; t++) if (ou[t] != want[t]) { bad++; if (first < 0) first = t; }
            CHECK(bad == 0, "ops_u op %d order %d: %d wrong (first t %d: got 0x%08x want 0x%08x, a 0x%08x b 0x%08x)", op, order, bad, first,
                  first >= 0 ? ou[first] : 0, first >= 0 ? want[first] : 0, first >= 0 ? x[first] : 0, first >= 0 ? y[first] : 0);
            for (unsigned c = 1; c < OSC_GPU_STATUS_WORDS; c++) CHECK(r.status[c] == (code_seen[c] ? 1u : 0u), "ops_u op %d: status word %u is %u", op, c, r.status[c]);
            if (order == 0) for (unsigned c = 1; c < OSC_GPU_STATUS_WORDS; c++) caught[c] += r.status[c] != 0;
        }
        (void)traps;
    }
    CHECK(caught[1] >= 3 && caught[5] == 1 && caught[7] == 2, "ops_u: the trap ops did not all trap (1: %d, 5: %d, 7: %d)", caught[1], caught[5], caught[7]);
    osc_gpu_free(&k);
}

/* ops_f: float results */
static float ref_f(int op, float fa, float fb, uint32_t xa) {
    switch (op) {
    case 0: return fa + fb; case 1: return fa - fb; case 2: return fa * fb;
    case 3: return fminf(fa, fb); case 4: return fmaxf(fa, fb);
    case 5: return bitsf(fbits(fa) ^ 0x80000000u);
    case 6: return fmaf(fa, fb, fa);
    case 7: { float m = fa * bitsf(0x3fb8aa3bu); return exp2f(m); }
    case 8: return 1.0f / fa; case 9: return 1.0f / sqrtf(fa); case 10: return 1.0f / (1.0f / sqrtf(fa));
    case 11: return (float)xa;
    case 12: return 0.0f;
    default: return 0.0f;
    }
}
#define OPS_F 13
static void test_ops_f(void) {
    OscGpuKernel k;
    if (load_kernel("ops_f", &k)) return;
    enum { N = 256 };
    static float f[N], h[N], of[N];
    static uint32_t x[N];
    float_inputs(f, N, 51); float_inputs(h, N, 61);
    uint32_t seed = 71;
    for (int t = 0; t < N; t++) x[t] = t % 2 ? lcg(&seed) : lcg(&seed) >> (t % 32);
    x[0] = 0xffffffffu; x[2] = 0x80000000u; x[4] = 0x7fffffffu; x[6] = 16777217u; x[8] = 0xffffff7fu; x[10] = 0xffffff80u;
    f[1] = -0.0f; h[1] = 0.0f; f[3] = 0.0f; h[3] = -0.0f; /* min/max of zeros */
    for (int op = 0; op < OPS_F; op++)
        for (int order = 0; order < 2; order++) {
            for (int t = 0; t < N; t++) of[t] = bitsf(0xdeadbeefu);
            Run r; run_init(&r, &k, 1, 1);
            run_buf(&r, &k, 0, f, N); run_buf(&r, &k, 1, h, N); run_buf(&r, &k, 2, x, N); run_buf(&r, &k, 3, of, N);
            r.s.cbank[(OSC_GPU_ARG_BASE + k.params[4].off) / 4] = (uint32_t)op;
            if (run_go(&r, order, 1, 1, "ops_f")) continue;
            int bad = 0, first = -1;
            for (int t = 0; t < N; t++) if (!same_f(of[t], ref_f(op, f[t], h[t], x[t]))) { bad++; if (first < 0) first = t; }
            CHECK(bad == 0 && status_code(&r) == 0, "ops_f op %d order %d: %d wrong (first t %d: got 0x%08x want 0x%08x), status %d", op, order, bad, first,
                  first >= 0 ? fbits(of[first]) : 0, first >= 0 ? fbits(ref_f(op, f[first], h[first], x[first])) : 0, status_code(&r));
        }
    osc_gpu_free(&k);
}

static void test_nest16j(void) {
    OscGpuKernel k;
    if (load_kernel("nest16j", &k)) return;
    CHECK(k.regions == 16 && k.region_bars == 16, "nest16j: %u regions on %u barriers, want 16 on 16", k.regions, k.region_bars);
    static uint32_t out[32];
    for (int order = 0; order < 2; order++) {
        for (int t = 0; t < 32; t++) out[t] = 0xdeadbeefu;
        Run r; run_init(&r, &k, 1, 1);
        run_buf(&r, &k, 0, out, 32);
        if (run_go(&r, order, 1, 1, "nest16j")) continue;
        int bad = 0;
        for (uint32_t t = 0; t < 32; t++) {
            uint32_t want = 1;                                   /* J_0 */
            for (uint32_t i = 1; i <= 15; i++) if (t < 33 - i) want++;  /* J_i reached with B_i */
            if (t < 17) want += 100;                             /* B_16 */
            if (out[t] != want) bad++;
        }
        CHECK(bad == 0 && status_code(&r) == 0, "nest16j order %d: %d wrong counts", order, bad);
        CHECK(r.s.peak_bars == 16, "nest16j order %d: %d barriers armed at once, want 16", order, r.s.peak_bars);
    }
    osc_gpu_free(&k);
}

static void test_vloop(void) {
    OscGpuKernel k;
    if (load_kernel("vloop", &k)) return;
    static uint32_t x[64], out[64];
    uint32_t seed = 81;
    for (int t = 0; t < 64; t++) x[t] = lcg(&seed);
    for (int order = 0; order < 2; order++) {
        for (int t = 0; t < 64; t++) out[t] = 0xdeadbeefu;
        Run r; run_init(&r, &k, 1, 1);
        run_buf(&r, &k, 0, x, 64); run_buf(&r, &k, 1, out, 64);
        if (run_go(&r, order, 1, 1, "vloop")) continue;
        int bad = 0;
        for (int t = 0; t < 64; t++) { uint32_t n = x[t] % 8, want = 0; for (uint32_t i = 0; i < n; i++) want += 3 * i + 1; if (out[t] != want) bad++; }
        CHECK(bad == 0 && status_code(&r) == 0, "vloop order %d: %d wrong sums", order, bad);
        CHECK(r.s.splits > 0, "vloop order %d: the loop never split the warp (not a varying loop)", order);
    }
    osc_gpu_free(&k);
}

static void kernels(void) {
    test_vadd(); test_gate(); test_gemm(); test_reduce(); test_nest16(); test_ops_u(); test_ops_f(); test_nest16j(); test_vloop();
}

/* each mutant of the back end breaks one rule; the kernel tests must catch every one */
static void test_mutants(void) {
    static const char *const what[] = { "", "stores ignore poison", "joins dropped", "no add overflow trap", "intervals not stretched",
                                        "every region on barrier 0", "shared not zeroed", "bounds-failed loads run", "division magic off by one" };
    for (int m = 1; m <= 8; m++) {
        int checks = g_checks, failed = g_failed;
        g_quiet = 1; osc_gpu_mutant = m;
        kernels();
        osc_gpu_mutant = 0; g_quiet = 0;
        int caught = g_failed - failed;
        g_checks = checks; g_failed = failed;
        CHECK(caught > 0, "mutant %d (%s) was not caught", m, what[m]);
        printf("  mutant %d (%s): caught by %d checks\n", m, what[m], caught);
    }
}

/* ---------------------------------------------------------------- pieces */

static void test_division(void) {
    uint32_t seed = 5;
    static const uint32_t ds[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 12, 13, 16, 25, 32, 100, 255, 256, 641, 1000, 6700417,
                                   0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu };
    long bad = 0, n = 0;
    for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
        uint32_t d = ds[i];
        uint32_t edge[] = { 0, 1, d - 1, d, d + 1, 2 * d - 1, 2 * d, 0xffffffffu, 0xfffffffeu, 0x80000000u, 0x7fffffffu };
        for (unsigned e = 0; e < sizeof edge / sizeof edge[0]; e++, n++) if (osc_gpu_udiv_model(edge[e], d) != edge[e] / d) bad++;
        for (int t = 0; t < 200000; t++, n++) { uint32_t x = lcg(&seed); if (osc_gpu_udiv_model(x, d) != x / d) bad++; }
    }
    for (uint32_t d = 1; d < 3000; d++)
        for (int t = 0; t < 300; t++, n++) { uint32_t x = lcg(&seed); if (osc_gpu_udiv_model(x, d) != x / d) bad++; }
    CHECK(bad == 0, "division by a number: %ld of %ld quotients wrong", bad, n);
    printf("  division by a number: %ld quotients checked\n", n);
}

static void test_refusals(void) {
    OscGpuKernel k;
    char err[256];
    CHECK(osc_gpu_build(&g_unit, 5, &k, err, sizeof err) != 0, "a missing function is not refused");
    /* a host function is not a kernel */
    static OscUnit u;
    memset(&u, 0, sizeof u);
    u.nfuncs = 1; snprintf(u.funcs[0].name, sizeof u.funcs[0].name, "host");
    u.funcs[0].kind = OSC_FN_HOST; u.funcs[0].nblocks = 1; u.funcs[0].ninsns = 1;
    u.funcs[0].insns[0].op = OSC_I_RET; u.funcs[0].insns[0].a = -1; u.funcs[0].blocks[0].count = 1;
    CHECK(osc_gpu_build(&u, 0, &k, err, sizeof err) != 0 && strstr(err, "not a kernel"), "a host function is not refused (%s)", err);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: test_osc_gpu <ir-dir>\n"); return 2; }
    g_dir = argv[1];
    CHECK(omega_blackwell_verify_codegen_fixtures_guard() == 0, "guard predicate / conversion words (rc %d)", omega_blackwell_verify_codegen_fixtures_guard());
    test_division();
    kernels();
    test_refusals();
    test_mutants();
    printf("test_osc_gpu: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
