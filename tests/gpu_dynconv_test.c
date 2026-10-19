/* gpu_dynconv_test: the DFlash 2 two-tap grouped dynamic causal conv on the chip
 * (omega_gpu_dynconv2_f32, cascade/dflash-drafter 2026-10-19).
 *
 *   --host-only   refusals (no device) and the codegen of the production kernel and its mutant
 *   (default)     chip parity: every output word bit-identical to the f32 host oracle in the same
 *                 order (FMUL then FADD, no FMA: this file is built with -ffp-contract=off), at the
 *                 drafter's shape (8 rows x 5120) and two others; the mutant (base taps swapped)
 *                 must differ; no output word left at its poison. --out writes the receipt JSON.
 *
 * The oracle is the same arithmetic as aien-sovereign-core crates/aien-inference-abi/src/
 * qwen38_dflash.rs dynconv2 (the CPU reference of the drafter).
 */
#include "omega_gpu_elementwise_api.h"
#include "omega_blackwell_codegen.h"
#include "bw_warp_sim.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }

static void oracle(uint32_t rows, uint32_t hidden, const float *v, const float *dk, const float *base, float *out) {
    uint32_t groups = hidden / 16;
    for (uint32_t p = 0; p < rows; p++)
        for (uint32_t c = 0; c < hidden; c++) {
            uint32_t g = c / 16;
            float v0 = v[(size_t)p * hidden + c];
            float v1 = p == 0 ? 0.0f : v[(size_t)(p - 1) * hidden + c];
            float d0 = dk[(size_t)p * 2 * groups + g], d1 = dk[(size_t)p * 2 * groups + groups + g];
            float o = 0.0f, t;
            t = base[c] * v0; o = o + t;
            t = d0 * v0; o = o + t;
            t = base[hidden + c] * v1; o = o + t;
            t = d1 * v1; o = o + t;
            out[(size_t)p * hidden + c] = o;
        }
}


/* ---------------------------------------------------------------- host simulator (bw_warp_sim) */
/* Scoreboard walk (as tests/gpu_reconv_test.c sb_hazards). */
static int sb_hazards(const BlackwellIRProgram *p) {
    int inflight[64], n = 0, bad = 0;
    for (size_t i = 0; i < p->count; i++) {
        const BlackwellIRInsn *in = &p->insns[i];
        uint32_t c = in->control;
        if ((c >> 20) & 1u) n = 0;
        const int src[3] = { in->src1_vreg, in->src2_vreg, in->src3_vreg };
        for (int s = 0; s < 3; s++) for (int k = 0; src[s] >= 0 && k < n; k++) if (inflight[k] == src[s]) bad++;
        if (c != 0 && ((c >> 14) & 7u) == 0 && in->dst_vreg >= 0 && n < 64) inflight[n++] = in->dst_vreg;
    }
    return bad;
}

static uint8_t g_smem[64];
/* Runs the generated program on the host warp simulator over the same buffers the API stages;
 * returns the simulator error (0 clean) and writes out. */
static int sim_dynconv(const BlackwellIRProgram *p, uint32_t rows, uint32_t hidden, const float *v, const float *dk,
                       const float *base, float *out) {
    uint32_t groups = hidden / 16;
    size_t aw = (size_t)(rows + 1) * hidden, bw = (size_t)2 * hidden + (size_t)rows * 2 * groups, cw = (size_t)rows * hidden;
    float *a = calloc(aw, 4), *b = malloc(bw * 4);
    memcpy(a + hidden, v, cw * 4);
    memcpy(b, base, (size_t)2 * hidden * 4);
    memcpy(b + 2 * hidden, dk, (size_t)rows * 2 * groups * 4);
    for (size_t i = 0; i < cw; i++) { uint32_t pz = 0xffbadbadu; memcpy(&out[i], &pz, 4); }
    WsSim s; memset(&s, 0, sizeof s);
    s.p = p; s.threads = OMEGA_GPU_EW_THREADS; s.smem = g_smem; s.smem_bytes = sizeof g_smem; s.order = 0;
    uint64_t aa = (uintptr_t)a, ba = (uintptr_t)b, ca = (uintptr_t)out;
    s.cbank[0x380 / 4] = (uint32_t)aa; s.cbank[0x380 / 4 + 1] = (uint32_t)(aa >> 32);
    s.cbank[0x388 / 4] = (uint32_t)ba; s.cbank[0x388 / 4 + 1] = (uint32_t)(ba >> 32);
    s.cbank[0x390 / 4] = (uint32_t)ca; s.cbank[0x390 / 4 + 1] = (uint32_t)(ca >> 32);
    s.cbank[0x398 / 4] = hidden; s.cbank[0x39c / 4] = groups;
    s.ranges[0] = (WsRange){ (uintptr_t)aa, (uintptr_t)aa + aw * 4 };
    s.ranges[1] = (WsRange){ (uintptr_t)ba, (uintptr_t)ba + bw * 4 };
    s.ranges[2] = (WsRange){ (uintptr_t)ca, (uintptr_t)ca + cw * 4 };
    s.nranges = 3;
    int err = ws_run(&s, hidden / OMEGA_GPU_EW_THREADS, rows);
    free(a); free(b);
    return err;
}

