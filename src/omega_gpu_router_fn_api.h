#ifndef OMEGA_GPU_ROUTER_FN_API_H
#define OMEGA_GPU_ROUTER_FN_API_H
/*
 * omega_gpu_router_fn_api: one-launch MoE router for Qwen3.8-Flash-Next (flashnext/port, 2026-10-19). Kernel:
 * omega_blackwell_codegen_router_fn.h. The Qwen3-30B router API (omega_gpu_router_api.h) is untouched.
 *
 *   int  omega_gpu_router_fn_alloc(uint32_t layers, OmegaGpuRouterFn **out);
 *   int  omega_gpu_router_fn_set_layer(r, layer, gate_bf16 [512][2560], shared_gate_bf16 [2560]);
 *   int  omega_gpu_router_fn_set_directory(r, layer, gu [513][2], dn [513][2]);   raw {w_va, s_va}; slot 512 = shared
 *   int  omega_gpu_router_fn_set_directory_nv4(r, layer, gate_up_slab, down_slab); slots 0..512 of two NV4 slabs
 *   int  omega_gpu_router_fn(r, layer, x [2560], norm_topk, out_scale, ids [10], weights [11], info);
 *        One launch. ids descending by logit (lowest id first on a tie); weights[0..9] the routing weights
 *        (renormalised over the 10 when norm_topk), weights[10] = sigmoid(shared_expert_gate . x). On the device:
 *        x' = x * out_scale and the two 11-entry group tables (entry 10 = directory slot 512) the grouped NV4 GEMV reads.
 *   int  omega_gpu_router_fn_record(cl, r, layer, x_va, norm_topk, out_scale, &devout);  into a command list
 *   int  omega_gpu_router_fn_read_back(r, xout, tab_gu, tab_dn);                          test helper
 * Buffers: GPU-uncached host-visible session memory; outputs poisoned before a launch, a survivor is CHIP_FAIL.
 */
#include "omega_blackwell_codegen_router_fn.h"
#include <stdint.h>
#include <stddef.h>

enum {
    OMEGA_GPU_RFN_OK = 0,
    OMEGA_GPU_RFN_BAD_ARGS = -1,
    OMEGA_GPU_RFN_TOO_LARGE = -2,
    OMEGA_GPU_RFN_CODEGEN_FAIL = -3,
    OMEGA_GPU_RFN_CHIP_FAIL = -4
};

#define OMEGA_GPU_RFN_MAX_LAYERS 64u
#define OMEGA_GPU_RFN_WARPS 16u
#define OMEGA_GPU_RFN_DEPTH 5u

typedef struct OmegaGpuRouterFn OmegaGpuRouterFn;
typedef struct OmegaGpuNv4Slab OmegaGpuNv4Slab;

typedef struct {
    uint32_t layer, warps, depth, threads;
    uint64_t weight_bytes;
    uint64_t elapsed_ns;
    uint64_t call_ns;
    uint32_t completion_marker;
    int kernel_cache_hit;
} OmegaGpuRouterFnInfo;

int omega_gpu_router_fn_alloc(uint32_t layers, OmegaGpuRouterFn **out);
void omega_gpu_router_fn_free(OmegaGpuRouterFn *r);
int omega_gpu_router_fn_set_layer(OmegaGpuRouterFn *r, uint32_t layer, const uint16_t *gate_bf16, const uint16_t *shared_gate_bf16);
int omega_gpu_router_fn_set_directory(OmegaGpuRouterFn *r, uint32_t layer, const uint64_t *gu, const uint64_t *dn);
int omega_gpu_router_fn_set_directory_nv4(OmegaGpuRouterFn *r, uint32_t layer, const OmegaGpuNv4Slab *gate_up, const OmegaGpuNv4Slab *down);
int omega_gpu_router_fn(OmegaGpuRouterFn *r, uint32_t layer, const float *x, int norm_topk, float out_scale,
                        uint32_t *ids, float *weights, OmegaGpuRouterFnInfo *info);
void omega_gpu_router_fn_device_outputs(const OmegaGpuRouterFn *r, uint64_t *xout_va, uint64_t *tab_gu_va, uint64_t *tab_dn_va);
typedef struct { uint64_t xout_va, ids_va, wts_va, tab_gu_va, tab_dn_va; } OmegaGpuRouterFnDevOut;
struct OmegaGpuCmdList;
int omega_gpu_router_fn_record(struct OmegaGpuCmdList *cl, OmegaGpuRouterFn *r, uint32_t layer, uint64_t x_va, int norm_topk,
                               float out_scale, OmegaGpuRouterFnDevOut *out);
int omega_gpu_router_fn_read_back(OmegaGpuRouterFn *r, float *xout, uint64_t *tab_gu, uint64_t *tab_dn);
const char *omega_gpu_router_fn_last_error(void);
const char *omega_gpu_router_fn_rc_name(int rc);
void omega_gpu_router_fn_set_shape(uint32_t warps, uint32_t depth);
void omega_gpu_router_fn_test_set_mutant(int mutant);

#endif
