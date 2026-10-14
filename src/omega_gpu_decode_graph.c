/*
 * omega_gpu_decode_graph.c: one decode token as one command list (see the header). Records the four decode APIs'
 * *_record launches and the omega_blackwell_codegen_dec glue kernels into an omega_gpu_cmdlist. Buffers:
 * omega_gpu_session_alloc only (GPU-uncached, nvos.h:1117); the host touches x (before), token and logits (after).
 */
#include "omega_gpu_decode_graph.h"
#include "omega_gpu_cmdlist.h"
#include "omega_blackwell_codegen_dec.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_gpu_resident_api.h"
#include "omega_gpu_fp8gemm_api.h"
#include "omega_blackwell_codegen_vrows.h"
#include <stdlib.h>
#include <string.h>

#define TIMEOUT_MS 20000ull
#define SCALE_2P64 18446744073709551616.0f
#define NEG_PAD (-1e30f)
#define TOKEN_POISON 0xffffffffu

/* glue kernels */
enum { K_NORM, K_ADD, K_CN, K_SW, K_AM0, K_AM1, K_SW_M1, K_CN_M2, K_COUNT };

typedef struct { OmegaGpuMem code; uint32_t gpr; } Kern;

/* the one buffer: byte offsets, every region 256-byte aligned (RES/SWIGLU load 16-byte vectors) */
typedef struct {
    size_t x, xq, qkv, attn, o, gu, act, dn, hidden, logits, am_v, am_i, token, norms, fnorm, tabs, pf_x, pf_y, bytes;
} Layout;

struct OmegaGpuDecodeGraph {
    OmegaGpuDecodeGraphConfig cfg;
    uint32_t am_ctas;                    /* stage-1 ARGMAX CTAs = ceil(vocab / 2048) */
    Layout lo;
    OmegaGpuMem buf;
    Kern k[K_COUNT];
    OmegaGpuCmdList *cl;
    struct { const OmegaGpuFp8Tensor *qkv, *o, *gu, *dn; int set; } layer[OMEGA_GPU_DGRAPH_MAX_LAYERS];
    int final_set;
    /* token state */
    int in_token, failed;
    int xq_layer;                        /* layer whose input-norm x' sits in xq, -1 none */
    int hidden_ready;
    uint64_t t_begin;
    OmegaGpuDecodeGraphStats acc;
    int flush_each, mutant;
    /* timeline (set_stamps): one kind per recorded launch and the GPU timestamps around every launch of the last token */
    int stamps;
    uint32_t n_kind, tagged_in_list, n_stamp;
    uint8_t kind[OMEGA_GPU_CMDLIST_MAX_LAUNCHES];
    uint64_t stamp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1];
    /* W-L2h resident decode chain (omega_gpu_decode_graph_set_resident): built at the first resident token */
    OmegaGpuResident *res;
    int res_mutant;                      /* the mutant the resident kernel was built with (test red) */
    uint32_t res_rows;                   /* W-L2i: rows per warp the resident kernel was built with */
    int res_used;                        /* this token recorded resident launches (end_token closes the batch) */
    int qkv_layer;                       /* layer whose qkv the previous chain already computed, -1 none */
    /* W-L4f batched verify (omega_gpu_decode_graph_verify): its own buffer, the vrows kernels, controls */
    OmegaGpuMem vbuf;
    size_t v_off[24];
    Kern vk[4];                          /* vrows: plain, lane 8, lane 16, TEST mutant */
    int v_ready, v_mutant;
    OmegaGpuDecodeGraphVerifyHook v_hook;
    void *v_hook_ctx;
};

static size_t al(size_t v) { return (v + 255u) & ~(size_t)255u; }
static uint64_t va_of(const OmegaGpuDecodeGraph *g, size_t off) { return g->buf.va + off; }
static void *cpu_of(const OmegaGpuDecodeGraph *g, size_t off) { return (uint8_t *)g->buf.cpu + off; }
static void put64(uint32_t *a, unsigned off, uint64_t v) { a[(off - 0x380) / 4] = (uint32_t)v; a[(off - 0x380) / 4 + 1] = (uint32_t)(v >> 32); }
static void putf(uint32_t *a, unsigned off, float f) { memcpy(&a[(off - 0x380) / 4], &f, 4); }
static int fail(OmegaGpuDecodeGraph *g, const char *what, int rc) {
    if (g) g->failed = 1;
    if (what) omega_gpu_session_set_error(what);
    return rc;
}

