#ifndef OMEGA_GPU_FNIDX_API_H
#define OMEGA_GPU_FNIDX_API_H
/*
 * omega_gpu_fnidx_api: launch of the Flash-Next QSA indexer phases (omega_blackwell_codegen_fnidx.h; flashnext/port,
 * 2026-10-19). The caller owns the buffers (omega_gpu_session_alloc, GPU-uncached host-visible, synced) and passes device
 * addresses.
 *   int omega_gpu_fnidx_score_run(q_va, kb_va, scores_va, nb, info): scores of nb block keys (nb >= 1) against the 4 index query heads.
 *   int omega_gpu_fnidx_select_run(scores_va, plist_va, nb, vis, info): the attended position list for `vis` visible tokens,
 *        nb = vis / 4 >= 1 complete blocks, K = min(nb, 512); writes 4 K + (vis - 4 nb) u32 positions.
 *   record variants append to a command list (caller holds the session lock; prepare first).
 * Argument and kernel details: omega_blackwell_codegen_fnidx.h.
 */
#include "omega_blackwell_codegen_fnidx.h"
#include <stdint.h>

enum { OMEGA_GPU_FNIDX_OK = 0, OMEGA_GPU_FNIDX_BAD_ARGS = -1, OMEGA_GPU_FNIDX_CODEGEN_FAIL = -3, OMEGA_GPU_FNIDX_CHIP_FAIL = -4 };
struct OmegaGpuCmdList;
typedef struct { uint32_t phase, grid_x, threads, gprs, insns; uint64_t elapsed_ns, call_ns; int kernel_cache_hit; } OmegaGpuFnIdxInfo;

int omega_gpu_fnidx_score_run(uint64_t q_va, uint64_t kb_va, uint64_t scores_va, uint32_t nb, OmegaGpuFnIdxInfo *info);
int omega_gpu_fnidx_select_run(uint64_t scores_va, uint64_t plist_va, uint32_t nb, uint32_t vis, OmegaGpuFnIdxInfo *info);
int omega_gpu_fnidx_score_record(struct OmegaGpuCmdList *cl, uint64_t q_va, uint64_t kb_va, uint64_t scores_va, uint32_t nb);
int omega_gpu_fnidx_select_record(struct OmegaGpuCmdList *cl, uint64_t scores_va, uint64_t plist_va, uint32_t nb, uint32_t vis);
int omega_gpu_fnidx_prepare(uint32_t phase);
void omega_gpu_fnidx_test_set_mutant(int mutant);
const char *omega_gpu_fnidx_last_error(void);
#endif
