#ifndef OMEGA_EXEC_H
#define OMEGA_EXEC_H

#include "omega_realize.h"
#include <stdint.h>
#include <stdbool.h>

/* Native in-process execution on AArch64 host.
 * Executable pages are kept for the last OMEGA_EXEC_CACHE_SLOTS distinct code byte strings
 * (identity = the bytes, not the RealizationObject address). Measured 2026-10-19 on the Spark
 * (omega-mv#574): mmap+copy+mprotect+munmap per call cost 19 to 23 us against under 40 ns of
 * code, so a repeated realization paid 99.9 percent of its time in page setup. */
#define OMEGA_EXEC_CACHE_SLOTS 64u
typedef struct {
    uint64_t hits;      /* calls served from a resident mapping */
    uint64_t misses;    /* calls that mapped fresh pages */
    uint64_t evictions; /* resident mappings dropped to make room */
    uint64_t resident;  /* mappings currently held */
} OmegaExecCacheStats;
int omega_exec_native_f3(const RealizationObject *real, uint64_t a, uint64_t b, uint64_t c, uint64_t *out_res);
void omega_exec_native_cache_stats(OmegaExecCacheStats *out);
void omega_exec_native_cache_reset(void);   /* unmaps every resident mapping and zeroes the counters */

/* Bare-metal QEMU runner generation and execution */
int omega_build_qemu_runner(const RealizationObject *real, const char *out_bin_path);
int omega_exec_qemu_virt(const char *runner_bin_path, char *out_log, size_t out_log_len);

#endif /* OMEGA_EXEC_H */
