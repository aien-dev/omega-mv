#ifndef OMEGA_BLACKWELL_CODEGEN_FNHC_H
#define OMEGA_BLACKWELL_CODEGEN_FNHC_H
/*
 * omega_blackwell_codegen_fnhc: the small kernels of the Qwen3.8-Flash-Next 4-lane gated residual ("hyper connections";
 * flashnext/port, 2026-10-19). New file. Model rule (crates/aien-inference-abi/src/flashnext.rs GatedResidual::forward and
 * `inject`, read as the spec; every sub-block, attention and MoE, has one):
 *     normed = RMSNorm-zero-centred over each 2560 group of the 10240 lanes:  x * rsqrt(mean(x^2) + eps) * (1 + w)
 *     d      = down[320][10240] . normed          then   d' = silu(d / 4)
 *     mix    = sigmoid(up[10240][320] . d')
 *     x_in   = (1/4) * sum over the 4 lanes of (mix * normed)                  [2560]  (the block's input)
 *     inj    = 2 * sigmoid((inject[4][10240] . normed) / 4)                      [4]
 *     after the block:  lanes[l][j] += block_out[j] * inj[l]
 * The three matrix-vector products are bf16 GEMVs of omega_gpu_bf16_gemv_api (the up matrix padded with zero columns to k 512);
 * the four phases here are everything between them. One program per phase (spec.phase), 256 threads per CTA:
 *   NORM     grid 4  (lane l): A = lanes f32 [10240], B = (1 + w) f32 [10240], C = normed f32 [10240]
 *   ACT      grid 2:           A = d f32 [320], B = d' f32 [>= 320]  (d' = silu(d / 4); words past 320 untouched)
 *   COMBINE  grid 10:          A = u f32 [10240] (the up product), B = normed, C = g f32 [4] (the inject product),
 *                              D = x_in f32 [2560], E = inj f32 [4] (written by CTA 0)
 *   INJECT   grid 40:          A = lanes f32 [10240] (updated in place), B = block_out f32 [2560], C = inj f32 [4]
 * sigmoid(x) = 1 / (1 + EX2(-x log2 e)) with MUFU.EX2 / MUFU.RCP (approximate units, 2^-22 class); the products and sums use
 * FMUL and FADD (no FMA) in the host reference's order.
 */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#define OMEGA_FNHC_H 2560u
#define OMEGA_FNHC_L 10240u
#define OMEGA_FNHC_RANK 320u
#define OMEGA_FNHC_THREADS 256u
#define OMEGA_FNHC_ARG_A 0x380u
#define OMEGA_FNHC_ARG_B 0x388u
#define OMEGA_FNHC_ARG_C 0x390u
#define OMEGA_FNHC_ARG_D 0x398u
#define OMEGA_FNHC_ARG_E 0x3a0u
#define OMEGA_FNHC_ARG_EPS 0x3a8u /* f32 */
#define OMEGA_FNHC_ARGS_WORDS 11u
#define OMEGA_FNHC_SHARED_BYTES 64u

typedef enum { OMEGA_FNHC_NORM = 0, OMEGA_FNHC_ACT = 1, OMEGA_FNHC_COMBINE = 2, OMEGA_FNHC_INJECT = 3 } OmegaFnHcPhase;
#define OMEGA_FNHC_PHASES 4u

typedef struct {
    uint32_t phase;
    int mutant; /* TEST ONLY. 0 none; 1 NORM skips the weight; 2 ACT applies silu(d), not silu(d / 4); 3 COMBINE uses the raw u, not sigmoid(u);
                 * 4 COMBINE's inj uses g, not g / 4; 5 INJECT adds block * inj[0] on every lane; 6 NORM divides by 2560 + 1 */
} OmegaFnHcSpec;

uint32_t omega_fnhc_grid(uint32_t phase);
int omega_blackwell_codegen_fnhc_prog(const OmegaFnHcSpec *spec, BlackwellIRProgram *prog);
int omega_fnhc_prog_stats(const OmegaFnHcSpec *spec, uint32_t *insns, uint32_t *gprs);
#endif
