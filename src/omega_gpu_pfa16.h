#ifndef OMEGA_GPU_PFA16_H
#define OMEGA_GPU_PFA16_H
/*
 * omega_gpu_pfa16 (OWN-30B W-L1H, 2026-10-14): the W8A16 whole-prompt prefill kernels on the device-resident prefill
 * session (omega_gpu_prefill.h). New file. Same weights (read IN PLACE from an omega_gpu_fp8_api tensor, checkpoint order,
 * scales [n/128][KB] f32 * 2^56), same tile table entry and rows-per-tile as the W8A8 path (omega_gpu_pf_qgemm_tiles_va
 * builds both when omega_gpu_pf_hgemm_bm() == omega_gpu_pf_qgemm_bm()), but the activations stay bf16 instead of e4m3:
 *
 *   omega_gpu_pf_hconv    x (f32 or bf16 rows of k) -> xh bf16 rows of k: bf16_rn(x * 2^64), (0,2,1,3)-permuted per group of 4
 *   omega_gpu_pf_hgemm    y f32 rows of n = sum over 128-blocks kb of S'[c/128][kb] * (8 HMMA.16816.F32.BF16 steps from zero);
 *                         weights e4m3 -> bf16 exactly in registers (omega_blackwell_codegen_tc.h, the cut-1 kernel, chip
 *                         evidence OWN30B-T3-TCGEMM-6d2803ec); rowidx_va required (the down GEMM passes an identity row index: the cut-1 kernel with a direct-rows variant
 *                         peaks at 128 registers, over the 126 the 128-register declaration allows)
 *   omega_gpu_pf_hswiglu  gu f32 rows of 2I (gate | up, the gate_up GEMM's plain output) -> act bf16 rows of I in the
 *                         hconv format (silu as the W8A8 epilogue, then * 2^64, bf16 RN)
 *
 * A gate_up GEMM therefore runs as hgemm (f32 rows of 2I) + hswiglu, where W8A8 fuses the SwiGLU into the GEMM.
 * Buffers: xh rows must exist up to row0 + bm - 1 of every tile (pad bm rows, as the W8A8 xq buffers); y rows past a tile's
 * cnt are not stored. Return codes OMEGA_GPU_PF_*.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
enum { OMEGA_GPU_PF_HX_F32 = 0, OMEGA_GPU_PF_HX_BF16 = 1 };
int omega_gpu_pf_hconv(uint32_t k, uint32_t in_type, uint32_t rows, uint64_t x_va, uint64_t xh_va, uint64_t *ns);
int omega_gpu_pf_hswiglu(uint32_t i, uint32_t rows, uint64_t gu_va, uint64_t act_va, uint64_t *ns);
uint32_t omega_gpu_pf_hgemm_bm(void);
int omega_gpu_pf_hgemm(uint32_t n, uint32_t k, uint64_t tiles_va, uint32_t ntiles, uint64_t rowidx_va, uint64_t xh_va, uint64_t y_va, uint64_t *ns);
/* knobs: shape (all 0 restores rt 4, ct 2, warps 8), TEST ONLY mutants (GEMM: omega_blackwell_codegen_tc.h 1-4; hx: 1-2) */
void omega_gpu_pf_hgemm_set_shape(uint32_t rt, uint32_t ct, uint32_t warps);
void omega_gpu_pf_hgemm_test_set_mutant(int m);
void omega_gpu_pf_hx_test_set_mutant(int m);
#ifdef __cplusplus
}
#endif
#endif
