#ifndef OMEGA_BLACKWELL_CODEGEN_RESIDENT_H
#define OMEGA_BLACKWELL_CODEGEN_RESIDENT_H
/*
 * omega_blackwell_codegen_resident: ONE resident (persistent) kernel that runs a chain of dependent FP8 GEMV stages
 * with an on-chip grid barrier between stages (OWN-30B W-L2f "Proof A", 2026-10-14). New file; nothing else changes.
 *
 * Stage s computes y_s[r] = sum_k W_s[r][k] * x_s[k] * S_s[r/128][k/128] exactly as the variant 1 GEMV
 * (omega_blackwell_codegen_moe.c, depth 0, groups 1): same lane mapping, same e4m3 decode, same per-row FFMA chain,
 * the block-scale fma per step, the SHFL.DOWN 16..1 reduction, lane 0 stores. So every output word is bit-identical to
 * the launched kernel with the same (lane_bytes, rows) whatever CTA or warp computes the row.
 *
 * The stage parameters are not baked in: the kernel walks a STAGE TABLE in device memory (written once by the host,
 * read-only for the GPU) of n_stages descriptors, OMEGA_RES_DESC_BYTES each:
 *   +0  w_va u64   e4m3 weights, row-major [n][k]
 *   +8  s_va u64   f32 scales (x 2^56), [n/128][k/128]
 *   +16 x_va u64   x' (f32, k, packed by omega_fp8_pack_x_f32)
 *   +24 y_va u64   y (f32, n)
 *   +32 k u32, ks u32     (ks = k / (32 * lane_bytes))
 *   +40 rg u32, kb4 u32   (rg = n / rows row groups; kb4 = (k / 128) * 4 bytes per scale row)
 * so one kernel per (lane_bytes, rows, warps, mutant) serves any chain whose k are multiples of 32 * lane_bytes.
 *
 * Grid barrier between stage s and s + 1 (none after the last stage): every thread MEMBAR.ALL.SYS (fence 0), BAR.SYNC;
 * thread 0 ATOMG.ADD +1 on the BARRIER WORD (GPU-cached, GPU scope when scope_gpu = 1) and polls it (bounded by spin_max)
 * until it reaches base + (s + 1) * grid; verdict via shared memory; BAR.SYNC; CCTL.IVALL. Abort (abort = 1): an aborting
 * CTA adds OMEGA_RES_ABORT_UNIT to the barrier word, every waiter sees it at once; only CTA 0 reads the cancel word,
 * batched with its arrive. A spin expiry adds 1 to the control block error word. Full rules and chip costs:
 * docs/own30b/resident-control-block.md.
 *
 * CONTROL BLOCK (u32 words, GPU-uncached memory: the host polls it):
 *   +0x00 magic OMEGA_RES_MAGIC    +0x04 version OMEGA_RES_VERSION    +0x08 n_stages    +0x0c grid
 *   +0x10 progress (CTA 0)        +0x14 error (GPU: spin expiry)   +0x18 cancel (host)   +0x1c spin_max
 * The kernel reads n_stages, grid, spin_max, the barrier word address and base from constant bank arguments.
 *
 * Constant bank 0 argument words (cbank + 0x380):
 *   0x380 ctl u64   0x388 table u64   0x390 n_stages u32   0x394 grid u32   0x398 stride u32 (grid * warps)   0x39c spin_max u32
 *   0x3a0 barrier word u64   0x3a8 base u32
 *
 * Co-residency: every CTA of the grid must be resident at once or the barrier cannot complete. The generator cannot know
 * that; the launcher refuses grids above the measured capacity (omega_gpu_resident_api.c) and the bounded spin turns a
 * mistake into an error word and a finished kernel instead of a hang.
 *
 * mutant (tests only): 1 = no grid barrier at all (no membar, no arrive, no wait): a NEGATIVE CONTROL that must give
 * different output (stage s + 1 reads x before stage s has written it).
 */
#include <stdint.h>
#include "omega_blackwell_codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_RES_MAGIC 0x31445352u /* "RSD1" */
#define OMEGA_RES_VERSION 1u
#define OMEGA_RES_DESC_BYTES 64u
#define OMEGA_RES_MAX_STAGES 4096u

