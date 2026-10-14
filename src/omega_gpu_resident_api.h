#ifndef OMEGA_GPU_RESIDENT_API_H
#define OMEGA_GPU_RESIDENT_API_H
/*
 * omega_gpu_resident_api: host side of the resident FP8 GEMV chain (OWN-30B W-L2f Proof A, 2026-10-14). New file.
 * Kernel contract: omega_blackwell_codegen_resident.h. Control block design: docs/own30b/resident-control-block.md.
 *
 * One OmegaGpuResident = one kernel (spec), one stage table (GPU-cached, written once at create, never rewritten),
 * one control block (GPU-uncached: the host writes it before every launch and reads it after, or polls it during).
 * Every call: caller holds omega_gpu_session_lock() with the device open (omega_gpu_session_open()).
 *
 * Co-residency: record refuses a grid above omega_gpu_resident_capacity() (OMEGA_GPU_RESIDENT_TOO_LARGE, nothing
 * recorded) unless force = 1 (tests only: the oversize negative control). Capacity = SMs x CTAs per SM, CTAs per SM
 * from the documented 64K-register file and the per-SM thread / block limits below; the SM count and those limits
 * are NOT in the docs we have for GB10 (DOCS SILENT) and are checked by the chip probe in tests/gpu_resident_chain_test.c,
 * which may lower them with omega_gpu_resident_set_limits().
 */
#include <stdint.h>
#include "omega_blackwell_codegen_resident.h"
#include "omega_gpu_cmdlist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_GPU_RESIDENT_OK 0
#define OMEGA_GPU_RESIDENT_BAD_ARGS (-1)
#define OMEGA_GPU_RESIDENT_TOO_LARGE (-2)
#define OMEGA_GPU_RESIDENT_CODEGEN_FAIL (-3)
#define OMEGA_GPU_RESIDENT_CHIP_FAIL (-4)

/* defaults (UNVERIFIED for GB10 until the probe confirms them) */
#define OMEGA_GPU_RESIDENT_SMS 48u
#define OMEGA_GPU_RESIDENT_THREADS_PER_SM 1536u
#define OMEGA_GPU_RESIDENT_BLOCKS_PER_SM 32u

typedef struct {
    uint64_t w_va, s_va; /* omega_gpu_fp8_tensor_entry */
    uint64_t x_va, y_va; /* x' (k f32), y (n f32) */
    uint32_t k, n;
} OmegaGpuResidentStage;

typedef struct OmegaGpuResident OmegaGpuResident;

int omega_gpu_resident_create(const OmegaResidentSpec *spec, const OmegaGpuResidentStage *stages, uint32_t n_stages, OmegaGpuResident **out);
void omega_gpu_resident_free(OmegaGpuResident *r);

/* largest grid (CTAs) the launcher accepts for this kernel; peak / declared registers for the record */
uint32_t omega_gpu_resident_capacity(const OmegaGpuResident *r);
uint32_t omega_gpu_resident_gpr_declared(const OmegaGpuResident *r);
void omega_gpu_resident_set_limits(uint32_t sms, uint32_t threads_per_sm, uint32_t blocks_per_sm);

/* Writes the control block (magic, version, n_stages, grid, arrive = error = cancel = 0, spin_max) and appends the one
 * launch to cl. grid 0 = capacity. */
int omega_gpu_resident_record(OmegaGpuCmdList *cl, OmegaGpuResident *r, uint32_t grid, uint32_t spin_max, int force);

/* Reads the control block back (after the submit returned): progress = barriers passed (CTA 0), error. A launch that
 * aborted (error or cancel) leaves the barrier word unknown: status then replaces it with a fresh one. Every recorded
 * launch must be submitted and followed by status() before the next record. */
int omega_gpu_resident_status(OmegaGpuResident *r, uint32_t *progress, uint32_t *error);

/* Host cancel: sets the control block cancel word (uncached). CTA 0 reads it at every barrier; the chain stops at the next
 * barrier after it is seen (abort bit through the barrier word). Safe while the kernel runs. */
int omega_gpu_resident_cancel(OmegaGpuResident *r);

#ifdef __cplusplus
}
#endif
#endif
