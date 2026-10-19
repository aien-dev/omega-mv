/*
 * gpu_two_front_test: the two-front arena for the Qwen3.8-27B per-conversation state (omega#571, FORTRAN idea 3).
 *
 *   --host-only   bookkeeping (scripted fronts, fill-the-gap refusal, 3000 random sequences against a page oracle),
 *                 the plane-layout kernels are byte for byte the ones before this change (pinned sha256), the block
 *                 stride rule, and the fixed-maximum vs arena byte table at 1.7k / 30k / 128k
 *   --sim         the warp simulator runs the decode attention in the plane layout and in the block layout on the same
 *                 rows: outputs and appended rows must be bit-identical (plus a wrong-layer control that must differ)
 *   (default)     chip, under /tmp/aien-gb10.lock: the exact-recall gate (decode and prefill attention bit-identical
 *                 between a plane cache and a cache bound to the arena, positions up to 131071; DeltaNet live set in the
 *                 arena bit-identical to its own memory, snapshot + restore continues bit-identically), the fronts
 *                 meeting exactly and the next block refused with the snapshot intact, the inline control (overflow
 *                 check off: the KV front runs over the snapshot and the snapshot is corrupted), driver bytes measured
 *                 for the old 128K cache vs the arena, decode attention chip time in both layouts.
 *   --break-overflow   TEST ONLY: runs with the overflow check skipped; the suite MUST then FAIL (red before green).
 *   --max-pos N   chip: longest position tested (default 131072; the arena is N/256 blocks of 16 MiB)
 *   --reps N      chip: timed decode calls per layout and position (default 15)
 * Exit 0 only when every check passed.
 */
#include "omega_two_front.h"
#include "omega_gpu_decode_attn256_api.h"
#include "omega_gpu_prefill_attn256_api.h"
#include "omega_gpu_gdn_api.h"
#include "omega_gpu_session.h"
#include "sha256.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_failed, g_break;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static const double THETA = 10000000.0;
#define ROT 64u
#define HD 256u
#define KVH 4u
#define RB OMEGA_DATTN256_ROW_BYTES
#define QW OMEGA_DATTN256_QKV_WORDS
#define OW OMEGA_DATTN256_OUT_WORDS
#define ATTN_LAYERS 16u   /* Qwen3.8-27B full-attention layers */
#define GDN_LAYERS 48u    /* Qwen3.8-27B DeltaNet layers */
#define MIB (1024ull * 1024ull)
#define GIB (1024ull * MIB)

/* ================================================================ host: bookkeeping ================================ */
static void scripted(void) {
    OmegaTf tf;
    const uint64_t set = omega_gpu_gdn_state_set_bytes(GDN_LAYERS), blk = OMEGA_GPU_DATTN256_BLOCK_BYTES(ATTN_LAYERS);
    CHECK(set == 165150720ull, "48-layer DeltaNet set is %" PRIu64 " bytes", set);
    CHECK(blk == 16ull * MIB, "KV block is %" PRIu64 " bytes", blk);
    CHECK(omega_tf_init(&tf, 8 * GIB, blk) == OMEGA_TF_OK, "init");
    uint32_t kv1, live1, snap1, kv2, live2;
    CHECK(omega_tf_kv_open(&tf, 1, 1700, "conv1 kv", &kv1) == OMEGA_TF_OK, "open 1700");
    CHECK(omega_tf_item(&tf, kv1)->bytes == 7 * blk && omega_tf_item(&tf, kv1)->positions == 1792, "1700 positions = 7 blocks (1792)");
    CHECK(omega_tf_rec_push(&tf, OMEGA_TF_GDN_LIVE, 1, set, 0, "conv1 gdn live", &live1) == OMEGA_TF_OK, "push live");
    CHECK(omega_tf_rec_push(&tf, OMEGA_TF_GDN_SNAPSHOT, 1, set, 1700, "conv1 turn1", &snap1) == OMEGA_TF_OK, "push snapshot");
    CHECK(omega_tf_gap(&tf) == 8 * GIB - 7 * blk - 2 * set, "gap after three items");
    CHECK(omega_tf_kv_grow(&tf, kv1, 30000) == OMEGA_TF_OK && omega_tf_item(&tf, kv1)->positions == 30208, "grow in place to 30000 (118 blocks)");
    CHECK(omega_tf_kv_open(&tf, 2, 0, "conv2 kv", &kv2) == OMEGA_TF_OK && omega_tf_item(&tf, kv2)->bytes == 0, "open an empty extent above");
    CHECK(omega_tf_kv_grow(&tf, kv1, 31000) == OMEGA_TF_NOT_TOP, "a sealed extent does not grow in place");
    CHECK(omega_tf_rec_push(&tf, OMEGA_TF_GDN_LIVE, 2, set, 0, "conv2 gdn live", &live2) == OMEGA_TF_OK, "push live 2");
    CHECK(omega_tf_kv_grow(&tf, kv2, 512) == OMEGA_TF_OK, "grow conv2");
    CHECK(omega_tf_check(&tf) == 0, "check after the script: %s", tf.err);
    uint64_t old_off; uint32_t kv1b;
    CHECK(omega_tf_kv_move_top(&tf, kv1, &old_off, &kv1b) == OMEGA_TF_OK && old_off == 0 && omega_tf_item(&tf, kv1b)->off == 2 * blk + 118 * blk,
          "move conv1 to the top");
    CHECK(omega_tf_kv_grow(&tf, kv1b, 31000) == OMEGA_TF_OK, "the moved extent grows");
    CHECK(omega_tf_check(&tf) == 0, "check after move: %s", tf.err);
    const uint64_t low_before = tf.low;
    CHECK(omega_tf_release(&tf, kv2) == OMEGA_TF_OK && tf.low == low_before, "a hole below the top does not move the front");
    CHECK(omega_tf_release(&tf, kv1b) == OMEGA_TF_OK && tf.low == 0, "closing the top retreats through every hole to 0");
    CHECK(omega_tf_release(&tf, live1) == OMEGA_TF_OK && tf.high == 8 * GIB - 3 * set, "live1 released: a hole at the top of the arena, the high front stays");
    CHECK(omega_tf_release(&tf, live2) == OMEGA_TF_OK && omega_tf_release(&tf, snap1) == OMEGA_TF_OK && tf.high == 8 * GIB, "all records gone");
    CHECK(omega_tf_check(&tf) == 0 && omega_tf_gap(&tf) == 8 * GIB, "empty again: %s", tf.err);
}

