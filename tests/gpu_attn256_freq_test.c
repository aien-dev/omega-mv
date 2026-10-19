/*
 * gpu_attn256_freq_test: FREQUENCY (omega-mv#570, FORTRAN 1956 idea 2) for the Qwen3.8-27B decode attention kernel
 * (omega_blackwell_codegen_attn256.c). The kernel's V pass has one data-dependent, warp-uniform branch: a step skips
 * the rescale block (alpha = EX2(m_old - m_new) on acc and l, 6 EX2 + 30 FMUL) when no head's running max moved.
 * How often it is taken depends on the score sequence each warp sees (positions cb + pair + 4i of its chunk).
 *
 * Inputs whose odds we control: q is 1.0 on the 192 unrotated kernel slots (so RoPE and RMSNorm change only its
 * scale), K row j is c_j / 16 on the same slots (bf16-exact), so score_j = 1.25 * c_j in log2 units, with integer c_j.
 * Rounding is monotone, so the order of the chip's f32 scores is the order of the integers: the host replica of the
 * branch is exact. The new token's K and V are zero (score 0 at position pos). Classes:
 *   rho<permille>  per warp subsequence, step 0 at c = -64, then a new max (M + 1) with probability rho, else
 *                  M - 1 - (0..7). rho1000 = every step moves the max, rho0 = only the first step.
 *   iid            c uniform in -64..-1 (record odds fall like 1/i along the subsequence; ties do not move the max).
 * V rows: random bf16 in [-1, 1], or with --denormal every V entry a bf16 subnormal (exercises flush-to-zero).
 *
 *   --host-only   pins: the default program (rescale 0, count_paths 0) encodes to the pinned bytes (sha256) and size;
 *                 rescale 1 drops exactly the 7-instruction test; refusals; replica properties per class.
 *   --sim         simulator (no chip): counters == replica per warp (4 classes x contexts 1, 257, 700); count mutant
 *                 goes red; rescale 0 and 1 outputs bit-equal (normal and subnormal V).
 *   --chip-check  on the chip: the same three checks at contexts 4096 and 32768.
 *   --chip-traces-mixed FILE  the same with every class, position and mode interleaved in one run (profile v1).
 *   --chip-traces FILE  timing traces on the chip, one TSV line per launch (default program and rescale 1; the
 *                 counting build is never timed). --classes a,b,c --pos p1,p2 --reps N --seed S --tag T.
 * Each run prints PASS/FAIL lines and exits 0 only when every check passed.
 */
#include "omega_gpu_decode_attn256_api.h"
#include "omega_gpu_session.h"
#include "omega_blackwell_codegen.h"
#include "sha256.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static const double THETA = 10000000.0;
static const float EPS = 1e-6f;
#define HD 256u
#define KVH 4u
#define QW OMEGA_DATTN256_QKV_WORDS
#define OW OMEGA_DATTN256_OUT_WORDS
#define CH OMEGA_DATTN256_CHUNK
#define NW OMEGA_DATTN256_WARPS
/* pinned from the base commit 6e24f7fd (tools: --print-pins): default program code bytes and their sha256 */
#define PIN_DEFAULT_BYTES 14464u
#define PIN_DEFAULT_SHA "1e7ddc7d0f46fc2876edb2fe472f2b0ddbe1d3b1e724eb3ab6f6eeeb0d4b94d3"

static int unrot(unsigned i) { return (i >= 32 && i < 128) || i >= 160; } /* kernel slots RoPE leaves alone (rotary 64) */

