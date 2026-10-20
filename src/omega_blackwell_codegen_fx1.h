#ifndef OMEGA_BLACKWELL_CODEGEN_FX1_H
#define OMEGA_BLACKWELL_CODEGEN_FX1_H
/*
 * omega_blackwell_codegen_fx1: the FP8 e4m3 GEMV reading LOSSLESS-CODED weights (FX1, ledger L274,
 * docs/adr/0002-mathematics-before-compute.md, docs/speed/WORK_PER_TOKEN.md section 4). New file: the raw FP8 GEMV
 * (omega_blackwell_codegen_moe.c) is unchanged and is the reference this kernel must match bit for bit.
 *
 * y[r] = sum_k W[r][k] * x[k] * S[r/128][k/128], m = 1, one tensor per launch (the Qwen3.8-27B graph's FP8 GEMVs are
 * all groups = 1). Same thread mapping, numeric scheme and FFMA order as the moe kernel with lane_bytes 16, depth 0:
 * per K step lane L owns weights [s * 512 + L * 16, +16) of each of its rows; the 16 weights are decoded to the same
 * f32 bits ((b & 0x7F) << 20 | (b & 0x80) << 24 = e4m3(b) * 2^-120), multiplied into one dot in the same order
 * (0..15), dot folded once with the step's scale, then the same SHFL.DOWN reduction. So y is bit-identical whenever
 * the decoded bytes equal the stored ones, which the FX1 code guarantees (lossless).
 *
 * The FX1 code (host encoder: omega_gpu_fx1_api.c; whole-checkpoint round trip: tools/wpt_fxcodec.c):
 *   nibble plane  q = (b >> 4 & 8) | (b & 7) (sign, mantissa) for every weight, two per byte, low nibble first, row-major:
 *                 lane L, step s, row r reads 8 bytes at r * k / 2 + s * 256 + L * 8 (LDG.E.64, fixed address).
 *   exponent      e = (b >> 3) & 15 per weight, canonical Huffman code per tensor, lengths <= 8. Per row one region:
 *                 the 32 lane streams (MSB first, in consumption order) interleaved by 32-bit word, word j of lane L at
 *                 region + (j * 32 + L) * 4, each padded to the row's longest stream. The tensor ends with 4 zero word
 *                 rows so a lane's look-ahead load stays inside the buffer.
 *   row table     u32 per row: the row's region offset in 32-bit words from the exponent base.
 *   lut           256 u32, indexed by the next 8 bits of a stream: (e << 23) | length. Staged in shared memory once
 *                 per CTA (one round of 32 * warps threads, zero padded to max(256, 32 * warps) words).
 * Decode per lane and row, bit position p (starts at 0 per row): per group of 4 codes (at most 32 bits)
 *   i = p >> 5, o = p & 31, window = (w[i] << o) + hi32(w[i + 1] * 2^o)   (two LDG.E, SHF.L, IMAD.HI)
 *   per code: ent = lut[window >> 24]; len = ent & 0xff; window <<= len; p += len; f = (ent & 0x07800000)
 *             + the sign and mantissa of the nibble at their f32 places.
 *
 * Constant bank 0 argument words (cbank + 0x380):
 *   0x380 nib  u64  nibble plane        0x388 exp  u64  exponent base      0x390 rows u64  row table (u32 per row)
 *   0x398 lut  u64  256 (padded) u32    0x3a0 s    u64  scales (f32 * 2^56, the moe layout)
 *   0x3a8 x    u64  x' (f32 * 2^64)     0x3b0 y    u64  y (f32, n)
 *   0x3b8 RG   u32  row groups (n / rows)      0x3bc stride u32 row groups per warp iteration (grid_x * warps)
 * mutant (tests only): 1 decodes every exponent as the table's first symbol (wrong bytes: parity must fail),
 *   2 never advances the bit position across groups (wrong whenever a code is not of length 0: must fail).
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_FX1_LMAX 8u
#define OMEGA_FX1_LANE_BYTES 16u
#define OMEGA_FX1_TAIL_WORD_ROWS 4u
#define OMEGA_FX1_ARG_NIB 0x380u
#define OMEGA_FX1_ARG_EXP 0x388u
#define OMEGA_FX1_ARG_ROWS 0x390u
#define OMEGA_FX1_ARG_LUT 0x398u
#define OMEGA_FX1_ARG_S 0x3a0u
#define OMEGA_FX1_ARG_X 0x3a8u
#define OMEGA_FX1_ARG_Y 0x3b0u
#define OMEGA_FX1_ARG_RG 0x3b8u
#define OMEGA_FX1_ARG_STRIDE 0x3bcu
#define OMEGA_FX1_ARGS_WORDS 16u

typedef struct {
    uint32_t k;     /* input columns: multiple of 512 and of 128, <= 32768 */
    uint32_t rows;  /* rows per warp: 1 or 2 (instruction budget: about 300 instructions per row per step) */
    uint32_t warps; /* warps per CTA: 1 .. 32 */
    int mutant;     /* 0 production; 1, 2 as above */
} OmegaFx1GemvSpec;

/* LUT words the kernel stages (and the device buffer must hold): max(256, 32 * warps). */
uint32_t omega_fx1_lut_words(uint32_t warps);
/* Shared memory bytes of one CTA. */
uint32_t omega_fx1_shared_bytes(const OmegaFx1GemvSpec *spec);
/* 0 on success (prog solved and register-allocated), -1 on a refused spec or an allocator failure. */
int omega_blackwell_codegen_fx1_gemv_prog(const OmegaFx1GemvSpec *spec, BlackwellIRProgram *prog);
int omega_fx1_gemv_prog_stats(const OmegaFx1GemvSpec *spec, uint32_t *insns, uint32_t *gprs);

#ifdef __cplusplus
}
#endif
#endif
