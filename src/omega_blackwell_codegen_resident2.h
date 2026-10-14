#ifndef OMEGA_BLACKWELL_CODEGEN_RESIDENT2_H
#define OMEGA_BLACKWELL_CODEGEN_RESIDENT2_H
/*
 * omega_blackwell_codegen_resident2: the resident kernel with DECODE STAGE KINDS (OWN-30B W-L2h, 2026-10-14). New file; the
 * W-L2f/L2g kernel (omega_blackwell_codegen_resident.c) is unchanged and stays byte-identical.
 *
 * Why: a real decode token has no two GEMVs back to back (glue between every pair), and a 1-stage resident GEMV is no faster
 * than a launched one (chip r1, evidence/OWN30B-L2H-RESIDENT-DECODE-20261014/r1-*). The time is saved only across barriers,
 * so the glue has to run inside the resident kernel too. This kernel runs, per stage, one of:
 *
 *   kind 0 GEMV16      y = W . x', lane_bytes 16       (instruction for instruction the variant 1 depth-0 rows-1 body,
 *   kind 3 GEMV8       y = W . x', lane_bytes 8         as in the W-L2f kernel; arithmetic order per row unchanged)
 *   kind 4 GEMV16_ADD  GEMV16, then lane 0 also does add[r] = add[r] + y[r]   (the decode graph's RES ADD, FADD)
 *   kind 1 SWIGLU      act[g][i] = silu(gu[g][i]) * gu[g][I + i] * scale      (omega_blackwell_codegen_dec.c gen_swiglu)
 *   kind 2 RESNORM     CTA 0 only: x += sum_g wts[g] * dn[g]; y = RMSNorm(x) * nw * scale
 *                      (gen_res with mode COMBINE | NORM, 256 threads = 8 warps, same reduction tree)
 *
 * GEMV stages are GROUPED: G groups (experts) of n rows each, group g's weights from an entry table (tab + 16 g = { w_va, s_va },
 * the layout the router writes, omega_blackwell_codegen_moe.h), x for group g at x + g * xgs, y for group g at y + g * n * 4.
 * The grid's warps are split evenly across the groups: wpg = warps per group (host: grid * warps / G, a stage field, so the
 * table is built for one grid); warp gw serves group g = gw / wpg rows t, t + wpg, ... (t = gw mod wpg). One entry load per stage.
 *
 * DESCRIPTOR v2 (OMEGA_RES2_DESC_BYTES = 128, little-endian):
 *   GEMV*:  +0 tab u64   +8 x u64   +16 y u64   +24 add u64 (kind 4)   +32 k u32, ks u32   +40 n u32, kb4 u32
 *           +48 kind u32, G u32   +56 xgs u32 (bytes), wpg u32   +64 inline entry { w, s } (tab may point here)
 *   SWIGLU: +8 gu u64   +16 act u64   +48 kind, G   +80 scale f32, I u32
 *   RESNORM:+0 x u64   +8 dn u64 ([8][2048])   +16 y u64   +24 wts u64 ([8])   +64 nw u64   +48 kind   +80 eps f32, scale f32
 * Barrier, control block, argument words, abort and spin rules: exactly omega_blackwell_codegen_resident.h (scope_gpu = 1,
 * fence 0, abort 1 are fixed here). warps must be 8 (RESNORM's 256-thread mapping). Shared memory: 128 bytes (RESNORM's
 * per-warp partials at 0..31, the barrier verdict word at 64).
 *
 * Registers (W-L2i): peak 61, declared 64, so 4 CTAs of 8 warps fit per SM and grid 192 (1536 warps) runs. The ctl pointer,
 * barrier target and SWIGLU stride come from the constant bank where used, and the e4m3 decode reuses two temporaries.
 *
 * mutant (tests only): 1 = no grid barrier (NEGATIVE CONTROL).
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"
#include "omega_blackwell_codegen_resident.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_RES2_DESC_BYTES 128u
#define OMEGA_RES2_SHARED_BYTES 128u

enum { OMEGA_RES2_GEMV16 = 0, OMEGA_RES2_SWIGLU = 1, OMEGA_RES2_RESNORM = 2, OMEGA_RES2_GEMV8 = 3, OMEGA_RES2_GEMV16_ADD = 4 };

#define OMEGA_RES2_D_TAB 0u
#define OMEGA_RES2_D_X 8u
#define OMEGA_RES2_D_Y 16u
#define OMEGA_RES2_D_ADD 24u
#define OMEGA_RES2_D_K 32u
#define OMEGA_RES2_D_KS 36u
#define OMEGA_RES2_D_N 40u
#define OMEGA_RES2_D_KB4 44u
#define OMEGA_RES2_D_KIND 48u
#define OMEGA_RES2_D_G 52u
#define OMEGA_RES2_D_XGS 56u
#define OMEGA_RES2_D_WPG 60u
#define OMEGA_RES2_D_ENTRY 64u /* GEMV inline entry; RESNORM nw */
#define OMEGA_RES2_D_F0 80u    /* SWIGLU scale | RESNORM eps */
#define OMEGA_RES2_D_F1 84u    /* SWIGLU I     | RESNORM scale */

typedef struct {
    uint32_t warps; /* must be 8 */
    int mutant;     /* 0 production; 1 no barrier */
} OmegaResident2Spec;

int omega_blackwell_codegen_resident2_prog_p(const OmegaResident2Spec *spec, BlackwellIRProgram *prog, OmegaResidentPatches *pt);
int omega_resident2_prog_stats(const OmegaResident2Spec *spec, uint32_t *insns, uint32_t *gprs);

#ifdef __cplusplus
}
#endif
#endif
