/* omega_blackwell_codegen_ceil.h: ceiling probes for the GB10 (L151 machine ceiling map, 2026-10-19).
 * Three small kernels that each push one unit of the chip as hard as one instruction form allows, and leave a result
 * the host can check exactly (a fast wrong run must not count):
 *   ffma_peak  every thread runs `chains` independent FFMA chains acc = acc * 1.0 + 1.0 for `iters` rounds, then
 *              stores each acc (expected exactly iters, iters <= 2^24). 2 flop per FFMA per thread.
 *              `stall` is the issue stall per FFMA; chains * stall must cover the FFMA latency (refused below 4).
 *   lds_bw     each thread stores 32 words (word index i at word i) to shared memory, BAR.SYNC, then `iters` rounds of
 *              8 LDS.128 per thread (thread t, load q reads words 4t + 4q*threads .. +3) summed with IADD3; stores the
 *              sum (expected iters * sum of the 32 word indices it reads, mod 2^32). Shared bytes per CTA = threads * 128.
 *   stg_stream each thread writes `iters` rounds of 8 coalesced 32-bit stores, address advancing threads*32 bytes per
 *              round inside its CTA's own region (iters * threads * 32 bytes); value = rounds left (iters .. 1).
 * Constant bank arguments: ffma_peak / lds_bw: 0x380 = output; stg_stream: 0x380 = region base. */
#ifndef OMEGA_BLACKWELL_CODEGEN_CEIL_H
#define OMEGA_BLACKWELL_CODEGEN_CEIL_H
#include "omega_blackwell_codegen.h"
#include <stdint.h>

int omega_blackwell_codegen_ffma_peak_prog(uint32_t iters, uint32_t chains, uint32_t stall, uint32_t threads, BlackwellIRProgram *prog);
int omega_blackwell_codegen_lds_bw_prog(uint32_t iters, uint32_t threads, BlackwellIRProgram *prog);
int omega_blackwell_codegen_stg_stream_prog(uint32_t iters, uint32_t threads, BlackwellIRProgram *prog);

#endif