/* the fronts meet exactly; the next block is refused and nothing changes */
static int fill_the_gap(int mutant) {
    OmegaTf tf;
    const uint64_t set = 6881280ull, blk = 16ull * MIB;
    omega_tf_test_set_mutant(mutant);
    omega_tf_init(&tf, 8 * blk + 2 * set, blk);
    uint32_t kv, live, snap;
    omega_tf_rec_push(&tf, OMEGA_TF_GDN_LIVE, 1, set, 0, "live", &live);
    omega_tf_rec_push(&tf, OMEGA_TF_GDN_SNAPSHOT, 1, set, 2048, "snap", &snap);
    omega_tf_kv_open(&tf, 1, 0, "kv", &kv);
    int ok = 1;
    ok &= omega_tf_kv_grow(&tf, kv, 2048) == OMEGA_TF_OK && omega_tf_gap(&tf) == 0;          /* 8 blocks: the fronts meet */
    const uint64_t lo = tf.low, hi = tf.high;
    const int rc = omega_tf_kv_grow(&tf, kv, 2049);
    ok &= rc == OMEGA_TF_OVERFLOW && tf.low == lo && tf.high == hi && omega_tf_item(&tf, kv)->positions == 2048;
    uint32_t extra;
    ok &= omega_tf_rec_push(&tf, OMEGA_TF_GDN_SNAPSHOT, 1, 4096, 0, "one page", &extra) == OMEGA_TF_OVERFLOW;
    ok &= omega_tf_check(&tf) == 0;
    ok &= strstr(tf.err, "short by") != NULL;
    omega_tf_test_set_mutant(0);
    return ok;
}

/* random sequences against a page-ownership oracle (independent of omega_tf_check) */
static void random_oracle(void) {
    enum { PAGES = 256 };   /* 1 MiB of 4 KiB pages: small enough that the fronts meet often */
    static int32_t own[PAGES];
    uint32_t s = 12345u, bad_cases_mutant = 0, caught_mutant = 0, unsafe = 0, refused = 0, ops_total = 0;
    for (int mutant = 0; mutant < 2; mutant++) {
        omega_tf_test_set_mutant(mutant);
        for (int cs = 0; cs < 1500; cs++) {
            OmegaTf tf;
            omega_tf_init(&tf, (uint64_t)PAGES * 4096u, 8u * 4096u); /* block = 8 pages (32 KiB) */
            uint32_t kvs[32], nk = 0, recs[32], nr = 0;
            int case_bad = 0, case_miss = 0;
            for (int op = 0; op < 40; op++) {
                ops_total++;
                const uint32_t r = lcg(&s) % 6;
                OmegaTf before = tf;
                int rc = 0;
                if (r == 0 && nk < 32) rc = omega_tf_kv_open(&tf, 1, (lcg(&s) % 64) * 16u, "kv", &kvs[nk]), nk += rc == 0;
                else if (r == 1 && nk) rc = omega_tf_kv_grow(&tf, kvs[lcg(&s) % nk], (lcg(&s) % 128) * 16u);
                else if (r == 2 && nr < 32) rc = omega_tf_rec_push(&tf, 2 + lcg(&s) % 3, 1, (1 + lcg(&s) % 24) * 4096u, 0, "rec", &recs[nr]), nr += rc == 0;
                else if (r == 3 && nk) { uint32_t i = lcg(&s) % nk; if (omega_tf_item(&tf, kvs[i]) && omega_tf_item(&tf, kvs[i])->live) rc = omega_tf_release(&tf, kvs[i]); kvs[i] = kvs[--nk]; }
                else if (r == 4 && nr) { uint32_t i = lcg(&s) % nr; if (omega_tf_item(&tf, recs[i]) && omega_tf_item(&tf, recs[i])->live) rc = omega_tf_release(&tf, recs[i]); recs[i] = recs[--nr]; }
                else if (r == 5 && nk) rc = omega_tf_kv_shrink(&tf, kvs[lcg(&s) % nk], (lcg(&s) % 64) * 32u);
                int op_bad = 0;
                if (rc == OMEGA_TF_OVERFLOW) {
                    refused++;
                    if (memcmp(before.item, tf.item, sizeof tf.item) != 0 || before.low != tf.low || before.high != tf.high) op_bad = 1; /* a refusal changed something */
                }
                /* oracle: every live or held item owns its pages; two owners of one page = unsafe */
                for (int p = 0; p < PAGES; p++) own[p] = -1;
                for (uint32_t i = 0; i < tf.n; i++) {
                    const OmegaTfItem *it = &tf.item[i];
                    if (!it->kind) continue;
                    for (uint64_t p = it->off / 4096u; p < (it->off + it->bytes) / 4096u; p++) {
                        if (p >= PAGES || own[p] >= 0) { op_bad = 1; break; }
                        own[p] = (int32_t)i;
                    }
                }
                case_bad |= op_bad;
                if (op_bad && omega_tf_check(&tf) == 0) case_miss = 1;   /* the checker must be red at the very op the oracle is */
                if (!mutant && omega_tf_check(&tf) != 0) { CHECK(0, "random case %d op %d: check red on a correct table: %s", cs, op, tf.err); }
            }
            if (mutant) { bad_cases_mutant += case_bad; caught_mutant += case_bad && !case_miss; }
            else unsafe += case_bad;
        }
    }
    omega_tf_test_set_mutant(0);
    printf("random: %u ops, %u overflow refusals, unsafe (correct code) %u; broken check: %u unsafe cases, %u caught by omega_tf_check\n",
           ops_total, refused, unsafe, bad_cases_mutant, caught_mutant);
    CHECK(unsafe == 0, "the oracle found %u unsafe cases with the real code", unsafe);
    CHECK(bad_cases_mutant > 0 && caught_mutant == bad_cases_mutant, "broken overflow check: %u unsafe, %u caught (must be all, and > 0)", bad_cases_mutant, caught_mutant);
}

