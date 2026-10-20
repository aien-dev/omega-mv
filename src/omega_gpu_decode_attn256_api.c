/*
 * omega_gpu_decode_attn256_api.c: resident KV cache, RoPE table and launch for the head_dim 256 decode attention
 * (omega-mv#544). A derivative of omega_gpu_decode_attn_api.c (see the header); the double-precision host reference
 * lives in tests/gpu_decode_attn256_test.c so the library links without libm. Session use mirrors the 128 kernel:
 * every buffer from omega_gpu_session_alloc (GPU-uncached, host-visible), sync_to_gpu after host writes, sync_to_cpu
 * before host reads, one omega_gpu_session_launch per call.
 */
#include "omega_gpu_decode_attn256_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_gpu_hostmath.h"
#include <string.h>
#include "omega_gpu_cmdlist.h"
#include "omega_blackwell_codegen_qmma.h"

#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)
#define HD OMEGA_DATTN256_HEAD_DIM
#define RB OMEGA_DATTN256_ROW_BYTES
#define KVH OMEGA_DATTN256_KV_HEADS
#define OUTW OMEGA_DATTN256_OUT_WORDS

struct OmegaGpuDecodeAttn256Cache {
    uint32_t layers, max_pos, max_splits, rotary_dim, kv8; /* kv8: OMEGA_DATTN256_KV8_*, which planes hold e4m3 rows */
    uint32_t rb[2];                           /* bytes per row of the K (0) and V (1) plane: 512 bf16, 256 e4m3 */
    double theta;
    uint64_t plane_bytes[2];                  /* the K and V plane of a layer: 4 * max_pos * rb + slack */
    uint64_t scale_bytes;                     /* kv8 != 0: one K or V scale plane, 4 * max_pos * 4 (0 for bf16) */
    OmegaGpuMem kv[OMEGA_GPU_DATTN256_MAX_LAYERS]; /* [K plane | V plane | K scales | V scales (kv8 != 0)] per layer */
    OmegaGpuMem rope, norms, part, cnt, qkv, out;
};
static uint64_t kv_total(const OmegaGpuDecodeAttn256Cache *c) { return c->plane_bytes[0] + c->plane_bytes[1] + 2 * c->scale_bytes; }