/* lock held */
static int kern_build(Kern *k, OmegaDecKind kind, uint32_t mode, int mutant) {
    OmegaDecSpec s; memset(&s, 0, sizeof s); s.kind = kind; s.mode = mode; s.mutant = mutant;
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) return OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    int rc = OMEGA_GPU_DGRAPH_OK;
    size_t need = 0;
    if (omega_blackwell_codegen_dec_prog(&s, prog) != 0 || (need = omega_bw_program_code_bytes(prog)) == 0) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    else if (omega_gpu_session_alloc_code(need, &k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    else {
        OmegaBlackwellKernel kk;
        if (omega_blackwell_kernel_encode_into(prog, k->code.cpu, need, &kk) != 0 || kk.code_size != need) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
        else if (omega_gpu_session_sync_to_gpu(&k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
        else { k->gpr = omega_bw_gpr_declared(kk.gpr_count); if (k->gpr < 64u) k->gpr = 64u; } /* as tests/gpu_cmdlist_test.c (chip PASS) */
        if (rc != OMEGA_GPU_DGRAPH_OK) { omega_gpu_session_free(&k->code); memset(k, 0, sizeof *k); }
    }
    omega_gpu_host_free(prog);
    return rc;
}

void omega_gpu_decode_graph_free(OmegaGpuDecodeGraph *g) {
    if (!g) return;
    if (g->cl) omega_gpu_cmdlist_free(g->cl);
    omega_gpu_session_lock();
    for (unsigned i = 0; i < K_COUNT; i++) if (g->k[i].code.cpu) omega_gpu_session_free(&g->k[i].code);
    if (g->res) omega_gpu_resident_free(g->res);
    for (unsigned i = 0; i < 4; i++) if (g->vk[i].code.cpu) omega_gpu_session_free(&g->vk[i].code);
    if (g->vbuf.cpu) omega_gpu_session_free(&g->vbuf);
    if (g->buf.cpu) omega_gpu_session_free(&g->buf);
    omega_gpu_session_unlock();
    omega_gpu_host_free(g);
}

int omega_gpu_decode_graph_create(const OmegaGpuDecodeGraphConfig *cfg, OmegaGpuDecodeGraph **out) {
    if (out) *out = NULL;
    if (!cfg || !out || cfg->layers == 0 || cfg->layers > OMEGA_GPU_DGRAPH_MAX_LAYERS || !cfg->router || !cfg->attn || !cfg->lm_head ||
        cfg->vocab == 0 || cfg->vocab > OMEGA_DEC_ARGMAX_SPAN * OMEGA_DEC_ARGMAX_SPAN) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    uint32_t n = 0, k = 0;
    omega_gpu_bf16_tensor_shape(cfg->lm_head, &n, &k);
    if (n != cfg->vocab || k != OMEGA_GPU_DGRAPH_HIDDEN) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    OmegaGpuDecodeGraph *g = omega_gpu_host_calloc(1, sizeof *g);
    if (!g) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    g->cfg = *cfg;
    g->am_ctas = (cfg->vocab + OMEGA_DEC_ARGMAX_SPAN - 1) / OMEGA_DEC_ARGMAX_SPAN;
    g->xq_layer = -1;
    const size_t H = OMEGA_GPU_DGRAPH_HIDDEN, TK = OMEGA_GPU_DGRAPH_TOPK, I = OMEGA_GPU_DGRAPH_INTER;
    Layout *L = &g->lo; size_t o = 0;
#define REG(f, bytes) do { L->f = o; o = al(o + (bytes)); } while (0)
    REG(x, H * 4); REG(xq, H * 4); REG(qkv, OMEGA_GPU_DGRAPH_QKV * 4); REG(attn, OMEGA_GPU_DGRAPH_ATTN * 4); REG(o, H * 4);
    REG(gu, TK * 2 * I * 4); REG(act, TK * I * 4); REG(dn, TK * H * 4); REG(hidden, H * 4);
    REG(logits, (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN * 4); REG(am_v, OMEGA_DEC_ARGMAX_SPAN * 4); REG(am_i, OMEGA_DEC_ARGMAX_SPAN * 4);
    REG(token, 16); REG(norms, (size_t)cfg->layers * H * 4); REG(fnorm, H * 4); REG(tabs, (size_t)cfg->layers * 2 * 16);
    REG(pf_x, OMEGA_GPU_DGRAPH_ATTN * 4); REG(pf_y, H * 4); /* prefetch launch (W-L2d): x read by nothing else, y read by nothing */
#undef REG
    L->bytes = o;
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (omega_gpu_cmdlist_create(OMEGA_GPU_CMDLIST_MAX_LAUNCHES, &g->cl) != 0) { omega_gpu_decode_graph_free(g); return OMEGA_GPU_DGRAPH_CHIP_FAIL; }
    omega_gpu_session_lock();
    if (!omega_gpu_session_open() || omega_gpu_session_alloc(L->bytes, &g->buf) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    static const struct { OmegaDecKind kind; uint32_t mode; int mutant; } spec[K_COUNT] = {
        [K_NORM] = { OMEGA_DEC_RES, OMEGA_DEC_RES_NORM, 0 },
        [K_ADD] = { OMEGA_DEC_RES, OMEGA_DEC_RES_ADD, 0 },
        [K_CN] = { OMEGA_DEC_RES, OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM, 0 },
        [K_SW] = { OMEGA_DEC_SWIGLU, 0, 0 },
        [K_AM0] = { OMEGA_DEC_ARGMAX, 0, 0 },
        [K_AM1] = { OMEGA_DEC_ARGMAX, 1, 0 },
        [K_SW_M1] = { OMEGA_DEC_SWIGLU, 0, 1 },
        [K_CN_M2] = { OMEGA_DEC_RES, OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM, 2 },
    };
    for (unsigned i = 0; i < K_COUNT && rc == OMEGA_GPU_DGRAPH_OK; i++) rc = kern_build(&g->k[i], spec[i].kind, spec[i].mode, spec[i].mutant);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        memset(g->buf.cpu, 0, L->bytes);
        float *lg = cpu_of(g, L->logits), *amv = cpu_of(g, L->am_v);
        for (size_t i = cfg->vocab; i < (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN; i++) lg[i] = NEG_PAD; /* lm_head writes [0, vocab) only */
        for (size_t i = 0; i < OMEGA_DEC_ARGMAX_SPAN; i++) amv[i] = NEG_PAD;                              /* stage 1 writes [0, ctas) only */
        if (omega_gpu_session_sync_to_gpu(&g->buf) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    }
    omega_gpu_session_unlock();
    if (rc != OMEGA_GPU_DGRAPH_OK) { omega_gpu_decode_graph_free(g); return rc; }
    *out = g;
    return OMEGA_GPU_DGRAPH_OK;
}

static int shape_is(const OmegaGpuFp8Tensor *t, uint32_t e, uint32_t n, uint32_t k) {
    uint32_t te = 0, tn = 0, tk = 0; omega_gpu_fp8_tensor_shape(t, &te, &tn, &tk);
    return t && te == e && tn == n && tk == k;
}

int omega_gpu_decode_graph_set_layer(OmegaGpuDecodeGraph *g, uint32_t layer, const OmegaGpuFp8Tensor *qkv, const OmegaGpuFp8Tensor *o,
                                     const OmegaGpuFp8Tensor *gate_up, const OmegaGpuFp8Tensor *down, const float *input_norm) {
    if (!g || layer >= g->cfg.layers || !input_norm || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    const uint32_t H = OMEGA_GPU_DGRAPH_HIDDEN, I = OMEGA_GPU_DGRAPH_INTER;
    if (!shape_is(qkv, 1, OMEGA_GPU_DGRAPH_QKV, H) || !shape_is(o, 1, H, OMEGA_GPU_DGRAPH_ATTN) || !shape_is(gate_up, 128, 2 * I, H) || !shape_is(down, 128, H, I))
        return OMEGA_GPU_DGRAPH_BAD_ARGS;
    uint64_t t[4];
    if (omega_gpu_fp8_tensor_entry(qkv, 0, &t[0], &t[1]) != 0 || omega_gpu_fp8_tensor_entry(o, 0, &t[2], &t[3]) != 0) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy((uint8_t *)cpu_of(g, g->lo.tabs) + (size_t)layer * 32u, t, sizeof t);
    memcpy((float *)cpu_of(g, g->lo.norms) + (size_t)layer * H, input_norm, H * 4u);
    int rc = omega_gpu_session_sync_to_gpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_DGRAPH_OK) { g->layer[layer].qkv = qkv; g->layer[layer].o = o; g->layer[layer].gu = gate_up; g->layer[layer].dn = down; g->layer[layer].set = 1; }
    return rc;
}

int omega_gpu_decode_graph_set_final_norm(OmegaGpuDecodeGraph *g, const float *final_norm) {
    if (!g || !final_norm || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy(cpu_of(g, g->lo.fnorm), final_norm, OMEGA_GPU_DGRAPH_HIDDEN * 4u);
    int rc = omega_gpu_session_sync_to_gpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_DGRAPH_OK) g->final_set = 1;
    return rc;
}

OmegaGpuCmdList *omega_gpu_decode_graph_list(OmegaGpuDecodeGraph *g) { return g ? g->cl : NULL; }
void omega_gpu_decode_graph_set_flush_each(OmegaGpuDecodeGraph *g, int on) { if (g) g->flush_each = on ? 1 : 0; }
void omega_gpu_decode_graph_test_set_mutant(OmegaGpuDecodeGraph *g, int mutant) { if (g) g->mutant = mutant; }

void omega_gpu_decode_graph_vas(const OmegaGpuDecodeGraph *g, OmegaGpuDecodeGraphVas *v) {
    if (!v) return;
    memset(v, 0, sizeof *v);
    if (!g) return;
    v->x = va_of(g, g->lo.x); v->xq = va_of(g, g->lo.xq); v->qkv = va_of(g, g->lo.qkv); v->attn = va_of(g, g->lo.attn); v->o = va_of(g, g->lo.o);
    v->gu = va_of(g, g->lo.gu); v->act = va_of(g, g->lo.act); v->dn = va_of(g, g->lo.dn); v->hidden = va_of(g, g->lo.hidden);
    v->logits = va_of(g, g->lo.logits); v->token = va_of(g, g->lo.token);
}

/* ---- the list ---- */

/* barrier between the launches of a token (W-L2c launch-cost study): omega_gpu_decode_graph_set_barrier, else env
 * AIEN_OMEGA_DGRAPH_BARRIER (0 wfi+inval_all = default, 2 wfi_only, 3 wfi+inval_data, 4 flush_data; 1 none is refused: the
 * token is a dependency chain). Read once. */
static int g_barrier = -1;
void omega_gpu_decode_graph_set_barrier(int b) { g_barrier = (b >= 0 && b <= 4 && b != 1) ? b : 0; }
int omega_gpu_decode_graph_barrier(void) {
    if (g_barrier < 0) { const char *e = getenv("AIEN_OMEGA_DGRAPH_BARRIER"); omega_gpu_decode_graph_set_barrier(e ? atoi(e) : 0); }
    return g_barrier;
}
void omega_gpu_decode_graph_test_set_barrier_none(void) { g_barrier = 1; }
/* weight prefetch (W-L2d): 1 = beside each layer's attention, an independent launch (no barrier before it) runs the o_proj GEMV
 * on a scratch x into a scratch y, so the o_proj weights stream from DRAM while the latency-bound attention runs and the real
 * o_proj then reads them from L2 (needs GPU-cached weights: an uncached mapping is never kept in L2, gpu_l2_reuse_test r2).
 * omega_gpu_decode_graph_set_prefetch, else env AIEN_OMEGA_DGRAPH_PREFETCH ("1"). Read once. Default 0. */
static int g_prefetch = -1, g_prefetch_red;
void omega_gpu_decode_graph_set_prefetch(int on) { g_prefetch = on ? 1 : 0; }
int omega_gpu_decode_graph_prefetch(void) {
    if (g_prefetch < 0) { const char *e = getenv("AIEN_OMEGA_DGRAPH_PREFETCH"); g_prefetch = (e && e[0] == '1' && e[1] == 0) ? 1 : 0; }
    return g_prefetch;
}
void omega_gpu_decode_graph_test_set_prefetch_red(int on) { g_prefetch_red = on ? 1 : 0; }
/* resident decode chain (W-L2h): omega_gpu_decode_graph_set_resident, else env AIEN_OMEGA_DGRAPH_RESIDENT ("1"); grid from
 * omega_gpu_decode_graph_set_resident_grid, else env AIEN_OMEGA_DGRAPH_RESIDENT_GRID, else 128 (1024 warps: divides every
 * decode GEMV's rows evenly, W-L2g). Read once. Default off. */
static int g_resident = -1, g_resident_red;
static uint32_t g_resident_grid;
void omega_gpu_decode_graph_set_resident(int on) { g_resident = on ? 1 : 0; }
int omega_gpu_decode_graph_resident(void) {
    if (g_resident < 0) { const char *e = getenv("AIEN_OMEGA_DGRAPH_RESIDENT"); g_resident = (e && e[0] == '1' && e[1] == 0) ? 1 : 0; }
    return g_resident;
}
void omega_gpu_decode_graph_set_resident_grid(uint32_t grid) { g_resident_grid = grid; }
uint32_t omega_gpu_decode_graph_resident_grid(void) {
    if (g_resident_grid == 0) { const char *e = getenv("AIEN_OMEGA_DGRAPH_RESIDENT_GRID"); int v = e ? atoi(e) : 0; g_resident_grid = v > 0 ? (uint32_t)v : 128u; }
    return g_resident_grid;
}
void omega_gpu_decode_graph_test_set_resident_red(int on) { g_resident_red = on ? 1 : 0; }
/* W-L2i: GEMV rows per warp at a time in the resident kernel: set_resident_rows, else env AIEN_OMEGA_DGRAPH_RESIDENT_ROWS ("2"), else 1. */
static uint32_t g_resident_rows;
void omega_gpu_decode_graph_set_resident_rows(uint32_t rows) { g_resident_rows = rows; }
uint32_t omega_gpu_decode_graph_resident_rows(void) {
    if (g_resident_rows == 0) { const char *e = getenv("AIEN_OMEGA_DGRAPH_RESIDENT_ROWS"); g_resident_rows = (e && e[0] == '2' && e[1] == 0) ? 2u : 1u; }
    return g_resident_rows;
}

/* stage index of layer l's [o + add] launch; [gate_up, swiglu, down, combine+norm, next qkv] follow it */
#define RES_PER_LAYER 6u
/* barrier spin budget (polls): a wait this long only happens if a CTA is not co-resident or faulted (W-L2f r4: 2^22 ~ 20 ms) */
#define RES_SPIN_MAX (1u << 22)

/* lock held, every layer and the final norm set. One stage table for the whole model (5 or 6 stages per layer). */
static int res_build(OmegaGpuDecodeGraph *g) {
    const uint32_t L = g->cfg.layers, H = OMEGA_GPU_DGRAPH_HIDDEN, I = OMEGA_GPU_DGRAPH_INTER, TK = OMEGA_GPU_DGRAPH_TOPK;
    for (uint32_t l = 0; l < L; l++) if (!g->layer[l].set) return fail(g, "decode graph: resident chain needs every layer set", OMEGA_GPU_DGRAPH_BAD_ARGS);
    if (!g->final_set) return fail(g, "decode graph: resident chain needs the final norm", OMEGA_GPU_DGRAPH_BAD_ARGS);
    OmegaGpuDecodeGraphVas v; omega_gpu_decode_graph_vas(g, &v);
    OmegaGpuRouterDevOut r; omega_gpu_router_device_outputs_all(g->cfg.router, &r);
    OmegaGpuResident2Stage *s = omega_gpu_host_calloc((size_t)L * RES_PER_LAYER, sizeof *s);
    if (!s) return fail(g, "decode graph: resident stage table alloc", OMEGA_GPU_DGRAPH_CHIP_FAIL);
    uint32_t n = 0;
    for (uint32_t l = 0; l < L; l++) {
        const int last = l + 1 == L;
        const uint64_t tab = va_of(g, g->lo.tabs) + (uint64_t)l * 32u, norm_next = va_of(g, g->lo.norms) + (uint64_t)(l + 1) * H * 4u;
        s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_GEMV16_ADD, .tab = tab + 16u, .x = v.attn, .y = v.o, .add = v.x,
                                           .k = OMEGA_GPU_DGRAPH_ATTN, .n = H, .groups = 1 };
        s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_GEMV16, .tab = r.tab_gu_va, .x = r.xout_va, .y = v.gu, .k = H, .n = 2 * I, .groups = TK };
        s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_SWIGLU, .x = v.gu, .y = v.act, .n = I, .groups = TK, .scale = SCALE_2P64 };
        s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_GEMV8, .tab = r.tab_dn_va, .x = v.act, .y = v.dn, .k = I, .n = H, .groups = TK, .xgs = I * 4u };
        s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_RESNORM, .x = v.x, .dn = v.dn, .wts = r.wts_va, .nw = last ? va_of(g, g->lo.fnorm) : norm_next,
                                           .y = last ? v.hidden : v.xq, .eps = g->cfg.eps, .scale = last ? 1.0f : SCALE_2P64 };
        if (!last) s[n++] = (OmegaGpuResident2Stage){ .kind = OMEGA_RES2_GEMV16, .tab = va_of(g, g->lo.tabs) + (uint64_t)(l + 1) * 32u, .x = v.xq, .y = v.qkv,
                                                      .k = H, .n = OMEGA_GPU_DGRAPH_QKV, .groups = 1 };
        else { s[n] = s[n - 1]; n++; } /* padding: keeps RES_PER_LAYER stages per layer (never run) */
    }
    OmegaResident2Spec spec = { 8u, g_resident_red ? 1 : 0, omega_gpu_decode_graph_resident_rows() };
    int rc = omega_gpu_resident2_create(&spec, s, n, omega_gpu_decode_graph_resident_grid(), &g->res);
    omega_gpu_host_free(s);
    if (rc != OMEGA_GPU_RESIDENT_OK) { g->res = NULL; return fail(g, NULL, rc == OMEGA_GPU_RESIDENT_CODEGEN_FAIL ? OMEGA_GPU_DGRAPH_CODEGEN_FAIL : OMEGA_GPU_DGRAPH_CHIP_FAIL); }
    g->res_mutant = spec.mutant; g->res_rows = spec.rows;
    return OMEGA_GPU_DGRAPH_OK;
}

static OmegaGpuCmdListBarrier barrier_now(void) { return (OmegaGpuCmdListBarrier)omega_gpu_decode_graph_barrier(); }

static OmegaGpuCmdListOpts list_opts(const OmegaGpuDecodeGraph *g) {
    OmegaGpuCmdListOpts o; memset(&o, 0, sizeof o);
    o.mode = OMEGA_GPU_CMDLIST_PCAS; o.barrier = barrier_now(); o.timeout_ms = TIMEOUT_MS; o.stamps = g->stamps;
    return o;
}

static int submit_now(OmegaGpuDecodeGraph *g) {
    if (omega_gpu_cmdlist_count(g->cl) == 0) return OMEGA_GPU_DGRAPH_OK;
    OmegaGpuCmdListOpts o = list_opts(g);
    OmegaGpuCmdListStats st; memset(&st, 0, sizeof st);
    int rc = omega_gpu_cmdlist_submit(g->cl, &o, &st);
    g->acc.launches += st.launches; g->acc.submits += st.submits; g->acc.chip_ns += st.elapsed_ns; g->acc.sems_done += st.sems_done;
    if (rc == 0 && g->stamps) { /* before reset: the accessor sizes by the list; later submits (flush_each) drop their start slot */
        uint64_t tmp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1];
        int cnt = omega_gpu_cmdlist_stamps(g->cl, tmp, OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1);
        if (cnt < 0) rc = -1;
        else { const int from = g->n_stamp == 0 ? 0 : 1; for (int i = from; i < cnt; i++) g->stamp[g->n_stamp++] = tmp[i]; }
    }
    g->tagged_in_list = 0;
    omega_gpu_cmdlist_reset(g->cl);
    if (rc != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (st.sems_done != st.launches) {
        char e[128]; omega_gpu_fmt(e, sizeof e, "decode graph: %u of %u launch semaphores DONE (first missing %d)", st.sems_done, st.launches, st.first_missing);
        return fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    }
    return OMEGA_GPU_DGRAPH_OK;
}

/* kinds for the launches added since the last tag (the timeline) */
static void tag(OmegaGpuDecodeGraph *g, uint8_t kind) {
    const uint32_t n = omega_gpu_cmdlist_count(g->cl);
    for (; g->tagged_in_list < n && g->n_kind < OMEGA_GPU_CMDLIST_MAX_LAUNCHES; g->tagged_in_list++) g->kind[g->n_kind++] = kind;
}

/* after every recorded launch: flush_each submits it on its own (the comparison path) */
static int after_op(OmegaGpuDecodeGraph *g, uint8_t kind, int api_rc) {
    if (api_rc != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    tag(g, kind);
    return g->flush_each ? submit_now(g) : OMEGA_GPU_DGRAPH_OK;
}

static int add_glue(OmegaGpuDecodeGraph *g, uint8_t kind, const Kern *k, uint32_t gx, uint32_t gy, size_t elems, const uint32_t *args, uint32_t n_args) {
    OmegaGpuLaunch L; memset(&L, 0, sizeof L);
    L.code_va = k->code.va; L.gpr_count = k->gpr; L.threads_x = OMEGA_DEC_THREADS; L.threads_y = 1; L.grid_x = gx; L.grid_y = gy;
    L.num_elements = elems; L.args = args; L.n_args = n_args; L.timeout_ms = TIMEOUT_MS;
    return after_op(g, kind, omega_gpu_cmdlist_add(g->cl, &L) < 0 ? -1 : 0);
}

static int res_kernel(const OmegaGpuDecodeGraph *g, uint32_t mode) {
    if (mode == OMEGA_DEC_RES_NORM) return K_NORM;
    if (mode == OMEGA_DEC_RES_ADD) return K_ADD;
    if (mode == (OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM)) return g->mutant == 2 ? K_CN_M2 : K_CN;
    return -1;
}

int omega_gpu_decode_graph_record_res(OmegaGpuDecodeGraph *g, uint32_t mode, uint64_t x_va, uint64_t a_va, uint64_t w_va,
                                      uint64_t nw_va, uint64_t y_va, float eps, float scale) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    int ki = res_kernel(g, mode);
    if (ki < 0) return fail(g, "decode graph: RES mode not built (NORM, ADD, COMBINE|NORM)", OMEGA_GPU_DGRAPH_BAD_ARGS);
    uint32_t a[OMEGA_DEC_RES_WORDS]; memset(a, 0, sizeof a);
    put64(a, OMEGA_DEC_RES_X, x_va); put64(a, OMEGA_DEC_RES_A, a_va); put64(a, OMEGA_DEC_RES_W, w_va); put64(a, OMEGA_DEC_RES_NW, nw_va);
    put64(a, OMEGA_DEC_RES_Y, y_va); putf(a, OMEGA_DEC_RES_EPS, eps); putf(a, OMEGA_DEC_RES_SCALE, scale);
    const uint8_t kind = ki == K_ADD ? OMEGA_GPU_DGRAPH_K_ADD : ki == K_NORM ? OMEGA_GPU_DGRAPH_K_NORM : OMEGA_GPU_DGRAPH_K_COMBINE;
    return add_glue(g, kind, &g->k[ki], 1, 1, OMEGA_GPU_DGRAPH_HIDDEN, a, OMEGA_DEC_RES_WORDS);
}

int omega_gpu_decode_graph_record_swiglu(OmegaGpuDecodeGraph *g, uint64_t gu_va, uint64_t act_va, float scale) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    uint32_t a[OMEGA_DEC_SW_WORDS]; memset(a, 0, sizeof a);
    put64(a, OMEGA_DEC_SW_GU, gu_va); put64(a, OMEGA_DEC_SW_ACT, act_va); putf(a, OMEGA_DEC_SW_SCALE, scale);
    return add_glue(g, OMEGA_GPU_DGRAPH_K_SWIGLU, &g->k[g->mutant == 1 ? K_SW_M1 : K_SW], OMEGA_DEC_INTER / OMEGA_DEC_THREADS, OMEGA_DEC_TOPK, (size_t)OMEGA_DEC_TOPK * OMEGA_DEC_INTER, a, OMEGA_DEC_SW_WORDS);
}

