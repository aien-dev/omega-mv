#ifndef OMEGA_GPU_TGEMM_API_H
#define OMEGA_GPU_TGEMM_API_H
/*
 * omega_gpu_tgemm_api: the trainer's device-resident bf16 GEMM (lane E1, 2026-10-16). New file.
 * C[m][n] = A[m][k] bf16 x B[k][n] bf16, operands already resident on the GPU in the layout the trainer keeps them (no host pack
 * per call): layout NN (A[m][k], B[k][n]), NT (A[m][k], Bt[n][k]), TN (At[k][m], B[k][n]); all row-major, dense.
 * Epilogue: F32 overwrite, F32_ACC (C += A B), BF16 overwrite. Kernels: omega_blackwell_codegen_tgemm.h, cached through the
 * prefill module's resident-kernel cache (omega_gpu_prefill.h, kinds OMEGA_TGEMM_KIND, OMEGA_TGEMM_REDUCE_KIND). Return codes are
 * the prefill module's (OMEGA_GPU_PF_OK, _BAD_ARGS, _TOO_LARGE, _CHIP_FAIL).
 * Split-K (deterministic, no atomics): splits > 1 runs the GEMM into f32 partial buffers (module-owned, GPU-cached, never read by
 * the CPU) and a fixed-order reduce kernel writes C (old C first when accumulating, then partial 0, 1, ...). splits 0 picks: 1
 * when the plain grid already has >= 3 CTAs per SM, otherwise the smallest power of two up to 16 dividing k / 128 that gets there.
 * Memory rule (trainer plan R-8): buffers from omega_gpu_pf_alloc_dev are GPU-cached and must never be read or written by the
 * CPU; the tile table is a host (uncached) buffer the CPU fills and the session syncs.
 * Limits: m a multiple of the tile height (omega_gpu_tgemm_bm), n a multiple of the span (warps * 16), k a multiple of 128,
 * tiles x splits <= 65535, m * n < 2^32 and k * max(m, n) < 2^32 (32-bit element arithmetic in the kernel), m * n a multiple
 * of 1024 when split (the reduce CTA). omega_gpu_tgemm serializes on the session lock; the module's table and partials are
 * rebuilt under it.
 * Grid order: the kernel runs the CTAs of one A tile together (columns fastest) unless B is the larger operand, when the CTAs of
 * one B column slab run together (lm_head forward, n 50304: B is 77 MB, A 1.5 MB; evidence TGEMM-CUT1-FIX-403d5cf5 measured the
 * wrong order re-reading B 16 times).
 */
#include <stdint.h>
#include "omega_gpu_session.h"
#include "omega_blackwell_codegen_tgemm.h"

#ifdef __cplusplus
extern "C" {
#endif
#define OMEGA_TGEMM_KIND 0x60u
#define OMEGA_TGEMM_REDUCE_KIND 0x61u
typedef struct {
    uint32_t layout;  /* OMEGA_TGEMM_LAYOUT_* */
    uint32_t epi;     /* OMEGA_TGEMM_EPI_* */
    uint32_t m, n, k;
    uint32_t splits;  /* 0 auto, 1 none, 2 .. 16 partial buffers */
    uint64_t a_va, b_va, c_va;
} OmegaTgemmCall;
typedef struct { uint64_t c_va; uint32_t row0, kb0, nkb, pad; uint64_t pad2; } OmegaTgemmTile;

/* the kernel spec the current knobs pick for a call (BAD_ARGS when the call cannot be served); *splits_out the split count */
int omega_gpu_tgemm_spec(const OmegaTgemmCall *c, OmegaTgemmSpec *out, uint32_t *splits_out);
uint32_t omega_gpu_tgemm_bm(uint32_t n); /* rows per tile the current knobs pick for width n (what m must be a multiple of) */
/* tile table for a call: one entry per (split, bm rows), split-major; partials at part_va + split * m * n * 4 (c_va when 1 split);
 * returns the count (0: refused or more than max) */
uint32_t omega_gpu_tgemm_tiles(const OmegaTgemmCall *c, uint64_t part_va, OmegaTgemmTile *tab, uint32_t max);
/* synchronous: builds the table in a module-owned host buffer, launches (and reduces), waits; chip ns of every launch added to *ns */
int omega_gpu_tgemm(const OmegaTgemmCall *c, uint64_t *ns);
/* non-blocking: lock held and device open (omega_gpu_pf_open_locked); fills *L for omega_gpu_cmdlist_add with the resident
 * kernel, the grid and args[OMEGA_TGEMM_ARGS_WORDS] (the caller's array; L->args points at it, so it must outlive the add).
 * tiles_va: a GPU-visible table of ntiles entries already synced (omega_gpu_tgemm_tiles order). Caller-built entries must have
 * nkb >= 1 (the kernel's block counter is unsigned: nkb 0 would run until the timeout) and kb0 * 128 * n < 2^32. The GEMM
 * launch writes f32 partials when the call splits; omega_gpu_tgemm_fill_reduce_launch_locked then fills the reduce launch
 * (args[OMEGA_TGEMM_REDUCE_ARGS_WORDS]) that must follow it in the same command list. */
int omega_gpu_tgemm_fill_launch_locked(const OmegaTgemmCall *c, uint64_t tiles_va, uint32_t ntiles, OmegaGpuLaunch *L, uint32_t *args);
int omega_gpu_tgemm_fill_reduce_launch_locked(const OmegaTgemmCall *c, uint64_t part_va, OmegaGpuLaunch *L, uint32_t *args);
/* knobs: rt (2/4/8), warps (8/16), aqn (1/2); 0 keeps the default (rt 8 x 16 warps; rt 4 x 8 warps when n % 256 != 0) */
void omega_gpu_tgemm_set_shape(uint32_t rt, uint32_t warps, uint32_t aqn);
void omega_gpu_tgemm_test_set_mutant(int m);        /* GEMM kernel mutant (tests) */
void omega_gpu_tgemm_test_set_reduce_mutant(int m); /* reduce kernel mutant (tests) */
#ifdef __cplusplus
}
#endif
#endif
