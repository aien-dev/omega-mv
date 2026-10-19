#ifndef OMEGA_BLACKWELL_CODEGEN_ROUTER_FN_H
#define OMEGA_BLACKWELL_CODEGEN_ROUTER_FN_H
/*
 * omega_blackwell_codegen_router_fn: the Qwen3.8-Flash-Next MoE router in ONE launch per layer (flashnext/port,
 * 2026-10-19). New file; the Qwen3-30B router (omega_blackwell_codegen_router.c) is untouched and this one keeps its
 * phases, tricks and argument style. Model rule (transformers Qwen4ExpTextTopKRouter + SparseMoeBlock, read as the spec):
 * logits = W x over 512 experts (bf16 weights, f32), softmax in f32, top 10, weights renormalised when norm_topk_prob
 * (the default, on for this checkpoint), plus the always-on shared expert scaled by sigmoid(shared_expert_gate . x).
 * The router input x is the 4-lane gated residual's block input (no RMSNorm here: the residual mixer normalised it).
 *
 * Outputs, so the next launches need no host round trip:
 *   - x'[2560] f32 = x * out_scale (2^64: the NV4 GEMV x' row);
 *   - ids[10] u32 (descending logit, the lowest id first on a tie) and wts[11] f32: the 10 routing weights, then
 *     sigmoid(shared gate logit) as the 11th (the combine weights of the 11 down outputs);
 *   - two group tables of 11 entries (gate|up and down): entry g < 10 = directory[ids[g]], entry 10 = directory[512]
 *     (the shared expert's fixed slot). Directories: 513 x {w_va, s_va} (omega_gpu_nv4_slab_entry of slots 0..512).
 *
 * Grid 1 x 1, `warps` warps (8 or 16). Phases:
 *   1  thread t copies x[t + i * threads] (i < 2560 / threads) to shared and stores x * out_scale to x'.
 *   2  warp w computes rows w, w + warps, ... of the 513-row weight (512 experts, then the shared gate row): lane L
 *      streams bytes [L * 16, +16) of each 256-column step (10 steps, `depth` in flight), widens the bf16 pairs,
 *      FFMAs against x from shared memory, warp-reduces, lane 0 stores the logit to shared memory.
 *   3  every warp redundantly: lane L holds logits 16 L .. 16 L + 15; 10 rounds of argmax (warp max, then the lowest
 *      index holding it by the float 2^23 + index trick), the winner masked to -1e30. Then the full 512-way softmax sum
 *      (the denominator when norm_topk is off) and e_r = EX2((l_r - l_0) log2 e) / den.
 *   epilogue warp 0: lane 0 stores the 11 weights; lanes 0..10 store their id (lanes < 10) and copy their directory
 *      entry into both tables.
 */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#define OMEGA_RFN_HIDDEN 2560u
#define OMEGA_RFN_EXPERTS 512u
#define OMEGA_RFN_ROWS 513u              /* 512 experts + the shared expert gate row */
#define OMEGA_RFN_TOPK 10u
#define OMEGA_RFN_GROUPS 11u             /* 10 routed + the shared expert */
#define OMEGA_RFN_STEPS 10u              /* 256 bf16 columns per step */
#define OMEGA_RFN_ROW_BYTES (OMEGA_RFN_HIDDEN * 2u)
#define OMEGA_RFN_DIR_ENTRIES 513u
#define OMEGA_RFN_TAB_BYTES (OMEGA_RFN_GROUPS * 16u)

#define OMEGA_RFN_ARG_X 0x380u       /* f32 [2560] router input */
#define OMEGA_RFN_ARG_GATE 0x388u    /* bf16 [513][2560]: mlp.gate.weight rows then mlp.shared_expert_gate.weight */
#define OMEGA_RFN_ARG_XOUT 0x390u    /* f32 [2560] x * out_scale */
#define OMEGA_RFN_ARG_IDS 0x398u     /* u32 [10] */
#define OMEGA_RFN_ARG_WTS 0x3a0u     /* f32 [11] */
#define OMEGA_RFN_ARG_DIR_GU 0x3a8u  /* u64 [513][2] */
#define OMEGA_RFN_ARG_DIR_DN 0x3b0u  /* u64 [513][2] */
#define OMEGA_RFN_ARG_TAB_GU 0x3b8u  /* u64 [11][2] */
#define OMEGA_RFN_ARG_TAB_DN 0x3c0u  /* u64 [11][2] */
#define OMEGA_RFN_ARG_OUT_SCALE 0x3c8u /* f32 */
#define OMEGA_RFN_ARG_NORM_TOPK 0x3ccu /* u32 0 or 1 */
#define OMEGA_RFN_ARGS_WORDS 20u

#define OMEGA_RFN_SM_XN 0u          /* x [2560] f32: 10240 */
#define OMEGA_RFN_SM_LOGIT 10240u   /* logits [513] f32 (+ pad): 2064 */
#define OMEGA_RFN_SM_BC 12304u      /* broadcast words: 16 */
#define OMEGA_RFN_SM_SEL 12320u     /* selected ids u32 [10] (+ pad): 48 */
#define OMEGA_RFN_SM_SELV 12368u    /* selected logits f32 [10] (+ pad): 48 */
#define OMEGA_RFN_SHARED_BYTES 12416u

typedef struct {
    uint32_t warps; /* 8 or 16 */
    uint32_t depth; /* weight steps in flight per warp: 1, 2, 5 or 10 */
    int mutant;     /* TEST ONLY. 0 none; 2 the mask after each round removes id ^ 1; 3 directory entries of id ^ 1;
                     * 4 weights not normalised (den = 1); 5 the shared slot copies directory[511]; 6 the shared weight is
                     * the raw logit, not its sigmoid; 7 x' not scaled (out_scale ignored) */
} OmegaRouterFnSpec;

int omega_blackwell_codegen_router_fn_prog(const OmegaRouterFnSpec *spec, BlackwellIRProgram *prog);
int omega_router_fn_prog_stats(const OmegaRouterFnSpec *spec, uint32_t *insns, uint32_t *gprs);

#endif
