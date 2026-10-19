#ifndef OMEGA_GPU_FNHC_API_H
#define OMEGA_GPU_FNHC_API_H
/*
 * omega_gpu_fnhc_api: launch of the Flash-Next 4-lane gated residual phases (omega_blackwell_codegen_fnhc.h; flashnext/port,
 * 2026-10-19). The caller owns the buffers (omega_gpu_session_alloc, GPU-uncached host-visible, synced) and passes device
 * addresses; this layer only builds, caches and launches the four small kernels.
 *   int omega_gpu_fnhc_run(phase, a, b, c, d, e, eps, info): launch and wait (the caller syncs buffers to and from the device).
 *   int omega_gpu_fnhc_record(cl, phase, a, b, c, d, e, eps): append the launch to a command list (caller holds the session lock;
 *        warm the kernel first with omega_gpu_fnhc_prepare).
 *   int omega_gpu_fnhc_prepare(phase): build and pin the phase's kernel.
 * Argument meaning per phase: see omega_blackwell_codegen_fnhc.h.
 */
#include "omega_blackwell_codegen_fnhc.h"
#include <stdint.h>

enum { OMEGA_GPU_FNHC_OK = 0, OMEGA_GPU_FNHC_BAD_ARGS = -1, OMEGA_GPU_FNHC_CODEGEN_FAIL = -3, OMEGA_GPU_FNHC_CHIP_FAIL = -4 };
struct OmegaGpuCmdList;
typedef struct { uint32_t phase, grid_x, threads, gprs, insns; uint64_t elapsed_ns, call_ns; int kernel_cache_hit; } OmegaGpuFnHcInfo;

int omega_gpu_fnhc_run(uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, float eps, OmegaGpuFnHcInfo *info);
int omega_gpu_fnhc_record(struct OmegaGpuCmdList *cl, uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, float eps);
int omega_gpu_fnhc_prepare(uint32_t phase);
void omega_gpu_fnhc_test_set_mutant(int mutant);
const char *omega_gpu_fnhc_last_error(void);
#endif
