#ifndef OMEGA_GPU_FNATTN_API_H
#define OMEGA_GPU_FNATTN_API_H
/*
 * omega_gpu_fnattn_api: Qwen3.8-Flash-Next decode attention (flashnext/port, 2026-10-19), a derivative of
 * omega_gpu_decode_attn256_api.h (the 27B's) with 2 KV heads and a SPARSE position list; kernel and numbers in
 * omega_blackwell_codegen_fnattn.h. New files only; the 27B API is untouched and its helpers (RoPE row, dim permutation)
 * and return codes are reused.
 *
 *   int omega_gpu_fnattn_cache_alloc(layers, max_pos, rope_theta, rotary_dim, &cache): GPU-resident bf16 K and V planes
 *        [2 KV heads][max_pos][256] per layer, the RoPE table, norm weights, split partials, counters, qkv / out / list buffers.
 *   int omega_gpu_fnattn_set_norms(c, layer, q_norm, k_norm): 256 f32 each, folded to 1 + w and permuted (omega_dattn256_perm_vec).
 *   int omega_gpu_fnattn(c, layer, pos, qkv, plist, count, eps, out_scale, out, info): qkv f32 [7168] = q 24 x 256 (permuted
 *        dims, gate NOT included) | k 2 x 256 (permuted) | v 2 x 256; pos = the new token's position; plist = the attended
 *        positions, strictly ascending, each <= pos, count >= 1 (the list may omit pos: a completed block outside the
 *        indexer's top 512). Appends K/V at pos, writes out[24][256] * out_scale in model order. The gate and o_proj are the
 *        caller's.
 *   int omega_gpu_fnattn_record(cl, c, layer, pos, qkv_va, plist_va, count, eps, out_scale, out_va): the same launch into a
 *        command list with device buffers (the list written by the indexer kernel); the list is NOT checked.
 */
#include "omega_blackwell_codegen_fnattn.h"
#include "omega_gpu_decode_attn_api.h"
#include "omega_gpu_decode_attn256_api.h"
#include <stdint.h>
#include <stddef.h>

#define OMEGA_GPU_FNA_PER_SPLIT 256u
#define OMEGA_GPU_FNA_MAX_LAYERS 64u
#define OMEGA_GPU_FNA_MAX_POS 262144u
#define OMEGA_GPU_FNA_DEPTH 4u

typedef struct OmegaGpuFnAttnCache OmegaGpuFnAttnCache;
struct OmegaGpuCmdList;

typedef struct {
    uint32_t layer, pos, count, splits, per_split, depth;
    uint32_t grid_x, grid_y, threads;
    uint64_t kv_bytes;      /* K + V bytes the launch streams: 2 * 2 * count * 512 (the second CTA of a KV head re-reads from L2) */
    uint64_t elapsed_ns, call_ns;
    uint32_t completion_marker;
    int kernel_cache_hit;
} OmegaGpuFnAttnInfo;

int omega_gpu_fnattn_cache_alloc(uint32_t layers, uint32_t max_pos, double rope_theta, uint32_t rotary_dim, OmegaGpuFnAttnCache **out);
void omega_gpu_fnattn_cache_free(OmegaGpuFnAttnCache *c);
int omega_gpu_fnattn_set_norms(OmegaGpuFnAttnCache *c, uint32_t layer, const float *q_norm, const float *k_norm);
int omega_gpu_fnattn(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t pos, const float *qkv, const uint32_t *plist, uint32_t count, float eps,
                     float out_scale, float *out, OmegaGpuFnAttnInfo *info);
int omega_gpu_fnattn_record(struct OmegaGpuCmdList *cl, OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t pos, uint64_t qkv_va, uint64_t plist_va,
                            uint32_t count, float eps, float out_scale, uint64_t out_va);
int omega_gpu_fnattn_counters_check(OmegaGpuFnAttnCache *c);
int omega_gpu_fnattn_cache_write(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint32_t count, const uint16_t *k, const uint16_t *v);
int omega_gpu_fnattn_cache_read(OmegaGpuFnAttnCache *c, uint32_t layer, uint32_t kv_head, uint32_t pos, uint16_t *k, uint16_t *v);
void omega_gpu_fnattn_set_depth(uint32_t depth);
void omega_gpu_fnattn_set_per_split(uint32_t per_split);
void omega_gpu_fnattn_test_set_mutant(int mutant);
#endif
