#ifndef OMEGA_BLACKWELL_CODEGEN_CEIL_H
#define OMEGA_BLACKWELL_CODEGEN_CEIL_H
/*
 * omega_blackwell_codegen_ceil: microbenchmark kernels that measure the GB10's own ceilings for one-token decode
 * (L272, 2026-10-20; docs/speed/GB10_CEILINGS.md). New file; reuses the chip-proven emitter rules of omega_bw_em.h.
 *
 *   stream   every thread reads 8 x 16 bytes per iteration with LDG.E.128 (the decode GEMV's access shape: wide, coalesced,
 *            8 loads in flight per thread) and XORs them into one word, stored at py[gid]. The byte offset of load q in
 *            iteration i is ((base + gid * 16 + (8 i + q) * step) & mask), step = grid * threads * 16, so mask = 2^k - 1
 *            keeps the whole grid inside a 2^k-byte footprint (L2 size and bandwidth sweep) and mask = 0xffffffff streams
 *            distinct bytes (DRAM read bandwidth). phases > 1 splits the iterations into phases joined by a software grid
 *            barrier (the persistent-kernel boundary; every CTA must be resident: see resid).
 *            args: 0x380 px, 0x388 py, 0x390 pcnt (u32 counter, zero before the launch; phases > 1 only), 0x398 base (u32).
 *   chase    one warp follows a pointer chain p = *p, iters loads (iters even), and stores the last pointer at py: load-to-use
 *            latency of the memory the chain lives in (DRAM through an uncached mapping, L2 through a cached one).
 *            args: 0x380 p0 (the first node's GPU address), 0x388 py.
 *   resid    thread 0 of every CTA adds 1 to *pcnt, re-reads its own py[cta] spin times (strong, system scope), then reads *pcnt once, stores that
 *            value at py[cta] and subtracts 1. The largest stored value is the number of CTAs that were resident at the
 *            same time: the co-residency limit for the launch's threads, declared registers and shared memory.
 *            args: 0x380 pcnt, 0x388 py.
 *   empty    EXIT: the fixed cost of one launch in a command list.
 * Every generator returns 0, or -1 for bad arguments or an IR/allocator failure.
 */
#include "omega_blackwell_codegen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int omega_blackwell_codegen_ceil_stream_prog(uint32_t iters, uint32_t threads, uint32_t grid, uint32_t mask, uint32_t phases,
                                             BlackwellIRProgram *prog);
int omega_blackwell_codegen_ceil_chase_prog(uint32_t iters, BlackwellIRProgram *prog);
int omega_blackwell_codegen_ceil_resid_prog(uint32_t spin, BlackwellIRProgram *prog);
int omega_blackwell_codegen_ceil_empty_prog(BlackwellIRProgram *prog);

#ifdef __cplusplus
}
#endif
#endif
