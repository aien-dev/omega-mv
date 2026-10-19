/*
 * omega_gpu_fnattn_api.c: resident KV cache, RoPE table and launch for the head_dim 256 decode attention
 * (Flash-Next: 2 KV heads, sparse list). A derivative of omega_gpu_decode_attn256_api.c (see the header); the double-precision host reference
 * lives in tests/gpu_decode_attn256_test.c so the library links without libm. Session use mirrors the 128 kernel:
 * every buffer from omega_gpu_session_alloc (GPU-uncached, host-visible), sync_to_gpu after host writes, sync_to_cpu
 * before host reads, one omega_gpu_session_launch per call.
 */
#include "omega_gpu_fnattn_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_gpu_hostmath.h"
#include <string.h>
#include "omega_gpu_cmdlist.h"

#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)
#define HD OMEGA_FNA_HEAD_DIM
#define RB OMEGA_FNA_ROW_BYTES
#define KVH OMEGA_FNA_KV_HEADS
#define OUTW OMEGA_FNA_OUT_WORDS

struct OmegaGpuFnAttnCache {
    uint32_t layers, max_pos, max_splits, rotary_dim;
    double theta;
    uint64_t plane_bytes;                     /* one K or V plane of a layer: 4 * max_pos * 512 + slack */
    OmegaGpuMem kv[OMEGA_GPU_FNA_MAX_LAYERS]; /* [K plane | V plane] per layer */
    OmegaGpuMem rope, norms, part, cnt, qkv, out, plist;
};

typedef struct {
    bool valid;
    OmegaFnAttnSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
} Slot;

static struct {
    bool hooked;
    Slot slot;
    uint32_t depth, per_split;
    int mutant;
} g;
static int g_rec; /* 1 while a record call runs (lock held): kernel_for must not evict */

static void slot_clear_locked(void) { if (g.slot.valid) omega_gpu_session_free(&g.slot.code); memset(&g.slot, 0, sizeof g.slot); }
static void on_session_close(void) { memset(&g.slot, 0, sizeof g.slot); }
static OmegaGpuSession *dev_open_locked(void) {
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_session_close); g.hooked = true; }
    return omega_gpu_session_open();
}
static uint32_t gpr_declared(uint32_t peak) { return omega_bw_gpr_declared(peak); }