/* ---- classes and the host replica ---- */
typedef struct { char name[16]; int iid; uint32_t rho; } Class; /* rho in permille */
static int class_parse(const char *s, Class *c) {
    memset(c, 0, sizeof *c);
    if (strlen(s) >= sizeof c->name) return -1;
    strcpy(c->name, s);
    if (strcmp(s, "iid") == 0) { c->iid = 1; return 0; }
    if (strncmp(s, "rho", 3) == 0) { char *e; long v = strtol(s + 3, &e, 10); if (*e || v < 0 || v > 1000) return -1; c->rho = (uint32_t)v; return 0; }
    return -1;
}
/* c[j] for j < max_pos (history); position pos is the new token (score 0) and is handled by the replica */
static void gen_c(const Class *k, uint32_t seed, uint32_t max_pos, int8_t *c) {
    uint32_t s = seed * 2654435761u + 7u;
    for (uint32_t b = 0; b * CH < max_pos; b++)
        for (uint32_t g = 0; g < 4; g++) {
            int M = -64;
            for (uint32_t i = 0; i < CH / 4; i++) {
                uint32_t j = b * CH + g + 4 * i;
                if (j >= max_pos) break;
                int v;
                if (k->iid) v = -1 - (int)(lcg(&s) % 64u);
                else if (i == 0) v = -64;
                else if (lcg(&s) % 1000u < k->rho) v = M + 1;
                else v = M - 1 - (int)((lcg(&s) >> 8) % 8u);
                if (v > -1) v = -1;
                if (i == 0 || v > M) M = v;
                c[j] = (int8_t)v;
            }
        }
}
/* per warp {steps, rescales} for a launch at pos (per_split 256: CTA cx = chunk cx), all 4 KV heads identical */
static void replica(const int8_t *c, uint32_t pos, uint32_t *paths, uint64_t *steps, uint64_t *rescales) {
    const uint32_t ctx = pos + 1, splits = (pos + CH) / CH;
    uint64_t S = 0, R = 0;
    for (uint32_t cx = 0; cx < splits; cx++)
        for (uint32_t w = 0; w < NW; w++) {
            uint32_t g = w >> 1, n = 0, r = 0; int have = 0, m = 0;
            for (uint32_t j = cx * CH + g; j < cx * CH + CH && j < ctx; j += 4) {
                int v = j == pos ? 0 : c[j];
                n++;
                if (!have || v > m) { r++; m = v; have = 1; }
            }
            for (uint32_t cy = 0; cy < KVH; cy++) { paths[((cy * splits + cx) * NW + w) * 2] = n; paths[((cy * splits + cx) * NW + w) * 2 + 1] = r; }
            S += (uint64_t)n * KVH; R += (uint64_t)r * KVH;
        }
    if (steps) *steps = S;
    if (rescales) *rescales = R;
}

/* ---- inputs ---- */
static uint16_t bf16_trunc(float f) { uint32_t u; memcpy(&u, &f, 4); return (uint16_t)(u >> 16); }
static void make_qkv(float *qkv) { memset(qkv, 0, QW * 4); for (unsigned h = 0; h < 24; h++) for (unsigned i = 0; i < HD; i++) qkv[h * HD + i] = unrot(i) ? 1.0f : 0.0f; }
static void k_row(int cj, uint16_t *row) { for (unsigned i = 0; i < HD; i++) row[i] = unrot(i) ? bf16_trunc((float)cj / 16.0f) : 0; }
static void v_rows(uint32_t seed, uint32_t n, int denormal, uint16_t *v) {
    uint32_t s = seed * 40503u + 11u;
    for (size_t i = 0; i < (size_t)n * HD; i++) {
        uint32_t r = lcg(&s);
        if (denormal) v[i] = (uint16_t)(((r >> 31) << 15) | (1u + ((r >> 8) % 127u)));
        else { float f = -1.0f + 2.0f * (float)(r >> 8) / 16777216.0f; v[i] = bf16_trunc(f); }
    }
}
static uint64_t fnv(const void *p, size_t n) { const uint8_t *b = p; uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }

