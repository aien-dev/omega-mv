#ifndef OMEGA_BLACKWELL_CODEGEN_HX_H
#define OMEGA_BLACKWELL_CODEGEN_HX_H
/*
 * omega_blackwell_codegen_hx: activation kernels for the W8A16 whole-prompt prefill (OWN-30B W-L1H, 2026-10-14). New file.
 * They produce the activation rows the tensor-core FP8 GEMM of omega_blackwell_codegen_tc.h reads (cut-1 kernel, e4m3
 * weights dequantised to bf16 in registers, HMMA.16816.F32.BF16, f32 accumulate, per-128-block scale FMA): bf16 RNE of
 * x * 2^64, stored per aligned group of 4 as [x(4q), x(4q+2), x(4q+1), x(4q+3)] (omega_tcgemm_permute_x). The only rounding
 * of a GEMM input is this bf16 RNE (8 significant bits), against e4m3 (4 bits) on the W8A8 path.
 *
 * ---- CONVERT (mode OMEGA_HX_CONVERT) ----
 *   out[r][perm(j)] = bf16_rn(x[r][j] * 2^64), x f32 or bf16 rows of k (row stride k elements), out bf16 rows of k.
 * ---- SWIGLU (mode OMEGA_HX_SWIGLU, k = I) ----
 *   in f32 rows of 2I (gate columns 0 .. I-1, up columns I .. 2I-1: the checkpoint's gate_up row order, which the cut-1 GEMM
 *   writes unchanged), out[r][perm(a)] = bf16_rn(o * 2^64) with o = (g * rcp(2^min(-g log2 e, 125) + 1)) * u: the instruction
 *   sequence of the W8A8 GEMM's SwiGLU epilogue (omega_blackwell_codegen_qg.h: FMUL, FMNMX, MUFU.EX2, FADD, MUFU.RCP, FMUL, FMUL),
 *   then the exact FMUL by 2^64 and F2FP.BF16 RN.
 * One CTA per row (ctaid.x = row), k / 4 threads (k a multiple of 128, 128 .. 4096); thread t owns elements 4t .. 4t+3.
 * Arguments (cbank 0x380): in u64, out u64.
 * mutant (tests only): 1 stores the natural order (no (0,2,1,3) permutation) / SWIGLU: swaps gate and up; 2 drops the 2^64.
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif
enum { OMEGA_HX_CONVERT = 0, OMEGA_HX_SWIGLU = 1 };
enum { OMEGA_HX_IN_F32 = 0, OMEGA_HX_IN_BF16 = 1 };
typedef struct { uint32_t mode, k, in_type; int mutant; } OmegaHxSpec;
#define OMEGA_HX_ARGS_WORDS 4u
int omega_hx_spec_ok(const OmegaHxSpec *s);
int omega_blackwell_codegen_hx_prog(const OmegaHxSpec *s, BlackwellIRProgram *prog);
/* host mirrors (bit-exact for finite inputs; the SwiGLU mirror models MUFU.EX2 / MUFU.RCP as exp2f / 1.0f / x, the same
 * model the W8A8 SwiGLU epilogue is chip-checked against) */
uint16_t omega_hx_bf16_rn(float f);
float omega_hx_swiglu(float g, float u);
#ifdef __cplusplus
}
#endif
#endif
