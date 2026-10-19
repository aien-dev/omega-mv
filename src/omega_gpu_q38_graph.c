/*
 * omega_gpu_q38_graph.c: one Qwen3.8-27B decode token as one command list (see the header). Records the FP8 / bf16 GEMV,
 * GDN and hd256 attention APIs' *_record launches and the omega_blackwell_codegen_dec glue kernels at width 5120 into an
 * omega_gpu_cmdlist. Buffers: omega_gpu_session_alloc only (GPU-uncached, nvos.h:1117); the host touches x (before),
 * token and logits (after).
 */
#include "omega_gpu_q38_graph.h"
#include "omega_gpu_cmdlist.h"
#include "omega_blackwell_codegen_dec.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_lifetime.h"
#include <string.h>

#define TIMEOUT_MS 20000ull
#define SCALE_2P64 18446744073709551616.0f
#define NEG_PAD (-1e30f)
#define TOKEN_POISON 0xffffffffu
#define H OMEGA_GPU_Q38_HIDDEN
#define I OMEGA_GPU_Q38_INTER

/* glue kernels */
enum { K_NORM, K_ADDNORM, K_SW, K_GATHER, K_GATE, K_AM0, K_AM1, K_SW_M1, K_GATHER_M1, K_GATE_M1, K_ADDNORM_M1, K_COUNT };

typedef struct { OmegaGpuMem code; uint32_t gpr; } Kern;

/* the one buffer: byte offsets, every region 256-byte aligned (RES loads 16-byte vectors) */
typedef struct {
    size_t x, xq, mix, ab, agg, att, o, gu, act, hidden, logits, am_v, am_i, token, norms, fnorm, tabs, gtab, bytes;
} Layout;

/* the regions as omega_lifetime buffers, declared in Layout order (so the flat plan is the layout before omega#569) */
enum { B_X, B_XQ, B_MIX, B_AB, B_AGG, B_ATT, B_O, B_GU, B_ACT, B_HIDDEN, B_LOGITS, B_LOGITS_PAD, B_AMV, B_AMI, B_TOKEN, B_NORMS,
       B_FNORM, B_TABS, B_GTAB, B_COUNT };
#define LT_OPS 1024u   /* 64 layers x at most 12 ops + 4 */
#define LT_ACC 8192u

struct OmegaGpuQ38Graph {
    OmegaGpuQ38GraphConfig cfg;
    uint32_t am_ctas;                    /* stage-1 ARGMAX CTAs = ceil(vocab / 2048) */
    Layout lo;
    OmegaGpuMem buf;
    Kern k[K_COUNT];
    OmegaGpuCmdList *cl;
    struct { const OmegaGpuFp8Tensor *mix, *o, *gu, *dn; const OmegaGpuBf16Tensor *ab; uint32_t kind_index; int attn, set; } layer[OMEGA_GPU_Q38_MAX_LAYERS];
    int final_set;
    /* token state */
    int in_token, failed;
    int xq_layer;                        /* layer whose input-norm x' sits in xq, -1 none */
    int hidden_ready;
    uint64_t t_begin;
    OmegaGpuDecodeGraphStats acc;
    int flush_each, mutant;
    int stamps;
    uint32_t n_kind, tagged_in_list, n_stamp;
    uint8_t kind[OMEGA_GPU_CMDLIST_MAX_LAUNCHES];
    uint64_t stamp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1];
    /* omega#569: the token's access trace, the plan, and what stays readable after the token */
    uint32_t flags;                      /* OMEGA_GPU_Q38_SHARE | OMEGA_GPU_Q38_VERIFY */
    int dry;                             /* tracing only: no launch recorded, no device touched */
    int id[B_COUNT];                     /* omega_lifetime buffer id per region (-1: not declared, logits pad of 0 bytes) */
    OmegaLt lt;
    OmegaLtAccess lt_acc[LT_ACC];
    const char *lt_name[LT_OPS];
    uint32_t op_launch[LT_OPS + 1];      /* first launch index of each op in the list */
    uint64_t off[OMEGA_LT_MAX_BUFS];     /* offsets in use (the buffer layout) */
    uint64_t plan_off[OMEGA_LT_MAX_BUFS];/* the plan chosen at create (test mutants 5, 6 move off away from it) */
    OmegaGpuQ38Plan plan;
    uint8_t valid_end[B_COUNT];          /* region still holds its last value after the last token (shared layout) */
};

static size_t al(size_t v) { return (v + 255u) & ~(size_t)255u; }
static uint64_t va_of(const OmegaGpuQ38Graph *g, size_t off) { return g->buf.va + off; }
static void *cpu_of(const OmegaGpuQ38Graph *g, size_t off) { return (uint8_t *)g->buf.cpu + off; }
static void put64(uint32_t *a, unsigned off, uint64_t v) { a[(off - 0x380) / 4] = (uint32_t)v; a[(off - 0x380) / 4 + 1] = (uint32_t)(v >> 32); }
static void putf(uint32_t *a, unsigned off, float f) { memcpy(&a[(off - 0x380) / 4], &f, 4); }
static int fail(OmegaGpuQ38Graph *g, const char *what, int rc) {
    if (g) g->failed = 1;
    if (what) omega_gpu_session_set_error(what);
    return rc;
}

void omega_q38_gather_table(uint32_t *tab) {
    const uint32_t R = OMEGA_GPU_Q38_ROTARY, QP = 24u * 512u, KP = QP + 4u * 256u;
    for (uint32_t n = 0; n < OMEGA_GPU_Q38_ATT_MIX; n++) {
        uint32_t s;
        if (n < 6144u) s = (n / 256u) * 512u + omega_dattn256_perm_slot(n % 256u, R);              /* q, permuted */
        else if (n < 7168u) s = QP + ((n - 6144u) / 256u) * 256u + omega_dattn256_perm_slot(n % 256u, R); /* k, permuted */
        else if (n < 8192u) s = KP + (n - 7168u);                                                    /* v */
        else s = ((n - 8192u) / 256u) * 512u + 256u + (n - 8192u) % 256u;                            /* gate */
        tab[n] = s;
    }
}