/* ---- host-only ---- */
static int prog_bytes(const OmegaDecodeAttn256Spec *sp, uint8_t **buf, size_t *n, uint32_t *insns) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_decode_attn256_prog(sp, &prog) != 0) return -1;
    size_t need = omega_bw_program_code_bytes(&prog);
    if (!need) return -1;
    *buf = malloc(need);
    OmegaBlackwellKernel k;
    if (!*buf || omega_blackwell_kernel_encode_into(&prog, *buf, need, &k) != 0) { free(*buf); return -1; }
    *n = need; if (insns) *insns = (uint32_t)prog.count;
    return 0;
}
static void hex(const uint8_t *d, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]); }
static void host_only(int print_pins) {
    OmegaDecodeAttn256Spec d = { .depth = OMEGA_GPU_DATTN256_DEPTH };
    uint8_t *b = NULL; size_t n = 0; uint32_t ni = 0;
    CHECK(prog_bytes(&d, &b, &n, &ni) == 0, "default program builds");
    uint8_t dg[32]; char hx[65]; sha256_hash(b, n, dg); hex(dg, 32, hx);
    if (print_pins) printf("PIN default bytes %zu sha256 %s ir_insns %u\n", n, hx, ni);
    else CHECK(n == PIN_DEFAULT_BYTES && strcmp(hx, PIN_DEFAULT_SHA) == 0, "default program == base 6e24f7fd bytes (%zu %s)", n, hx);
    free(b);
    OmegaDecodeAttn256Spec r1 = d; r1.rescale = 1; uint32_t n1 = 0;
    CHECK(prog_bytes(&r1, &b, &n, &n1) == 0, "rescale 1 builds"); free(b);
    CHECK(ni - n1 == 7, "rescale 1 drops the 5 FADD + ISETP + BRA test (%u -> %u IR insns)", ni, n1);
    OmegaDecodeAttn256Spec cp = d; cp.count_paths = 1; uint32_t nc = 0;
    CHECK(prog_bytes(&cp, &b, &n, &nc) == 0, "count_paths builds"); free(b);
    uint32_t gp = 0; CHECK(omega_decode_attn256_prog_stats(&cp, NULL, &gp) == 0 && gp <= 126, "count_paths registers %u <= 126", gp);
    OmegaDecodeAttn256Spec bad = d; bad.count_mutant = 1; CHECK(omega_decode_attn256_prog_stats(&bad, NULL, NULL) != 0, "count mutant without count_paths refused");
    bad = d; bad.rescale = 2; CHECK(omega_decode_attn256_prog_stats(&bad, NULL, NULL) != 0, "rescale 2 refused");
    bad = d; bad.prep = 1; bad.rescale = 1; CHECK(omega_decode_attn256_prog_stats(&bad, NULL, NULL) != 0, "prep + rescale refused");
    /* replica properties */
    const uint32_t MP = 4096; int8_t *c = malloc(MP); uint32_t *paths = malloc((size_t)KVH * 16 * NW * 2 * 4);
    const char *names[] = { "rho0", "rho1000", "rho500", "iid" };
    for (unsigned k = 0; k < 4; k++) {
        Class cl; class_parse(names[k], &cl); gen_c(&cl, 1, MP, c);
        uint64_t S, R; replica(c, MP - 1, paths, &S, &R);
        CHECK(S == 2ull * KVH * MP, "%s steps = 2 * 4 * context (%" PRIu64 ")", names[k], S);
        if (k == 0) CHECK(R == (uint64_t)KVH * NW * 16 + 0 + 2 * KVH, "rho0: one rescale per warp + the new token's pair (%" PRIu64 ")", R);
        if (k == 1) CHECK(R == S, "rho1000: every step rescales (%" PRIu64 " of %" PRIu64 ")", R, S);
        if (k == 2) CHECK(R > S * 4 / 10 && R < S * 6 / 10, "rho500: rescale share %.3f", (double)R / (double)S);
        printf("  class %-8s context %u: steps %" PRIu64 " rescales %" PRIu64 " share %.4f\n", names[k], MP, S, R, (double)R / (double)S);
    }
    free(c); free(paths);
}