/* ======================================== host: the plane-layout kernels did not change ============================ */
/* sha256 of the encoded code bytes BEFORE this change (base qwen38/prefill-graph 6e24f7fd, same bytes as tools-free pins.c built against that base) */
static const char *PIN_DECODE = "1e7ddc7d0f46fc2876edb2fe472f2b0ddbe1d3b1e724eb3ab6f6eeeb0d4b94d3";
static const char *PIN_PREP = "2abdba2799a2f644cf1b86a42603560a72e16e52de038b383cfa383dc7e0c9ee";
static const char *PIN_ATTEND = "bc238908cb71ac57dbf1733527abe9b8482841d970b413cd28b1b95733e9cdfb";
static void hex(const uint8_t *d, char *out) { for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); }
static int sha_decode(uint32_t prep, uint32_t cs, char *out) {
    static BlackwellIRProgram prog;
    OmegaDecodeAttn256Spec spec; memset(&spec, 0, sizeof spec);
    spec.depth = OMEGA_GPU_DATTN256_DEPTH; spec.prep = prep; spec.kv_chunk_stride = cs;
    if (omega_blackwell_codegen_decode_attn256_prog(&spec, &prog) != 0) return -1;
    size_t need = omega_bw_program_code_bytes(&prog);
    uint8_t *code = calloc(1, need);
    OmegaBlackwellKernel k;
    int rc = omega_blackwell_kernel_encode_into(&prog, code, need, &k);
    uint8_t d[32]; sha256_hash(code, need, d); hex(d, out);
    free(code);
    return rc;
}
static int sha_attend(char *out) {
    static uint8_t code[1u << 20];
    size_t n = omega_gpu_prefill_attn256_encode(1u, code, sizeof code, NULL, NULL);
    if (!n) return -1;
    uint8_t d[32]; sha256_hash(code, n, d); hex(d, out);
    return 0;
}
static void pins(int print) {
    char a[65], b[65], c[65];
    int ra = sha_decode(0, 0, a), rb = sha_decode(1, 0, b), rc = sha_attend(c);
    if (print) { printf("PIN decode %s\nPIN prep %s\nPIN attend %s\n", a, b, c); return; }
    CHECK(ra == 0 && strcmp(a, PIN_DECODE) == 0, "plane-layout decode kernel changed: %s", a);
    CHECK(rb == 0 && strcmp(b, PIN_PREP) == 0, "plane-layout prefill prep kernel changed: %s", b);
    CHECK(rc == 0 && strcmp(c, PIN_ATTEND) == 0, "plane-layout prefill attend kernel changed: %s", c);
    char x[65];
    CHECK(sha_decode(0, 16u * MIB, x) == 0 && strcmp(x, a) != 0, "block layout builds a different decode kernel");
    OmegaDecodeAttn256Spec bad; memset(&bad, 0, sizeof bad); bad.depth = 4;
    static BlackwellIRProgram prog;
    bad.kv_chunk_stride = 65536u; CHECK(omega_blackwell_codegen_decode_attn256_prog(&bad, &prog) != 0, "stride below one plane refused");
    bad.kv_chunk_stride = 16u * MIB + 512u; CHECK(omega_blackwell_codegen_decode_attn256_prog(&bad, &prog) != 0, "stride not a page multiple refused");
    OmegaPrefillAttn256Spec pa = { 0, 16u * MIB }; uint32_t ins, gp, ph;
    CHECK(omega_prefill_attn256_prog_stats(&pa, &ins, &gp, &ph) == 0, "block-layout attend builds (%u insns, %u regs)", ins, gp);
    uint32_t ins0, gp0;
    pa.kv_chunk_stride = 0; omega_prefill_attn256_prog_stats(&pa, &ins0, &gp0, &ph);
    printf("attend insns plane %u block %u, regs plane %u block %u\n", ins0, ins, gp0, gp);
}

/* ======================================== host: the byte table ===================================================== */
static void byte_table(void) {
    const uint64_t set = omega_gpu_gdn_state_set_bytes(GDN_LAYERS);
    const uint32_t ctx[3] = { 1700, 30000, 131072 };
    const uint64_t fixed = omega_tf_fixed_conv_bytes(ATTN_LAYERS, 131072, set);
    printf("fixed-maximum conversation (KV for 131072 positions + one live DeltaNet set): %" PRIu64 " bytes\n", fixed);
    for (int i = 0; i < 3; i++) {
        const uint64_t a0 = omega_tf_arena_conv_bytes(ATTN_LAYERS, ctx[i], set, 0), a1 = omega_tf_arena_conv_bytes(ATTN_LAYERS, ctx[i], set, 1);
        /* in the bytes one fixed conversation reserves: conversations that fit, and snapshots beside one conversation */
        const uint64_t convs = fixed / a0, snaps = (fixed - a0) / set;
        printf("context %u: arena conversation %" PRIu64 " bytes (with one snapshot %" PRIu64 "); in the fixed reservation: %" PRIu64
               " conversations, or one conversation + %" PRIu64 " snapshots\n", ctx[i], a0, a1, convs, snaps);
    }
    CHECK(omega_tf_arena_conv_bytes(ATTN_LAYERS, 1700, set, 0) == 7ull * 16 * MIB + set, "1.7k arena bytes");
    CHECK(fixed == 16ull * 2 * (4ull * 131072 * 512 + OMEGA_DATTN256_PLANE_SLACK) + set, "fixed bytes formula");
}