static void host_sim(void) {
    const uint32_t rows = 3, hidden = 256, groups = hidden / 16;
    size_t n = (size_t)rows * hidden;
    float v[768], dk[96], base[512], want[768], got[768];
    uint32_t seed = 777u;
    for (size_t i = 0; i < n; i++) v[i] = frand(&seed, -3.0f, 3.0f);
    for (size_t i = 0; i < (size_t)rows * 2 * groups; i++) dk[i] = frand(&seed, -0.2f, 0.2f);
    for (uint32_t i = 0; i < 2 * hidden; i++) base[i] = frand(&seed, -1.0f, 1.5f);
    oracle(rows, hidden, v, dk, base, want);
    for (int m = 0; m <= 1; m++) {
        BlackwellIRProgram *p = calloc(1, sizeof *p);
        OmegaBlackwellKernel k; memset(&k, 0, sizeof k);
        int rc = omega_gpu_elementwise_codegen_ir(OMEGA_GPU_EW_DYNCONV2, m, p, &k);
        CHECK(rc == OMEGA_GPU_EW_OK, "sim codegen mutant %d rc %d", m, rc);
        if (rc != OMEGA_GPU_EW_OK) { free(p); continue; }
        int hz = sb_hazards(p);
        CHECK(hz == 0, "mutant %d: %d scoreboard hazards", m, hz);
        int err = sim_dynconv(p, rows, hidden, v, dk, base, got);
        size_t diff = 0;
        for (size_t i = 0; i < n; i++) if (memcmp(&got[i], &want[i], 4) != 0) diff++;
        printf("sim mutant %d: err %d, %zu of %zu differ, hazards %d\n", m, err, diff, n, hz);
        CHECK(err == 0, "sim mutant %d: simulator error %d", m, err);
        if (m == 0) CHECK(diff == 0, "sim production: %zu differ", diff);
        else CHECK(diff > n / 2, "sim mutant: only %zu differ", diff);
        free(k.code); free(p);
    }
}

static void host_only(void) {
    float a[256] = { 0 }, b[64] = { 0 }, c[256], d[512] = { 0 };
    CHECK(omega_gpu_dynconv2_f32(0, 128, 16, a, b, d, c, NULL) == OMEGA_GPU_EW_BAD_ARGS, "rows 0 refused");
    CHECK(omega_gpu_dynconv2_f32(1, 128, 8, a, b, d, c, NULL) == OMEGA_GPU_EW_BAD_ARGS, "group 8 refused");
    CHECK(omega_gpu_dynconv2_f32(1, 100, 16, a, b, d, c, NULL) == OMEGA_GPU_EW_BAD_ARGS, "hidden 100 refused");
    CHECK(omega_gpu_dynconv2_f32(1, 128, 16, NULL, b, d, c, NULL) == OMEGA_GPU_EW_BAD_ARGS, "NULL refused");
    CHECK(omega_gpu_dynconv2_f32(1025, 128, 16, a, b, d, c, NULL) == OMEGA_GPU_EW_TOO_LARGE, "rows 1025 too large");
    CHECK(omega_gpu_dynconv2_f32(1, 16512, 16, a, b, d, c, NULL) == OMEGA_GPU_EW_TOO_LARGE, "hidden 16512 too large");
    host_sim();
    for (int m = 0; m <= 1; m++) {
        OmegaBlackwellKernel k;
        int rc = omega_gpu_elementwise_codegen_ir(OMEGA_GPU_EW_DYNCONV2, m, NULL, &k);
        CHECK(rc == OMEGA_GPU_EW_OK, "codegen mutant %d rc %d", m, rc);
        if (rc == OMEGA_GPU_EW_OK) {
            printf("codegen mutant %d: %zu insns, %u gprs\n", m, k.insn_count, k.gpr_count);
            free(k.code);
        }
    }
}