/* ---- simulator ---- */
static int sim_run(const OmegaDecodeAttn256Spec *sp, const int8_t *c, const uint16_t *vh, uint32_t max_pos, uint32_t pos, float *out, uint32_t *paths, char *msg, size_t mn) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_decode_attn256_prog(sp, &prog) != 0) { snprintf(msg, mn, "codegen refused"); return -1; }
    const uint32_t splits = (pos + CH) / CH;
    size_t plane = (size_t)KVH * max_pos * OMEGA_DATTN256_ROW_BYTES + OMEGA_DATTN256_PLANE_SLACK;
    uint8_t *kv = aligned_alloc(16, 2 * plane); memset(kv, 0, 2 * plane);
    for (uint32_t g = 0; g < KVH; g++) for (uint32_t j = 0; j < pos; j++) {
        k_row(c[j], (uint16_t *)(kv + ((size_t)g * max_pos + j) * OMEGA_DATTN256_ROW_BYTES));
        memcpy(kv + plane + ((size_t)g * max_pos + j) * OMEGA_DATTN256_ROW_BYTES, vh + ((size_t)g * max_pos + j) * HD, OMEGA_DATTN256_ROW_BYTES);
    }
    float *rope = aligned_alloc(16, (size_t)max_pos * OMEGA_DATTN256_ROPE_ROW_BYTES); for (uint32_t p = 0; p < max_pos; p++) omega_dattn256_rope_row(p, THETA, 64, rope + (size_t)p * 256);
    float *norms = aligned_alloc(16, 2048); for (unsigned i = 0; i < 512; i++) norms[i] = 1.0f;
    float *qkv = aligned_alloc(16, QW * 4); make_qkv(qkv);
    float *o = aligned_alloc(16, OW * 4);
    float *part = aligned_alloc(16, (size_t)KVH * splits * OMEGA_DATTN256_PARTIAL_WORDS * 4);
    uint32_t *cnt = aligned_alloc(16, 16); memset(cnt, 0, 16);
    size_t pb = (size_t)KVH * splits * NW * 2 * 4;
    uint32_t *pp = aligned_alloc(16, pb + 16); memset(pp, 0xff, pb);
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 256; s.order = 0;
    static uint8_t smem[OMEGA_DATTN256_SHARED_BYTES]; s.smem = smem; s.smem_bytes = OMEGA_DATTN256_SHARED_BYTES;
    for (unsigned i = 0; i < OW; i++) o[i] = -999.0f;
    uint64_t A[10] = { (uintptr_t)qkv, (uintptr_t)kv, (uintptr_t)(kv + plane), (uintptr_t)rope, (uintptr_t)norms, (uintptr_t)(norms + HD), (uintptr_t)o, (uintptr_t)part, (uintptr_t)cnt, 0 };
    for (unsigned i = 0; i < 9; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)A[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(A[i] >> 32); }
    s.cbank[OMEGA_DATTN256_ARG_PATHS / 4] = (uint32_t)(uintptr_t)pp; s.cbank[OMEGA_DATTN256_ARG_PATHS / 4 + 1] = (uint32_t)((uintptr_t)pp >> 32);
    float fe = EPS, fo = 1.0f, fq = (float)((1.0 / 16.0) * 1.4426950408889634);
    memcpy(&s.cbank[OMEGA_DATTN256_ARG_EPS / 4], &fe, 4); memcpy(&s.cbank[OMEGA_DATTN256_ARG_OUT_SCALE / 4], &fo, 4); memcpy(&s.cbank[OMEGA_DATTN256_ARG_QK_SCALE / 4], &fq, 4);
    s.cbank[OMEGA_DATTN256_ARG_POS / 4] = pos; s.cbank[OMEGA_DATTN256_ARG_PER_SPLIT / 4] = CH; s.cbank[OMEGA_DATTN256_ARG_SPLITS / 4] = splits; s.cbank[OMEGA_DATTN256_ARG_KV_STRIDE / 4] = max_pos * OMEGA_DATTN256_ROW_BYTES;
    s.nranges = 8;
    s.ranges[0].lo = (uintptr_t)qkv; s.ranges[0].hi = (uintptr_t)qkv + QW * 4;
    s.ranges[1].lo = (uintptr_t)kv; s.ranges[1].hi = (uintptr_t)kv + 2 * plane;
    s.ranges[2].lo = (uintptr_t)rope; s.ranges[2].hi = (uintptr_t)rope + (size_t)max_pos * OMEGA_DATTN256_ROPE_ROW_BYTES;
    s.ranges[3].lo = (uintptr_t)norms; s.ranges[3].hi = (uintptr_t)norms + 2048;
    s.ranges[4].lo = (uintptr_t)o; s.ranges[4].hi = (uintptr_t)o + OW * 4;
    s.ranges[5].lo = (uintptr_t)part; s.ranges[5].hi = (uintptr_t)part + (size_t)KVH * splits * OMEGA_DATTN256_PARTIAL_WORDS * 4;
    s.ranges[6].lo = (uintptr_t)cnt; s.ranges[6].hi = (uintptr_t)cnt + 16;
    s.ranges[7].lo = (uintptr_t)pp; s.ranges[7].hi = (uintptr_t)pp + pb;
    int err = ws_run(&s, splits, KVH);
    if (err) snprintf(msg, mn, "%s (pc %d)", s.msg, s.err_pc);
    else { memcpy(out, o, OW * 4); if (paths) memcpy(paths, pp, pb); }
    free(kv); free(rope); free(norms); free(qkv); free(o); free(part); free(cnt); free(pp);
    return err;
}
static void sim_battery(void) {
    const uint32_t MP = 768, ctxs[] = { 1, 257, 700 };
    const char *names[] = { "rho0", "rho1000", "rho500", "iid" };
    int8_t *c = malloc(MP); uint16_t *vh = malloc((size_t)KVH * MP * HD * 2);
    uint32_t *pr = malloc((size_t)KVH * 4 * NW * 2 * 4), *pc = malloc((size_t)KVH * 4 * NW * 2 * 4);
    float *y0 = malloc(OW * 4), *y1 = malloc(OW * 4);
    char msg[200];
    OmegaDecodeAttn256Spec d = { .depth = OMEGA_GPU_DATTN256_DEPTH };
    for (unsigned k = 0; k < 4; k++) for (unsigned x = 0; x < 3; x++) for (int den = 0; den < 2; den++) {
        if (den && x != 2) continue;
        Class cl; class_parse(names[k], &cl); gen_c(&cl, 3 + k, MP, c); v_rows(9 + k, KVH * MP, den, vh);
        const uint32_t pos = ctxs[x] - 1, splits = (pos + CH) / CH, words = KVH * splits * NW * 2;
        replica(c, pos, pr, NULL, NULL);
        OmegaDecodeAttn256Spec sc = d; sc.count_paths = 1;
        int e = sim_run(&sc, c, vh, MP, pos, y0, pc, msg, sizeof msg);
        CHECK(!e, "sim count %s ctx %u: %s", names[k], ctxs[x], msg);
        if (!e) { size_t bad = 0; for (uint32_t i = 0; i < words; i++) bad += pc[i] != pr[i]; CHECK(bad == 0, "sim counters == replica %s ctx %u%s (%zu of %u words differ)", names[k], ctxs[x], den ? " subnormal V" : "", bad, words); }
        if (k == 2 && x == 2 && !den) {
            sc.count_mutant = 1;
            e = sim_run(&sc, c, vh, MP, pos, y0, pc, msg, sizeof msg);
            size_t bad = 0; for (uint32_t i = 0; i < words && !e; i++) bad += pc[i] != pr[i];
            CHECK(!e && bad > 0, "count mutant goes red (%zu words differ)", bad);
            printf("  mutant: counters differ in %zu of %u words (expected > 0)\n", bad, words);
        }
        OmegaDecodeAttn256Spec s1 = d; s1.rescale = 1;
        int e0 = sim_run(&d, c, vh, MP, pos, y0, NULL, msg, sizeof msg), e1 = e0 ? -1 : sim_run(&s1, c, vh, MP, pos, y1, NULL, msg, sizeof msg);
        CHECK(!e0 && !e1 && memcmp(y0, y1, OW * 4) == 0, "sim rescale 0 == rescale 1 bit for bit, %s ctx %u%s", names[k], ctxs[x], den ? " subnormal V" : "");
    }
    free(c); free(vh); free(pr); free(pc); free(y0); free(y1);
}