int omega_gpu_decode_graph_begin_token(OmegaGpuDecodeGraph *g, const float *x_embed) {
    if (!g || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    g->t_begin = omega_gpu_session_now_ns();
    g->in_token = 1; g->failed = 0; g->xq_layer = -1; g->hidden_ready = 0;
    g->qkv_layer = -1;
    memset(&g->acc, 0, sizeof g->acc);
    g->n_kind = 0; g->tagged_in_list = 0; g->n_stamp = 0;
    omega_gpu_cmdlist_reset(g->cl);
    if (!omega_gpu_session_open()) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    uint32_t *tok = cpu_of(g, g->lo.token);
    tok[0] = TOKEN_POISON; tok[1] = TOKEN_POISON;
    if (x_embed) memcpy(cpu_of(g, g->lo.x), x_embed, OMEGA_GPU_DGRAPH_HIDDEN * 4u);
    if (omega_gpu_session_sync_to_gpu(&g->buf) != 0) return fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (omega_gpu_decode_graph_resident() && (!g->res || g->res_mutant != (g_resident_red ? 1 : 0) || g->res_rows != omega_gpu_decode_graph_resident_rows())) {
        if (g->res) { omega_gpu_resident_free(g->res); g->res = NULL; }
        int rc = res_build(g);
        if (rc != OMEGA_GPU_DGRAPH_OK) return rc;
    }
    return OMEGA_GPU_DGRAPH_OK;
}

int omega_gpu_decode_graph_record_layer(OmegaGpuDecodeGraph *g, uint32_t l, uint32_t pos) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (g->failed) return OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (l >= g->cfg.layers || !g->layer[l].set || g->hidden_ready || (l + 1 == g->cfg.layers && !g->final_set))
        return fail(g, "decode graph: layer not set, out of order, or final norm missing", OMEGA_GPU_DGRAPH_BAD_ARGS);
    OmegaGpuDecodeGraphVas v; omega_gpu_decode_graph_vas(g, &v);
    const float eps = g->cfg.eps;
    const uint64_t norm_l = va_of(g, g->lo.norms) + (uint64_t)l * OMEGA_GPU_DGRAPH_HIDDEN * 4u, tab = va_of(g, g->lo.tabs) + (uint64_t)l * 32u;
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (g->res && omega_gpu_decode_graph_resident()) {
        /* W-L2h resident chain: launched NORM + qkv only when no chain computed them; attention and router stay launched;
         * [o + add] and [gate_up, swiglu, down, combine + norm, next layer's qkv] are one resident launch each (same
         * operations, same order: the token and logits must be bit-identical to the launched list). */
        const uint32_t s0 = l * RES_PER_LAYER;
        const int last = l + 1 == g->cfg.layers;
        if (g->qkv_layer != (int)l) {
            if (g->xq_layer != (int)l) rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_NORM, v.x, 0, 0, norm_l, v.xq, eps, SCALE_2P64);
            if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_QKV, omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].qkv, 1, tab, v.xq, 1, v.qkv));
        }
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_ATTN, omega_gpu_decode_attn_record(g->cl, g->cfg.attn, l, pos, v.qkv, eps, g->mutant == 3 ? 1.0f : SCALE_2P64, v.attn));
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_RES_O, omega_gpu_resident2_record_range(g->cl, g->res, s0, 1, RES_SPIN_MAX));
        g->res_used = 1;
        OmegaGpuRouterDevOut r; memset(&r, 0, sizeof r);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_ROUTER, omega_gpu_router_record(g->cl, g->cfg.router, l, v.x, eps, g->cfg.norm_topk, SCALE_2P64, &r));
        if (rc == OMEGA_GPU_DGRAPH_OK) {
            OmegaGpuRouterDevOut b; omega_gpu_router_device_outputs_all(g->cfg.router, &b);
            if (memcmp(&b, &r, sizeof r) != 0) rc = fail(g, "decode graph: router outputs moved since the resident table was built", OMEGA_GPU_DGRAPH_CHIP_FAIL);
        }
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_RES_MOE, omega_gpu_resident2_record_range(g->cl, g->res, s0 + 1, last ? 4u : 5u, RES_SPIN_MAX));
        if (rc == OMEGA_GPU_DGRAPH_OK) { if (last) g->hidden_ready = 1; else { g->xq_layer = (int)l + 1; g->qkv_layer = (int)l + 1; } }
        return rc == OMEGA_GPU_DGRAPH_OK ? rc : fail(g, NULL, rc);
    }
    if (g->xq_layer != (int)l) rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_NORM, v.x, 0, 0, norm_l, v.xq, eps, SCALE_2P64);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_QKV, omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].qkv, 1, tab, v.xq, 1, v.qkv));
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_ATTN, omega_gpu_decode_attn_record(g->cl, g->cfg.attn, l, pos, v.qkv, eps, g->mutant == 3 ? 1.0f : SCALE_2P64, v.attn));
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_decode_graph_prefetch()) { /* tagged attn: its time is the overlapped window */
        int prc = omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].o, 1, tab + 16u, va_of(g, g->lo.pf_x), 1, va_of(g, g->lo.pf_y));
        if (prc == 0 && omega_gpu_cmdlist_mark_independent(g->cl) != 0) prc = -1;
        rc = after_op(g, OMEGA_GPU_DGRAPH_K_ATTN, prc);
    }
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_O, omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].o, 1, tab + 16u, v.attn, 1, v.o));
    if (rc == OMEGA_GPU_DGRAPH_OK && g_prefetch_red && !g->flush_each && omega_gpu_cmdlist_mark_independent(g->cl) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL; /* TEST ONLY red: o_proj itself unordered after the attention */
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_ADD, v.x, v.o, 0, 0, 0, eps, 1.0f);
    OmegaGpuRouterDevOut r; memset(&r, 0, sizeof r);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_ROUTER, omega_gpu_router_record(g->cl, g->cfg.router, l, v.x, eps, g->cfg.norm_topk, SCALE_2P64, &r));
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_GATE_UP, omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].gu, OMEGA_GPU_DGRAPH_TOPK, r.tab_gu_va, r.xout_va, 1, v.gu));
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = omega_gpu_decode_graph_record_swiglu(g, v.gu, v.act, SCALE_2P64);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_DOWN, omega_gpu_fp8_gemv_grouped_record(g->cl, g->layer[l].dn, OMEGA_GPU_DGRAPH_TOPK, r.tab_dn_va, v.act, OMEGA_GPU_DGRAPH_TOPK, v.dn));
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        const int last = l + 1 == g->cfg.layers;
        const uint64_t nw = last ? va_of(g, g->lo.fnorm) : norm_l + OMEGA_GPU_DGRAPH_HIDDEN * 4u;
        rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM, v.x, v.dn, r.wts_va, nw, last ? v.hidden : v.xq, eps, last ? 1.0f : SCALE_2P64);
        if (rc == OMEGA_GPU_DGRAPH_OK) { if (last) g->hidden_ready = 1; else g->xq_layer = (int)l + 1; }
    }
    return rc == OMEGA_GPU_DGRAPH_OK ? rc : fail(g, NULL, rc);
}