void omega_gpu_fnattn_set_depth(uint32_t depth) { omega_gpu_session_lock(); g.depth = depth; slot_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_fnattn_set_per_split(uint32_t per_split) { omega_gpu_session_lock(); g.per_split = per_split; omega_gpu_session_unlock(); }
void omega_gpu_fnattn_test_set_mutant(int mutant) { omega_gpu_session_lock(); g.mutant = mutant; slot_clear_locked(); omega_gpu_session_unlock(); }

/* ---- cache ---- */
int omega_gpu_fnattn_cache_alloc(uint32_t layers, uint32_t max_pos, double rope_theta, uint32_t rotary_dim, OmegaGpuFnAttnCache **out) {
    if (!out) return OMEGA_GPU_DATTN_BAD_ARGS;
    *out = NULL;
    if (layers == 0 || layers > OMEGA_GPU_FNA_MAX_LAYERS || max_pos == 0 || max_pos > OMEGA_GPU_FNA_MAX_POS || max_pos % OMEGA_FNA_CHUNK ||
        !(rope_theta > 0) || rotary_dim < 2 || rotary_dim > HD || rotary_dim % 2) return OMEGA_GPU_DATTN_BAD_ARGS;
    OmegaGpuFnAttnCache *c = omega_gpu_host_calloc(1, sizeof *c);
    if (!c) return OMEGA_GPU_DATTN_CHIP_FAIL;
    c->layers = layers; c->max_pos = max_pos; c->theta = rope_theta; c->rotary_dim = rotary_dim;
    c->max_splits = max_pos / OMEGA_FNA_CHUNK; /* ceil(count / per) <= max_pos / 256 */
    c->plane_bytes = (uint64_t)KVH * max_pos * RB + OMEGA_FNA_PLANE_SLACK;
    int rc = OMEGA_GPU_DATTN_OK;
    omega_gpu_session_lock();
    if (!dev_open_locked()) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    for (uint32_t L = 0; L < layers && rc == OMEGA_GPU_DATTN_OK; L++) if (omega_gpu_session_alloc((size_t)(2 * c->plane_bytes), &c->kv[L]) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK && (omega_gpu_session_alloc((size_t)max_pos * OMEGA_FNA_ROPE_ROW_BYTES, &c->rope) != 0 ||
                                     omega_gpu_session_alloc((size_t)layers * 2 * HD * 4, &c->norms) != 0 ||
                                     omega_gpu_session_alloc((size_t)OMEGA_FNA_GRID_Y * c->max_splits * OMEGA_FNA_PARTIAL_WORDS * 4, &c->part) != 0 ||
                                     omega_gpu_session_alloc(64, &c->cnt) != 0 ||
                                     omega_gpu_session_alloc((size_t)OMEGA_FNA_QKV_WORDS * 4, &c->qkv) != 0 ||
                                     omega_gpu_session_alloc((size_t)OUTW * 4, &c->out) != 0 ||
                                     omega_gpu_session_alloc((size_t)max_pos * 4 + 64, &c->plist) != 0)) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK) {
        float *r = c->rope.cpu;
        for (uint32_t p = 0; p < max_pos; p++) omega_dattn256_rope_row(p, rope_theta, rotary_dim, r + (size_t)p * 256);
        memset(c->cnt.cpu, 0, 64);
        memset(c->norms.cpu, 0, (size_t)layers * 2 * HD * 4);
        for (uint32_t L = 0; L < layers; L++) memset(c->kv[L].cpu, 0, (size_t)(2 * c->plane_bytes));
        if (omega_gpu_session_sync_to_gpu(&c->rope) != 0 || omega_gpu_session_sync_to_gpu(&c->cnt) != 0 || omega_gpu_session_sync_to_gpu(&c->norms) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
        for (uint32_t L = 0; L < layers && rc == OMEGA_GPU_DATTN_OK; L++) if (omega_gpu_session_sync_to_gpu(&c->kv[L]) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }
    omega_gpu_session_unlock();
    if (rc != OMEGA_GPU_DATTN_OK) { omega_gpu_fnattn_cache_free(c); return rc; }
    *out = c;
    return OMEGA_GPU_DATTN_OK;
}
void omega_gpu_fnattn_cache_free(OmegaGpuFnAttnCache *c) {
    if (!c) return;
    omega_gpu_session_lock();
    for (uint32_t L = 0; L < c->layers; L++) if (c->kv[L].cpu) omega_gpu_session_free(&c->kv[L]);
    OmegaGpuMem *ms[] = { &c->rope, &c->norms, &c->part, &c->cnt, &c->qkv, &c->out, &c->plist };
    for (unsigned i = 0; i < 7; i++) if (ms[i]->cpu) omega_gpu_session_free(ms[i]);
    omega_gpu_session_unlock();
    omega_gpu_host_free(c);
}
int omega_gpu_fnattn_set_norms(OmegaGpuFnAttnCache *c, uint32_t layer, const float *q_norm, const float *k_norm) {
    if (!c || layer >= c->layers || !q_norm || !k_norm) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    float *n = (float *)c->norms.cpu + (size_t)layer * 2 * HD;
    memcpy(n, q_norm, HD * 4); memcpy(n + HD, k_norm, HD * 4);
    int rc = omega_gpu_session_sync_to_gpu(&c->norms) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    omega_gpu_session_unlock();
    return rc;
}
static uint16_t *row_ptr(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, int plane) {
    return (uint16_t *)((uint8_t *)c->kv[layer].cpu + (plane ? c->plane_bytes : 0) + ((size_t)kv_head * c->max_pos + pos) * RB);
}
int omega_gpu_fnattn_cache_write(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint32_t count, const uint16_t *k, const uint16_t *v) {
    if (!c || layer >= c->layers || kv_head >= KVH || pos >= c->max_pos || count == 0 || count > c->max_pos - pos || !k || !v) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy(row_ptr(c, layer, kv_head, pos, 0), k, (size_t)count * RB); memcpy(row_ptr(c, layer, kv_head, pos, 1), v, (size_t)count * RB);
    int rc = omega_gpu_session_sync_to_gpu(&c->kv[layer]) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    omega_gpu_session_unlock();
    return rc;
}
int omega_gpu_fnattn_cache_read(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint16_t *k, uint16_t *v) {
    if (!c || layer >= c->layers || kv_head >= KVH || pos >= c->max_pos || !k || !v) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = omega_gpu_session_sync_to_cpu(&c->kv[layer]) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK) { memcpy(k, row_ptr(c, layer, kv_head, pos, 0), RB); memcpy(v, row_ptr(c, layer, kv_head, pos, 1), RB); }
    omega_gpu_session_unlock();
    return rc;
}

/* ---- kernel ---- */
static int kernel_for(const OmegaFnAttnSpec *spec, Slot **out, bool *hit) {
    Slot *s = &g.slot;
    if (s->valid && memcmp(&s->spec, spec, sizeof *spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_DATTN_OK; }
    if (g_rec && s->valid) { omega_gpu_session_set_error("fnattn record: kernel cache miss would evict the resident kernel (warm up before recording)"); return OMEGA_GPU_DATTN_TOO_LARGE; }
    slot_clear_locked();
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("fnattn codegen: host memory"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_fnattn_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnattn codegen refused the spec"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    if ((uint64_t)OMEGA_FNA_CHUNK * gpr_declared(prog->regalloc.peak_gpr_usage) > 65536u) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnattn: register file"); return OMEGA_GPU_DATTN_TOO_LARGE; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); omega_gpu_session_set_error("fnattn encode"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code;
    *out = s; *hit = false;
    return OMEGA_GPU_DATTN_OK;
}

/* rec non-NULL (record path, caller holds the session lock): qkv / out are the device buffers dev_qkv / dev_out, the
 * launch is appended to rec instead of submitted, nothing is staged, poisoned or read back. */
static int dattn_core(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t pos, const float *qkv, const uint32_t *plist, uint32_t count, float eps, float out_scale,
                      float *out, OmegaGpuFnAttnInfo *info, OmegaGpuCmdList *rec, uint64_t dev_qkv, uint64_t dev_out, uint64_t dev_plist) {
    uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (!c || layer >= c->layers || pos >= c->max_pos || count == 0 || count > pos + 1u || (rec ? (!dev_qkv || !dev_out || !dev_plist) : (!qkv || !out || !plist))) return OMEGA_GPU_DATTN_BAD_ARGS;
    if (!rec) { for (uint32_t i = 0; i < count; i++) if (plist[i] > pos || (i && plist[i] <= plist[i - 1])) { omega_gpu_session_set_error("fnattn: the position list must be strictly ascending and at most pos"); return OMEGA_GPU_DATTN_BAD_ARGS; } }
    const uint32_t per = g.per_split ? g.per_split : OMEGA_GPU_FNA_PER_SPLIT;
    if (per % OMEGA_FNA_CHUNK) return OMEGA_GPU_DATTN_BAD_ARGS;
    const uint32_t splits = (count + per - 1u) / per;
    if (splits > c->max_splits) return OMEGA_GPU_DATTN_TOO_LARGE;
    OmegaFnAttnSpec spec; memset(&spec, 0, sizeof spec);
    spec.depth = g.depth ? g.depth : OMEGA_GPU_FNA_DEPTH; spec.mutant = g.mutant;

    if (!rec) omega_gpu_session_lock();
#define UNLOCK() do { if (!rec) omega_gpu_session_unlock(); } while (0)
    int rc = OMEGA_GPU_DATTN_OK;
    if (!dev_open_locked()) { UNLOCK(); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    Slot *ks = NULL; bool hit = false;
    g_rec = rec != NULL;
    rc = kernel_for(&spec, &ks, &hit);
    g_rec = 0;
    if (rc != OMEGA_GPU_DATTN_OK) { UNLOCK(); return rc; }
    float *od = c->out.cpu;
    if (!rec) {
        memcpy(c->qkv.cpu, qkv, (size_t)OMEGA_FNA_QKV_WORDS * 4);
        memcpy(c->plist.cpu, plist, (size_t)count * 4);
        for (size_t i = 0; i < OUTW; i++) od[i] = POISON_F32;
        memset(c->cnt.cpu, 0, 64);
        if (omega_gpu_session_sync_to_gpu(&c->qkv) != 0 || omega_gpu_session_sync_to_gpu(&c->out) != 0 || omega_gpu_session_sync_to_gpu(&c->cnt) != 0 || omega_gpu_session_sync_to_gpu(&c->plist) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }

    uint32_t args[OMEGA_FNA_ARGS_WORDS]; memset(args, 0, sizeof args);
    const uint64_t kc = c->kv[layer].va, vc = kc + c->plane_bytes, nm = c->norms.va + (uint64_t)layer * 2 * HD * 4;
    const uint64_t qkv_va = rec ? dev_qkv : c->qkv.va, out_va = rec ? dev_out : c->out.va, plist_va = rec ? dev_plist : c->plist.va;
    float fe = eps, fo = out_scale, fq = (float)(0.0625 * 1.4426950408889634); /* log2(e) / sqrt(256) */
    uint32_t ue, uo, uq; memcpy(&ue, &fe, 4); memcpy(&uo, &fo, 4); memcpy(&uq, &fq, 4);
#define A64(i, v) do { args[i] = (uint32_t)(v); args[(i) + 1] = (uint32_t)((v) >> 32); } while (0)
    A64(0, qkv_va); A64(2, kc); A64(4, vc); A64(6, c->rope.va); A64(8, nm); A64(10, nm + HD * 4); A64(12, out_va); A64(14, c->part.va); A64(16, c->cnt.va);
#undef A64
    args[18] = pos; args[19] = per; args[20] = splits; args[21] = (uint32_t)(c->max_pos * RB); args[22] = ue; args[23] = uo; args[24] = uq;
    args[26] = (uint32_t)plist_va; args[27] = (uint32_t)(plist_va >> 32); args[28] = count; /* ARG_PLIST 0x3e8, ARG_COUNT 0x3f0 */
    if (info) {
        info->layer = layer; info->pos = pos; info->count = count; info->splits = splits; info->per_split = per; info->depth = spec.depth;
        info->grid_x = splits; info->grid_y = OMEGA_FNA_GRID_Y; info->threads = OMEGA_FNA_CHUNK;
        info->kv_bytes = 2ull * KVH * (uint64_t)count * RB;
        info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_DATTN_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = gpr_declared(ks->kernel.gpr_count),
            .threads_x = OMEGA_FNA_CHUNK, .threads_y = 1, .grid_x = splits, .grid_y = OMEGA_FNA_GRID_Y,
            .num_elements = (size_t)OUTW,
            .args = args, .n_args = OMEGA_FNA_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = OMEGA_FNA_SHARED_BYTES,
        };
        if (!omega_bw_gpr_declared_ok(L.gpr_count, ks->kernel.gpr_count)) { omega_gpu_session_set_error("declared registers below peak + 2: refusing to launch"); rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else if (rec) { if (omega_gpu_cmdlist_add(rec, &L) < 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, info ? &info->completion_marker : NULL) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }
    if (rc == OMEGA_GPU_DATTN_OK && !rec && (omega_gpu_session_sync_to_cpu(&c->out) != 0 || omega_gpu_session_sync_to_cpu(&c->cnt) != 0)) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK && !rec) {
        size_t unchanged = 0;
        for (size_t i = 0; i < OUTW; i++) if (od[i] == POISON_F32) unchanged++;
        const uint32_t *cn = c->cnt.cpu;
        if (unchanged) { omega_gpu_session_set_error("fnattn: output poison survived"); rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else if (cn[0] | cn[1] | cn[2] | cn[3]) { omega_gpu_session_set_error("fnattn: a split counter did not return to zero"); rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else memcpy(out, od, (size_t)OUTW * 4);
    }
    UNLOCK();
#undef UNLOCK
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}

int omega_gpu_fnattn(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t pos, const float *qkv, const uint32_t *plist, uint32_t count, float eps,
                     float out_scale, float *out, OmegaGpuFnAttnInfo *info) {
    return dattn_core(c, layer, pos, qkv, plist, count, eps, out_scale, out, info, NULL, 0, 0, 0);
}

int omega_gpu_fnattn_record(OmegaGpuCmdList *cl, OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t pos, uint64_t qkv_va, uint64_t plist_va,
                            uint32_t count, float eps, float out_scale, uint64_t out_va) {
    if (!cl) return OMEGA_GPU_DATTN_BAD_ARGS;
    return dattn_core(c, layer, pos, NULL, NULL, count, eps, out_scale, NULL, NULL, cl, qkv_va, out_va, plist_va);
}

int omega_gpu_fnattn_counters_check(OmegaGpuFnAttnCache *c) {
    if (!c || !c->cnt.cpu) return OMEGA_GPU_DATTN_BAD_ARGS;
    if (omega_gpu_session_sync_to_cpu(&c->cnt) != 0) return OMEGA_GPU_DATTN_CHIP_FAIL;
    const uint32_t *cn = c->cnt.cpu;
    if (cn[0] | cn[1] | cn[2] | cn[3]) { omega_gpu_session_set_error("fnattn: a split counter did not return to zero"); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    return OMEGA_GPU_DATTN_OK;
}