/* ---- chip ---- */
static int load_class(OmegaGpuDecodeAttn256Cache *cache, const int8_t *c, const uint16_t *vh, uint32_t n, uint32_t max_pos) {
    uint16_t *kr = malloc((size_t)n * HD * 2);
    for (uint32_t j = 0; j < n; j++) k_row(c[j], kr + (size_t)j * HD);
    int rc = OMEGA_GPU_DATTN_OK;
    for (uint32_t g = 0; g < KVH && rc == OMEGA_GPU_DATTN_OK; g++) rc = omega_gpu_decode_attn256_cache_write(cache, 0, g, 0, n, kr, vh + (size_t)g * max_pos * HD);
    free(kr);
    return rc;
}
static int restore_row(OmegaGpuDecodeAttn256Cache *cache, const int8_t *c, const uint16_t *vh, uint32_t j, uint32_t max_pos) {
    uint16_t kr[HD]; k_row(c[j], kr);
    for (uint32_t g = 0; g < KVH; g++) { int rc = omega_gpu_decode_attn256_cache_write(cache, 0, g, j, 1, kr, vh + ((size_t)g * max_pos + j) * HD); if (rc) return rc; }
    return OMEGA_GPU_DATTN_OK;
}
static OmegaGpuDecodeAttn256Cache *chip_open(uint32_t max_pos) {
    OmegaGpuDecodeAttn256Cache *cache = NULL;
    int rc = omega_gpu_decode_attn256_cache_alloc(1, max_pos, THETA, 64, &cache);
    CHECK(rc == OMEGA_GPU_DATTN_OK, "cache alloc %u positions: %s %s", max_pos, omega_gpu_decode_attn_rc_name(rc), rc ? omega_gpu_decode_attn_last_error() : "");
    if (rc) return NULL;
    float ones[HD]; for (unsigned i = 0; i < HD; i++) ones[i] = 1.0f;
    CHECK(omega_gpu_decode_attn256_set_norms(cache, 0, ones, ones) == OMEGA_GPU_DATTN_OK, "set norms");
    return cache;
}
static void chip_check(void) {
    const uint32_t MP = 32768, ps[] = { 4095, 32767 };
    const char *names[] = { "rho0", "rho1000", "rho500", "iid" };
    OmegaGpuDecodeAttn256Cache *cache = chip_open(MP);
    if (!cache) return;
    int8_t *c = malloc(MP); uint16_t *vh = malloc((size_t)KVH * MP * HD * 2);
    uint32_t *pr = malloc((size_t)KVH * (MP / CH) * NW * 2 * 4), *pc = malloc((size_t)KVH * (MP / CH) * NW * 2 * 4);
    float qkv[QW], *y0 = malloc(OW * 4), *y1 = malloc(OW * 4); make_qkv(qkv);
    for (unsigned k = 0; k < 4; k++) for (int den = 0; den < 2; den++) {
        if (den && k != 3) continue;
        Class cl; class_parse(names[k], &cl); gen_c(&cl, 21 + k, MP, c); v_rows(31 + k, KVH * MP, den, vh);
        int rc = load_class(cache, c, vh, MP, MP);
        CHECK(rc == OMEGA_GPU_DATTN_OK, "load %s", names[k]);
        for (unsigned x = 0; x < 2 && rc == OMEGA_GPU_DATTN_OK; x++) {
            const uint32_t pos = ps[x], splits = (pos + CH) / CH, words = KVH * splits * NW * 2;
            OmegaGpuDecodeAttn256Info info;
            replica(c, pos, pr, NULL, NULL);
            omega_gpu_decode_attn256_set_rescale(0); omega_gpu_decode_attn256_set_count_paths(1, 0);
            rc = omega_gpu_decode_attn256(cache, 0, pos, qkv, EPS, 1.0f, y0, &info);
            if (!rc) rc = omega_gpu_decode_attn256_paths_read(cache, splits, pc);
            size_t bad = 0; for (uint32_t i = 0; i < words && !rc; i++) bad += pc[i] != pr[i];
            CHECK(!rc && bad == 0, "chip counters == replica %s ctx %u%s (%zu of %u words differ) %s", names[k], pos + 1, den ? " subnormal V" : "", bad, words, rc ? omega_gpu_decode_attn_last_error() : "");
            if (k == 2 && !den) {
                omega_gpu_decode_attn256_set_count_paths(1, 1);
                (void)restore_row(cache, c, vh, pos, MP);
                rc = omega_gpu_decode_attn256(cache, 0, pos, qkv, EPS, 1.0f, y0, &info);
                if (!rc) rc = omega_gpu_decode_attn256_paths_read(cache, splits, pc);
                bad = 0; for (uint32_t i = 0; i < words && !rc; i++) bad += pc[i] != pr[i];
                CHECK(!rc && bad > 0, "chip count mutant goes red ctx %u (%zu words differ)", pos + 1, bad);
                printf("  chip mutant ctx %u: counters differ in %zu of %u words (expected > 0)\n", pos + 1, bad, words);
            }
            omega_gpu_decode_attn256_set_count_paths(0, 0);
            (void)restore_row(cache, c, vh, pos, MP);
            rc = omega_gpu_decode_attn256(cache, 0, pos, qkv, EPS, 1.0f, y0, &info);
            (void)restore_row(cache, c, vh, pos, MP);
            omega_gpu_decode_attn256_set_rescale(1);
            if (!rc) rc = omega_gpu_decode_attn256(cache, 0, pos, qkv, EPS, 1.0f, y1, &info);
            (void)restore_row(cache, c, vh, pos, MP);
            omega_gpu_decode_attn256_set_rescale(0);
            size_t diff = 0; for (unsigned i = 0; i < OW && !rc; i++) diff += memcmp(&y0[i], &y1[i], 4) != 0;
            CHECK(!rc && diff == 0, "chip rescale 0 == rescale 1 bit for bit %s ctx %u%s (%zu words differ)", names[k], pos + 1, den ? " subnormal V" : "", diff);
        }
    }
    free(c); free(vh); free(pr); free(pc); free(y0); free(y1);
    omega_gpu_decode_attn256_cache_free(cache);
}
static int split_list(char *s, char **out, int max) { int n = 0; for (char *t = strtok(s, ","); t && n < max; t = strtok(NULL, ",")) out[n++] = t; return n; }
/* one TSV line per launch: tag class seed pos splits mode rep elapsed_ns call_ns steps rescales out_fnv out_equal_to_mode0 */
static void chip_traces(const char *path, char *classes, char *posl, uint32_t reps, uint32_t seed, const char *tag) {
    char *cn[16], *pn[16]; int ncl = split_list(classes, cn, 16), np = split_list(posl, pn, 16);
    uint32_t pos[16], maxp = 0;
    for (int i = 0; i < np; i++) { pos[i] = (uint32_t)strtoul(pn[i], NULL, 10); if (pos[i] + 1 > maxp) maxp = pos[i] + 1; }
    const uint32_t MP = (maxp + CH - 1) / CH * CH;
    FILE *f = fopen(path, "a");
    CHECK(f != NULL, "open %s", path);
    if (!f) return;
    OmegaGpuDecodeAttn256Cache *cache = chip_open(MP);
    if (!cache) { fclose(f); return; }
    int8_t *c = malloc(MP); uint16_t *vh = malloc((size_t)KVH * MP * HD * 2);
    uint32_t *pr = malloc((size_t)KVH * (MP / CH) * NW * 2 * 4);
    float qkv[QW], *y = malloc(OW * 4); make_qkv(qkv);
    size_t lines = 0;
    for (int k = 0; k < ncl; k++) {
        Class cl; if (class_parse(cn[k], &cl) != 0) { CHECK(0, "bad class %s", cn[k]); continue; }
        const uint32_t cs = seed * 977u + (uint32_t)k;
        gen_c(&cl, cs, MP, c); v_rows(cs + 5, KVH * MP, 0, vh);
        CHECK(load_class(cache, c, vh, MP, MP) == OMEGA_GPU_DATTN_OK, "load %s", cn[k]);
        for (int x = 0; x < np; x++) {
            uint64_t S, R; replica(c, pos[x], pr, &S, &R);
            uint64_t h0 = 0;
            for (int blk = 0; blk < 2; blk++) {
                const uint32_t mode = (uint32_t)(((k + x) & 1) ^ blk); /* alternate which mode runs first */
                omega_gpu_decode_attn256_set_rescale(mode);
                for (uint32_t r = 0; r <= reps; r++) {        /* r == 0: warm-up after the kernel upload, not recorded */
                    OmegaGpuDecodeAttn256Info info; memset(&info, 0, sizeof info);
                    int rc = omega_gpu_decode_attn256(cache, 0, pos[x], qkv, EPS, 1.0f, y, &info);
                    (void)restore_row(cache, c, vh, pos[x], MP);
                    CHECK(rc == OMEGA_GPU_DATTN_OK, "launch %s pos %u mode %u: %s", cn[k], pos[x], mode, rc ? omega_gpu_decode_attn_last_error() : "");
                    if (rc) break;
                    uint64_t h = fnv(y, OW * 4);
                    if (mode == 0 && !h0) h0 = h;
                    if (r == 0) continue;
                    fprintf(f, "%s\t%s\t%u\t%u\t%u\t%u\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%016" PRIx64 "\n", tag, cn[k], cs, pos[x], info.splits, mode, r,
                            info.elapsed_ns, info.call_ns, S, R, h);
                    lines++;
                }
            }
            /* every launch of this (class, pos) must give the mode-0 bits: checked from the file by the scorer too */
            (void)h0;
        }
    }
    fclose(f);
    printf("traces: %zu lines appended to %s\n", lines, path);
    omega_gpu_decode_attn256_set_rescale(0);
    free(c); free(vh); free(pr); free(y);
    omega_gpu_decode_attn256_cache_free(cache);
}