/* ======================================== sim: plane vs block on the same rows ===================================== */
typedef struct { int block; uint32_t layers, layer, max_pos; uint64_t bytes; uint8_t *mem; } Lay;
static uint64_t lay_row(const Lay *l, uint32_t h, uint32_t p, int plane) {
    if (!l->block) return (uint64_t)plane * ((uint64_t)KVH * l->max_pos * RB + OMEGA_DATTN256_PLANE_SLACK) + ((uint64_t)h * l->max_pos + p) * RB;
    return (uint64_t)(p / 256u) * OMEGA_GPU_DATTN256_BLOCK_BYTES(l->layers) + (uint64_t)l->layer * OMEGA_GPU_DATTN256_LAYER_BLOCK_BYTES +
           (uint64_t)plane * (OMEGA_GPU_DATTN256_LAYER_BLOCK_BYTES / 2) + (uint64_t)h * OMEGA_DATTN256_BLOCK_PLANE_BYTES + (uint64_t)(p % 256u) * RB;
}
static void lay_make(Lay *l, int block, uint32_t layers, uint32_t layer, uint32_t max_pos) {
    l->block = block; l->layers = layers; l->layer = layer; l->max_pos = max_pos;
    l->bytes = block ? (uint64_t)(max_pos / 256u) * OMEGA_GPU_DATTN256_BLOCK_BYTES(layers) + OMEGA_DATTN256_PLANE_SLACK
                     : 2ull * ((uint64_t)KVH * max_pos * RB + OMEGA_DATTN256_PLANE_SLACK);
    l->mem = aligned_alloc(4096, (size_t)((l->bytes + 4095) / 4096 * 4096)); memset(l->mem, 0, (size_t)l->bytes);
}
static int sim_decode(Lay *l, int wrong_layer, int mutant, uint32_t pos, uint32_t per, const float *qkv, const float *norms, float *out, uint16_t *rows) {
    static BlackwellIRProgram prog;
    OmegaDecodeAttn256Spec spec; memset(&spec, 0, sizeof spec);
    spec.depth = 4; spec.mutant = mutant; spec.kv_chunk_stride = l->block ? (uint32_t)OMEGA_GPU_DATTN256_BLOCK_BYTES(l->layers) : 0u;
    if (omega_blackwell_codegen_decode_attn256_prog(&spec, &prog) != 0) return -1;
    const uint32_t splits = (pos + per) / per;
    float *rope = aligned_alloc(16, (size_t)l->max_pos * 1024u); for (uint32_t p = 0; p < l->max_pos; p++) omega_dattn256_rope_row(p, THETA, ROT, rope + (size_t)p * 256);
    float *q = aligned_alloc(16, QW * 4), *o = aligned_alloc(16, OW * 4), *part = aligned_alloc(16, (size_t)KVH * splits * OMEGA_DATTN256_PARTIAL_WORDS * 4), *nm = aligned_alloc(16, 2048);
    uint32_t *cnt = aligned_alloc(16, 16);
    memcpy(q, qkv, QW * 4); memcpy(nm, norms, 2048); memset(cnt, 0, 16);
    const uint32_t L = wrong_layer ? (l->layer ? l->layer - 1 : 1) : l->layer;
    uint8_t *kc = l->block ? l->mem + (uint64_t)L * OMEGA_GPU_DATTN256_LAYER_BLOCK_BYTES : l->mem;
    uint8_t *vc = l->block ? kc + OMEGA_GPU_DATTN256_LAYER_BLOCK_BYTES / 2 : l->mem + (uint64_t)KVH * l->max_pos * RB + OMEGA_DATTN256_PLANE_SLACK;
    WsSim s; memset(&s, 0, sizeof s);
    s.p = &prog; s.threads = 256; s.order = 0;
    static uint8_t smem[OMEGA_DATTN256_SHARED_BYTES]; s.smem = smem; s.smem_bytes = OMEGA_DATTN256_SHARED_BYTES;
    uint64_t A[9] = { (uintptr_t)q, (uintptr_t)kc, (uintptr_t)vc, (uintptr_t)rope, (uintptr_t)nm, (uintptr_t)(nm + HD), (uintptr_t)o, (uintptr_t)part, (uintptr_t)cnt };
    for (unsigned i = 0; i < 9; i++) { s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)A[i]; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(A[i] >> 32); }
    float fe = 1e-6f, fo = 1.0f, fq = (float)((1.0 / 16.0) * 1.4426950408889634);
    memcpy(&s.cbank[OMEGA_DATTN256_ARG_EPS / 4], &fe, 4); memcpy(&s.cbank[OMEGA_DATTN256_ARG_OUT_SCALE / 4], &fo, 4); memcpy(&s.cbank[OMEGA_DATTN256_ARG_QK_SCALE / 4], &fq, 4);
    s.cbank[OMEGA_DATTN256_ARG_POS / 4] = pos; s.cbank[OMEGA_DATTN256_ARG_PER_SPLIT / 4] = per; s.cbank[OMEGA_DATTN256_ARG_SPLITS / 4] = splits;
    s.cbank[OMEGA_DATTN256_ARG_KV_STRIDE / 4] = l->block ? OMEGA_DATTN256_BLOCK_PLANE_BYTES : l->max_pos * RB;
    s.nranges = 7;
    s.ranges[0].lo = (uintptr_t)q; s.ranges[0].hi = (uintptr_t)q + QW * 4;
    s.ranges[1].lo = (uintptr_t)l->mem; s.ranges[1].hi = (uintptr_t)l->mem + l->bytes;
    s.ranges[2].lo = (uintptr_t)rope; s.ranges[2].hi = (uintptr_t)rope + (size_t)l->max_pos * 1024u;
    s.ranges[3].lo = (uintptr_t)nm; s.ranges[3].hi = (uintptr_t)nm + 2048;
    s.ranges[4].lo = (uintptr_t)o; s.ranges[4].hi = (uintptr_t)o + OW * 4;
    s.ranges[5].lo = (uintptr_t)part; s.ranges[5].hi = (uintptr_t)part + (size_t)KVH * splits * OMEGA_DATTN256_PARTIAL_WORDS * 4;
    s.ranges[6].lo = (uintptr_t)cnt; s.ranges[6].hi = (uintptr_t)cnt + 16;
    int err = ws_run(&s, splits, KVH);
    if (err) printf("sim: %s (pc %d)\n", s.msg, s.err_pc);
    memcpy(out, o, OW * 4);
    for (uint32_t h = 0; h < KVH; h++) for (int pl = 0; pl < 2; pl++) {
        Lay t = *l; t.layer = L;
        memcpy(rows + ((size_t)h * 2 + pl) * HD, l->mem + lay_row(&t, h, pos, pl), RB);
    }
    free(rope); free(q); free(o); free(part); free(nm); free(cnt);
    return err;
}
static void sim_battery(void) {
    const uint32_t MP = 1024, layers = 3, layer = 2;
    Lay pl, bl; lay_make(&pl, 0, 1, 0, MP); lay_make(&bl, 1, layers, layer, MP);
    uint32_t s = 777u;
    for (uint32_t h = 0; h < KVH; h++) for (uint32_t p = 0; p < MP; p++) for (int k = 0; k < 2; k++) {
        uint16_t row[HD]; for (unsigned i = 0; i < HD; i++) row[i] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f));
        memcpy(pl.mem + lay_row(&pl, h, p, k), row, RB); memcpy(bl.mem + lay_row(&bl, h, p, k), row, RB);
    }
    /* the other block layers get different rows: a wrong layer offset must show */
    for (uint32_t h = 0; h < KVH; h++) for (uint32_t p = 0; p < MP; p++) for (int k = 0; k < 2; k++) {
        Lay t = bl; t.layer = 1; uint16_t row[HD]; for (unsigned i = 0; i < HD; i++) row[i] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f));
        memcpy(bl.mem + lay_row(&t, h, p, k), row, RB);
    }
    float norms[512]; for (unsigned i = 0; i < 512; i++) norms[i] = 1.0f + frand(&s, -0.2f, 0.2f);
    const uint32_t pos_list[] = { 0, 1, 255, 256, 300, 511, 512, 777, 1023 };
    for (unsigned per_i = 0; per_i < 2; per_i++) {
        const uint32_t per = per_i ? 512u : 256u;
        for (unsigned i = 0; i < sizeof pos_list / sizeof pos_list[0]; i++) {
            const uint32_t pos = pos_list[i];
            float qkv[QW]; for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
            static float o1[OW], o2[OW]; static uint16_t r1[8 * HD], r2[8 * HD];
            int e1 = sim_decode(&pl, 0, 0, pos, per, qkv, norms, o1, r1), e2 = sim_decode(&bl, 0, 0, pos, per, qkv, norms, o2, r2);
            CHECK(!e1 && !e2 && memcmp(o1, o2, sizeof o1) == 0 && memcmp(r1, r2, sizeof r1) == 0, "sim pos %u per %u: block layout differs from plane", pos, per);
        }
    }
    /* control: the block layout pointed one layer off must NOT match */
    { float qkv[QW]; for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
      static float o1[OW], o2[OW]; static uint16_t r1[8 * HD], r2[8 * HD];
      sim_decode(&pl, 0, 0, 700, 256, qkv, norms, o1, r1); sim_decode(&bl, 1, 0, 700, 256, qkv, norms, o2, r2);
      CHECK(memcmp(o1, o2, sizeof o1) != 0, "control: a wrong layer offset was not caught by the sim comparison");
      sim_decode(&pl, 0, 0, 900, 256, qkv, norms, o1, r1); sim_decode(&bl, 0, 6, 900, 256, qkv, norms, o2, r2);
      CHECK(memcmp(o1, o2, sizeof o1) != 0, "control: kernel mutant 6 (in-block mask dropped) was not caught by the sim comparison"); }
    free(pl.mem); free(bl.mem);
}

