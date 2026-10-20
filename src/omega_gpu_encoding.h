/* omega_gpu_encoding.h -- the two pure push-buffer encoders, shared by every backend (L202, aienos#286 cut A).
 *
 * The method header and the GPFIFO entry are hardware encoding, not driver calls, but they lived in the Linux
 * driver layer: nvrm_mthd as a static inline in physics nvrm/nvrm.h, nvrm_gp_entry in physics nvrm/nvrm.c. Omega
 * code that must also build freestanding re-copied the first one (bw_mthd in the session and the command list).
 * This header is the one Omega copy. The arithmetic is nvrm's, character for character; the hosted test
 * (make test-gpu-encoding) compares both functions with the physics originals over full sweeps.
 *
 * omega_gpu_gp_entry has no caller in Omega yet: the only GPFIFO writer is physics nvrm.c (nvrm_enqueue). It is
 * here so a native queue encodes entries the same way, and the sweep test is its only consumer until then.
 *
 * Pure C, no libc header, freestanding.
 */
#ifndef OMEGA_GPU_ENCODING_H
#define OMEGA_GPU_ENCODING_H

#include <stdint.h>

/* Increasing method header (SEC_OP_INC_METHOD = 2): count data words follow, written to consecutive methods
 * starting at byte offset mthd on subchannel subc. physics nvrm.h nvrm_mthd. */
static inline uint32_t omega_gpu_mthd(uint32_t subc, uint32_t mthd, uint32_t count) {
    return (2u << 28) | (count << 16) | (subc << 13) | (mthd >> 2);
}

/* One GPFIFO entry: push-buffer GPU address va (dword aligned), length nwords (21 bits), bit 41 set as nvrm
 * writes it. physics nvrm.c nvrm_gp_entry. */
static inline uint64_t omega_gpu_gp_entry(uint64_t va, uint32_t nwords) {
    return (va & ~3ull) | ((uint64_t)(nwords & 0x1fffff) << 42) | (1ull << 41);
}

#endif /* OMEGA_GPU_ENCODING_H */