/* Profile v1 (docs/turing/F2_FREQUENCY_TIMING_PROFILE_V0.md, "v1"): every class in its own cache layer (layer = position
 * of the class in a seed-shuffled order), one shared V history, and each rep runs mode 0 and mode 1 (seed-shuffled
 * order, one discarded warm-up after each kernel switch) over all (class, position) pairs in a seed-shuffled order,
 * so slow drift of the machine lands on every class and both modes alike. Same TSV columns as --chip-traces. */
static void shuffle_u32(uint32_t *a, uint32_t n, uint32_t *s) { for (uint32_t i = n; i > 1; i--) { uint32_t j = lcg(s) % i, t = a[i - 1]; a[i - 1] = a[j]; a[j] = t; } }
static void chip_traces_mixed(const char *path, char *classes, char *posl, uint32_t reps, uint32_t seed, const char *tag) {
    char *cn[16], *pn[16]; int ncl = split_list(classes, cn, 16), np = split_list(posl, pn, 16);
    uint32_t pos[16], maxp = 0, rs = seed * 7919u + 3u;
    for (int i = 0; i < np; i++) { pos[i] = (uint32_t)strtoul(pn[i], NULL, 10); if (pos[i] + 1 > maxp) maxp = pos[i] + 1; }
    const uint32_t MP = (maxp + CH - 1) / CH * CH;
    Class cl[16]; uint32_t layer_of[16], perm[16];
    for (int k = 0; k < ncl; k++) { if (class_parse(cn[k], &cl[k]) != 0) { CHECK(0, "bad class %s", cn[k]); return; } perm[k] = (uint32_t)k; }
    shuffle_u32(perm, (uint32_t)ncl, &rs);
    for (int k = 0; k < ncl; k++) layer_of[perm[k]] = (uint32_t)k;
    FILE *f = fopen(path, "a");
    CHECK(f != NULL, "open %s", path);
    if (!f) return;
    OmegaGpuDecodeAttn256Cache *cache = NULL;
    int rc = omega_gpu_decode_attn256_cache_alloc((uint32_t)ncl, MP, THETA, 64, &cache);
    CHECK(rc == OMEGA_GPU_DATTN_OK, "cache alloc %d layers x %u: %s", ncl, MP, rc ? omega_gpu_decode_attn_last_error() : "");
    if (rc) { fclose(f); return; }
    float ones[HD]; for (unsigned i = 0; i < HD; i++) ones[i] = 1.0f;
    for (int k = 0; k < ncl; k++) CHECK(omega_gpu_decode_attn256_set_norms(cache, (uint32_t)k, ones, ones) == OMEGA_GPU_DATTN_OK, "set norms");
    int8_t *c = malloc((size_t)ncl * MP); uint16_t *vh = malloc((size_t)KVH * MP * HD * 2), *kr = malloc((size_t)MP * HD * 2);
    uint32_t *pr = malloc((size_t)KVH * (MP / CH) * NW * 2 * 4);
    uint64_t S[16][16], R[16][16];
    v_rows(seed + 5, KVH * MP, 0, vh);
    for (int k = 0; k < ncl; k++) {
        int8_t *ck = c + (size_t)k * MP; gen_c(&cl[k], seed * 977u + (uint32_t)k, MP, ck);
        for (uint32_t j = 0; j < MP; j++) k_row(ck[j], kr + (size_t)j * HD);
        for (uint32_t g = 0; g < KVH; g++) CHECK(omega_gpu_decode_attn256_cache_write(cache, layer_of[k], g, 0, MP, kr, vh + (size_t)g * MP * HD) == OMEGA_GPU_DATTN_OK, "load %s", cn[k]);
        for (int x = 0; x < np; x++) replica(ck, pos[x], pr, &S[k][x], &R[k][x]);
    }
    float qkv[QW], *y = malloc(OW * 4); make_qkv(qkv);
    uint32_t pairs[256], npairs = (uint32_t)(ncl * np);
    size_t lines = 0;
    for (uint32_t r = 1; r <= reps; r++) {
        uint32_t md[2] = { 0, 1 }; shuffle_u32(md, 2, &rs);
        for (int mi = 0; mi < 2; mi++) {
            const uint32_t mode = md[mi];
            omega_gpu_decode_attn256_set_rescale(mode);
            for (uint32_t i = 0; i < npairs; i++) pairs[i] = i;
            shuffle_u32(pairs, npairs, &rs);
            for (uint32_t i = 0; i <= npairs; i++) {               /* i == 0: warm-up after the kernel switch, not recorded */
                const uint32_t pi = pairs[i == 0 ? 0 : i - 1], k = pi / (uint32_t)np, x = pi % (uint32_t)np;
                OmegaGpuDecodeAttn256Info info; memset(&info, 0, sizeof info);
                rc = omega_gpu_decode_attn256(cache, layer_of[k], pos[x], qkv, EPS, 1.0f, y, &info);
                uint16_t krow[HD]; k_row(c[(size_t)k * MP + pos[x]], krow);
                for (uint32_t g = 0; g < KVH; g++) (void)omega_gpu_decode_attn256_cache_write(cache, layer_of[k], g, pos[x], 1, krow, vh + ((size_t)g * MP + pos[x]) * HD);
                CHECK(rc == OMEGA_GPU_DATTN_OK, "launch %s pos %u mode %u: %s", cn[k], pos[x], mode, rc ? omega_gpu_decode_attn_last_error() : "");
                if (rc) break;
                if (i == 0) continue;
                fprintf(f, "%s\t%s\t%u\t%u\t%u\t%u\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%016" PRIx64 "\n", tag, cn[k], seed, pos[x], info.splits, mode, r,
                        info.elapsed_ns, info.call_ns, S[k][x], R[k][x], fnv(y, OW * 4));
                lines++;
            }
            if (rc) break;
        }
        if (rc) break;
    }
    fclose(f);
    printf("mixed traces: %zu lines appended to %s (layers:", lines, path);
    for (int k = 0; k < ncl; k++) printf(" %s=%u", cn[k], layer_of[k]);
    printf(")\n");
    omega_gpu_decode_attn256_set_rescale(0);
    free(c); free(vh); free(kr); free(pr); free(y);
    omega_gpu_decode_attn256_cache_free(cache);
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, chk = 0, pins = 0;
    const char *traces = NULL, *mixed = NULL, *tag = "t";
    char *classes = NULL, *posl = NULL; uint32_t reps = 10, seed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--print-pins") == 0) host = pins = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--chip-check") == 0) chk = 1;
        else if (strcmp(argv[i], "--chip-traces") == 0 && i + 1 < argc) traces = argv[++i];
        else if (strcmp(argv[i], "--chip-traces-mixed") == 0 && i + 1 < argc) mixed = argv[++i];
        else if (strcmp(argv[i], "--classes") == 0 && i + 1 < argc) classes = argv[++i];
        else if (strcmp(argv[i], "--pos") == 0 && i + 1 < argc) posl = argv[++i];
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) tag = argv[++i];
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (!host && !sim && !chk && !traces && !mixed) host = 1;
    if (host) { printf("== host ==\n"); host_only(pins); }
    if (sim) { printf("== simulator ==\n"); sim_battery(); }
    if (chk) { printf("== chip check ==\n"); chip_check(); }
    if (traces) { printf("== chip traces ==\n"); if (!classes || !posl) { printf("--chip-traces needs --classes and --pos\n"); return 2; } chip_traces(traces, classes, posl, reps, seed, tag); }
    if (mixed) { printf("== chip traces, mixed order ==\n"); if (!classes || !posl) { printf("--chip-traces-mixed needs --classes and --pos\n"); return 2; } chip_traces_mixed(mixed, classes, posl, reps, seed, tag); }
    printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
