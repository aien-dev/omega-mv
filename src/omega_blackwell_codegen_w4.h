#ifndef OMEGA_BLACKWELL_CODEGEN_W4_H
#define OMEGA_BLACKWELL_CODEGEN_W4_H
/*
 * omega_blackwell_codegen_w4: 4-bit (OCP MXFP4) GEMV kernel for the Qwen3.8-27B decode graph
 * (qwen38/w4-gemv, 2026-10-19). New file: no existing generator changes.
 *
 * y[r] = sum_c q[r][c] * x[c] * 2^(E[r][c/32] - 127), m = 1 (decode), one group table entry per launch
 * group (the FP8 kernel's table and argument words, omega_blackwell_codegen_moe.h).
 *
 * Storage (host and device, one contract, both sides use the macros below):
 *   weights: [n][k/2] bytes, row-major. Weight c of a row is the nibble OMEGA_W4_NIBBLE(row_bytes, c):
 *            the LOW nibble of byte c/2 for even c, the HIGH nibble for odd c. A nibble is e2m1:
 *            bit 3 sign, bits 2..1 exponent (bias 1), bit 0 mantissa: 0, 0.5, 1, 1.5, 2, 3, 4, 6.
 *   scales:  [n][k/32] bytes, row-major, E8M0: group g of a row (weights 32 g .. 32 g + 31) is scaled by
 *            2^(E - 127). The upload refuses E > OMEGA_W4_E_MAX and E == 255 (NaN).
 *
 * Numeric scheme (checked exhaustively in tests/gpu_w4_api_test.c):
 *   f32 bits ((n & 7) << 22) | ((n & 8) << 28) equal e2m1(n) * 2^-126 exactly for all 16 codes (exponent 0 gives the
 *   f32 subnormal 0.5 * 2^-126 for codes 1 and 9; the FFMA carries no .FTZ). x' = x * 2^64 (host, exact, the FP8
 *   kernel's fold), so every product is q x 2^-62. A lane's dot over its chunk is scaled once by S' with f32 bits
 *   (E + 62) << 23 = 2^(E - 65): q x 2^-62 * 2^(E - 65) = q x 2^(E - 127). E <= 192 keeps S' finite and normal.
 *
 * Thread mapping (the FP8 kernel's): warp w of CTA (cx, cy) handles row groups rg = cx * warps + w, + stride, ...
 * A row group is `rows` consecutive rows. Per K step lane L reads lane_bytes bytes (2 * lane_bytes weights) of each
 * of its rows at byte L * lane_bytes, the matching 2 * lane_bytes floats of x', and for each row the 32-bit scale
 * word holding its group's byte (lanes sharing a word read it together). Steps KS = k / (64 * lane_bytes).
 * lane_bytes 16: one lane step = one whole 32-weight group; 8: half a group (two lanes share a scale byte).
 *
 * mutant (tests only, never production): 1 drops the scale (acc += dot), 2 never advances the scale pointer,
 * 3 reads group cy ^ 1's table entry, 4 extracts the wrong scale byte of the word (byte (b + 1) & 3), 5 pairs each
 * weight with the other nibble's x (low/high nibble order swapped), 6 (coal only) one output slot set (the round race).
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_W4_GROUP 32u     /* weights per E8M0 scale (OCP MX) */
#define OMEGA_W4_MAX_K 32768u
#define OMEGA_W4_E_MAX 192u    /* largest scale byte the kernel's S' = 2^(E - 65) holds as a normal f32 */
#define OMEGA_W4_SCALE_BIAS 62u /* S' f32 exponent field = E + 62 */

/* weight c of a row from its packed bytes (the one nibble order host and kernel share) */
#define OMEGA_W4_NIBBLE(row_bytes, c) ((uint8_t)(((row_bytes)[(c) >> 1] >> (((c) & 1u) * 4u)) & 0xFu))
#define OMEGA_W4_SET_NIBBLE(row_bytes, c, v) \
    ((row_bytes)[(c) >> 1] = (uint8_t)(((row_bytes)[(c) >> 1] & (0xF0u >> (((c) & 1u) * 4u))) | (((v) & 0xFu) << (((c) & 1u) * 4u))))

typedef struct {
    uint32_t k;          /* multiple of 64 * lane_bytes, <= OMEGA_W4_MAX_K */
    uint32_t lane_bytes; /* 8 or 16 */
    uint32_t rows;       /* 1, 2, 4 or 8 */
    uint32_t warps;      /* 1 .. 32 */
    uint32_t coal;       /* 0: lane 0 stores its rows; 1: coalesced output through shared slots (the FP8 kernel's variant A) */
    uint32_t sched;      /* 0: every fixed-latency op stalls 6; 1: pipelined nibble decode + dependency-driven stalls */
    uint32_t pf;         /* 0: a warp issues one K step's loads, waits, decodes. 1: the weights + scales of step s + 1 (or of the next row
                          * group's step 0) are in flight while step s decodes (scoreboard 4, copied into the decode set when landed);
                          * x' of step s is read just before its decode. 2: x' of step s + 1 too (two full sets, more registers). */
    int mutant;          /* 0 production; 1..6 as above; 7 (pf only): the decode does not wait for its loads */
} OmegaW4GemvSpec;

/* 0 on success (prog solved and register-allocated), -1 on a refused spec or an allocator failure. */
int omega_blackwell_codegen_w4_gemv_prog(const OmegaW4GemvSpec *spec, BlackwellIRProgram *prog);
/* K steps: k / (64 * lane_bytes); 0 when refused. */
uint32_t omega_w4_gemv_steps(const OmegaW4GemvSpec *spec);
/* shared bytes the launch declares: 2 * warps * rows * 4 with coal, else 0 */
uint32_t omega_w4_gemv_shared_bytes(const OmegaW4GemvSpec *spec);
int omega_w4_gemv_prog_stats(const OmegaW4GemvSpec *spec, uint32_t *insns, uint32_t *gprs);

/* e2m1 code -> value, and the kernel's f32 bits for it (value * 2^-126) */
float omega_w4_e2m1_ref(uint8_t nib);
uint32_t omega_w4_e2m1_kernel_bits(uint8_t nib);

#ifdef __cplusplus
}
#endif
#endif
