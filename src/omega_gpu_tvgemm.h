#ifndef OMEGA_GPU_TVGEMM_H
#define OMEGA_GPU_TVGEMM_H
/*
 * omega_gpu_tvgemm (cascade/tree-verify-gemm, 2026-10-19): the dense FP8 GEMM for decode and N-row verify on the tensor
 * cores, one code path for N = 1 .. 64 rows. New file.
 *
 * Kernel: the T3 cut-1 tensor-core GEMM (omega_blackwell_codegen_tc.h: e4m3 -> bf16 in registers, HMMA.16816.F32.BF16,
 * per 128-block temporary then fma with the folded block scale). Every output y[r][c] depends only on activation row r,
 * weight row c and the scales, in a fixed order (8 HMMA k16 steps per block, blocks in order): the value of row r is the
 * same bits whatever N, whatever the row tile shape (rt) and whichever other rows share the pass. That is the
 * "bit-identical by construction" property plain decode (N = 1) and verify (N rows) need; the chip test checks it.
 *
 * Differences from omega_gpu_tcgemm_api.h (the prefill entry point):
 *  - activations and outputs are DEVICE buffers (GPU-cached, omega_gpu_prefill.h): every CTA re-reads the N x k activation
 *    rows, and with the uncached session buffers each of those reads went to DRAM (as many bytes as the weights at
 *    32 rows: the measured cause of the slow weight stream);
 *  - no host work per call beyond a cached tile table: the launch can be recorded into a command list (decode graph);
 *  - the row tile follows N: rt = 1 (N <= 16), 2 (N <= 32), 4 (N <= 64), so N = 1 pays no 64-row arithmetic.
 * Layouts: x rows of k bf16 in the (0,2,1,3) stored order times 2^64 (omega_tcgemm_permute_x), row r at x_va + r k 2;
 * y rows of n f32, row r at y_va + r n 4. Weights in place from an omega_gpu_fp8_api tensor (one expert).
 * Locking: every function takes the session lock itself except *_locked.
 */
#include <stdint.h>
#include "omega_blackwell_codegen_tc.h"
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_prefill.h"

#ifdef __cplusplus
extern "C" {
#endif
#define OMEGA_GPU_TV_MAX_ROWS 64u

/* the spec used for N rows of an n x k tensor (knobs below override); 0 on success */
int omega_gpu_tv_spec(uint32_t rows, uint32_t n, uint32_t k, OmegaTcGemmSpec *spec);
/* measurement knobs: force rt, ct, warps (0 = automatic); TEST ONLY mutant 1-4 (codegen_tc.h) */
void omega_gpu_tv_set_shape(uint32_t rt, uint32_t ct, uint32_t warps);
void omega_gpu_tv_test_set_mutant(int mutant);

/* one launch: y[0..rows) = W . x[0..rows); *chip_ns gets the kernel's chip time (may be NULL) */
int omega_gpu_tv_gemm(const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va, uint64_t *chip_ns);
/* the same launch appended to a command list (lock held by the caller, as for omega_gpu_pf_record_locked) */
struct OmegaGpuCmdList;
int omega_gpu_tv_record_locked(struct OmegaGpuCmdList *cl, const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va);

#ifdef __cplusplus
}
#endif
#endif