static int chip(const char *out_path) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out) fprintf(out, "{\"schema\":\"OMEGA_GPU_DYNCONV2_V1\",\"rule\":\"bit-identical to the f32 oracle\",\"cases\":[");
    const uint32_t shapes[3][2] = { { 8, 5120 }, { 1, 5120 }, { 3, 256 } };
    int first = 1;
    for (int si = 0; si < 3; si++) {
        uint32_t rows = shapes[si][0], hidden = shapes[si][1], groups = hidden / 16;
        size_t n = (size_t)rows * hidden, nd = (size_t)rows * 2 * groups;
        float *v = malloc(n * 4), *dk = malloc(nd * 4), *base = malloc((size_t)2 * hidden * 4);
        float *want = malloc(n * 4), *got = malloc(n * 4);
        uint32_t seed = 12345u + (uint32_t)si;
        for (size_t i = 0; i < n; i++) v[i] = frand(&seed, -3.0f, 3.0f);
        for (size_t i = 0; i < nd; i++) dk[i] = frand(&seed, -0.2f, 0.2f);
        for (uint32_t i = 0; i < hidden; i++) { base[i] = frand(&seed, 0.5f, 1.5f); base[hidden + i] = frand(&seed, -0.3f, 0.3f); }
        oracle(rows, hidden, v, dk, base, want);
        OmegaGpuEwInfo info;
        int rc = omega_gpu_dynconv2_f32(rows, hidden, 16, v, dk, base, got, &info);
        size_t diff = 0;
        for (size_t i = 0; i < n; i++) if (memcmp(&got[i], &want[i], 4) != 0) diff++;
        CHECK(rc == OMEGA_GPU_EW_OK, "%ux%u rc %d (%s)", rows, hidden, rc, omega_gpu_elementwise_last_error());
        CHECK(diff == 0, "%ux%u: %zu of %zu words differ from the oracle", rows, hidden, diff, n);
        CHECK(info.unwritten_words == 0, "%ux%u: %u unwritten", rows, hidden, info.unwritten_words);
        uint64_t chip_ns = info.elapsed_ns;
        if (rc != 0) printf("  error: %s\n", omega_gpu_elementwise_last_error());
        /* the production kernel a second time (two launches of one resident kernel) */
        int rc1 = omega_gpu_dynconv2_f32(rows, hidden, 16, v, dk, base, got, &info);
        size_t diff1 = 0;
        for (size_t i = 0; i < n; i++) if (memcmp(&got[i], &want[i], 4) != 0) diff1++;
        CHECK(rc1 == OMEGA_GPU_EW_OK && diff1 == 0, "%ux%u second launch: rc %d, %zu differ (%s)", rows, hidden, rc1, diff1, rc1 ? omega_gpu_elementwise_last_error() : "");
        uint32_t gprs = info.gpr_count, insns = info.insn_count;
        omega_gpu_elementwise_test_set_mutant(OMEGA_GPU_EW_DYNCONV2);
        int mrc = omega_gpu_dynconv2_f32(rows, hidden, 16, v, dk, base, got, &info);
        omega_gpu_elementwise_test_set_mutant(0);
        if (mrc != 0) printf("  mutant error: %s\n", omega_gpu_elementwise_last_error());
        size_t mdiff = 0;
        for (size_t i = 0; i < n; i++) if (memcmp(&got[i], &want[i], 4) != 0) mdiff++;
        CHECK(mrc == OMEGA_GPU_EW_OK && mdiff > n / 2, "%ux%u mutant: rc %d, %zu of %zu differ (must be most)", rows, hidden, mrc, mdiff, n);
        /* after clearing the mutant, the production kernel again */
        int rc2 = omega_gpu_dynconv2_f32(rows, hidden, 16, v, dk, base, got, &info);
        size_t diff2 = 0;
        for (size_t i = 0; i < n; i++) if (memcmp(&got[i], &want[i], 4) != 0) diff2++;
        CHECK(rc2 == OMEGA_GPU_EW_OK && diff2 == 0, "%ux%u after mutant cleared: rc %d, %zu differ", rows, hidden, rc2, diff2);
        printf("dynconv2 %ux%u: rc %d, differ %zu, mutant differ %zu, again %zu, chip %.1f us, %u gprs, %u insns\n",
               rows, hidden, rc, diff, mdiff, diff2, chip_ns / 1e3, gprs, insns);
        if (out) {
            fprintf(out, "%s{\"rows\":%u,\"hidden\":%u,\"rc\":%d,\"words\":%zu,\"differ\":%zu,\"mutant_differ\":%zu,\"again_differ\":%zu,"
                         "\"unwritten\":%u,\"chip_ns\":%" PRIu64 ",\"gprs\":%u,\"insns\":%u}",
                    first ? "" : ",", rows, hidden, rc, n, diff, mdiff, diff2, info.unwritten_words, chip_ns, gprs, insns);
            first = 0;
        }
        free(v); free(dk); free(base); free(want); free(got);
    }
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d}\n", g_checks, g_failed); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    const char *out_path = NULL; int host = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
    }
    if (host) host_only();
    else if (chip(out_path) != 0) return 2;
    printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
