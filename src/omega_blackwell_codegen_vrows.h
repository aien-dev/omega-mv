#ifndef OMEGA_BLACKWELL_CODEGEN_VROWS_H
#define OMEGA_BLACKWELL_CODEGEN_VROWS_H
/*
 * omega_blackwell_codegen_vrows: the row copy kernel of the batched exact verify (OWN-30B W-L4f, 2026-10-14). New file.
 *
 * A pure data move (LDG.128 then four STG.32 of the same bits; no arithmetic on the values), so a row it copies is
 * bit-identical to its source. It lets the verify list feed the FP8 grouped GEMM (omega_blackwell_codegen_gemm.h) with
 * rows the decode glue kernels wrote, in the GEMM's order and lane layout, without a host round trip:
 *
 *   dst row r (r = ctaid.y), 16-byte unit q (q = ctaid.x * 256 + tid, q < units):
 *     src row s = idx[r]                         (u32 table: a gather; the identity table copies in order)
 *     plain (lane_bytes 0):  dst[r][q] = src[s][q]
 *     GEMM layout (lane_bytes LB = 8 or 16): the K step of 32 * LB columns holds U = 8 * LB units; unit q = step * U
 *       + w * 32 + L (word w < LB / 4, lane L < 32) takes source unit step * U + L * (LB / 4) + w. This is
 *       omega_fp8gemm_permute_x without the 2^64 factor: the decode glue already wrote x' * 2^64 (RES NORM, the router,
 *       SWIGLU and the decode attention all take the 2^64 scale), so the copy only moves words.
 *   dst rows are dense: units * 16 bytes apart. src rows are sstride bytes apart (multiple of 16).
 *
 * Constant bank 0 argument words (cbank + 0x380):
 *   0x380 dst u64, 0x388 src u64, 0x390 idx u64 (u32 per dst row), 0x398 units u32, 0x39c sstride u32 (bytes).
 * Grid: grid_x = ceil(units / 256), grid_y = rows; 256 threads. Threads past `units` exit before any memory access.
 *
 * mutant (tests only): 1 = the gather table is ignored (src row = r) and the lane layout is not applied (plain copy).
 * The chip test must catch it.
 */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_VROWS_THREADS 256u
#define OMEGA_VROWS_DST 0x380u
#define OMEGA_VROWS_SRC 0x388u
#define OMEGA_VROWS_IDX 0x390u
#define OMEGA_VROWS_UNITS 0x398u
#define OMEGA_VROWS_SSTRIDE 0x39cu
#define OMEGA_VROWS_WORDS 8u

typedef struct {
    uint32_t lane_bytes; /* 0 plain, 8 or 16: the GEMM lane layout */
    int mutant;          /* TEST ONLY, see the header comment; 0 none */
} OmegaVrowsSpec;

int omega_blackwell_codegen_vrows_prog(const OmegaVrowsSpec *spec, BlackwellIRProgram *prog);
/* host reference of one row: the source unit index of destination unit q */
uint32_t omega_vrows_src_unit(uint32_t lane_bytes, uint32_t q);

#ifdef __cplusplus
}
#endif
#endif