typedef struct {
    bool valid;
    OmegaDecodeAttn256Spec spec;
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

void omega_gpu_decode_attn256_set_depth(uint32_t depth) { omega_gpu_session_lock(); g.depth = depth; slot_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_decode_attn256_set_per_split(uint32_t per_split) { omega_gpu_session_lock(); g.per_split = per_split; omega_gpu_session_unlock(); }
void omega_gpu_decode_attn256_test_set_mutant(int mutant) { omega_gpu_session_lock(); g.mutant = mutant; slot_clear_locked(); omega_gpu_session_unlock(); }

/* ---- host math ---- */
void omega_dattn256_rope_row(uint32_t pos, double theta, uint32_t rotary_dim, float *row256) {
    const uint32_t P = rotary_dim / 2u;
    for (unsigned i = 0; i < 128; i++) {
        if (i < P) {
            double inv = omega_hostmath_pow(theta, -(double)(2 * i) / (double)rotary_dim), ang = (double)pos * inv, s, c;
            omega_hostmath_sincos(ang, &s, &c);
            row256[i] = (float)c; row256[128 + i] = (float)s;
        } else { row256[i] = 1.0f; row256[128 + i] = 0.0f; }
    }
}
/* ---- cache ---- */
int omega_gpu_decode_attn256_cache_alloc(uint32_t layers, uint32_t max_pos, double rope_theta, uint32_t rotary_dim, OmegaGpuDecodeAttn256Cache **out) {
    return omega_gpu_decode_attn256_cache_alloc_kv(layers, max_pos, rope_theta, rotary_dim, 0, out);
}
int omega_gpu_decode_attn256_cache_alloc_kv(uint32_t layers, uint32_t max_pos, double rope_theta, uint32_t rotary_dim, uint32_t kv8, OmegaGpuDecodeAttn256Cache **out) {
    if (!out) return OMEGA_GPU_DATTN_BAD_ARGS;
    *out = NULL;
    if (layers == 0 || layers > OMEGA_GPU_DATTN256_MAX_LAYERS || max_pos == 0 || max_pos > OMEGA_GPU_DATTN256_MAX_POS || max_pos % OMEGA_DATTN256_CHUNK ||
        !(rope_theta > 0) || rotary_dim < 2 || rotary_dim > HD || rotary_dim % 2 || kv8 > OMEGA_DATTN256_KV8_K) return OMEGA_GPU_DATTN_BAD_ARGS;
    OmegaGpuDecodeAttn256Cache *c = omega_gpu_host_calloc(1, sizeof *c);
    if (!c) return OMEGA_GPU_DATTN_CHIP_FAIL;
    c->layers = layers; c->max_pos = max_pos; c->theta = rope_theta; c->rotary_dim = rotary_dim;
    c->max_splits = max_pos / OMEGA_DATTN256_CHUNK;
    c->kv8 = kv8;
    c->rb[0] = OMEGA_DATTN256_KV8_K8(kv8) ? OMEGA_DATTN256_ROW_BYTES_KV8 : RB; c->rb[1] = OMEGA_DATTN256_KV8_V8(kv8) ? OMEGA_DATTN256_ROW_BYTES_KV8 : RB;
    for (int p = 0; p < 2; p++) c->plane_bytes[p] = (uint64_t)KVH * max_pos * c->rb[p] + OMEGA_DATTN256_PLANE_SLACK;
    c->scale_bytes = kv8 ? (uint64_t)KVH * max_pos * 4u : 0u;
    int rc = OMEGA_GPU_DATTN_OK;
    omega_gpu_session_lock();
    if (!dev_open_locked()) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    for (uint32_t L = 0; L < layers && rc == OMEGA_GPU_DATTN_OK; L++) if (omega_gpu_session_alloc((size_t)kv_total(c), &c->kv[L]) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK && (omega_gpu_session_alloc((size_t)max_pos * OMEGA_DATTN256_ROPE_ROW_BYTES, &c->rope) != 0 ||
                                     omega_gpu_session_alloc((size_t)layers * 2 * HD * 4, &c->norms) != 0 ||
                                     omega_gpu_session_alloc((size_t)KVH * c->max_splits * OMEGA_DATTN256_PARTIAL_WORDS * 4, &c->part) != 0 ||
                                     omega_gpu_session_alloc(64, &c->cnt) != 0 ||
                                     omega_gpu_session_alloc((size_t)OMEGA_DATTN256_QKV_WORDS * 4, &c->qkv) != 0 ||
                                     omega_gpu_session_alloc((size_t)OUTW * 4, &c->out) != 0)) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK) {
        float *r = c->rope.cpu;
        for (uint32_t p = 0; p < max_pos; p++) omega_dattn256_rope_row(p, rope_theta, rotary_dim, r + (size_t)p * 256);
        memset(c->cnt.cpu, 0, 64);
        memset(c->norms.cpu, 0, (size_t)layers * 2 * HD * 4);
        for (uint32_t L = 0; L < layers; L++) memset(c->kv[L].cpu, 0, (size_t)kv_total(c));
        if (omega_gpu_session_sync_to_gpu(&c->rope) != 0 || omega_gpu_session_sync_to_gpu(&c->cnt) != 0 || omega_gpu_session_sync_to_gpu(&c->norms) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
        for (uint32_t L = 0; L < layers && rc == OMEGA_GPU_DATTN_OK; L++) if (omega_gpu_session_sync_to_gpu(&c->kv[L]) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }
    omega_gpu_session_unlock();
    if (rc != OMEGA_GPU_DATTN_OK) { omega_gpu_decode_attn256_cache_free(c); return rc; }
    *out = c;
    return OMEGA_GPU_DATTN_OK;
}
void omega_gpu_decode_attn256_cache_free(OmegaGpuDecodeAttn256Cache *c) {
    if (!c) return;
    omega_gpu_session_lock();
    for (uint32_t L = 0; L < c->layers; L++) if (c->kv[L].cpu) omega_gpu_session_free(&c->kv[L]);
    OmegaGpuMem *ms[] = { &c->rope, &c->norms, &c->part, &c->cnt, &c->qkv, &c->out };
    for (unsigned i = 0; i < 6; i++) if (ms[i]->cpu) omega_gpu_session_free(ms[i]);
    omega_gpu_session_unlock();
    omega_gpu_host_free(c);
}
int omega_gpu_decode_attn256_set_norms(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, const float *q_norm, const float *k_norm) {
    if (!c || layer >= c->layers || !q_norm || !k_norm) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    float *n = (float *)c->norms.cpu + (size_t)layer * 2 * HD;
    memcpy(n, q_norm, HD * 4); memcpy(n + HD, k_norm, HD * 4);
    int rc = omega_gpu_session_sync_to_gpu(&c->norms) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    omega_gpu_session_unlock();
    return rc;
}
static uint16_t *row_ptr(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, int plane) {
    return (uint16_t *)((uint8_t *)c->kv[layer].cpu + (plane ? c->plane_bytes[0] : 0) + ((size_t)kv_head * c->max_pos + pos) * c->rb[plane]);
}
/* ---- kv8 rows ---- */
uint32_t omega_gpu_decode_attn256_cache_kv8(const OmegaGpuDecodeAttn256Cache *c) { return c ? c->kv8 : 0u; }
uint8_t omega_dattn256_e4m3_rne_sat(float x) {
    uint32_t u; memcpy(&u, &x, 4);
    const uint8_t s = (uint8_t)((u >> 24) & 0x80u);
    u &= 0x7fffffffu;
    if (u >= 0x43e00000u) return s | 0x7eu;                     /* |x| >= 448 (and inf): 448 */
    if (u < 0x3c800000u) {                                      /* |x| < 2^-6: multiples of 2^-9, RNE */
        float a; memcpy(&a, &u, 4);
        const float r = (a * 512.0f + 8388608.0f) - 8388608.0f;  /* round half even on the integer grid (a * 512 < 8) */
        return s | (uint8_t)r;
    }
    const uint32_t r = (u + 0x7ffffu + ((u >> 20) & 1u)) >> 20;  /* RNE to 3 mantissa bits: (exp << 3) | m */
    const uint32_t code = r - (120u << 3);
    return s | (uint8_t)(code > 0x7eu ? 0x7eu : code);
}
float omega_dattn256_kv8_quant_row(const float *x, uint8_t *q) {
    uint32_t amax = 0x0d800000u;                                /* 2^-100 */
    for (unsigned i = 0; i < HD; i++) { uint32_t u; memcpy(&u, &x[i], 4); u &= 0x7fffffffu; if (u > amax) amax = u; }
    uint32_t ec = (amax >> 23) + (((amax & 0x007fffffu) + 0x001fffffu) >> 23);
    if (ec > 142u) ec = 142u;
    const uint32_t sb = (ec - 8u) << 23, ib = (262u - ec) << 23;
    float scale, inv; memcpy(&scale, &sb, 4); memcpy(&inv, &ib, 4);
    for (unsigned i = 0; i < HD; i++) q[i] = omega_dattn256_e4m3_rne_sat(x[i] * inv);
    return scale;
}
static float *scale_ptr(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, int plane) {
    return (float *)((uint8_t *)c->kv[layer].cpu + c->plane_bytes[0] + c->plane_bytes[1] + (plane ? c->scale_bytes : 0)) + (size_t)kv_head * c->max_pos + pos;
}
static int plane8(const OmegaGpuDecodeAttn256Cache *c, int plane) { return plane ? OMEGA_DATTN256_KV8_V8(c->kv8) : OMEGA_DATTN256_KV8_K8(c->kv8); }
int omega_gpu_decode_attn256_cache_write_rows(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint32_t count, int plane,
                                              const void *rows, const float *scales) {
    if (!c || layer >= c->layers || kv_head >= KVH || pos >= c->max_pos || count == 0 || count > c->max_pos - pos || (plane != 0 && plane != 1) || !rows ||
        (plane8(c, plane) && !scales)) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    memcpy(row_ptr(c, layer, kv_head, pos, plane), rows, (size_t)count * c->rb[plane]);
    if (plane8(c, plane)) memcpy(scale_ptr(c, layer, kv_head, pos, plane), scales, (size_t)count * 4);
    int rc = omega_gpu_session_sync_to_gpu(&c->kv[layer]) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    omega_gpu_session_unlock();
    return rc;
}
int omega_gpu_decode_attn256_cache_read_rows(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, int plane, void *row, float *scale) {
    if (!c || layer >= c->layers || kv_head >= KVH || pos >= c->max_pos || (plane != 0 && plane != 1) || !row || (plane8(c, plane) && !scale))
        return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = omega_gpu_session_sync_to_cpu(&c->kv[layer]) == 0 ? OMEGA_GPU_DATTN_OK : OMEGA_GPU_DATTN_CHIP_FAIL;
    if (rc == OMEGA_GPU_DATTN_OK) {
        memcpy(row, row_ptr(c, layer, kv_head, pos, plane), c->rb[plane]);
        if (plane8(c, plane)) *scale = *scale_ptr(c, layer, kv_head, pos, plane);
    }
    omega_gpu_session_unlock();
    return rc;
}
int omega_gpu_decode_attn256_cache_write8(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint32_t count,
                                          const uint8_t *k8, const float *ks, const uint8_t *v8, const float *vs) {
    if (!c || c->kv8 != OMEGA_DATTN256_KV8_BOTH || !k8 || !ks || !v8 || !vs) return OMEGA_GPU_DATTN_BAD_ARGS;
    int rc = omega_gpu_decode_attn256_cache_write_rows(c, layer, kv_head, pos, count, 0, k8, ks);
    return rc == OMEGA_GPU_DATTN_OK ? omega_gpu_decode_attn256_cache_write_rows(c, layer, kv_head, pos, count, 1, v8, vs) : rc;
}
int omega_gpu_decode_attn256_cache_read8(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos,
                                         uint8_t *k8, float *ks, uint8_t *v8, float *vs) {
    if (!c || c->kv8 != OMEGA_DATTN256_KV8_BOTH || !k8 || !ks || !v8 || !vs) return OMEGA_GPU_DATTN_BAD_ARGS;
    int rc = omega_gpu_decode_attn256_cache_read_rows(c, layer, kv_head, pos, 0, k8, ks);
    return rc == OMEGA_GPU_DATTN_OK ? omega_gpu_decode_attn256_cache_read_rows(c, layer, kv_head, pos, 1, v8, vs) : rc;
}
int omega_gpu_decode_attn256_cache_bench_fill(OmegaGpuDecodeAttn256Cache *c, uint32_t count, uint32_t seed) {
    if (!c || count == 0 || count > c->max_pos) return OMEGA_GPU_DATTN_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = OMEGA_GPU_DATTN_OK;
    for (uint32_t L = 0; L < c->layers && rc == OMEGA_GPU_DATTN_OK; L++) {
        uint32_t s = seed ^ (L * 0x9e3779b9u) ^ 0x2545f491u;
        for (int plane = 0; plane < 2; plane++) for (uint32_t g = 0; g < KVH; g++) {
            uint8_t *row = (uint8_t *)row_ptr(c, L, g, 0, plane);
            const size_t n = (size_t)count * c->rb[plane];
            for (size_t i = 0; i < n; i += 4) {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                uint32_t w = s;
                if (plane8(c, plane)) w &= 0xbebebebeu;  /* e4m3 bytes with exponent field <= 7: below 2, never a NaN code */
                else w &= 0xbf7fbf7fu;                   /* two bf16 with exponent field <= 126: below 1, finite */
                memcpy(row + i, &w, 4);
            }
            if (plane8(c, plane)) { float *sp = scale_ptr(c, L, g, 0, plane); for (uint32_t p = 0; p < count; p++) sp[p] = 1.0f / 256.0f; }
        }
        if (omega_gpu_session_sync_to_gpu(&c->kv[L]) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }
    omega_gpu_session_unlock();
    return rc;
}
int omega_gpu_decode_attn256_cache_write(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint32_t count, const uint16_t *k, const uint16_t *v) {
    if (!c || c->kv8 || !k || !v) return OMEGA_GPU_DATTN_BAD_ARGS;
    int rc = omega_gpu_decode_attn256_cache_write_rows(c, layer, kv_head, pos, count, 0, k, NULL);
    return rc == OMEGA_GPU_DATTN_OK ? omega_gpu_decode_attn256_cache_write_rows(c, layer, kv_head, pos, count, 1, v, NULL) : rc;
}
int omega_gpu_decode_attn256_cache_read(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint16_t *k, uint16_t *v) {
    if (!c || c->kv8 || !k || !v) return OMEGA_GPU_DATTN_BAD_ARGS;
    int rc = omega_gpu_decode_attn256_cache_read_rows(c, layer, kv_head, pos, 0, k, NULL);
    return rc == OMEGA_GPU_DATTN_OK ? omega_gpu_decode_attn256_cache_read_rows(c, layer, kv_head, pos, 1, v, NULL) : rc;
}

/* ---- kernel ---- */
static int kernel_for(const OmegaDecodeAttn256Spec *spec, Slot **out, bool *hit) {
    Slot *s = &g.slot;
    if (s->valid && memcmp(&s->spec, spec, sizeof *spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_DATTN_OK; }
    if (g_rec && s->valid) { omega_gpu_session_set_error("decode attn256 record: kernel cache miss would evict the resident kernel (warm up before recording)"); return OMEGA_GPU_DATTN_TOO_LARGE; }
    slot_clear_locked();
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("decode attn256 codegen: host memory"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_decode_attn256_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("decode attn256 codegen refused the spec"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    if ((uint64_t)OMEGA_DATTN256_CHUNK * gpr_declared(prog->regalloc.peak_gpr_usage) > 65536u) { omega_gpu_host_free(prog); omega_gpu_session_set_error("decode attn256: register file"); return OMEGA_GPU_DATTN_TOO_LARGE; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    if (cg == 0 && spec->kv8) { /* the F2FP.E4M3 placeholders: every 8-bit program has some, except mutant 3 (writes no row) */
        const int np = omega_qmma_patch_code(prog, code.cpu, need);
        if (np < 0 || (np == 0 && spec->mutant != 3)) cg = -1;
    }
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); omega_gpu_session_set_error("decode attn256 encode"); return OMEGA_GPU_DATTN_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code;
    *out = s; *hit = false;
    return OMEGA_GPU_DATTN_OK;
}

/* rec non-NULL (record path, caller holds the session lock): qkv / out are the device buffers dev_qkv / dev_out, the
 * launch is appended to rec instead of submitted, nothing is staged, poisoned or read back. */
static int dattn_core(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t pos, const float *qkv, float eps, float out_scale,
                      float *out, OmegaGpuDecodeAttn256Info *info, OmegaGpuCmdList *rec, uint64_t dev_qkv, uint64_t dev_out) {
    uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (!c || layer >= c->layers || pos >= c->max_pos || (rec ? (!dev_qkv || !dev_out) : (!qkv || !out))) return OMEGA_GPU_DATTN_BAD_ARGS;
    const uint32_t per = g.per_split ? g.per_split : OMEGA_GPU_DATTN256_PER_SPLIT;
    if (per % OMEGA_DATTN256_CHUNK) return OMEGA_GPU_DATTN_BAD_ARGS;
    const uint32_t splits = (pos + per) / per; /* ceil((pos + 1) / per) */
    if (splits > c->max_splits) return OMEGA_GPU_DATTN_TOO_LARGE;
    OmegaDecodeAttn256Spec spec; memset(&spec, 0, sizeof spec);
    spec.depth = g.depth ? g.depth : OMEGA_GPU_DATTN256_DEPTH; spec.mutant = g.mutant; spec.kv8 = c->kv8;
    if (spec.mutant > 5 && !spec.kv8) return OMEGA_GPU_DATTN_BAD_ARGS; /* kv8 mutants need a kv8 cache */

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
        memcpy(c->qkv.cpu, qkv, (size_t)OMEGA_DATTN256_QKV_WORDS * 4);
        for (size_t i = 0; i < OUTW; i++) od[i] = POISON_F32;
        memset(c->cnt.cpu, 0, 64);
        if (omega_gpu_session_sync_to_gpu(&c->qkv) != 0 || omega_gpu_session_sync_to_gpu(&c->out) != 0 || omega_gpu_session_sync_to_gpu(&c->cnt) != 0) rc = OMEGA_GPU_DATTN_CHIP_FAIL;
    }

    uint32_t args[OMEGA_DATTN256_ARGS_WORDS_KV8]; memset(args, 0, sizeof args);
    const uint64_t kc = c->kv[layer].va, vc = kc + c->plane_bytes[0], nm = c->norms.va + (uint64_t)layer * 2 * HD * 4;
    const uint64_t qkv_va = rec ? dev_qkv : c->qkv.va, out_va = rec ? dev_out : c->out.va;
    float fe = eps, fo = out_scale, fq = (float)(0.0625 * 1.4426950408889634); /* log2(e) / sqrt(256) */
    uint32_t ue, uo, uq; memcpy(&ue, &fe, 4); memcpy(&uo, &fo, 4); memcpy(&uq, &fq, 4);
#define A64(i, v) do { args[i] = (uint32_t)(v); args[(i) + 1] = (uint32_t)((v) >> 32); } while (0)
    A64(0, qkv_va); A64(2, kc); A64(4, vc); A64(6, c->rope.va); A64(8, nm); A64(10, nm + HD * 4); A64(12, out_va); A64(14, c->part.va); A64(16, c->cnt.va);
#undef A64
    args[18] = pos; args[19] = per; args[20] = splits; args[21] = (uint32_t)(c->max_pos * (c->kv8 ? OMEGA_DATTN256_ROW_BYTES_KV8 : RB)); /* the kernel doubles it for a bf16 side of a mixed cache */ args[22] = ue; args[23] = uo; args[24] = uq;
    if (c->kv8) { const uint64_t ksv = kc + c->plane_bytes[0] + c->plane_bytes[1], vsv = ksv + c->scale_bytes; args[26] = (uint32_t)ksv; args[27] = (uint32_t)(ksv >> 32); args[28] = (uint32_t)vsv; args[29] = (uint32_t)(vsv >> 32); }
    if (info) {
        info->layer = layer; info->pos = pos; info->splits = splits; info->per_split = per; info->depth = spec.depth;
        info->grid_x = splits; info->grid_y = KVH; info->threads = OMEGA_DATTN256_CHUNK;
        info->kv_bytes = (uint64_t)KVH * (pos + 1ull) * (c->rb[0] + c->rb[1] + (plane8(c, 0) ? 4u : 0u) + (plane8(c, 1) ? 4u : 0u));
        info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_DATTN_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = gpr_declared(ks->kernel.gpr_count),
            .threads_x = OMEGA_DATTN256_CHUNK, .threads_y = 1, .grid_x = splits, .grid_y = KVH,
            .num_elements = (size_t)OUTW,
            .args = args, .n_args = c->kv8 ? OMEGA_DATTN256_ARGS_WORDS_KV8 : OMEGA_DATTN256_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = OMEGA_DATTN256_SHARED_BYTES,
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
        if (unchanged) { omega_gpu_session_set_error("decode attn256: output poison survived"); rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else if (cn[0] | cn[1] | cn[2] | cn[3]) { omega_gpu_session_set_error("decode attn256: a split counter did not return to zero"); rc = OMEGA_GPU_DATTN_CHIP_FAIL; }
        else memcpy(out, od, (size_t)OUTW * 4);
    }
    UNLOCK();
#undef UNLOCK
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}

int omega_gpu_decode_attn256_cache_view(const OmegaGpuDecodeAttn256Cache *c, uint32_t layer, OmegaGpuDecodeAttn256CacheView *v) {
    if (!c || !v || layer >= c->layers) return OMEGA_GPU_DATTN_BAD_ARGS;
    v->layers = c->layers; v->max_pos = c->max_pos; v->rotary_dim = c->rotary_dim; v->theta = c->theta;
    v->k_va = c->kv[layer].va; v->v_va = c->kv[layer].va + c->plane_bytes[0];
    v->kv8 = c->kv8; v->row_bytes = c->rb[0];
    v->ks_va = c->kv8 ? c->kv[layer].va + c->plane_bytes[0] + c->plane_bytes[1] : 0; v->vs_va = c->kv8 ? v->ks_va + c->scale_bytes : 0;
    v->rope_va = c->rope.va;
    v->q_norm_va = c->norms.va + (uint64_t)layer * 2 * HD * 4; v->k_norm_va = v->q_norm_va + HD * 4;
    v->part_va = c->part.va; v->cnt_va = c->cnt.va;
    return OMEGA_GPU_DATTN_OK;
}

int omega_gpu_decode_attn256(OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t pos, const float *qkv, float eps, float out_scale, float *out, OmegaGpuDecodeAttn256Info *info) {
    return dattn_core(c, layer, pos, qkv, eps, out_scale, out, info, NULL, 0, 0);
}

int omega_gpu_decode_attn256_record(OmegaGpuCmdList *cl, OmegaGpuDecodeAttn256Cache *c, uint32_t layer, uint32_t pos, uint64_t qkv_va,
                                    float eps, float out_scale, uint64_t out_va) {
    if (!cl) return OMEGA_GPU_DATTN_BAD_ARGS;
    return dattn_core(c, layer, pos, NULL, eps, out_scale, NULL, NULL, cl, qkv_va, out_va);
}

int omega_gpu_decode_attn256_counters_check(OmegaGpuDecodeAttn256Cache *c) {
    if (!c || !c->cnt.cpu) return OMEGA_GPU_DATTN_BAD_ARGS;
    if (omega_gpu_session_sync_to_cpu(&c->cnt) != 0) return OMEGA_GPU_DATTN_CHIP_FAIL;
    const uint32_t *cn = c->cnt.cpu;
    if (cn[0] | cn[1] | cn[2] | cn[3]) { omega_gpu_session_set_error("decode attn256: a split counter did not return to zero"); return OMEGA_GPU_DATTN_CHIP_FAIL; }
    return OMEGA_GPU_DATTN_OK;
}
