#ifndef OMEGA_BLACKWELL_CODEGEN_OMMA_H
#define OMEGA_BLACKWELL_CODEGEN_OMMA_H
/*
 * omega_blackwell_codegen_omma: the GB10's 4-bit (e2m1) tensor-core ops, for the FP4 rate probe (L284 own-fp4, 2026-10-20). New file.
 *
 * Same method as omega_blackwell_codegen_qmma: emitted as BW_IR_HMMA_BF16 with a variant mark in imm, patched after encoding.
 * Oracle: ptxas/nvdisasm 13.0 (build 36424714) -arch=sm_121a, aien-train numerics/own-fp4 streams/own-fp4/asm (the .sass files):
 *   E2M1  QMMA.16832.F32.E2M1.E2M1 R4, R4, R8, R12                              = 0x000000080404727a / 0x004fde000028ec0c
 *   SF8   QMMA.SF.16832.F32.E2M1.E2M1.E8 R4, R4, R8, R12, R10, R11, URZ          = 0x70b00a080404747a / 0x004fde000028fe0c
 *   MX    OMMA.SF.16864.F32.E2M1.E2M1.E8 R4, R4, R8, R12, R10, R11, URZ          = 0x70b00a080404747f / 0x004fde0000083e0c
 *   NV    OMMA.SF.16864.F32.E2M1.E2M1.UE4M3.4X R4, R4, R8, R12, R10, R11, URZ    = 0x70b00a080404747f / 0x004fde0000043e0c
 * Fields (from selector/register variants in the same oracle): word1 byte0 = Rb, bits 8..15 = scale-A register, bits 20..27 =
 * scale-B register, bits 28..31 + word2 0x3e00 = URZ (byte/thread selectors all 0); word2 low byte = Rc. Here scale A and scale B
 * share one register S (every byte equal), so the selector choice cannot matter.
 * PTX forms: E2M1 = kind::f8f6f4 (e2m1 in the middle 4 bits of a byte, k = 32), SF8 = kind::mxf8f6f4 scale_vec::1X ue8m0 (32 per
 * scale), MX = kind::mxf4 scale_vec::2X ue8m0 (two nibbles per byte, k = 64, 32 per scale), NV = kind::mxf4nvf4 scale_vec::4X
 * ue4m3 (16 per scale).
 */
#include <stddef.h>
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif
enum { OMMA_V_E4M3 = 0, OMMA_V_E2M1 = 1, OMMA_V_SF8 = 2, OMMA_V_MX = 3, OMMA_V_NV = 4, OMMA_V_N = 5 };
#define OMEGA_OMMA_MARK_BASE 0x4f4d4d00u /* imm = BASE + variant (E4M3 uses the QMMA mark and patch) */
/* patch every 4-bit placeholder; scale_vreg = the vreg holding the scale bytes (ignored for E2M1). Returns count or -1. */
int omega_omma_patch_code(const BlackwellIRProgram *prog, int scale_vreg, uint8_t *code, size_t len);
/* encoder self-test against the oracle words above, registers masked; 0 = pass */
int omega_omma_patch_selftest(char *msg, size_t msgn);
/* peak kernel: chains independent accumulators, A = B = data_byte everywhere, scale register = scale_word; every stored value is
 * per_mma * iters (see test). *scale_vreg_out receives the scale vreg for the patch. */
int omega_blackwell_codegen_omma_peak_prog(int variant, uint32_t data_word, uint32_t scale_word, uint32_t iters, uint32_t chains,
                                           uint32_t stall, uint32_t threads, BlackwellIRProgram *prog, int *scale_vreg_out);
#ifdef __cplusplus
}
#endif
#endif