int omega_gpu_decode_graph_record_final(OmegaGpuDecodeGraph *g) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (g->failed) return OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (!g->final_set) return fail(g, "decode graph: final norm not set", OMEGA_GPU_DGRAPH_BAD_ARGS);
    OmegaGpuDecodeGraphVas v; omega_gpu_decode_graph_vas(g, &v);
    int rc = OMEGA_GPU_DGRAPH_OK;
    if (!g->hidden_ready) rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_NORM, v.x, 0, 0, va_of(g, g->lo.fnorm), v.hidden, g->cfg.eps, 1.0f);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = after_op(g, OMEGA_GPU_DGRAPH_K_LM_HEAD, omega_gpu_bf16_gemv_record(g->cl, g->cfg.lm_head, v.hidden, v.logits));
    uint32_t a0[OMEGA_DEC_AM_WORDS], a1[OMEGA_DEC_AM_WORDS]; memset(a0, 0, sizeof a0); memset(a1, 0, sizeof a1);
    const uint64_t amv = va_of(g, g->lo.am_v), ami = va_of(g, g->lo.am_i);
    put64(a0, OMEGA_DEC_AM_V, v.logits); put64(a0, OMEGA_DEC_AM_I, 0); put64(a0, OMEGA_DEC_AM_OV, amv); put64(a0, OMEGA_DEC_AM_OI, ami);
    put64(a1, OMEGA_DEC_AM_V, amv); put64(a1, OMEGA_DEC_AM_I, ami); put64(a1, OMEGA_DEC_AM_OV, v.token + 4u); put64(a1, OMEGA_DEC_AM_OI, v.token);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_glue(g, OMEGA_GPU_DGRAPH_K_ARGMAX, &g->k[K_AM0], g->am_ctas, 1, (size_t)g->am_ctas * OMEGA_DEC_ARGMAX_SPAN, a0, OMEGA_DEC_AM_WORDS);
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_glue(g, OMEGA_GPU_DGRAPH_K_ARGMAX, &g->k[K_AM1], 1, 1, OMEGA_DEC_ARGMAX_SPAN, a1, OMEGA_DEC_AM_WORDS);
    return rc == OMEGA_GPU_DGRAPH_OK ? rc : fail(g, NULL, rc);
}

