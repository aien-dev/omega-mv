#ifndef OMEGA_BLACKWELL_ENGINE_H
#define OMEGA_BLACKWELL_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    OMEGA_GPU_INITIAL, OMEGA_GPU_PREPARED, OMEGA_GPU_SUBMITTED,
    OMEGA_GPU_GPU_COMPLETE, OMEGA_GPU_OUTPUT_VISIBLE,
    OMEGA_GPU_OUTPUT_PRODUCED, OMEGA_GPU_SUCCESS
} OmegaGpuState;

typedef enum {
    OMEGA_GPU_STAGE_NONE, OMEGA_GPU_STAGE_ARGUMENTS, OMEGA_GPU_STAGE_OPEN,
    OMEGA_GPU_STAGE_CHANNEL, OMEGA_GPU_STAGE_ALLOC, OMEGA_GPU_STAGE_PREPARE,
    OMEGA_GPU_STAGE_SUBMIT, OMEGA_GPU_STAGE_COMPLETION,
    OMEGA_GPU_STAGE_VISIBILITY, OMEGA_GPU_STAGE_RELEASE,
    OMEGA_GPU_STAGE_BARRIER, OMEGA_GPU_STAGE_OUTPUT, OMEGA_GPU_STAGE_CLEANUP
} OmegaGpuStage;

typedef enum {
    OMEGA_GPU_OK = 0,
    OMEGA_GPU_ERR_INVALID = -100, OMEGA_GPU_ERR_OPEN = -101,
    OMEGA_GPU_ERR_CHANNEL = -102, OMEGA_GPU_ERR_ALLOC = -103,
    OMEGA_GPU_ERR_PREPARE = -104, OMEGA_GPU_ERR_SUBMIT = -105,
    OMEGA_GPU_ERR_COMPLETION_TIMEOUT = -106, OMEGA_GPU_ERR_COMPLETION_FAILED = -107,
    OMEGA_GPU_ERR_RELEASE_TIMEOUT = -108, OMEGA_GPU_ERR_RELEASE_FAILED = -109,
    OMEGA_GPU_ERR_VISIBILITY_TIMEOUT = -110, OMEGA_GPU_ERR_VISIBILITY_FAILED = -111,
    OMEGA_GPU_ERR_OUTPUT_NOT_WRITTEN = -112, OMEGA_GPU_ERR_INVARIANT = -113,
    OMEGA_GPU_ERR_BUSY = -114, OMEGA_GPU_ERR_QUARANTINED = -115,
    OMEGA_GPU_ERR_CLEANUP = -116
} OmegaGpuError;

/* First cut: calibrated GB10 1D ABI, 64 threads, two uint32 input buffers,
 * one uint32 output buffer, seven argument words, at most 4096 code bytes.
 * Caller owns kernel semantics. Every output poison word must differ from
 * the permitted output at that position; a collision is conservatively
 * reported as output not proven. No mathematical oracle runs in this layer.
 * Buffers must remain stable for the duration of this synchronous call. */
typedef struct {
    const uint8_t *code;
    size_t code_bytes;
    const uint32_t *inputs[2];
    uint32_t *output;
    const uint32_t *output_poison;
    size_t word_count;
    uint64_t timeout_ms;
} OmegaGpuJob;

typedef struct {
    OmegaGpuState state;
    OmegaGpuStage failed_stage;
    OmegaGpuError error;
    int native_rc;
    int saved_errno;
    char driver_error[256];
    int cleanup_rc;
    int cleanup_errno;
    char cleanup_error[256];
    uint32_t completion_marker;
    uint32_t visibility_marker;
    uint32_t release_semaphore;
    size_t first_poison_index;
    size_t poison_count;
    uint64_t launch_timestamp_ns;
    uint64_t completion_timestamp_ns;
    bool resource_retained;
} OmegaGpuResult;

/* On uncertain submission/completion the process retains the native context
 * and refuses subsequent jobs. There is deliberately no automatic retry,
 * reset, or timeout cleanup. result is required. */
int omega_gpu_execute(const OmegaGpuJob *job, OmegaGpuResult *result);

#ifdef OMEGA_GPU_ENGINE_TEST
/* Fake backend observes/barriers without executing AArch64 instructions.
 * Nonzero models an observable visibility failure. No production bypass. */
int omega_gpu_engine_test_barrier(void);
#endif

#endif