/* ======================================== chip ===================================================================== */
static uint64_t mem_available(void) {
    FILE *f = fopen("/proc/meminfo", "r"); char line[256]; uint64_t kb = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %" SCNu64 " kB", &kb) == 1) break;
    fclose(f);
    return kb * 1024ull;
}
static int mem_guard(uint64_t need, const char *what) {
    const uint64_t av = mem_available();
    printf("memory: %s needs %" PRIu64 " MiB, MemAvailable %" PRIu64 " MiB\n", what, (uint64_t)(need / MIB), (uint64_t)(av / MIB));
    if (av < need + 12ull * GIB) { printf("REFUSED: %s would leave less than 12 GiB for the box\n", what); return -1; }
    return 0;
}
static void fill_rows(OmegaGpuDecodeAttn256Cache *a, uint32_t la, OmegaGpuDecodeAttn256Cache *b, uint32_t lb, uint32_t upto, uint32_t seed) {
    const uint32_t CH = 4096;
    uint16_t *k = malloc((size_t)CH * HD * 2), *v = malloc((size_t)CH * HD * 2);
    uint32_t s = seed;
    for (uint32_t h = 0; h < KVH; h++) for (uint32_t p0 = 0; p0 < upto; p0 += CH) {
        const uint32_t n = upto - p0 < CH ? upto - p0 : CH;
        for (size_t i = 0; i < (size_t)n * HD; i++) { k[i] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f)); v[i] = omega_dattn_bf16_rne(frand(&s, -1.0f, 1.0f)); }
        CHECK(omega_gpu_decode_attn256_cache_write(a, la, h, p0, n, k, v) == 0 && omega_gpu_decode_attn256_cache_write(b, lb, h, p0, n, k, v) == 0, "history write h %u p0 %u", h, p0);
    }
    free(k); free(v);
}
static void set_norms(OmegaGpuDecodeAttn256Cache *c, uint32_t layers, uint32_t seed) {
    uint32_t s = seed; float qn[HD], kn[HD];
    for (unsigned i = 0; i < HD; i++) { qn[i] = 1.0f + frand(&s, -0.2f, 0.2f); kn[i] = 1.0f + frand(&s, -0.2f, 0.2f); }
    for (uint32_t L = 0; L < layers; L++) omega_gpu_decode_attn256_set_norms(c, L, qn, kn);
}
static int rows_equal(OmegaGpuDecodeAttn256Cache *a, uint32_t la, OmegaGpuDecodeAttn256Cache *b, uint32_t lb, uint32_t pos) {
    for (uint32_t h = 0; h < KVH; h++) {
        uint16_t k1[HD], v1[HD], k2[HD], v2[HD];
        if (omega_gpu_decode_attn256_cache_read(a, la, h, pos, k1, v1) || omega_gpu_decode_attn256_cache_read(b, lb, h, pos, k2, v2)) return 0;
        if (memcmp(k1, k2, sizeof k1) || memcmp(v1, v2, sizeof v1)) return 0;
    }
    return 1;
}

/* driver bytes of the old 128K cache vs the arena pieces, from the session's own allocation counter */
static void chip_bytes(uint32_t max_pos) {
    OmegaGpuAllocStats s0, s1, s2, s3;
    if (mem_guard(16ull * 2 * KVH * max_pos * RB, "old 16-layer KV cache") != 0) { CHECK(0, "memory guard refused the old-cache measurement"); return; }
    OmegaGpuDecodeAttn256Cache *old = NULL, *sh = NULL;
    omega_gpu_session_alloc_stats(&s0);
    CHECK(omega_gpu_decode_attn256_cache_alloc(ATTN_LAYERS, max_pos, THETA, ROT, &old) == 0, "old cache alloc: %s", omega_gpu_decode_attn_last_error());
    omega_gpu_session_alloc_stats(&s1);
    omega_gpu_decode_attn256_cache_free(old);
    CHECK(omega_gpu_decode_attn256_cache_alloc_shared(ATTN_LAYERS, max_pos, THETA, ROT, &sh) == 0, "shared cache alloc");
    omega_gpu_session_alloc_stats(&s2);
    OmegaGpuGdn *g = NULL;
    CHECK(omega_gpu_gdn_alloc(GDN_LAYERS, &g) == 0, "gdn alloc 48");
    omega_gpu_session_alloc_stats(&s3);
    printf("BYTES old 16-layer cache at %u positions: %" PRIu64 " driver bytes in %" PRIu64 " allocations\n", max_pos, s1.alloc_bytes - s0.alloc_bytes, s1.allocs - s0.allocs);
    printf("BYTES shared cache (no KV; RoPE, norms, partials, staging): %" PRIu64 " driver bytes\n", s2.alloc_bytes - s1.alloc_bytes);
    printf("BYTES DeltaNet 48 layers own memory (params + tails + states + launch buffer): %" PRIu64 " driver bytes\n", s3.alloc_bytes - s2.alloc_bytes);
    omega_gpu_gdn_free(g);
    omega_gpu_decode_attn256_cache_free(sh);
}