/* lock held */
static int kern_build(Kern *k, const OmegaDecSpec *s) {
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) return OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    int rc = OMEGA_GPU_DGRAPH_OK;
    size_t need = 0;
    if (omega_blackwell_codegen_dec_prog(s, prog) != 0 || (need = omega_bw_program_code_bytes(prog)) == 0) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    else if (omega_gpu_session_alloc_code(need, &k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    else {
        OmegaBlackwellKernel kk;
        if (omega_blackwell_kernel_encode_into(prog, k->code.cpu, need, &kk) != 0 || kk.code_size != need) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
        else if (omega_gpu_session_sync_to_gpu(&k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
        else { k->gpr = omega_bw_gpr_declared(kk.gpr_count); if (k->gpr < 64u) k->gpr = 64u; } /* as the decode graph */
        if (rc != OMEGA_GPU_DGRAPH_OK) { omega_gpu_session_free(&k->code); memset(k, 0, sizeof *k); }
    }
    omega_gpu_host_free(prog);
    return rc;
}

void omega_gpu_q38_graph_free(OmegaGpuQ38Graph *g) {
    if (!g) return;
    if (g->cl) omega_gpu_cmdlist_free(g->cl);
    omega_gpu_session_lock();
    for (unsigned i = 0; i < K_COUNT; i++) if (g->k[i].code.cpu) omega_gpu_session_free(&g->k[i].code);
    if (g->buf.cpu) omega_gpu_session_free(&g->buf);
    omega_gpu_session_unlock();
    omega_gpu_host_free(g);
}

/* ---- omega#569: the regions, the dry trace of one whole token and the plan ---- */
static int rec_layer_ops(OmegaGpuQ38Graph *g, uint32_t l, uint32_t pos);
static int rec_final_ops(OmegaGpuQ38Graph *g);

static int lt_declare(OmegaGpuQ38Graph *g) {
    const uint64_t lg = (uint64_t)g->cfg.vocab * 4u, all = (uint64_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN * 4u;
    const struct { const char *name; uint64_t bytes; uint32_t align, flags; } d[B_COUNT] = {
        [B_X] = { "x", H * 4, 256, OMEGA_LT_LIVE_IN | OMEGA_LT_LIVE_OUT }, [B_XQ] = { "xq", H * 4, 256, 0 },
        [B_MIX] = { "mix", OMEGA_GPU_Q38_GDN_MIX * 4, 256, 0 }, [B_AB] = { "ab", OMEGA_GPU_Q38_AB * 4, 256, 0 },
        [B_AGG] = { "agg", OMEGA_GPU_Q38_ATT_MIX * 4, 256, 0 }, [B_ATT] = { "att", OMEGA_GPU_Q38_MIXER_OUT * 4, 256, 0 },
        [B_O] = { "o", H * 4, 256, 0 }, [B_GU] = { "gate_up", 2 * I * 4, 256, 0 }, [B_ACT] = { "act", I * 4, 256, 0 },
        [B_HIDDEN] = { "hidden", H * 4, 256, OMEGA_LT_LIVE_OUT }, [B_LOGITS] = { "logits", lg, 256, OMEGA_LT_LIVE_OUT },
        /* argmax stage 1 reads whole 2048-value spans: the pad after the logits and the stage-1 value pad are written once
         * at create (NEG_PAD), so they are pinned */
        [B_LOGITS_PAD] = { "logits pad", all - lg, 4, OMEGA_LT_TAIL | OMEGA_LT_PINNED },
        [B_AMV] = { "argmax v", OMEGA_DEC_ARGMAX_SPAN * 4, 256, OMEGA_LT_PINNED }, [B_AMI] = { "argmax i", OMEGA_DEC_ARGMAX_SPAN * 4, 256, 0 },
        [B_TOKEN] = { "token", 16, 256, OMEGA_LT_LIVE_IN | OMEGA_LT_LIVE_OUT },
        [B_NORMS] = { "norms", (uint64_t)g->cfg.layers * 2 * H * 4, 256, OMEGA_LT_PINNED }, [B_FNORM] = { "final norm", H * 4, 256, OMEGA_LT_PINNED },
        [B_TABS] = { "tensor tables", (uint64_t)g->cfg.layers * 4 * 16, 256, OMEGA_LT_PINNED },
        [B_GTAB] = { "gather table", OMEGA_GPU_Q38_ATT_MIX * 4, 256, OMEGA_LT_PINNED },
    };
    omega_lt_init(&g->lt, g->lt_acc, LT_ACC, g->lt_name, LT_OPS);
    for (int b = 0; b < B_COUNT; b++) {
        g->id[b] = -1;
        if (d[b].bytes == 0) continue; /* a vocab that fills its last span has no pad */
        if ((g->id[b] = omega_lt_buf(&g->lt, d[b].name, d[b].bytes, d[b].align, d[b].flags)) < 0) return -1;
    }
    return 0;
}

static void layout_from_off(OmegaGpuQ38Graph *g, uint64_t arena) {
    Layout *L = &g->lo;
    size_t *f[B_COUNT] = { [B_X] = &L->x, [B_XQ] = &L->xq, [B_MIX] = &L->mix, [B_AB] = &L->ab, [B_AGG] = &L->agg, [B_ATT] = &L->att, [B_O] = &L->o,
                           [B_GU] = &L->gu, [B_ACT] = &L->act, [B_HIDDEN] = &L->hidden, [B_LOGITS] = &L->logits, [B_AMV] = &L->am_v,
                           [B_AMI] = &L->am_i, [B_TOKEN] = &L->token, [B_NORMS] = &L->norms, [B_FNORM] = &L->fnorm, [B_TABS] = &L->tabs,
                           [B_GTAB] = &L->gtab };
    for (int b = 0; b < B_COUNT; b++) if (f[b] && g->id[b] >= 0) *f[b] = (size_t)g->off[g->id[b]];
    L->bytes = (size_t)arena;
}

/* the dry trace of one whole token, then the plan (flat, or shared with SHARE); fills g->plan, g->off, g->lo */
static int plan_compute(OmegaGpuQ38Graph *g) {
    if (lt_declare(g) != 0) return -1;
    g->dry = 1; g->xq_layer = -1; g->hidden_ready = 0;
    for (uint32_t l = 0; l < g->cfg.layers; l++) (void)rec_layer_ops(g, l, 0);
    (void)rec_final_ops(g);
    g->dry = 0; g->xq_layer = -1; g->hidden_ready = 0;
    if (omega_lt_live(&g->lt) != 0) return -1;
    uint64_t flat = 0, shared = 0;
    if (omega_lt_plan(&g->lt, g->off, &shared) != 0 || omega_lt_check(&g->lt, g->off, NULL, 0) != 0) return -1;
    uint64_t pinned = 0;
    for (int b = 0; b < B_COUNT; b++)
        if (g->id[b] >= 0 && (g->lt.buf[g->id[b]].flags & (OMEGA_LT_PINNED | OMEGA_LT_LIVE_IN))) pinned += g->lt.buf[g->id[b]].bytes;
    if (omega_lt_plan_flat(&g->lt, g->off, &flat) != 0) return -1;
    g->plan.flat_bytes = flat; g->plan.shared_bytes = shared; g->plan.peak_live = omega_lt_peak_live(&g->lt);
    g->plan.pinned_bytes = pinned; g->plan.ops = g->lt.n_op; g->plan.accesses = g->lt.n_acc;
    uint64_t arena = flat;
    if (g->flags & OMEGA_GPU_Q38_SHARE) (void)omega_lt_plan(&g->lt, g->off, &arena);
    layout_from_off(g, arena);
    memcpy(g->plan_off, g->off, sizeof g->off);
    for (int b = 0; b < B_COUNT; b++) g->valid_end[b] = 1;
    return 0;
}

int omega_gpu_q38_graph_plan(uint32_t layers, uint64_t attn_mask, uint32_t vocab, uint32_t flags, OmegaGpuQ38Plan *plan, char *map, size_t map_cap) {
    if (!plan || layers == 0 || layers > OMEGA_GPU_Q38_MAX_LAYERS || vocab == 0 || vocab > OMEGA_DEC_ARGMAX_SPAN * OMEGA_DEC_ARGMAX_SPAN ||
        (layers < 64u && (attn_mask >> layers) != 0) || (flags & ~(uint32_t)(OMEGA_GPU_Q38_SHARE | OMEGA_GPU_Q38_VERIFY)))
        return OMEGA_GPU_DGRAPH_BAD_ARGS;
    OmegaGpuQ38Graph *g = omega_gpu_host_calloc(1, sizeof *g);
    if (!g) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    g->cfg.layers = layers; g->cfg.attn_mask = attn_mask; g->cfg.vocab = vocab; g->flags = flags;
    g->am_ctas = (vocab + OMEGA_DEC_ARGMAX_SPAN - 1) / OMEGA_DEC_ARGMAX_SPAN;
    for (uint32_t l = 0, na = 0, ng = 0; l < layers; l++) {
        g->layer[l].attn = (int)((attn_mask >> l) & 1u);
        g->layer[l].kind_index = g->layer[l].attn ? na++ : ng++;
    }
    int rc = plan_compute(g) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    if (rc == OMEGA_GPU_DGRAPH_OK) { *plan = g->plan; if (map && map_cap) (void)omega_lt_map(&g->lt, g->off, map, map_cap); }
    omega_gpu_host_free(g);
    return rc;
}

int omega_gpu_q38_graph_get_plan(const OmegaGpuQ38Graph *g, OmegaGpuQ38Plan *plan) {
    if (!g || !plan) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    *plan = g->plan;
    return OMEGA_GPU_DGRAPH_OK;
}

int omega_gpu_q38_graph_create(const OmegaGpuQ38GraphConfig *cfg, OmegaGpuQ38Graph **out) { return omega_gpu_q38_graph_create_ex(cfg, 0, out); }

int omega_gpu_q38_graph_create_ex(const OmegaGpuQ38GraphConfig *cfg, uint32_t flags, OmegaGpuQ38Graph **out) {
    if (out) *out = NULL;
    if (!cfg || !out || cfg->layers == 0 || cfg->layers > OMEGA_GPU_Q38_MAX_LAYERS || !cfg->gdn || !cfg->attn || !cfg->lm_head ||
        cfg->vocab == 0 || cfg->vocab > OMEGA_DEC_ARGMAX_SPAN * OMEGA_DEC_ARGMAX_SPAN) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (cfg->layers < 64u && (cfg->attn_mask >> cfg->layers) != 0) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    uint32_t n = 0, k = 0;
    omega_gpu_bf16_tensor_shape(cfg->lm_head, &n, &k);
    if (flags & ~(uint32_t)(OMEGA_GPU_Q38_SHARE | OMEGA_GPU_Q38_VERIFY)) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (n != cfg->vocab || k != H) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    OmegaGpuQ38Graph *g = omega_gpu_host_calloc(1, sizeof *g);
    if (!g) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    g->cfg = *cfg; g->flags = flags;
    g->am_ctas = (cfg->vocab + OMEGA_DEC_ARGMAX_SPAN - 1) / OMEGA_DEC_ARGMAX_SPAN;
    g->xq_layer = -1;
    for (uint32_t l = 0, na = 0, ng = 0; l < cfg->layers; l++) {
        g->layer[l].attn = (int)((cfg->attn_mask >> l) & 1u);
        g->layer[l].kind_index = g->layer[l].attn ? na++ : ng++;
    }
    /* the layout before omega#569, kept as the reference: the flat plan must reproduce it byte for byte */
    Layout legacy; memset(&legacy, 0, sizeof legacy); Layout *L = &legacy; size_t o = 0;
#define REG(f, bytes) do { L->f = o; o = al(o + (bytes)); } while (0)
    REG(x, H * 4); REG(xq, H * 4); REG(mix, OMEGA_GPU_Q38_GDN_MIX * 4); REG(ab, OMEGA_GPU_Q38_AB * 4); REG(agg, OMEGA_GPU_Q38_ATT_MIX * 4);
    REG(att, OMEGA_GPU_Q38_MIXER_OUT * 4); REG(o, H * 4); REG(gu, 2 * I * 4); REG(act, I * 4); REG(hidden, H * 4);
    REG(logits, (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN * 4); REG(am_v, OMEGA_DEC_ARGMAX_SPAN * 4); REG(am_i, OMEGA_DEC_ARGMAX_SPAN * 4);
    REG(token, 16); REG(norms, (size_t)cfg->layers * 2 * H * 4); REG(fnorm, H * 4); REG(tabs, (size_t)cfg->layers * 4 * 16);
    REG(gtab, OMEGA_GPU_Q38_ATT_MIX * 4);
#undef REG
    L->bytes = o;
    if (plan_compute(g) != 0 || (!(flags & OMEGA_GPU_Q38_SHARE) && memcmp(&legacy, &g->lo, sizeof legacy) != 0)) {
        omega_gpu_host_free(g); omega_gpu_session_set_error("q38 graph: buffer plan failed (or the flat plan left the legacy layout)");
        return OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    }
    L = &g->lo;
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (omega_gpu_cmdlist_create(OMEGA_GPU_CMDLIST_MAX_LAUNCHES, &g->cl) != 0) { omega_gpu_q38_graph_free(g); return OMEGA_GPU_DGRAPH_CHIP_FAIL; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open() || omega_gpu_session_alloc(L->bytes, &g->buf) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    static const OmegaDecSpec spec[K_COUNT] = {
        [K_NORM] = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_NORM, .width = H },
        [K_ADDNORM] = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_ADD | OMEGA_DEC_RES_NORM, .width = H },
        [K_SW] = { .kind = OMEGA_DEC_SWIGLU, .width = I },
        [K_GATHER] = { .kind = OMEGA_DEC_GATHER, .width = OMEGA_GPU_Q38_ATT_MIX, .stride = OMEGA_GPU_Q38_ATT_MIX },
        [K_GATE] = { .kind = OMEGA_DEC_GATE },
        [K_AM0] = { .kind = OMEGA_DEC_ARGMAX, .mode = 0 },
        [K_AM1] = { .kind = OMEGA_DEC_ARGMAX, .mode = 1 },
        [K_SW_M1] = { .kind = OMEGA_DEC_SWIGLU, .width = I, .mutant = 1 },
        [K_GATHER_M1] = { .kind = OMEGA_DEC_GATHER, .width = OMEGA_GPU_Q38_ATT_MIX, .stride = OMEGA_GPU_Q38_ATT_MIX, .mutant = 1 },
        [K_GATE_M1] = { .kind = OMEGA_DEC_GATE, .mutant = 1 },
        [K_ADDNORM_M1] = { .kind = OMEGA_DEC_RES, .mode = OMEGA_DEC_RES_ADD | OMEGA_DEC_RES_NORM, .width = H, .mutant = 1 },
    };
    for (unsigned i = 0; i < K_COUNT && rc == OMEGA_GPU_DGRAPH_OK; i++) rc = kern_build(&g->k[i], &spec[i]);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        memset(g->buf.cpu, 0, L->bytes);
        float *lg = cpu_of(g, L->logits), *amv = cpu_of(g, L->am_v);
        for (size_t i = cfg->vocab; i < (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN; i++) lg[i] = NEG_PAD; /* lm_head writes [0, vocab) only */
        for (size_t i = 0; i < OMEGA_DEC_ARGMAX_SPAN; i++) amv[i] = NEG_PAD;                              /* stage 1 writes [0, ctas) only */
        omega_q38_gather_table(cpu_of(g, L->gtab));
        if (omega_gpu_session_sync_to_gpu(&g->buf) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    }
    omega_gpu_session_unlock();
    if (rc != OMEGA_GPU_DGRAPH_OK) { omega_gpu_q38_graph_free(g); return rc; }
    *out = g;
    return OMEGA_GPU_DGRAPH_OK;
}

static int shape_is(const OmegaGpuFp8Tensor *t, uint32_t n, uint32_t k) {
    uint32_t te = 0, tn = 0, tk = 0; if (t) omega_gpu_fp8_tensor_shape(t, &te, &tn, &tk);
    return t && te == 1 && tn == n && tk == k;
}

int omega_gpu_q38_graph_set_layer(OmegaGpuQ38Graph *g, uint32_t layer, const OmegaGpuFp8Tensor *mix, const OmegaGpuBf16Tensor *ab,
                                  const OmegaGpuFp8Tensor *o, const OmegaGpuFp8Tensor *gate_up, const OmegaGpuFp8Tensor *down,
                                  const float *in_norm, const float *post_norm) {
    if (!g || layer >= g->cfg.layers || !in_norm || !post_norm || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    const int attn = g->layer[layer].attn;
    if (!shape_is(mix, attn ? OMEGA_GPU_Q38_ATT_MIX : OMEGA_GPU_Q38_GDN_MIX, H) || !shape_is(o, H, OMEGA_GPU_Q38_MIXER_OUT) ||
        !shape_is(gate_up, 2 * I, H) || !shape_is(down, H, I)) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (attn ? ab != NULL : ab == NULL) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (!attn) { uint32_t n = 0, k = 0; omega_gpu_bf16_tensor_shape(ab, &n, &k); if (n != OMEGA_GPU_Q38_AB || k != H) return OMEGA_GPU_DGRAPH_BAD_ARGS; }
    uint64_t t[8];
    if (omega_gpu_fp8_tensor_entry(mix, 0, &t[0], &t[1]) != 0 || omega_gpu_fp8_tensor_entry(o, 0, &t[2], &t[3]) != 0 ||
        omega_gpu_fp8_tensor_entry(gate_up, 0, &t[4], &t[5]) != 0 || omega_gpu_fp8_tensor_entry(down, 0, &t[6], &t[7]) != 0) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy((uint8_t *)cpu_of(g, g->lo.tabs) + (size_t)layer * 64u, t, sizeof t);
    memcpy((float *)cpu_of(g, g->lo.norms) + (size_t)layer * 2 * H, in_norm, H * 4u);
    memcpy((float *)cpu_of(g, g->lo.norms) + (size_t)layer * 2 * H + H, post_norm, H * 4u);
    int rc = omega_gpu_session_sync_to_gpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        g->layer[layer].mix = mix; g->layer[layer].ab = ab; g->layer[layer].o = o; g->layer[layer].gu = gate_up; g->layer[layer].dn = down;
        g->layer[layer].set = 1;
    }
    return rc;
}

int omega_gpu_q38_graph_set_final_norm(OmegaGpuQ38Graph *g, const float *final_norm) {
    if (!g || !final_norm || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy(cpu_of(g, g->lo.fnorm), final_norm, H * 4u);
    int rc = omega_gpu_session_sync_to_gpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_DGRAPH_OK) g->final_set = 1;
    return rc;
}

void omega_gpu_q38_graph_set_flush_each(OmegaGpuQ38Graph *g, int on) { if (g) g->flush_each = on ? 1 : 0; }
void omega_gpu_q38_graph_test_set_mutant(OmegaGpuQ38Graph *g, int mutant) {
    if (!g || g->in_token) return;
    g->mutant = mutant;
    if (!(g->flags & OMEGA_GPU_Q38_SHARE) || g->id[B_XQ] < 0 || g->id[B_MIX] < 0) return;
    /* 5, 6: xq moved onto mix (both live in every mix GEMV); anything else: the plan back */
    memcpy(g->off, g->plan_off, sizeof g->off);
    if (mutant == 5 || mutant == 6) g->off[g->id[B_XQ]] = g->off[g->id[B_MIX]];
    layout_from_off(g, g->lo.bytes);
}

/* ---- the list ---- */

static OmegaGpuCmdListOpts list_opts(const OmegaGpuQ38Graph *g) {
    OmegaGpuCmdListOpts o; memset(&o, 0, sizeof o);
    o.mode = OMEGA_GPU_CMDLIST_PCAS; o.barrier = (OmegaGpuCmdListBarrier)omega_gpu_decode_graph_barrier(); o.timeout_ms = TIMEOUT_MS; o.stamps = g->stamps;
    return o;
}

static int submit_now(OmegaGpuQ38Graph *g) {
    if (omega_gpu_cmdlist_count(g->cl) == 0) return OMEGA_GPU_DGRAPH_OK;
    OmegaGpuCmdListOpts o = list_opts(g);
    OmegaGpuCmdListStats st; memset(&st, 0, sizeof st);
    int rc = omega_gpu_cmdlist_submit(g->cl, &o, &st);
    g->acc.launches += st.launches; g->acc.submits += st.submits; g->acc.chip_ns += st.elapsed_ns; g->acc.sems_done += st.sems_done;
    if (rc == 0 && g->stamps) {
        uint64_t tmp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1];
        int cnt = omega_gpu_cmdlist_stamps(g->cl, tmp, OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1);
        if (cnt < 0) rc = -1;
        else { const int from = g->n_stamp == 0 ? 0 : 1; for (int i = from; i < cnt; i++) g->stamp[g->n_stamp++] = tmp[i]; }
    }
    g->tagged_in_list = 0;
    omega_gpu_cmdlist_reset(g->cl);
    if (rc != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (st.sems_done != st.launches) {
        char e[128]; omega_gpu_fmt(e, sizeof e, "q38 graph: %u of %u launch semaphores DONE (first missing %d)", st.sems_done, st.launches, st.first_missing);
        return fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    }
    return OMEGA_GPU_DGRAPH_OK;
}

static void tag(OmegaGpuQ38Graph *g, uint8_t kind) {
    const uint32_t n = omega_gpu_cmdlist_count(g->cl);
    for (; g->tagged_in_list < n && g->n_kind < OMEGA_GPU_CMDLIST_MAX_LAUNCHES; g->tagged_in_list++) g->kind[g->n_kind++] = kind;
}

static int after_op(OmegaGpuQ38Graph *g, uint8_t kind, int api_rc) {
    if (api_rc != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    tag(g, kind);
    return g->flush_each ? submit_now(g) : OMEGA_GPU_DGRAPH_OK;
}

static int add_glue(OmegaGpuQ38Graph *g, uint8_t kind, int ki, uint32_t gx, uint32_t gy, size_t elems, const uint32_t *args, uint32_t n_args) {
    const Kern *k = &g->k[ki];
    OmegaGpuLaunch L; memset(&L, 0, sizeof L);
    L.code_va = k->code.va; L.gpr_count = k->gpr; L.threads_x = OMEGA_DEC_THREADS; L.threads_y = 1; L.grid_x = gx; L.grid_y = gy;
    L.num_elements = elems; L.args = args; L.n_args = n_args; L.timeout_ms = TIMEOUT_MS;
    return after_op(g, kind, omega_gpu_cmdlist_add(g->cl, &L) < 0 ? -1 : 0);
}

/* ---- the access trace (omega#569): each op names the regions it reads (0) and writes (1) before it records ---- */
static void op(OmegaGpuQ38Graph *g, const char *name) {
    if (omega_lt_op(&g->lt, name) == 0) g->op_launch[g->lt.n_op - 1u] = g->dry ? 0u : omega_gpu_cmdlist_count(g->cl);
}
static void use(OmegaGpuQ38Graph *g, int b, int write) { if (g->id[b] >= 0) (void)omega_lt_use(&g->lt, g->id[b], write); }
static void lt_rewind(OmegaGpuQ38Graph *g) { g->lt.n_op = 0; g->lt.n_acc = 0; g->lt.overflow = 0; }

/* RES at width 5120: NORM (x -> y) or ADD|NORM (x += a; y = norm(x)) */
static int rec_res(OmegaGpuQ38Graph *g, int ki, uint64_t x, uint64_t a, uint64_t nw, uint64_t y, float scale) {
    if (g->dry) return OMEGA_GPU_DGRAPH_OK;
    uint32_t w[OMEGA_DEC_RES_WORDS]; memset(w, 0, sizeof w);
    put64(w, OMEGA_DEC_RES_X, x); put64(w, OMEGA_DEC_RES_A, a); put64(w, OMEGA_DEC_RES_NW, nw); put64(w, OMEGA_DEC_RES_Y, y);
    putf(w, OMEGA_DEC_RES_EPS, g->cfg.eps); putf(w, OMEGA_DEC_RES_SCALE, scale);
    return add_glue(g, ki == K_NORM ? OMEGA_GPU_Q38_K_NORM : OMEGA_GPU_Q38_K_RES, ki, 1, 1, H, w, OMEGA_DEC_RES_WORDS);
}

static int rec_fp8(OmegaGpuQ38Graph *g, uint8_t kind, const OmegaGpuFp8Tensor *t, uint64_t tab, uint64_t x, uint64_t y) {
    if (g->dry) return OMEGA_GPU_DGRAPH_OK;
    return after_op(g, kind, omega_gpu_fp8_gemv_grouped_record(g->cl, t, 1, tab, x, 1, y));
}

int omega_gpu_q38_graph_begin_token(OmegaGpuQ38Graph *g, const float *x_embed) {
    if (!g || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    g->t_begin = omega_gpu_session_now_ns();
    g->in_token = 1; g->failed = 0; g->xq_layer = -1; g->hidden_ready = 0;
    memset(&g->acc, 0, sizeof g->acc);
    g->n_kind = 0; g->tagged_in_list = 0; g->n_stamp = 0;
    lt_rewind(g);
    omega_gpu_cmdlist_reset(g->cl);
    if (!omega_gpu_session_open()) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    uint32_t *tok = cpu_of(g, g->lo.token);
    tok[0] = TOKEN_POISON; tok[1] = TOKEN_POISON;
    if (x_embed) memcpy(cpu_of(g, g->lo.x), x_embed, H * 4u);
    if (omega_gpu_session_sync_to_gpu(&g->buf) != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    return OMEGA_GPU_DGRAPH_OK;
}

/* one layer's ops (dry: the trace only) */
static int rec_layer_ops(OmegaGpuQ38Graph *g, uint32_t l, uint32_t pos) {
    const uint64_t x = va_of(g, g->lo.x), xq = va_of(g, g->lo.xq), mix = va_of(g, g->lo.mix), agg = va_of(g, g->lo.agg);
    const uint64_t att = va_of(g, g->lo.att), o = va_of(g, g->lo.o), gu = va_of(g, g->lo.gu), act = va_of(g, g->lo.act);
    const uint64_t in_norm = va_of(g, g->lo.norms) + (uint64_t)l * 2 * H * 4u, post_norm = in_norm + H * 4u, tab = va_of(g, g->lo.tabs) + (uint64_t)l * 64u;
    const uint32_t ki = g->layer[l].kind_index;
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (g->xq_layer != (int)l) {
        op(g, "norm"); use(g, B_X, 0); use(g, B_NORMS, 0); use(g, B_XQ, 1);
        rc = rec_res(g, K_NORM, x, 0, in_norm, xq, SCALE_2P64);
    }
    op(g, "fp8 mix"); use(g, B_XQ, 0); use(g, B_TABS, 0); use(g, B_MIX, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = rec_fp8(g, OMEGA_GPU_Q38_K_MIX, g->layer[l].mix, tab, xq, mix);
    uint64_t mixer_out = att;
    if (!g->layer[l].attn) {
        const uint64_t ab = va_of(g, g->lo.ab);
        op(g, "bf16 ab"); use(g, B_XQ, 0); use(g, B_AB, 1);
        if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry) rc = after_op(g, OMEGA_GPU_Q38_K_AB, omega_gpu_bf16_gemv_record(g->cl, g->layer[l].ab, xq, ab));
        op(g, "gdn"); use(g, B_MIX, 0); use(g, B_AB, 0); /* writes the GDN module's own out row (outside this buffer) */
        if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry)
            rc = after_op(g, OMEGA_GPU_Q38_K_GDN, omega_gpu_gdn_record(g->cl, g->cfg.gdn, ki, mix, mix + OMEGA_GPU_Q38_GDN_QKV * 4u, ab,
                                                                     g->cfg.eps_l2, g->cfg.eps, 1.0f, SCALE_2P64, &mixer_out));
        op(g, "fp8 o"); use(g, B_TABS, 0); use(g, B_O, 1);
    } else {
        uint32_t w[OMEGA_DEC_GA_WORDS]; memset(w, 0, sizeof w);
        put64(w, OMEGA_DEC_GA_IN, mix); put64(w, OMEGA_DEC_GA_TAB, va_of(g, g->lo.gtab)); put64(w, OMEGA_DEC_GA_OUT, agg);
        op(g, "gather"); use(g, B_MIX, 0); if (g->mutant != 7) use(g, B_GTAB, 0); use(g, B_AGG, 1);
        if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry)
            rc = add_glue(g, OMEGA_GPU_Q38_K_GATHER, g->mutant == 2 ? K_GATHER_M1 : K_GATHER, (OMEGA_GPU_Q38_ATT_MIX + OMEGA_DEC_THREADS - 1) / OMEGA_DEC_THREADS, 1,
                          OMEGA_GPU_Q38_ATT_MIX, w, OMEGA_DEC_GA_WORDS);
        op(g, "attn"); use(g, B_AGG, 0); use(g, B_ATT, 1);
        if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry)
            rc = after_op(g, OMEGA_GPU_Q38_K_ATTN, omega_gpu_decode_attn256_record(g->cl, g->cfg.attn, ki, pos, agg, g->cfg.eps, SCALE_2P64, att));
        uint32_t w2[OMEGA_DEC_GT_WORDS]; memset(w2, 0, sizeof w2);
        put64(w2, OMEGA_DEC_GT_A, att); put64(w2, OMEGA_DEC_GT_G, agg + OMEGA_GPU_Q38_ATT_QKV * 4u); put64(w2, OMEGA_DEC_GT_OUT, att);
        w2[(OMEGA_DEC_GT_N - 0x380) / 4] = OMEGA_GPU_Q38_MIXER_OUT;
        op(g, "gate"); use(g, B_ATT, 0); use(g, B_AGG, 0); use(g, B_ATT, 1);
        if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry)
            rc = add_glue(g, OMEGA_GPU_Q38_K_GATE, g->mutant == 3 ? K_GATE_M1 : K_GATE, OMEGA_GPU_Q38_MIXER_OUT / OMEGA_DEC_THREADS, 1,
                          OMEGA_GPU_Q38_MIXER_OUT, w2, OMEGA_DEC_GT_WORDS);
        op(g, "fp8 o"); use(g, B_ATT, 0); use(g, B_TABS, 0); use(g, B_O, 1);
    }
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = rec_fp8(g, OMEGA_GPU_Q38_K_O, g->layer[l].o, tab + 16u, mixer_out, o);
    op(g, "add+norm"); use(g, B_X, 0); use(g, B_O, 0); use(g, B_NORMS, 0); use(g, B_X, 1); use(g, B_XQ, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = rec_res(g, g->mutant == 4 ? K_ADDNORM_M1 : K_ADDNORM, x, o, post_norm, xq, SCALE_2P64);
    op(g, "fp8 gate_up"); use(g, B_XQ, 0); use(g, B_TABS, 0); use(g, B_GU, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = rec_fp8(g, OMEGA_GPU_Q38_K_GATE_UP, g->layer[l].gu, tab + 32u, xq, gu);
    uint32_t w3[OMEGA_DEC_SW_WORDS]; memset(w3, 0, sizeof w3);
    put64(w3, OMEGA_DEC_SW_GU, gu); put64(w3, OMEGA_DEC_SW_ACT, act); putf(w3, OMEGA_DEC_SW_SCALE, SCALE_2P64);
    op(g, "swiglu"); use(g, B_GU, 0); use(g, B_ACT, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry) rc = add_glue(g, OMEGA_GPU_Q38_K_SWIGLU, g->mutant == 1 ? K_SW_M1 : K_SW, I / OMEGA_DEC_THREADS, 1, I, w3, OMEGA_DEC_SW_WORDS);
    op(g, "fp8 down"); use(g, B_ACT, 0); use(g, B_TABS, 0); use(g, B_O, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = rec_fp8(g, OMEGA_GPU_Q38_K_DOWN, g->layer[l].dn, tab + 48u, act, o);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        const int last = l + 1 == g->cfg.layers;
        const uint64_t nw = last ? va_of(g, g->lo.fnorm) : in_norm + 2 * H * 4u;
        op(g, "add+norm"); use(g, B_X, 0); use(g, B_O, 0); use(g, last ? B_FNORM : B_NORMS, 0); use(g, B_X, 1); use(g, last ? B_HIDDEN : B_XQ, 1);
        rc = rec_res(g, K_ADDNORM, x, o, nw, last ? va_of(g, g->lo.hidden) : xq, last ? 1.0f : SCALE_2P64);
        if (rc == OMEGA_GPU_DGRAPH_OK) { if (last) g->hidden_ready = 1; else g->xq_layer = (int)l + 1; }
    }
    return rc;
}

int omega_gpu_q38_graph_record_layer(OmegaGpuQ38Graph *g, uint32_t l, uint32_t pos) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (g->failed) return OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (l >= g->cfg.layers || !g->layer[l].set || g->hidden_ready || (l + 1 == g->cfg.layers && !g->final_set))
        return fail(g, "q38 graph: layer not set, out of order, or final norm missing", OMEGA_GPU_DGRAPH_BAD_ARGS);
    const int rc = rec_layer_ops(g, l, pos);
    return rc == OMEGA_GPU_DGRAPH_OK ? rc : fail(g, NULL, rc);
}

static int rec_final_ops(OmegaGpuQ38Graph *g) {
    const uint64_t x = va_of(g, g->lo.x), hidden = va_of(g, g->lo.hidden), logits = va_of(g, g->lo.logits), token = va_of(g, g->lo.token);
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (!g->hidden_ready) {
        op(g, "norm"); use(g, B_X, 0); use(g, B_FNORM, 0); use(g, B_HIDDEN, 1);
        rc = rec_res(g, K_NORM, x, 0, va_of(g, g->lo.fnorm), hidden, 1.0f);
    }
    op(g, "bf16 lm_head"); use(g, B_HIDDEN, 0); use(g, B_LOGITS, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry) rc = after_op(g, OMEGA_GPU_Q38_K_LM_HEAD, omega_gpu_bf16_gemv_record(g->cl, g->cfg.lm_head, hidden, logits));
    uint32_t a0[OMEGA_DEC_AM_WORDS], a1[OMEGA_DEC_AM_WORDS]; memset(a0, 0, sizeof a0); memset(a1, 0, sizeof a1);
    const uint64_t amv = va_of(g, g->lo.am_v), ami = va_of(g, g->lo.am_i);
    put64(a0, OMEGA_DEC_AM_V, logits); put64(a0, OMEGA_DEC_AM_I, 0); put64(a0, OMEGA_DEC_AM_OV, amv); put64(a0, OMEGA_DEC_AM_OI, ami);
    put64(a1, OMEGA_DEC_AM_V, amv); put64(a1, OMEGA_DEC_AM_I, ami); put64(a1, OMEGA_DEC_AM_OV, token + 4u); put64(a1, OMEGA_DEC_AM_OI, token);
    op(g, "argmax 1"); use(g, B_LOGITS, 0); use(g, B_LOGITS_PAD, 0); use(g, B_AMV, 1); use(g, B_AMI, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry) rc = add_glue(g, OMEGA_GPU_Q38_K_ARGMAX, K_AM0, g->am_ctas, 1, (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN, a0, OMEGA_DEC_AM_WORDS);
    op(g, "argmax 2"); use(g, B_AMV, 0); use(g, B_AMI, 0); use(g, B_TOKEN, 1);
    if (rc == OMEGA_GPU_DGRAPH_OK && !g->dry) rc = add_glue(g, OMEGA_GPU_Q38_K_ARGMAX, K_AM1, 1, 1, OMEGA_DEC_ARGMAX_SPAN, a1, OMEGA_DEC_AM_WORDS);
    return rc;
}

int omega_gpu_q38_graph_record_final(OmegaGpuQ38Graph *g) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (g->failed) return OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (!g->final_set) return fail(g, "q38 graph: final norm not set", OMEGA_GPU_DGRAPH_BAD_ARGS);
    const int rc = rec_final_ops(g);
    return rc == OMEGA_GPU_DGRAPH_OK ? rc : fail(g, NULL, rc);
}

/* VERIFY: every argument word pair of every launch that points into the graph buffer must land in a region its op
 * declared (the recorded command list checked against the trace). Lock held, before the submit. */
static int verify_list(OmegaGpuQ38Graph *g) {
    const uint32_t n = omega_gpu_cmdlist_count(g->cl);
    uint32_t args[OMEGA_GPU_SESSION_MAX_ARGS];
    for (uint32_t i = 0, s = 0; i < n; i++) {
        while (s + 1u < g->lt.n_op && g->op_launch[s + 1u] <= i) s++;
        const int na = omega_gpu_cmdlist_launch_args(g->cl, i, args, OMEGA_GPU_SESSION_MAX_ARGS);
        if (na < 0) return fail(g, "q38 graph verify: launch arguments unreadable", OMEGA_GPU_DGRAPH_CHIP_FAIL);
        for (int w = 0; w + 1 < na; w++) { /* every word pair: a pointer may sit at any word */
            const uint64_t v = (uint64_t)args[w] | ((uint64_t)args[w + 1] << 32);
            if (v < g->buf.va || v >= g->buf.va + g->lo.bytes) continue;
            const uint64_t at = v - g->buf.va;
            int ok = 0;
            for (uint32_t k = 0; k < g->lt.n_acc && !ok; k++) {
                if (g->lt.acc[k].step != s) continue;
                const uint32_t b = g->lt.acc[k].buf;
                if (at >= g->off[b] && at < g->off[b] + g->lt.buf[b].bytes) ok = 1;
                /* a row read as one with the region declared before it (the logits and their pad) */
                if (!ok && b + 1u < g->lt.n_buf && (g->lt.buf[b + 1u].flags & OMEGA_LT_TAIL) && at >= g->off[b] &&
                    at < g->off[b + 1u] + g->lt.buf[b + 1u].bytes) ok = 1;
            }
            if (!ok) {
                char e[160];
                omega_gpu_fmt(e, sizeof e, "q38 graph verify: launch %u (op %u %s) points at buffer byte %llu, in no region the op declared", i, s,
                              g->lt_name[s], (unsigned long long)at);
                return fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
            }
        }
    }
    return OMEGA_GPU_DGRAPH_OK;
}

/* SHARE: the token's real trace against the layout, before anything runs */
static int check_token(OmegaGpuQ38Graph *g) {
    if (g->lt.overflow) return fail(g, "q38 graph: access trace overflowed", OMEGA_GPU_DGRAPH_CHIP_FAIL);
    char why[200];
    if (g->mutant == 6) return OMEGA_GPU_DGRAPH_OK; /* TEST ONLY: the unsafe layout runs unchecked */
    if (omega_lt_live(&g->lt) != 0 || omega_lt_check(&g->lt, g->off, why, sizeof why) != 0) {
        char e[260]; omega_gpu_fmt(e, sizeof e, "q38 graph: shared layout unsafe for this token: %s", why);
        return fail(g, e, OMEGA_GPU_DGRAPH_BAD_ARGS);
    }
    for (int b = 0; b < B_COUNT; b++) g->valid_end[b] = g->id[b] >= 0 && omega_lt_valid_at_end(&g->lt, g->off, g->id[b]);
    return OMEGA_GPU_DGRAPH_OK;
}

int omega_gpu_q38_graph_end_token(OmegaGpuQ38Graph *g, uint32_t *token, OmegaGpuDecodeGraphStats *st) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    int rc = g->failed ? OMEGA_GPU_DGRAPH_CHIP_FAIL : OMEGA_GPU_DGRAPH_OK;
    const uint64_t t_rec = omega_gpu_session_now_ns();
    if (rc == OMEGA_GPU_DGRAPH_OK && (g->flags & OMEGA_GPU_Q38_SHARE)) rc = check_token(g); /* flush_each: the launches already ran */
    if (rc == OMEGA_GPU_DGRAPH_OK && (g->flags & OMEGA_GPU_Q38_VERIFY) && !g->flush_each) rc = verify_list(g);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = submit_now(g);
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_decode_attn256_counters_check(g->cfg.attn) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_cpu(&g->buf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        const uint32_t t = ((const uint32_t *)cpu_of(g, g->lo.token))[0];
        if (t == TOKEN_POISON || t >= g->cfg.vocab) {
            char e[96]; omega_gpu_fmt(e, sizeof e, "q38 graph: token output %u not written or out of range", t);
            rc = fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
        } else if (token) *token = t;
    }
    omega_gpu_cmdlist_reset(g->cl);
    g->in_token = 0;
    const uint64_t t_end = omega_gpu_session_now_ns();
    if (st) { *st = g->acc; st->record_ns = (g->flush_each ? t_end : t_rec) - g->t_begin; st->total_ns = t_end - g->t_begin; }
    omega_gpu_session_unlock();
    return rc;
}

int omega_gpu_q38_graph_token(OmegaGpuQ38Graph *g, const float *x_embed, uint32_t pos, uint32_t *token, OmegaGpuDecodeGraphStats *st) {
    if (!g) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (omega_gpu_q38_graph_begin_token(g, x_embed) == OMEGA_GPU_DGRAPH_BAD_ARGS) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    for (uint32_t l = 0; l < g->cfg.layers && !g->failed; l++) (void)omega_gpu_q38_graph_record_layer(g, l, pos);
    if (!g->failed) (void)omega_gpu_q38_graph_record_final(g);
    return omega_gpu_q38_graph_end_token(g, token, st);
}

int omega_gpu_q38_graph_read(OmegaGpuQ38Graph *g, float *x, float *hidden, float *logits) {
    if (!g || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = omega_gpu_session_sync_to_cpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        if (x) memcpy(x, cpu_of(g, g->lo.x), H * 4u);
        if (hidden) memcpy(hidden, cpu_of(g, g->lo.hidden), H * 4u);
        if (logits) memcpy(logits, cpu_of(g, g->lo.logits), (size_t)g->cfg.vocab * 4u);
    }
    omega_gpu_session_unlock();
    return rc;
}

int omega_gpu_q38_graph_read_buf(OmegaGpuQ38Graph *g, int which, float *out, uint32_t n) {
    if (!g || g->in_token || !out || which < 0 || which >= OMEGA_GPU_Q38_BUF_COUNT) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    const struct { size_t off; uint32_t cap; } b[OMEGA_GPU_Q38_BUF_COUNT] = {
        [OMEGA_GPU_Q38_BUF_X] = { g->lo.x, H }, [OMEGA_GPU_Q38_BUF_XQ] = { g->lo.xq, H }, [OMEGA_GPU_Q38_BUF_MIX] = { g->lo.mix, OMEGA_GPU_Q38_GDN_MIX },
        [OMEGA_GPU_Q38_BUF_AB] = { g->lo.ab, OMEGA_GPU_Q38_AB }, [OMEGA_GPU_Q38_BUF_AGG] = { g->lo.agg, OMEGA_GPU_Q38_ATT_MIX },
        [OMEGA_GPU_Q38_BUF_ATT] = { g->lo.att, OMEGA_GPU_Q38_MIXER_OUT }, [OMEGA_GPU_Q38_BUF_O] = { g->lo.o, H },
        [OMEGA_GPU_Q38_BUF_GU] = { g->lo.gu, 2 * I }, [OMEGA_GPU_Q38_BUF_ACT] = { g->lo.act, I }, [OMEGA_GPU_Q38_BUF_HIDDEN] = { g->lo.hidden, H },
    };
    if (n > b[which].cap) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if ((g->flags & OMEGA_GPU_Q38_SHARE) && !g->valid_end[which]) { /* OMEGA_GPU_Q38_BUF_* == B_* for these ten */
        omega_gpu_session_set_error("q38 graph: shared layout: a later op reused this region's bytes in the last token");
        return OMEGA_GPU_DGRAPH_BAD_ARGS;
    }
    omega_gpu_session_lock();
    int rc = omega_gpu_session_sync_to_cpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (rc == OMEGA_GPU_DGRAPH_OK) memcpy(out, cpu_of(g, b[which].off), (size_t)n * 4u);
    omega_gpu_session_unlock();
    return rc;
}

/* ---- timeline ---- */
void omega_gpu_q38_graph_set_stamps(OmegaGpuQ38Graph *g, int on) { if (g && !g->in_token) g->stamps = on ? 1 : 0; }

int omega_gpu_q38_graph_stamps(const OmegaGpuQ38Graph *g, uint64_t *stamps, uint8_t *kinds, uint32_t cap) {
    if (!g || g->in_token || !stamps || !kinds) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (!g->stamps || g->n_stamp != g->n_kind + 1u) { omega_gpu_session_set_error("q38 graph: no timeline for the last token (stamps off or it failed)"); return OMEGA_GPU_DGRAPH_BAD_ARGS; }
    if (cap < g->n_stamp) { omega_gpu_session_set_error("q38 graph: timeline buffers too small"); return OMEGA_GPU_DGRAPH_BAD_ARGS; }
    memcpy(stamps, g->stamp, (size_t)g->n_stamp * sizeof stamps[0]);
    memcpy(kinds, g->kind, (size_t)g->n_kind);
    return (int)g->n_kind;
}

const char *omega_gpu_q38_graph_kind_name(uint8_t kind) {
    static const char *const names[OMEGA_GPU_Q38_K_COUNT] = { "norm", "mix", "ab", "gdn", "gather", "attn", "gate", "o_proj", "res",
                                                              "gate_up", "swiglu", "down", "lm_head", "argmax" };
    return kind < OMEGA_GPU_Q38_K_COUNT ? names[kind] : "?";
}
