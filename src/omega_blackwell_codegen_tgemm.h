#ifndef OMEGA_BLACKWELL_CODEGEN_TGEMM_H
#define OMEGA_BLACKWELL_CODEGEN_TGEMM_H
/*
 * omega_blackwell_codegen_tgemm: device-resident bf16 training GEMM on the tensor cores (trainer lane E1, 2026-10-16).
 * New file. C[M][N] (row-major) = A[M][K] bf16 x B[K][N] bf16 with the operands stored as the trainer has them (no per-call host
 * pack): layout NN (A[M][K], B[K][N]: the backward dX = dY W), NT (A[M][K], Bt[N][K]: the forward Y = X W^T with W[N][K]),
 * TN (At[K][M], B[K][N]: the weight gradient dW = dY^T X). Epilogues: F32 overwrite, F32 accumulate (C += A B, one FADD per
 * output after the HMMA chain, round to nearest), BF16 overwrite (F2FP.BF16.F32.PACK_AB, round to nearest even). Fragment
 * layout, emitter rules (ldw_pf scoreboards 3/4, hmma stall 8, nop 15 + nop 14 before the first accumulator read) and the
 * double-buffered shared staging are the chip-proven ones of omega_blackwell_codegen_tc2 (OWN30B-L1G-GEMM-1e76e728).
 *
 * K is walked in 128-wide blocks of four 32-wide chunks. Inside a 32-wide chunk lane t (lane % 4) of every HMMA owns
 * k = 8t + {0..7}: HMMA step s (0, 1) of the chunk puts k = 8t + 4s + {0, 1} at fragment positions (2t, 2t + 1) and
 * k = 8t + 4s + {2, 3} at (2t + 8, 2t + 9). The 16-term HMMA sum is order independent (tests/bw_hmma_model.h), so any
 * consistent lane -> k assignment is exact; this one makes every A row word and every B row word contiguous in memory.
 *  - A tile (16 rt rows x 32 k) is staged through shared once per CTA (every warp reads the same A fragments): per row tile i
 *    and step s the 32 lanes' 16-byte fragment slots lie at (i * 2 + s) * 512; BAR.SYNC, LDS.128 per row tile. Two buffers of
 *    16 rt x 32 x 2 bytes: 2048 rt bytes of shared (rt 8: 16 KB, the QMD cap).
 *      NN / NT: LDG.E.64 pieces (4 bf16 of one row of A), STS32 x 2 into the slot.
 *      TN: a thread loads 4 consecutive m of rows k0 and k0 + 1 of At (two LDG.E.64), packs the (k0, k0 + 1) half words of each
 *          m with LOP3 / SHF / IADD3 (the fragment word holds two consecutive k of one m) and stores the four words (STS32 x 4).
 *  - B (each warp its own 16 columns, so straight from global, two register sets on scoreboards 3 and 4 so the next chunk's B
 *    is in flight during the HMMAs). Column tile E holds the warp's even physical columns (n0w + 2 g), tile O the odd ones
 *    (n0w + 2 g + 1), so a lane's four output words of a row are physically consecutive (columns 4 t .. 4 t + 3).
 *      NN / TN (B[K][N], N contiguous): per step four LDG.E.64 at rows k = 8t + 4s + j, columns n0w + 4 (g / 2) .. + 3; the lane
 *          needs the half word (g % 2), selected with one LOP3 per row word; lanes g, g ^ 1 touch the same 8 bytes. IMAD and
 *          SHF + IADD3 pack the two column tiles (IMAD.HI.U32 would be one instruction, but its Rc is a 64-bit pair on the chip).
 *      NT (Bt[N][K], K contiguous): per step two LDG.E.64, Bt[n0w + 2g + h][8t + 4s .. + 3], already in fragment order.
 *  - Epilogue: the tile leaves through shared memory in 8-row passes (lane (g, t) parks its words of row g at the pass columns
 *    n0w + 4 t .. + 3, BAR.SYNC, then thread tid reads words tid + T j and stores them: every warp STG writes 128 contiguous
 *    bytes). Storing straight from the fragment layout with 4-byte STG touched 16 half-filled sectors per instruction and cost
 *    more than half of the kernel time at K 768 (chip probe, no-store mutant 5). F32_ACC loads the old words of the pass
 *    (LDG.E, scoreboard 3, issued before the barriers) and adds them after the LDS. BF16 packs the lane's word pairs before the
 *    STS (two words per row per lane). Store immediates stay under the encoder's 2^23 for N up to 2^17 (50304: the lm_head).
 *
 * Tile table entry (32 bytes, indexed by the tile id): { c_va u64 (output base for this tile's K part), row0 u32, kb0 u32 (first 128-block),
 * nkb u32 (blocks, >= 1), pad u32, pad u64 }. Constant bank 0 (cbank + 0x380): tiles u64, a u64, b u64.
 * Grid: grid_x = n / span (span = warps * 8 * ct), grid_y = tiles (tfast 1: the two swapped); CTA 32 * warps threads; rows of a
 * tile are always 16 rt (the caller pads M to a multiple of the tile height).
 * Split-K (deterministic, no atomics): tiles of the same rows with different kb0 / nkb write f32 partials to separate buffers;
 * omega_blackwell_codegen_tgemm_reduce_prog sums them in a fixed order (old C first when accumulating, then partial 0, 1, ...),
 * one FADD per term, and writes f32 or bf16. Results are bit-identical run to run.
 * mutant (tests only): 1 drops HMMA step 1 of the last chunk of every block, 2 never advances the A pointer to the next
 * block, 3 row tile i reads tile i ^ 1, 4 swaps the column parity (tile E takes the odd columns), 5 stores nothing (timing probe),
 * 6 F32_ACC stores the sum without the old value, 7 BF16 packs the pair swapped. Reduce: 8 skips the last partial, 7 as above.
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif
enum { OMEGA_TGEMM_LAYOUT_NN = 0, OMEGA_TGEMM_LAYOUT_NT = 1, OMEGA_TGEMM_LAYOUT_TN = 2 };
enum { OMEGA_TGEMM_EPI_F32 = 0, OMEGA_TGEMM_EPI_F32_ACC = 1, OMEGA_TGEMM_EPI_BF16 = 2 };
typedef struct {
    uint32_t k;      /* multiple of 128, 128 .. 65536 (the per-tile K range is kb0 .. kb0 + nkb blocks of 128) */
    uint32_t n;      /* output width, a multiple of span, < 2^17 */
    uint32_t rt;     /* row tiles of 16 per CTA: 2, 4 or 8 */
    uint32_t ct;     /* column tiles of 8 per warp: 2 */
    uint32_t warps;  /* 8 or 16 */
    uint32_t kc;     /* k per chunk: 32 */
    uint32_t aqn;    /* A fragments in flight: 1 or 2 */
    uint32_t layout; /* OMEGA_TGEMM_LAYOUT_* */
    uint32_t epi;    /* OMEGA_TGEMM_EPI_* */
    uint32_t m;      /* rows of C (the row stride of At for TN; multiple of 16 rt, < 2^17) */
    uint32_t tfast;  /* 0: ctaid.x is the column block, ctaid.y the tile (CTAs of one A tile run together); 1: swapped (CTAs of one
                        B slab run together: the right order when B is the larger operand, e.g. the lm_head forward, N 50304) */
    int mutant;
} OmegaTgemmSpec;
typedef struct {
    uint32_t splits;  /* partial buffers summed: 2 .. 16 */
    uint32_t epi;     /* OMEGA_TGEMM_EPI_*: F32 / F32_ACC / BF16 output */
    uint32_t pstride; /* bytes between partial buffers (m * n * 4, a multiple of 4096) */
    int mutant;
} OmegaTgemmReduceSpec;
#define OMEGA_TGEMM_ARGS_WORDS 6u
#define OMEGA_TGEMM_TILE_BYTES 32u
#define OMEGA_TGEMM_MAX_INSNS 1000u
#define OMEGA_TGEMM_REDUCE_THREADS 256u
#define OMEGA_TGEMM_REDUCE_WORDS_PER_CTA 1024u /* f32 words of the output per reduce CTA (two pairs per thread) */
#define OMEGA_TGEMM_REDUCE_ARGS_WORDS 4u       /* out u64, partials u64 */
int omega_tgemm_spec_ok(const OmegaTgemmSpec *s);
uint32_t omega_tgemm_shared_bytes(const OmegaTgemmSpec *s);
uint32_t omega_tgemm_span(const OmegaTgemmSpec *s);
uint32_t omega_tgemm_bm(const OmegaTgemmSpec *s); /* rows per tile */
/* -1: refused (spec, emitter, allocator, or more than OMEGA_TGEMM_MAX_INSNS instructions / 128 declared registers) */
int omega_blackwell_codegen_tgemm_prog(const OmegaTgemmSpec *s, BlackwellIRProgram *prog);
int omega_tgemm_reduce_spec_ok(const OmegaTgemmReduceSpec *r);
int omega_blackwell_codegen_tgemm_reduce_prog(const OmegaTgemmReduceSpec *r, BlackwellIRProgram *prog);
#ifdef __cplusplus
}
#endif
#endif
