#ifndef OMEGA_BLACKWELL_CODEGEN_PISTON_H
#define OMEGA_BLACKWELL_CODEGEN_PISTON_H
/* omega_blackwell_codegen_piston.h (L312, 2026-10-20): the chip piston of the engine. One resident launch fires `beats`
 * beats on `slots` cylinder buffers the host fills while it runs. Per beat b:
 *   1. thread 0 of every CTA spins (strong, system scope) until fill[b] >= b + 1: the host's "this slot is full" word;
 *   2. CTA 0 thread 0 stamps the chip clock (%globaltimer, low 32 bits, ns) at stamp[2b]   (fire start);
 *   3. every thread streams its share of slot b % slots (16-byte loads, XOR into one word: the read the math will do);
 *   4. software grid barrier (every CTA must be resident; see the ceil resid probe);
 *   5. CTA 0 thread 0 stamps stamp[2b + 1] (fire done), then writes done[b] = b + 1: the slot may be refilled.
 * The bytes of one slot are grid x threads x 128 x iters. Chip idle before beat b (b > 0) is stamp[2b] - stamp[2b - 1]: the
 * time the math waited for fuel plus the flag round trip. The XOR per thread lets the host check every byte was read.
 * args: 0x380 px (slot 0; slot s at px + s * slot bytes), 0x388 py (u32 per thread), 0x390 pc (u32 barrier counter, zero
 *       before the launch), 0x398 pfill (u32[beats]), 0x3a0 pdone (u32[beats]), 0x3a8 pstamp (u32[2 * beats]).
 * Returns 0, or -1 for bad arguments or an IR/allocator failure. */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int omega_blackwell_codegen_piston_prog(uint32_t iters, uint32_t threads, uint32_t grid, uint32_t beats, uint32_t slots,
                                        BlackwellIRProgram *prog);
/* Bytes of one cylinder slot for the shape above. */
static inline uint64_t omega_piston_slot_bytes(uint32_t iters, uint32_t threads, uint32_t grid) {
    return (uint64_t)grid * threads * 128u * iters;
}

#ifdef __cplusplus
}
#endif
#endif
