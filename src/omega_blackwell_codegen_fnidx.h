#ifndef OMEGA_BLACKWELL_CODEGEN_FNIDX_H
#define OMEGA_BLACKWELL_CODEGEN_FNIDX_H
/*
 * omega_blackwell_codegen_fnidx: the Qwen3.8-Flash-Next QSA indexer on the chip (flashnext/port, 2026-10-19). New file.
 * Model rule (crates/aien-inference-abi/src/flashnext.rs `indexer_select`, read as the spec): for the new token at position p
 * (visible = p + 1 tokens, nb = visible / 4 complete blocks of 4), the pooled block keys kb_b (mean of the block's 4 raw
 * index keys, zero-centred RMSNorm with k_layernorm, RoPE at the block's first position) are scored against the 4 index
 * query heads q_h (already normalised and rotated): score_b = (sum_h max(0, q_h . kb_b)) / sqrt(128). The 512 highest
 * blocks (ties: the lower index wins) are attended, in ascending order, 4 positions each, followed by the incomplete tail
 * block's positions nb * 4 .. p. For nb <= 512 every block is attended (the list is every position).
 *   SCORE   grid ceil(nb / 256), 256 threads: A = q f32 [4][128], B = kb f32 [nb][128], C = scores f32 [nb];
 *           ARG_NB = nb, ARG_SCALE = 1/sqrt(128). Thread = block.
 *   SELECT  grid 1, 256 threads: A = scores f32 [nb] (non-negative: the order of the bit patterns is the order of the values),
 *           B = plist u32 out [4 K + tail]; ARG_NB, ARG_K = min(nb, 512), ARG_VIS = visible. Exact, branch-free counting:
 *           (1) the K-th largest key T by a 31-step bitwise search, each step a block-wide count of keys >= candidate;
 *           (2) per-thread chunk counts of keys > T and == T, prefix sums over the 256 chunks; the first (K - count(> T))
 *           blocks with key == T by index are taken; (3) every thread writes its chunk's selected blocks ascending at
 *           the slot given by the prefix; (4) threads 0..2 append the tail positions after the 4 K block positions.
 *           Every list is a pure function of the scores: the test compares it with a host sort of the same scores.
 */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#define OMEGA_FNIDX_DIM 128u
#define OMEGA_FNIDX_HEADS 4u
#define OMEGA_FNIDX_RATIO 4u
#define OMEGA_FNIDX_BUDGET_BLOCKS 512u
#define OMEGA_FNIDX_THREADS 256u
#define OMEGA_FNIDX_MAX_BLOCKS 65536u /* 262144 / 4: the model's whole context */
#define OMEGA_FNIDX_ARG_A 0x380u
#define OMEGA_FNIDX_ARG_B 0x388u
#define OMEGA_FNIDX_ARG_C 0x390u
#define OMEGA_FNIDX_ARG_NB 0x398u
#define OMEGA_FNIDX_ARG_K 0x39cu
#define OMEGA_FNIDX_ARG_VIS 0x3a0u
#define OMEGA_FNIDX_ARG_SCALE 0x3a4u
#define OMEGA_FNIDX_ARGS_WORDS 10u
#define OMEGA_FNIDX_SHARED_BYTES 4096u

typedef enum { OMEGA_FNIDX_SCORE = 0, OMEGA_FNIDX_SELECT = 1 } OmegaFnIdxPhase;
#define OMEGA_FNIDX_PHASES 2u

typedef struct {
    uint32_t phase;
    int mutant; /* TEST ONLY. SCORE: 1 no relu, 2 sum of the first 3 heads only, 3 scale ignored.
                 * SELECT: 4 ties go to the higher index, 5 the threshold search stops one bit early (bit 0 never tried),
                 *         6 the tail is not appended, 7 selected blocks written descending by chunk (chunk order reversed) */
} OmegaFnIdxSpec;

uint32_t omega_fnidx_grid(uint32_t phase, uint32_t nb);
int omega_blackwell_codegen_fnidx_prog(const OmegaFnIdxSpec *spec, BlackwellIRProgram *prog);
int omega_fnidx_prog_stats(const OmegaFnIdxSpec *spec, uint32_t *insns, uint32_t *gprs);
#endif