int omega_gpu_decode_graph_end_token(OmegaGpuDecodeGraph *g, uint32_t *token, OmegaGpuDecodeGraphStats *st) {
    if (!g || !g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    int rc = g->failed ? OMEGA_GPU_DGRAPH_CHIP_FAIL : OMEGA_GPU_DGRAPH_OK;
    const uint64_t t_rec = omega_gpu_session_now_ns();
    if (rc == OMEGA_GPU_DGRAPH_OK) rc = submit_now(g);
    if (g->res && g->res_used) { /* W-L2h: close the batch; any failure leaves the barrier count unknown: fresh word */
        uint32_t prog = 0, err = 0;
        if (rc != OMEGA_GPU_DGRAPH_OK) (void)omega_gpu_resident2_reset(g->res);
        else if (omega_gpu_resident_status(g->res, &prog, &err) != OMEGA_GPU_RESIDENT_OK || err != 0) {
            char e[96]; omega_gpu_fmt(e, sizeof e, "decode graph: resident chain error word %u (barrier wait expired or aborted)", err);
            rc = fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
        }
        g->res_used = 0;
    }
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_decode_attn_counters_check(g->cfg.attn) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_cpu(&g->buf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        const uint32_t t = ((const uint32_t *)cpu_of(g, g->lo.token))[0];
        if (t == TOKEN_POISON || t >= g->cfg.vocab) {
            char e[96]; omega_gpu_fmt(e, sizeof e, "decode graph: token output %u not written or out of range", t);
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

int omega_gpu_decode_graph_token(OmegaGpuDecodeGraph *g, const float *x_embed, uint32_t pos, uint32_t *token, OmegaGpuDecodeGraphStats *st) {
    if (!g) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (omega_gpu_decode_graph_begin_token(g, x_embed) == OMEGA_GPU_DGRAPH_BAD_ARGS) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    for (uint32_t l = 0; l < g->cfg.layers && !g->failed; l++) (void)omega_gpu_decode_graph_record_layer(g, l, pos);
    if (!g->failed) (void)omega_gpu_decode_graph_record_final(g);
    return omega_gpu_decode_graph_end_token(g, token, st);
}

int omega_gpu_decode_graph_read(OmegaGpuDecodeGraph *g, float *x, float *hidden, float *logits) {
    if (!g || g->in_token) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = omega_gpu_session_sync_to_cpu(&g->buf) == 0 ? OMEGA_GPU_DGRAPH_OK : OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        if (x) memcpy(x, cpu_of(g, g->lo.x), OMEGA_GPU_DGRAPH_HIDDEN * 4u);
        if (hidden) memcpy(hidden, cpu_of(g, g->lo.hidden), OMEGA_GPU_DGRAPH_HIDDEN * 4u);
        if (logits) memcpy(logits, cpu_of(g, g->lo.logits), (size_t)g->cfg.vocab * 4u);
    }
    omega_gpu_session_unlock();
    return rc;
}

/* ---- timeline ---- */
void omega_gpu_decode_graph_set_stamps(OmegaGpuDecodeGraph *g, int on) { if (g && !g->in_token) g->stamps = on ? 1 : 0; }

int omega_gpu_decode_graph_stamps(const OmegaGpuDecodeGraph *g, uint64_t *stamps, uint8_t *kinds, uint32_t cap) {
    if (!g || g->in_token || !stamps || !kinds) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (!g->stamps || g->n_stamp != g->n_kind + 1u) { omega_gpu_session_set_error("decode graph: no timeline for the last token (stamps off or it failed)"); return OMEGA_GPU_DGRAPH_BAD_ARGS; }
    if (cap < g->n_stamp) { omega_gpu_session_set_error("decode graph: timeline buffers too small"); return OMEGA_GPU_DGRAPH_BAD_ARGS; }
    memcpy(stamps, g->stamp, (size_t)g->n_stamp * sizeof stamps[0]);
    memcpy(kinds, g->kind, (size_t)g->n_kind);
    return (int)g->n_kind;
}

const char *omega_gpu_decode_graph_kind_name(uint8_t kind) {
    static const char *const names[OMEGA_GPU_DGRAPH_K_COUNT] = { "norm", "qkv", "attn", "o_proj", "res_add", "router", "gate_up", "swiglu", "down", "combine_norm", "lm_head", "argmax", "res_o_add", "res_moe_chain" };
    return kind < OMEGA_GPU_DGRAPH_K_COUNT ? names[kind] : "?";
}

/* ---- W-L4f: batched exact verify (see the header) ---- */

enum { VX, VXQ, VXP, VQKV, VATTN, VO, VXR, VXS, VGU, VACT, VACTP, VDN, VDNT, VHID, VIDENT, VZERO, VIGU, VIINV, VSQKV, VSO, VSGU, VSDN, V_COUNT };
enum { VK_PLAIN, VK_L8, VK_L16, VK_MUT };
#define VR OMEGA_GPU_DGRAPH_VERIFY_MAX_ROWS
#define VG (VR * OMEGA_GPU_DGRAPH_TOPK)
#define VPAD OMEGA_GPU_FP8GEMM_RECORD_PAD
#define VXR_F OMEGA_GPU_DGRAPH_VERIFY_XR      /* router copy row: xout [2048] | ids [8] | gap [8] | wts [8] */
#define VXR_IDS 2048u
#define VXR_WTS 2064u
#define HIDDEN_POISON 0xffffffffu

void omega_gpu_decode_graph_verify_set_hook(OmegaGpuDecodeGraph *g, OmegaGpuDecodeGraphVerifyHook hook, void *ctx) { if (g) { g->v_hook = hook; g->v_hook_ctx = ctx; } }
void omega_gpu_decode_graph_verify_test_set_mutant(OmegaGpuDecodeGraph *g, int mutant) { if (g) g->v_mutant = mutant; }

static uint64_t vva(const OmegaGpuDecodeGraph *g, int r) { return g->vbuf.va + g->v_off[r]; }
static void *vcpu(const OmegaGpuDecodeGraph *g, int r) { return (uint8_t *)g->vbuf.cpu + g->v_off[r]; }

/* lock held */
static int vkern_build(Kern *k, uint32_t lane_bytes, int mutant) {
    OmegaVrowsSpec s = { lane_bytes, mutant };
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) return OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    int rc = OMEGA_GPU_DGRAPH_OK;
    size_t need = 0;
    if (omega_blackwell_codegen_vrows_prog(&s, prog) != 0 || (need = omega_bw_program_code_bytes(prog)) == 0) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
    else if (omega_gpu_session_alloc_code(need, &k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    else {
        OmegaBlackwellKernel kk;
        if (omega_blackwell_kernel_encode_into(prog, k->code.cpu, need, &kk) != 0 || kk.code_size != need) rc = OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
        else if (omega_gpu_session_sync_to_gpu(&k->code) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
        else { k->gpr = omega_bw_gpr_declared(kk.gpr_count); if (k->gpr < 64u) k->gpr = 64u; }
        if (rc != OMEGA_GPU_DGRAPH_OK) { omega_gpu_session_free(&k->code); memset(k, 0, sizeof *k); }
    }
    omega_gpu_host_free(prog);
    return rc;
}

/* lock held: the verify buffer, the vrows kernels and the constant tables, once */
static int verify_ready(OmegaGpuDecodeGraph *g) {
    if (g->v_ready) return OMEGA_GPU_DGRAPH_OK;
    const size_t H = OMEGA_GPU_DGRAPH_HIDDEN, I = OMEGA_GPU_DGRAPH_INTER;
    static const size_t bytes[V_COUNT] = {
        [VX] = VR * H * 4, [VXQ] = VR * H * 4, [VXP] = (VR + VPAD) * OMEGA_GPU_DGRAPH_ATTN * 4, [VQKV] = VR * OMEGA_GPU_DGRAPH_QKV * 4,
        [VATTN] = VR * OMEGA_GPU_DGRAPH_ATTN * 4, [VO] = VR * H * 4, [VXR] = VR * VXR_F * 4, [VXS] = (VG + VPAD) * H * 4,
        [VGU] = VG * 2 * I * 4, [VACT] = VG * I * 4, [VACTP] = (VG + VPAD) * I * 4, [VDN] = VG * H * 4, [VDNT] = VG * H * 4,
        [VHID] = VR * H * 4, [VIDENT] = VG * 4, [VZERO] = 16, [VIGU] = VG * 4, [VIINV] = VG * 4,
        [VSQKV] = 32, [VSO] = 32, [VSGU] = VG * 32, [VSDN] = VG * 32,
    };
    size_t o = 0;
    for (int r = 0; r < V_COUNT; r++) { g->v_off[r] = o; o = al(o + bytes[r]); }
    if (omega_gpu_session_alloc(o, &g->vbuf) != 0) return fail(g, "decode graph verify: buffer alloc", OMEGA_GPU_DGRAPH_CHIP_FAIL);
    memset(g->vbuf.cpu, 0, o);
    uint32_t *ident = vcpu(g, VIDENT);
    for (uint32_t i = 0; i < VG; i++) ident[i] = i;
    static const uint32_t lb[4] = { 0, 8, 16, 16 };
    int rc = OMEGA_GPU_DGRAPH_OK;
    for (unsigned i = 0; i < 4 && rc == OMEGA_GPU_DGRAPH_OK; i++) rc = vkern_build(&g->vk[i], lb[i], i == VK_MUT ? 1 : 0);
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_gpu(&g->vbuf) != 0) rc = OMEGA_GPU_DGRAPH_CHIP_FAIL;
    if (rc != OMEGA_GPU_DGRAPH_OK) {
        for (unsigned i = 0; i < 4; i++) if (g->vk[i].code.cpu) omega_gpu_session_free(&g->vk[i].code);
        memset(g->vk, 0, sizeof g->vk);
        omega_gpu_session_free(&g->vbuf); memset(&g->vbuf, 0, sizeof g->vbuf);
        return fail(g, "decode graph verify: vrows kernels", rc);
    }
    g->v_ready = 1;
    return OMEGA_GPU_DGRAPH_OK;
}

/* one vrows launch: rows x k floats, dst dense, src rows sstride bytes apart, gathered by idx (u32 per dst row) */
static int add_vrows(OmegaGpuDecodeGraph *g, uint8_t kind, int ki, uint64_t dst, uint64_t src, uint64_t idx, uint32_t rows, uint32_t k, uint32_t sstride) {
    const Kern *kk = &g->vk[g->v_mutant == 1 && ki != VK_PLAIN ? VK_MUT : ki];
    uint32_t a[OMEGA_VROWS_WORDS]; memset(a, 0, sizeof a);
    put64(a, OMEGA_VROWS_DST, dst); put64(a, OMEGA_VROWS_SRC, src); put64(a, OMEGA_VROWS_IDX, idx);
    a[(OMEGA_VROWS_UNITS - 0x380) / 4] = k / 4u; a[(OMEGA_VROWS_SSTRIDE - 0x380) / 4] = sstride;
    return add_glue(g, kind, kk, (k / 4u + OMEGA_VROWS_THREADS - 1) / OMEGA_VROWS_THREADS, rows, (size_t)rows * k, a, OMEGA_VROWS_WORDS);
}

static int add_gemm(OmegaGpuDecodeGraph *g, uint8_t kind, const OmegaGpuFp8Tensor *t, uint32_t M, const uint32_t *ids, int seg, uint64_t xp, uint64_t y) {
    int rc = omega_gpu_fp8gemm_record(g->cl, t, M, ids, vcpu(g, seg), vva(g, seg), xp, y, NULL);
    if (rc != 0) {
        char e[160]; omega_gpu_fmt(e, sizeof e, "decode graph verify: gemm record %d (%s)", rc, omega_gpu_fp8gemm_last_error());
        return fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    }
    return after_op(g, kind, 0);
}

int omega_gpu_decode_graph_verify(OmegaGpuDecodeGraph *g, uint32_t rows, const float *x_embed, uint32_t pos0, float *hidden, OmegaGpuDecodeGraphStats *st) {
    if (st) memset(st, 0, sizeof *st);
    if (!g || g->in_token || !x_embed || !hidden || rows == 0 || rows > VR) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    for (uint32_t l = 0; l < g->cfg.layers; l++) if (!g->layer[l].set) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    if (!g->final_set) return OMEGA_GPU_DGRAPH_BAD_ARGS;
    /* the GEMM kernels of the three widths, pinned (prepare takes the lock itself; a cached shape is a no-op) */
    if (omega_gpu_fp8gemm_prepare(OMEGA_GPU_DGRAPH_HIDDEN) != 0 || omega_gpu_fp8gemm_prepare(OMEGA_GPU_DGRAPH_ATTN) != 0 ||
        omega_gpu_fp8gemm_prepare(OMEGA_GPU_DGRAPH_INTER) != 0) return OMEGA_GPU_DGRAPH_CHIP_FAIL;
    omega_gpu_session_lock();
    const uint64_t t0 = omega_gpu_session_now_ns();
    uint64_t t_sub = 0;
    const int stamps = g->stamps;
    g->stamps = 0; /* the timeline buffers hold one token */
    g->in_token = 1; g->failed = 0; g->xq_layer = -1; g->hidden_ready = 0; g->qkv_layer = -1;
    memset(&g->acc, 0, sizeof g->acc);
    g->n_kind = 0; g->tagged_in_list = 0; g->n_stamp = 0;
    omega_gpu_cmdlist_reset(g->cl);
    int rc = omega_gpu_session_open() ? verify_ready(g) : fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    const uint32_t L = g->cfg.layers, H = OMEGA_GPU_DGRAPH_HIDDEN, I = OMEGA_GPU_DGRAPH_INTER, TK = OMEGA_GPU_DGRAPH_TOPK, G = rows * TK;
    const float eps = g->cfg.eps;
    uint32_t zeros[VR], sorted[VG], start[129];
    memset(zeros, 0, sizeof zeros);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        memcpy(vcpu(g, VX), x_embed, (size_t)rows * H * 4u);
        uint32_t *hp = vcpu(g, VHID);
        for (size_t i = 0; i < (size_t)rows * H; i++) hp[i] = HIDDEN_POISON;
        if (omega_gpu_session_sync_to_gpu(&g->vbuf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    }
    const uint64_t vx = vva(g, VX), vxq = vva(g, VXQ), vxp = vva(g, VXP), vqkv = vva(g, VQKV), vattn = vva(g, VATTN), vo = vva(g, VO);
    const uint64_t vxr = vva(g, VXR), vxs = vva(g, VXS), vgu = vva(g, VGU), vact = vva(g, VACT), vactp = vva(g, VACTP), vdn = vva(g, VDN);
    const uint64_t vdnt = vva(g, VDNT), vhid = vva(g, VHID), vident = vva(g, VIDENT), vzero = vva(g, VZERO);
    const uint64_t norms = va_of(g, g->lo.norms), fnorm = va_of(g, g->lo.fnorm);
#define ROW(base, r, floats) ((base) + (uint64_t)(r) * (floats) * 4u)
    for (uint32_t l = 0; l < L && rc == OMEGA_GPU_DGRAPH_OK; l++) {
        const uint64_t norm_l = norms + (uint64_t)l * H * 4u;
        /* ---- A: input norm (layer 0; later layers got it from the combine), qkv, attention, o, residual add, routers ---- */
        if (l == 0)
            for (uint32_t r = 0; r < rows && rc == OMEGA_GPU_DGRAPH_OK; r++)
                rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_NORM, ROW(vx, r, H), 0, 0, norm_l, ROW(vxq, r, H), eps, SCALE_2P64);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_QKV, VK_L16, vxp, vxq, vident, rows, H, H * 4u);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_gemm(g, OMEGA_GPU_DGRAPH_K_QKV, g->layer[l].qkv, rows, zeros, VSQKV, vxp, vqkv);
        for (uint32_t r = 0; r < rows && rc == OMEGA_GPU_DGRAPH_OK; r++)
            rc = after_op(g, OMEGA_GPU_DGRAPH_K_ATTN, omega_gpu_decode_attn_record(g->cl, g->cfg.attn, l, pos0 + r, ROW(vqkv, r, OMEGA_GPU_DGRAPH_QKV), eps, SCALE_2P64, ROW(vattn, r, OMEGA_GPU_DGRAPH_ATTN)));
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_O, VK_L16, vxp, vattn, vident, rows, OMEGA_GPU_DGRAPH_ATTN, OMEGA_GPU_DGRAPH_ATTN * 4u);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_gemm(g, OMEGA_GPU_DGRAPH_K_O, g->layer[l].o, rows, zeros, VSO, vxp, vo);
        for (uint32_t r = 0; r < rows && rc == OMEGA_GPU_DGRAPH_OK; r++)
            rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_ADD, ROW(vx, r, H), ROW(vo, r, H), 0, 0, 0, eps, 1.0f);
        for (uint32_t r = 0; r < rows && rc == OMEGA_GPU_DGRAPH_OK; r++) {
            OmegaGpuRouterDevOut ro; memset(&ro, 0, sizeof ro);
            rc = after_op(g, OMEGA_GPU_DGRAPH_K_ROUTER, omega_gpu_router_record(g->cl, g->cfg.router, l, ROW(vx, r, H), eps, g->cfg.norm_topk, SCALE_2P64, &ro));
            if (rc == OMEGA_GPU_DGRAPH_OK && (ro.ids_va != ro.xout_va + VXR_IDS * 4u || ro.wts_va != ro.xout_va + VXR_WTS * 4u))
                rc = fail(g, "decode graph verify: router outputs are not xout | ids | wts at the expected offsets", OMEGA_GPU_DGRAPH_CHIP_FAIL);
            if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_ROUTER, VK_PLAIN, ROW(vxr, r, VXR_F), ro.xout_va, vzero, 1, VXR_F, 0);
        }
        if (rc == OMEGA_GPU_DGRAPH_OK) { const uint64_t ts = omega_gpu_session_now_ns(); rc = submit_now(g); t_sub += omega_gpu_session_now_ns() - ts; }
        /* ---- host: the rows' expert ids -> GEMM order (counting sort by expert, stable in (row, slot) order: step_verify_exact's order) ---- */
        if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_cpu(&g->vbuf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
        if (rc == OMEGA_GPU_DGRAPH_OK) {
            float *xr = vcpu(g, VXR);
            uint32_t *igu = vcpu(g, VIGU), *iinv = vcpu(g, VIINV);
            memset(start, 0, sizeof start);
            for (uint32_t gi = 0; gi < G && rc == OMEGA_GPU_DGRAPH_OK; gi++) {
                uint32_t ex; memcpy(&ex, &xr[(size_t)(gi / TK) * VXR_F + VXR_IDS + gi % TK], 4);
                if (ex >= 128u) { char e[96]; omega_gpu_fmt(e, sizeof e, "decode graph verify: layer %u row %u expert id %u", l, gi / TK, ex); rc = fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL); }
                else start[ex + 1]++;
            }
            for (uint32_t ex = 0; ex < 128u && rc == OMEGA_GPU_DGRAPH_OK; ex++) start[ex + 1] += start[ex];
            for (uint32_t gi = 0; gi < G && rc == OMEGA_GPU_DGRAPH_OK; gi++) {
                uint32_t ex; memcpy(&ex, &xr[(size_t)(gi / TK) * VXR_F + VXR_IDS + gi % TK], 4);
                const uint32_t slot = start[ex]++;
                sorted[slot] = ex; igu[slot] = gi / TK; iinv[gi] = slot;
            }
            if (rc == OMEGA_GPU_DGRAPH_OK && g->v_mutant == 2) { const uint32_t t = iinv[0]; iinv[0] = iinv[1]; iinv[1] = t; }
            if (rc == OMEGA_GPU_DGRAPH_OK && g->v_hook) g->v_hook(g->v_hook_ctx, l, rows, xr);
        }
        /* ---- B: experts (gather + layout, gate|up GEMM, SWIGLU, layout, down GEMM, back to row order), combine + next norm ---- */
        const int last = l + 1 == L;
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_GATE_UP, VK_L16, vxs, vxr, vva(g, VIGU), G, H, VXR_F * 4u);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_gemm(g, OMEGA_GPU_DGRAPH_K_GATE_UP, g->layer[l].gu, G, sorted, VSGU, vxs, vgu);
        if (rc == OMEGA_GPU_DGRAPH_OK) {
            uint32_t a[OMEGA_DEC_SW_WORDS]; memset(a, 0, sizeof a);
            put64(a, OMEGA_DEC_SW_GU, vgu); put64(a, OMEGA_DEC_SW_ACT, vact); putf(a, OMEGA_DEC_SW_SCALE, SCALE_2P64);
            rc = add_glue(g, OMEGA_GPU_DGRAPH_K_SWIGLU, &g->k[g->mutant == 1 ? K_SW_M1 : K_SW], I / OMEGA_DEC_THREADS, G, (size_t)G * I, a, OMEGA_DEC_SW_WORDS);
        }
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_DOWN, VK_L8, vactp, vact, vident, G, I, I * 4u);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_gemm(g, OMEGA_GPU_DGRAPH_K_DOWN, g->layer[l].dn, G, sorted, VSDN, vactp, vdn);
        if (rc == OMEGA_GPU_DGRAPH_OK) rc = add_vrows(g, OMEGA_GPU_DGRAPH_K_COMBINE, VK_PLAIN, vdnt, vdn, vva(g, VIINV), G, H, H * 4u);
        for (uint32_t r = 0; r < rows && rc == OMEGA_GPU_DGRAPH_OK; r++)
            rc = omega_gpu_decode_graph_record_res(g, OMEGA_DEC_RES_COMBINE | OMEGA_DEC_RES_NORM, ROW(vx, r, H), ROW(vdnt, r * TK, H), ROW(vxr, r, VXR_F) + VXR_WTS * 4u,
                                                   last ? fnorm : norm_l + H * 4u, last ? ROW(vhid, r, H) : ROW(vxq, r, H), eps, last ? 1.0f : SCALE_2P64);
        /* the tables this list reads (segments, gather) and any hook change, before the next submission */
        if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_gpu(&g->vbuf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
        if (rc == OMEGA_GPU_DGRAPH_OK && last) { const uint64_t ts = omega_gpu_session_now_ns(); rc = submit_now(g); t_sub += omega_gpu_session_now_ns() - ts; }
    }
#undef ROW
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_decode_attn_counters_check(g->cfg.attn) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK && omega_gpu_session_sync_to_cpu(&g->vbuf) != 0) rc = fail(g, NULL, OMEGA_GPU_DGRAPH_CHIP_FAIL);
    if (rc == OMEGA_GPU_DGRAPH_OK) {
        const uint32_t *hp = vcpu(g, VHID);
        size_t left = 0;
        for (size_t i = 0; i < (size_t)rows * H; i++) left += hp[i] == HIDDEN_POISON;
        if (left) { char e[96]; omega_gpu_fmt(e, sizeof e, "decode graph verify: %zu hidden words not written", left); rc = fail(g, e, OMEGA_GPU_DGRAPH_CHIP_FAIL); }
        else memcpy(hidden, hp, (size_t)rows * H * 4u);
    }
    omega_gpu_cmdlist_reset(g->cl);
    g->tagged_in_list = 0; g->n_kind = 0; g->n_stamp = 0;
    g->stamps = stamps;
    g->in_token = 0;
    const uint64_t t1 = omega_gpu_session_now_ns();
    if (st) { *st = g->acc; st->total_ns = t1 - t0; st->record_ns = st->total_ns - t_sub; }
    omega_gpu_session_unlock();
    return rc;
}