static void chip_recall(uint32_t max_pos, uint32_t reps) {
    const uint64_t blk = OMEGA_GPU_DATTN256_BLOCK_BYTES(ATTN_LAYERS);
    OmegaTf tf;
    omega_tf_init(&tf, (uint64_t)(max_pos / 256u) * blk + 4 * omega_gpu_gdn_state_set_bytes(2), blk);
    const uint64_t need = omega_tf_alloc_bytes(&tf) + 2ull * 2 * KVH * max_pos * RB;
    if (mem_guard(need, "arena + 2-layer plane cache") != 0) { CHECK(0, "memory guard refused the recall run"); return; }
    OmegaGpuMem ar;
    memset(&ar, 0, sizeof ar);
    omega_gpu_session_lock();
    const int arc = omega_gpu_session_open() != NULL && omega_gpu_session_alloc((size_t)omega_tf_alloc_bytes(&tf), &ar) == 0;
    omega_gpu_session_unlock();
    CHECK(arc, "arena alloc %" PRIu64 ": %s", omega_tf_alloc_bytes(&tf), omega_gpu_session_last_error());
    if (!arc) return;
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    memset(ar.cpu, 0, (size_t)ar.size);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("TIME arena zero %" PRIu64 " bytes: %.1f ms (host memset of the handed-out blocks)\n", ar.size, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
    omega_gpu_session_lock(); omega_gpu_session_sync_to_gpu(&ar); omega_gpu_session_unlock();
    uint32_t kv, live, snap;
    CHECK(omega_tf_kv_open(&tf, 1, max_pos, "recall kv", &kv) == 0, "kv extent");
    CHECK(omega_tf_rec_push(&tf, OMEGA_TF_GDN_LIVE, 1, omega_gpu_gdn_state_set_bytes(2), 0, "recall gdn live", &live) == 0, "live set");
    CHECK(omega_tf_rec_push(&tf, OMEGA_TF_GDN_SNAPSHOT, 1, omega_gpu_gdn_state_set_bytes(2), 3, "recall snapshot", &snap) == 0, "snapshot");
    static char map[8192]; omega_tf_map(&tf, map, sizeof map); printf("%s", map);

    OmegaGpuDecodeAttn256Cache *pc = NULL, *sc = NULL;
    CHECK(omega_gpu_decode_attn256_cache_alloc(2, max_pos, THETA, ROT, &pc) == 0, "plane cache");
    CHECK(omega_gpu_decode_attn256_cache_alloc_shared(ATTN_LAYERS, max_pos, THETA, ROT, &sc) == 0, "shared cache");
    CHECK(omega_gpu_decode_attn256_cache_bind_kv(sc, &ar, omega_tf_item(&tf, kv)->off, max_pos) == 0, "bind kv");
    set_norms(pc, 2, 5u); set_norms(sc, ATTN_LAYERS, 5u);

    /* decode: plane layer 0 vs arena layer 15 (the highest offsets inside every block) */
    fill_rows(pc, 0, sc, 15, max_pos, 99u);
    uint32_t s = 4242u;
    const uint32_t plist[] = { 0, 1, 255, 256, 257, 511, 1699, 4095, 29999, 65535, 131071 };
    uint32_t tested = 0, equal = 0;
    for (unsigned i = 0; i < sizeof plist / sizeof plist[0]; i++) {
        const uint32_t pos = plist[i];
        if (pos >= max_pos) continue;
        static float qkv[QW], o1[OW], o2[OW];
        for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
        int r1 = omega_gpu_decode_attn256(pc, 0, pos, qkv, 1e-6f, 1.0f, o1, NULL), r2 = omega_gpu_decode_attn256(sc, 15, pos, qkv, 1e-6f, 1.0f, o2, NULL);
        const int ok = r1 == 0 && r2 == 0 && memcmp(o1, o2, sizeof o1) == 0 && rows_equal(pc, 0, sc, 15, pos);
        tested++; equal += ok;
        CHECK(ok, "decode pos %u: rc %d/%d, arena output or appended rows differ (%s)", pos, r1, r2, omega_gpu_decode_attn_last_error());
    }
    printf("RECALL decode: %u of %u positions bit-identical (outputs + appended K/V rows), plane cache vs arena\n", equal, tested);
    { /* control: kernel mutant 6 (block layout forgets the in-block mask) must break the equality */
      static float qkv[QW], o1[OW], o2[OW]; for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
      omega_gpu_decode_attn256_test_set_mutant(6);
      int r1 = omega_gpu_decode_attn256(pc, 0, 1699, qkv, 1e-6f, 1.0f, o1, NULL), r2 = omega_gpu_decode_attn256(sc, 15, 1699, qkv, 1e-6f, 1.0f, o2, NULL);
      omega_gpu_decode_attn256_test_set_mutant(0);
      const int caught = r1 != 0 || r2 != 0 || memcmp(o1, o2, sizeof o1) != 0;
      CHECK(caught, "control: decode mutant 6 was not caught by the plane-vs-arena comparison");
      printf("CONTROL decode mutant 6 (in-block mask dropped) at pos 1699: %s\n", caught ? "caught (outputs differ)" : "NOT caught"); }

    /* prefill: plane layer 1 vs arena layer 0 */
    fill_rows(pc, 1, sc, 0, max_pos, 199u);
    CHECK(omega_gpu_prefill_attn256_reserve_cache(sc, 512) == 0, "reserve block-layout prefill");
    const uint32_t pf[][2] = { { 0, 300 }, { 1500, 300 }, { 29800, 400 }, { 130816, 256 } };
    tested = equal = 0;
    for (unsigned i = 0; i < 4; i++) {
        const uint32_t p0 = pf[i][0], m = pf[i][1];
        if (p0 + m > max_pos) continue;
        float *qkv = malloc((size_t)m * QW * 4), *o1 = malloc((size_t)m * OW * 4), *o2 = malloc((size_t)m * OW * 4);
        for (size_t j = 0; j < (size_t)m * QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
        int r1 = omega_gpu_prefill_attn256(pc, 1, p0, m, qkv, 1e-6f, 1.0f, o1, NULL), r2 = omega_gpu_prefill_attn256(sc, 0, p0, m, qkv, 1e-6f, 1.0f, o2, NULL);
        int ok = r1 == 0 && r2 == 0 && memcmp(o1, o2, (size_t)m * OW * 4) == 0;
        for (uint32_t p = p0; ok && p < p0 + m; p++) ok = rows_equal(pc, 1, sc, 0, p);
        tested++; equal += ok;
        CHECK(ok, "prefill pos0 %u m %u: rc %d/%d, outputs or rows differ (%s)", p0, m, r1, r2, omega_gpu_decode_attn_last_error());
        free(qkv); free(o1); free(o2);
    }
    printf("RECALL prefill: %u of %u chunks bit-identical (outputs + written rows), crossing block edges\n", equal, tested);
    { /* control: attend mutant 3 (no jump at a block edge) must break the equality */
      const uint32_t p0 = 1500, m = 300;
      float *qkv = malloc((size_t)m * QW * 4), *o1 = malloc((size_t)m * OW * 4), *o2 = malloc((size_t)m * OW * 4);
      for (size_t j = 0; j < (size_t)m * QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
      omega_gpu_prefill_attn256_test_set_mutant(0, 3);
      int r1 = omega_gpu_prefill_attn256(pc, 1, p0, m, qkv, 1e-6f, 1.0f, o1, NULL), r2 = omega_gpu_prefill_attn256(sc, 0, p0, m, qkv, 1e-6f, 1.0f, o2, NULL);
      omega_gpu_prefill_attn256_test_set_mutant(0, 0);
      const int caught = r1 != 0 || r2 != 0 || memcmp(o1, o2, (size_t)m * OW * 4) != 0;
      CHECK(caught, "control: attend mutant 3 was not caught by the plane-vs-arena comparison");
      printf("CONTROL prefill attend mutant 3 (no block jump) at pos0 1500 m 300: %s\n", caught ? "caught (outputs differ)" : "NOT caught");
      free(qkv); free(o1); free(o2); }

    /* DeltaNet: own memory vs the arena live set; snapshot, run, restore, rerun */
    OmegaGpuGdn *ga = NULL, *gb = NULL;
    CHECK(omega_gpu_gdn_alloc(2, &ga) == 0 && omega_gpu_gdn_alloc(2, &gb) == 0, "gdn alloc");
    { uint32_t ps = 31u; float *cw = malloc(10240 * 4 * 4), al[48], dt[48], nw[128];
      for (int L = 0; L < 2; L++) {
          for (int i = 0; i < 10240 * 4; i++) cw[i] = frand(&ps, -0.5f, 0.5f);
          for (int i = 0; i < 48; i++) { al[i] = frand(&ps, -1.0f, 1.0f); dt[i] = frand(&ps, -1.0f, 1.0f); }
          for (int i = 0; i < 128; i++) nw[i] = frand(&ps, 0.5f, 1.5f);
          omega_gpu_gdn_set_layer(ga, (uint32_t)L, cw, al, dt, nw); omega_gpu_gdn_set_layer(gb, (uint32_t)L, cw, al, dt, nw);
      }
      free(cw); }
    CHECK(omega_gpu_gdn_bind_state(gb, &ar, omega_tf_item(&tf, live)->off) == 0, "bind gdn live set");
    const uint64_t snap_off = omega_tf_item(&tf, snap)->off;
    static float qx[10240], zx[6144], abx[96], oa[6144], ob[6144];
    static float y1[3][2][6144];
    int gdn_ok = 1;
    uint32_t gs = 555u;
    for (int t = 0; t < 9; t++) {
        if (t == 3) CHECK(omega_gpu_gdn_snapshot(gb, &ar, snap_off) == 0, "snapshot at token 3");
        if (t == 6) { CHECK(omega_gpu_gdn_restore(gb, &ar, snap_off) == 0, "restore to token 3"); gs = 777u; } /* tokens 6..8 replay 3..5 */
        if (t == 3) gs = 777u;
        for (uint32_t L = 0; L < 2; L++) {
            for (int i = 0; i < 10240; i++) qx[i] = frand(&gs, -1.0f, 1.0f);
            for (int i = 0; i < 6144; i++) zx[i] = frand(&gs, -1.0f, 1.0f);
            for (int i = 0; i < 96; i++) abx[i] = frand(&gs, -1.0f, 1.0f);
            int rb = omega_gpu_gdn_step(gb, L, qx, zx, abx, 1e-6f, 1e-6f, 1.0f, 1.0f, ob, NULL);
            if (t < 6) { int ra = omega_gpu_gdn_step(ga, L, qx, zx, abx, 1e-6f, 1e-6f, 1.0f, 1.0f, oa, NULL); gdn_ok &= ra == 0 && rb == 0 && memcmp(oa, ob, sizeof oa) == 0; }
            if (t >= 3 && t < 6) memcpy(y1[t - 3][L], ob, sizeof ob);
            if (t >= 6) gdn_ok &= rb == 0 && memcmp(y1[t - 6][L], ob, sizeof ob) == 0;
        }
    }
    CHECK(gdn_ok, "DeltaNet: arena live set differs from own memory, or the restored replay differs");
    { static float ta[3 * 10240], tb[3 * 10240]; static float sa[786432], sb2[786432];
      int same = omega_gpu_gdn_get_state(ga, 1, ta, sa) == 0;
      /* ga ran 6 tokens; gb ran 3 + replayed 3 from the snapshot: the same 6-token history */
      same = same && omega_gpu_gdn_get_state(gb, 1, tb, sb2) == 0 && memcmp(ta, tb, sizeof ta) == 0 && memcmp(sa, sb2, sizeof sa) == 0;
      CHECK(same, "DeltaNet: final tails/states differ (own memory vs restored arena set)"); }
    printf("RECALL DeltaNet: arena live set == own memory for 6 tokens x 2 layers; snapshot at 3, restore, replay 3: %s\n", gdn_ok ? "bit-identical" : "DIFFERENT");

    /* timing: decode attention chip time, both layouts, interleaved */
    const uint32_t tpos[] = { 1699, 29999, 131071 };
    for (unsigned i = 0; i < 3; i++) {
        if (tpos[i] >= max_pos) continue;
        uint64_t *ta = calloc(reps, 8), *tb = calloc(reps, 8);
        static float qkv[QW], o[OW]; for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
        for (uint32_t r = 0; r < reps; r++) {
            OmegaGpuDecodeAttn256Info ia, ib;
            omega_gpu_decode_attn256(pc, 0, tpos[i], qkv, 1e-6f, 1.0f, o, &ia);
            omega_gpu_decode_attn256(sc, 15, tpos[i], qkv, 1e-6f, 1.0f, o, &ib);
            ta[r] = ia.elapsed_ns; tb[r] = ib.elapsed_ns;
        }
        qsort(ta, reps, 8, cmp_u64); qsort(tb, reps, 8, cmp_u64);
        printf("TIME decode attention pos %u (context %u, %" PRIu64 " KV bytes): plane median %" PRIu64 " ns (min %" PRIu64 "), block median %" PRIu64 " ns (min %" PRIu64 "), %u reps\n",
               tpos[i], tpos[i] + 1, (uint64_t)(2ull * KVH * (tpos[i] + 1ull) * RB), ta[reps / 2], ta[0], tb[reps / 2], tb[0], reps);
        free(ta); free(tb);
    }
    omega_gpu_gdn_free(ga); omega_gpu_gdn_free(gb);
    omega_gpu_decode_attn256_cache_free(pc); omega_gpu_decode_attn256_cache_free(sc);
    omega_gpu_prefill_attn256_release();
    omega_gpu_session_lock(); omega_gpu_session_free(&ar); omega_gpu_session_unlock();
}

/* the fronts meet on the chip; the next block is refused and the snapshot survives. mutant: the check is skipped, the KV
 * front runs into the snapshot, decode writes land in it. Returns 1 when the snapshot bytes are intact. */
static int chip_overflow_once(int mutant, int *refused) {
    const uint64_t blk = OMEGA_GPU_DATTN256_BLOCK_BYTES(ATTN_LAYERS), set = omega_gpu_gdn_state_set_bytes(2);
    OmegaTf tf;
    /* 5 blocks of bytes: 4 for KV, the 5th holds the records (live set, snapshot, engine scratch), so the KV front meets
     * them after 1024 positions and a 5th block would land on them */
    omega_tf_init(&tf, 5 * blk, blk);
    OmegaGpuMem ar; memset(&ar, 0, sizeof ar);
    omega_gpu_session_lock();
    const int arc = omega_gpu_session_open() != NULL && omega_gpu_session_alloc((size_t)omega_tf_alloc_bytes(&tf), &ar) == 0;
    if (arc) { memset(ar.cpu, 0, (size_t)ar.size); omega_gpu_session_sync_to_gpu(&ar); }
    omega_gpu_session_unlock();
    if (!arc) { CHECK(0, "overflow arena alloc"); return 0; }
    uint32_t live, snap, kv, scr;
    omega_tf_rec_push(&tf, OMEGA_TF_GDN_LIVE, 1, set, 0, "live", &live);
    omega_tf_rec_push(&tf, OMEGA_TF_GDN_SNAPSHOT, 1, set, 1024, "turn snapshot", &snap);
    omega_tf_rec_push(&tf, OMEGA_TF_SCRATCH, 0, blk - 2 * set, 0, "engine scratch", &scr);
    omega_tf_kv_open(&tf, 1, 0, "kv", &kv);
    OmegaGpuGdn *g = NULL; omega_gpu_gdn_alloc(2, &g);
    omega_gpu_gdn_bind_state(g, &ar, omega_tf_item(&tf, live)->off);
    { /* a recognisable live set, snapshotted */
      static float tl[3 * 10240], st[786432]; uint32_t s = 9u;
      for (int i = 0; i < 3 * 10240; i++) tl[i] = frand(&s, -1.0f, 1.0f);
      for (int i = 0; i < 786432; i++) st[i] = frand(&s, -1.0f, 1.0f);
      omega_gpu_gdn_set_state(g, 0, tl, st); omega_gpu_gdn_set_state(g, 1, tl, st); }
    const uint64_t soff = omega_tf_item(&tf, snap)->off;
    omega_gpu_gdn_snapshot(g, &ar, soff);
    uint8_t *saved = malloc((size_t)set); memcpy(saved, (uint8_t *)ar.cpu + soff, (size_t)set);
    omega_tf_test_set_mutant(mutant);
    int rc_meet = omega_tf_kv_grow(&tf, kv, 1024);       /* 4 blocks: the gap is now exactly 0 */
    const uint64_t gap_meet = omega_tf_gap(&tf);
    int rc_next = omega_tf_kv_grow(&tf, kv, 1280);       /* one block more: must be refused */
    omega_tf_test_set_mutant(0);
    *refused = rc_next == OMEGA_TF_OVERFLOW;
    static char map[4096]; omega_tf_map(&tf, map, sizeof map); printf("%s", map);
    printf("overflow%s: meet rc %s gap %" PRIu64 "; next block rc %s: %s\n", mutant ? " (CHECK SKIPPED)" : "", omega_tf_rc_name(rc_meet), (uint64_t)gap_meet,
           omega_tf_rc_name(rc_next), rc_next ? tf.err : "granted");
    OmegaGpuDecodeAttn256Cache *c = NULL;
    omega_gpu_decode_attn256_cache_alloc_shared(ATTN_LAYERS, 2048, THETA, ROT, &c);
    set_norms(c, ATTN_LAYERS, 3u);
    const uint32_t cap = omega_tf_item(&tf, kv)->positions;
    omega_gpu_decode_attn256_cache_bind_kv(c, &ar, omega_tf_item(&tf, kv)->off, cap);
    uint32_t s = 17u; static float qkv[QW], o[OW];
    for (uint32_t pos = 1000; pos < cap; pos++) {   /* the conversation keeps talking up to what the fronts granted */
        for (unsigned j = 0; j < QW; j++) qkv[j] = frand(&s, -1.0f, 1.0f);
        for (uint32_t L = 0; L < ATTN_LAYERS; L++) omega_gpu_decode_attn256(c, L, pos, qkv, 1e-6f, 1.0f, o, NULL);
    }
    omega_gpu_session_lock(); omega_gpu_session_sync_to_cpu(&ar); omega_gpu_session_unlock();
    const int intact = memcmp(saved, (uint8_t *)ar.cpu + soff, (size_t)set) == 0;
    printf("overflow%s: decoded positions 1000..%u in 16 layers; snapshot %s; omega_tf_check: %s\n", mutant ? " (CHECK SKIPPED)" : "", cap - 1,
           intact ? "intact" : "CORRUPTED", omega_tf_check(&tf) == 0 ? "ok" : tf.err);
    free(saved);
    omega_gpu_decode_attn256_cache_free(c); omega_gpu_gdn_free(g);
    omega_gpu_session_lock(); omega_gpu_session_free(&ar); omega_gpu_session_unlock();
    return intact && rc_meet == OMEGA_TF_OK && gap_meet == 0;
}
static void chip_overflow(void) {
    int refused = 0;
    const int intact = chip_overflow_once(g_break, &refused);
    CHECK(intact && refused, "the fronts met, the next block %s, snapshot %s", refused ? "was refused" : "was GRANTED", intact ? "intact" : "CORRUPTED");
    if (!g_break) {  /* the inline control: the same run with the check skipped must corrupt the snapshot */
        int r2 = 0;
        const int intact2 = chip_overflow_once(1, &r2);
        CHECK(!intact2 && !r2, "control: with the overflow check skipped the snapshot must be overrun (it was %s)", intact2 ? "intact" : "overrun");
        printf("CONTROL overflow check skipped: snapshot %s (expected overrun)\n", intact2 ? "intact" : "overrun");
    }
}

int main(int argc, char **argv) {
    int host = 0, sim = 0, print_pins = 0; uint32_t max_pos = 131072, reps = 15;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--print-pins") == 0) print_pins = 1;
        else if (strcmp(argv[i], "--break-overflow") == 0) g_break = 1;
        else if (strcmp(argv[i], "--max-pos") == 0 && i + 1 < argc) max_pos = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else { printf("usage: %s [--host-only | --sim] [--break-overflow] [--max-pos N] [--reps N]\n", argv[0]); return 2; }
    }
    if (print_pins) { pins(1); return 0; }
    if (max_pos % 256u || max_pos < 2048u || max_pos > OMEGA_GPU_DATTN256_MAX_POS || reps == 0) { printf("bad --max-pos / --reps\n"); return 2; }
    scripted();
    const int gap_ok = fill_the_gap(g_break);
    CHECK(gap_ok, "fill the gap: the fronts meet, the next block is refused and nothing moves");
    if (!g_break) CHECK(!fill_the_gap(1), "control: with the overflow check skipped the fill-the-gap test must fail");
    random_oracle();
    pins(0);
    byte_table();
    if (sim) sim_battery();
    if (!host && !sim) {
        chip_bytes(max_pos);
        chip_recall(max_pos, reps);
        chip_overflow();
    }
    printf("gpu_two_front_test%s: %d checks, %d failed: %s\n", g_break ? " (--break-overflow)" : "", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