/* control block word offsets (bytes) */
#define OMEGA_RES_CTL_MAGIC 0x00u
#define OMEGA_RES_CTL_VERSION 0x04u
#define OMEGA_RES_CTL_NSTAGES 0x08u
#define OMEGA_RES_CTL_GRID 0x0cu
#define OMEGA_RES_CTL_PROGRESS 0x10u /* r2: barriers passed, written by CTA 0 (r1: the arrive counter itself) */
#define OMEGA_RES_CTL_ERROR 0x14u
#define OMEGA_RES_CTL_CANCEL 0x18u
#define OMEGA_RES_CTL_SPINMAX 0x1cu
#define OMEGA_RES_CTL_BYTES 64u

/* argument words */
#define OMEGA_RES_ARG_CTL 0x380u
#define OMEGA_RES_ARG_TABLE 0x388u
#define OMEGA_RES_ARG_NSTAGES 0x390u
#define OMEGA_RES_ARG_GRID 0x394u
#define OMEGA_RES_ARG_STRIDE 0x398u
#define OMEGA_RES_ARG_SPINMAX 0x39cu
#define OMEGA_RES_ARG_BAR 0x3a0u  /* u64 barrier word (arrive counter) */
#define OMEGA_RES_ARG_BASE 0x3a8u /* u32 counter value at launch start (arrivals of earlier launches) */
#define OMEGA_RES_ARG_STAMP 0x3b0u /* u64 stamp buffer (spec.stamps = 1; W-L2g), 0x3ac is padding */
#define OMEGA_RES_ARGS_WORDS 14u
#define OMEGA_RES_STAMP_BYTES 256u /* per (stage, CTA) record, see spec.stamps */

#define OMEGA_RES_ABORT_UNIT (1u << 23) /* added to the barrier word by an aborting CTA; arrival counts stay below it */
#define OMEGA_RES_MAX_PATCHES 24u
#define OMEGA_RES_PATCH_LDG_GPU 1u
#define OMEGA_RES_PATCH_ATOM_GPU 2u
#define OMEGA_RES_PATCH_MEMBAR_GPU 3u
#define OMEGA_RES_PATCH_CS2R 4u /* S2R placeholder -> CS2R.32 (oracle: w0 0x7805, fixed latency) */
typedef struct { uint32_t n; uint32_t idx[OMEGA_RES_MAX_PATCHES]; uint32_t kind[OMEGA_RES_MAX_PATCHES]; } OmegaResidentPatches;

typedef struct {
    uint32_t lane_bytes; /* 8 or 16 */
    uint32_t rows;       /* 1, 2, 4, 8 */
    uint32_t warps;      /* 2 .. 32 (warp 0 runs the barrier wait) */
    int mutant;          /* 0 production; 1 no barrier (negative control) */
    uint32_t scope_gpu;  /* 0: arrive + poll at system scope (r1); 1: GPU scope (patched words, barrier word in GPU-cached memory) */
    uint32_t fence;      /* before the arrive, every thread: 0 MEMBAR.ALL.SYS, 1 MEMBAR.ALL.GPU, 2 none (DIAGNOSTIC ONLY: unordered) */
    uint32_t abort;      /* 0 (r2): every CTA reads the error + cancel words each barrier; 1 (r4): abort bit in the barrier word, CTA 0 reads cancel */
    uint32_t stamps;     /* W-L2g instrumentation: 1 = per (stage, CTA) timer record (OMEGA_RES_STAMP_BYTES) at OMEGA_RES_ARG_STAMP:
                          *   w0 globaltimer_lo at stage start (thread 0), w1 at CTA done (after BAR.SYNC), w2 clock_lo start,
                          *   w3 clock_lo done, w4 SM id, w8 + warp * 2 (+0 first row, +1 latest later row): globaltimer_lo when
                          *   that warp finished that row (warps <= 28). 0 = production (no stamp code at all). */
    uint32_t prefetch;   /* W-L2g variant: see omega_blackwell_codegen_resident.c (0 = production) */
} OmegaResidentSpec;

int omega_blackwell_codegen_resident_prog(const OmegaResidentSpec *spec, BlackwellIRProgram *prog);
int omega_blackwell_codegen_resident_prog_p(const OmegaResidentSpec *spec, BlackwellIRProgram *prog, OmegaResidentPatches *pt);
int omega_resident_apply_patches(uint8_t *code, size_t bytes, const OmegaResidentPatches *pt);
int omega_resident_prog_stats(const OmegaResidentSpec *spec, uint32_t *insns, uint32_t *gprs);

#ifdef __cplusplus
}
#endif
#endif
