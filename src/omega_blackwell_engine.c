#define _POSIX_C_SOURCE 200809L
#include "omega_blackwell_engine.h"
#include "omega_blackwell_qmd.h"
#include "omega_blackwell_submit.h"
#include "omega_unwritten_trap.h"
#include "m16_native.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Finite GB10 jobs only. The retained context is intentionally process owned:
 * an uncertain submission must never leave allocations reachable only through
 * a dead stack frame, or permit another job to overwrite those allocations. */
static M16NativeContext ctx;
static atomic_flag engine_lock = ATOMIC_FLAG_INIT;
static bool quarantined;

static const uint32_t SETUP_WORDS[18] = {
    0x20012061, 0x0000cec0, 0x20012092, 0x00000001, 0x200120a8, 0x0000000f, 0x2001255d, 0x00000003,
    0x2001255e, 0x20000000, 0x2001255f, 0x000fffff, 0x20012557, 0x00000003, 0x20012558, 0x22000000,
    0x20012559, 0x00000000,
};

static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int visibility_barrier(void) {
#ifdef OMEGA_GPU_ENGINE_TEST
    return omega_gpu_engine_test_barrier();
#elif defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
#else
#error "The physical Omega GB10 engine requires AArch64"
#endif
}

static int failure(OmegaGpuResult *r, OmegaGpuStage stage, OmegaGpuError error,
                   int rc, int saved_errno) {
    r->failed_stage = stage;
    r->error = error;
    r->native_rc = rc;
    r->saved_errno = saved_errno;
    snprintf(r->driver_error, sizeof r->driver_error, "%s", ctx.rm.err);
    return error;
}

static int checked_alloc(uint64_t bytes, NvrmMem *m, OmegaGpuResult *r) {
    errno = 0;
    int rc = nvrm_alloc(&ctx.rm, bytes, m);
    if (rc != 0) return failure(r, OMEGA_GPU_STAGE_ALLOC, OMEGA_GPU_ERR_ALLOC, rc, errno);
    if (!m->cpu || m->size < bytes || (m->va & 255u))
        return failure(r, OMEGA_GPU_STAGE_ALLOC, OMEGA_GPU_ERR_INVARIANT, 0, 0);
    return 0;
}

static int checked_wait(volatile uint32_t *word, uint32_t expected,
                        uint64_t timeout, uint32_t *observed,
                        OmegaGpuStage stage, OmegaGpuError timed_out,
                        OmegaGpuError failed, OmegaGpuResult *r) {
    errno = 0;
    int rc = m16_native_wait_marker(word, expected, timeout);
    int saved = errno;
    *observed = *word;
    if (rc != 0) return failure(r, stage, rc == -1 ? timed_out : failed, rc, saved);
    /* Native wait is >=, whereas a fresh one-job context requires equality. */
    if (*observed != expected) return failure(r, stage, failed, rc, saved);
    return 0;
}

int omega_gpu_execute(const OmegaGpuJob *job, OmegaGpuResult *r) {
    if (!r) return OMEGA_GPU_ERR_INVALID;
    memset(r, 0, sizeof *r);
    r->first_poison_index = SIZE_MAX;
    if (!job || !job->code || !job->code_bytes || job->code_bytes > 4096 ||
        job->code_bytes % 16 || !job->inputs[0] || !job->inputs[1] ||
        !job->output || !job->output_poison || !job->word_count ||
        job->word_count > UINT32_MAX - 63u ||
        job->word_count > (SIZE_MAX - 4095u) / sizeof(uint32_t) || !job->timeout_ms) {
        r->failed_stage = OMEGA_GPU_STAGE_ARGUMENTS;
        return r->error = OMEGA_GPU_ERR_INVALID;
    }
    if (atomic_flag_test_and_set_explicit(&engine_lock, memory_order_acquire)) {
        r->failed_stage = OMEGA_GPU_STAGE_OPEN;
        return r->error = OMEGA_GPU_ERR_BUSY;
    }
    bool opened = false, possibly_active = false;
    int rc;
    if (quarantined) {
        failure(r, OMEGA_GPU_STAGE_OPEN, OMEGA_GPU_ERR_QUARANTINED, 0, 0);
        r->resource_retained = true;
        goto done;
    }
    memset(&ctx, 0, sizeof ctx);
    errno = 0;
    rc = m16_native_open(&ctx);
    if (rc != 0) {
        failure(r, OMEGA_GPU_STAGE_OPEN, OMEGA_GPU_ERR_OPEN, rc, errno);
        /* Native open has already attempted its own cleanup. */
        if (ctx.rm.faulted || ctx.rm.live_count) {
            quarantined = true;
            r->resource_retained = true;
        }
        goto done;
    }
    opened = true;
    errno = 0;
    rc = m16_native_create_channel(&ctx);
    if (rc != 0) { failure(r, OMEGA_GPU_STAGE_CHANNEL, OMEGA_GPU_ERR_CHANNEL, rc, errno); goto done; }
    uint32_t n = (uint32_t)job->word_count;
    size_t output_bytes = job->word_count * sizeof(uint32_t);
    size_t vector_bytes = (output_bytes + 4095u) & ~(size_t)4095u;
    NvrmMem large_pb, code_mem, cbank_mem, a_mem, b_mem, c_mem, marker_mem, qmd_mem;
    if (checked_alloc(0x10000, &large_pb, r) || checked_alloc(0x1000, &code_mem, r) ||
        checked_alloc(0x1000, &cbank_mem, r) || checked_alloc(vector_bytes, &a_mem, r) ||
        checked_alloc(vector_bytes, &b_mem, r) || checked_alloc(vector_bytes, &c_mem, r) ||
        checked_alloc(0x1000, &marker_mem, r) || checked_alloc(0x10000, &qmd_mem, r)) goto done;
    ctx.pb_mem = large_pb;
    memcpy(code_mem.cpu, job->code, job->code_bytes);
    memcpy(a_mem.cpu, job->inputs[0], output_bytes);
    memcpy(b_mem.cpu, job->inputs[1], output_bytes);
    memcpy(c_mem.cpu, job->output_poison, output_bytes);
    uint32_t cbank_data[OMEGA_BW_CBANK_DRIVER_WORDS];
    uint32_t cbank_args[OMEGA_BW_CBANK_ARGS_WORDS];
    uint64_t qmd0_va = qmd_mem.va, qmd1_va = qmd_mem.va + 0x1000;
    uint64_t sem_va = qmd_mem.va + 0x2000;
    OmegaBlackwellQmdConfig cfg = {
        .code_va = code_mem.va, .cbank_va = cbank_mem.va,
        .scratch_va = qmd_mem.va + 0x4000, .sem_va = sem_va,
        .qmd0_va = qmd0_va, .qmd1_va = qmd1_va,
        .num_elements = n, .threads_per_block = 64, .grid_width = (n + 63u) / 64u
    };
    uint32_t qmd0_words[OMEGA_BW_QMD_WORDS], qmd1_words[OMEGA_BW_QMD_WORDS];
#define PREPARE(call) do { rc = (call); if (rc != 0) { \
    failure(r, OMEGA_GPU_STAGE_PREPARE, OMEGA_GPU_ERR_PREPARE, rc, 0); goto done; } } while (0)
    PREPARE(omega_blackwell_build_cbank_driver(cbank_data, cbank_mem.va));
    PREPARE(omega_blackwell_build_cbank_args(cbank_args, a_mem.va, b_mem.va, c_mem.va, n));
    PREPARE(omega_blackwell_build_qmd0(qmd0_words, qmd0_va, qmd1_va));
    PREPARE(omega_blackwell_build_qmd1(qmd1_words, &cfg));
    PREPARE(omega_blackwell_verify_qmd_invariants(qmd1_words));
#undef PREPARE
    if (qmd1_words[9] != 3u || qmd1_words[15] != (uint32_t)sem_va ||
        qmd1_words[16] != (uint32_t)(sem_va >> 32) ||
        qmd1_words[17] != OMEGA_BW_SEMAPHORE_INTERMEDIATE_DONE ||
        qmd1_words[19] != 0x81810000u) {
        failure(r, OMEGA_GPU_STAGE_PREPARE, OMEGA_GPU_ERR_INVARIANT, 0, 0); goto done;
    }
    memcpy(cbank_mem.cpu, cbank_data, sizeof cbank_data);
    memcpy((uint8_t *)cbank_mem.cpu + 0x380, cbank_args, sizeof cbank_args);
    memcpy(qmd_mem.cpu, qmd0_words, sizeof qmd0_words);
    memcpy((uint8_t *)qmd_mem.cpu + 0x1000, qmd1_words, sizeof qmd1_words);
    volatile uint32_t *hsem = (volatile uint32_t *)((uint8_t *)qmd_mem.cpu + 0x2000);
    volatile uint32_t *hmarker = marker_mem.cpu;
    volatile uint32_t *hmarker2 = (volatile uint32_t *)((uint8_t *)marker_mem.cpu + 0x10);
    *hsem = 0; *hmarker = 0; *hmarker2 = 0;
    /* Assemble unified pushbuffer (481 words) */
    uint32_t pb[1024];
    size_t pb_len = 0;

    /* 1. Setup words (18 words) */
    memcpy(&pb[pb_len], SETUP_WORDS, sizeof(SETUP_WORDS));
    pb_len += sizeof(SETUP_WORDS) / 4;

    /* 2. Cbank driver data upload (224 words via DMA) */
    pb[pb_len++] = nvrm_mthd(1, 0x0188, 2);
    pb[pb_len++] = (uint32_t)(cbank_mem.va >> 32);
    pb[pb_len++] = (uint32_t)cbank_mem.va;
    pb[pb_len++] = nvrm_mthd(1, 0x0180, 2);
    pb[pb_len++] = 0x00000380;
    pb[pb_len++] = 0x00000001;
    pb[pb_len++] = nvrm_mthd(1, 0x01b0, 1);
    pb[pb_len++] = 0x00000041;
    pb[pb_len++] = (224 << 16) | (1 << 13) | (0x01b4 >> 2) | (6u << 28);
    memcpy(&pb[pb_len], cbank_data, 224 * 4);
    pb_len += 224;

    /* 3. Kernel args upload (7 words via DMA) */
    pb[pb_len++] = nvrm_mthd(1, 0x0188, 2);
    pb[pb_len++] = (uint32_t)((cbank_mem.va + 0x380) >> 32);
    pb[pb_len++] = (uint32_t)(cbank_mem.va + 0x380);
    pb[pb_len++] = nvrm_mthd(1, 0x0180, 2);
    pb[pb_len++] = 0x0000001c;
    pb[pb_len++] = 0x00000001;
    pb[pb_len++] = nvrm_mthd(1, 0x01b0, 1);
    pb[pb_len++] = 0x00000041;
    pb[pb_len++] = (7 << 16) | (1 << 13) | (0x01b4 >> 2) | (6u << 28);
    memcpy(&pb[pb_len], cbank_args, 7 * 4);
    pb_len += 7;

    /* 4. Inline QMD 0 (98 words: 2 addr + 96 QMD words) */
    pb[pb_len++] = (98 << 16) | (1 << 13) | (0x0318 >> 2) | (2u << 28);
    pb[pb_len++] = (1u << 30) | (uint32_t)((qmd0_va >> 40) & 0x1ff);
    pb[pb_len++] = (uint32_t)(qmd0_va >> 8);
    memcpy(&pb[pb_len], qmd0_words, 96 * 4);
    pb_len += 96;

    /* 5. Intermediate semaphore upload (1 word = 5 via DMA) */
    pb[pb_len++] = nvrm_mthd(1, 0x0188, 2);
    pb[pb_len++] = (uint32_t)(sem_va >> 32);
    pb[pb_len++] = (uint32_t)sem_va;
    pb[pb_len++] = nvrm_mthd(1, 0x0180, 2);
    pb[pb_len++] = 0x00000004;
    pb[pb_len++] = 0x00000001;
    pb[pb_len++] = nvrm_mthd(1, 0x01b0, 1);
    pb[pb_len++] = 0x00000041;
    pb[pb_len++] = (1 << 16) | (1 << 13) | (0x01b4 >> 2) | (6u << 28);
    pb[pb_len++] = OMEGA_BW_SEMAPHORE_INTERMEDIATE_INIT;

    /* 6. Inline QMD 1 (98 words: 2 addr + 96 QMD words) */
    pb[pb_len++] = (98 << 16) | (1 << 13) | (0x0318 >> 2) | (2u << 28);
    pb[pb_len++] = (1u << 30) | (uint32_t)((qmd1_va >> 40) & 0x1ff);
    pb[pb_len++] = (uint32_t)(qmd1_va >> 8);
    memcpy(&pb[pb_len], qmd1_words, 96 * 4);
    pb_len += 96;

    /* 7. Subchannel 0 completion release (WFI) */
    pb[pb_len++] = nvrm_mthd(0, 0x005c, 5);
    pb[pb_len++] = (uint32_t)marker_mem.va;
    pb[pb_len++] = (uint32_t)(marker_mem.va >> 32);
    pb[pb_len++] = OMEGA_BW_MARKER_COMPLETION_PAYLOAD;
    pb[pb_len++] = 0;
    pb[pb_len++] = 0x1 | (1u << 20); // RELEASE | WFI

    /* Exact C3 FIX A sequence from omega_ds_gb10_run: L2 dirty flush then
     * second WFI marker. This engine adaptation still needs chip qualification. */
    pb[pb_len++] = nvrm_mthd(0, 0x0028, 4);
    pb[pb_len++] = 0; pb[pb_len++] = 0; pb[pb_len++] = 0;
    pb[pb_len++] = 0x10u << 27;
    pb[pb_len++] = nvrm_mthd(0, 0x005c, 5);
    pb[pb_len++] = (uint32_t)(marker_mem.va + 0x10);
    pb[pb_len++] = (uint32_t)((marker_mem.va + 0x10) >> 32);
    pb[pb_len++] = 0x46464646u; pb[pb_len++] = 0;
    pb[pb_len++] = 0x1u | (1u << 20);
    rc = visibility_barrier();
    if (rc != 0) { failure(r, OMEGA_GPU_STAGE_BARRIER, OMEGA_GPU_ERR_VISIBILITY_FAILED, rc, 0); goto done; }
    r->state = OMEGA_GPU_PREPARED;
    r->launch_timestamp_ns = now_ns();
    possibly_active = true; /* A failed native submit may still have side effects. */
    errno = 0;
    rc = m16_native_submit_methods(&ctx, pb, pb_len);
    if (rc != 0) { failure(r, OMEGA_GPU_STAGE_SUBMIT, OMEGA_GPU_ERR_SUBMIT, rc, errno); goto done; }
    r->state = OMEGA_GPU_SUBMITTED;
    if (checked_wait(hmarker, OMEGA_BW_MARKER_COMPLETION_PAYLOAD, job->timeout_ms,
                     &r->completion_marker, OMEGA_GPU_STAGE_COMPLETION,
                     OMEGA_GPU_ERR_COMPLETION_TIMEOUT, OMEGA_GPU_ERR_COMPLETION_FAILED, r)) goto done;
    if (checked_wait(hmarker2, 0x46464646u, job->timeout_ms,
                     &r->visibility_marker, OMEGA_GPU_STAGE_VISIBILITY,
                     OMEGA_GPU_ERR_VISIBILITY_TIMEOUT, OMEGA_GPU_ERR_VISIBILITY_FAILED, r)) goto done;
    if (checked_wait(hsem, OMEGA_BW_SEMAPHORE_INTERMEDIATE_DONE, job->timeout_ms,
                     &r->release_semaphore, OMEGA_GPU_STAGE_RELEASE,
                     OMEGA_GPU_ERR_RELEASE_TIMEOUT, OMEGA_GPU_ERR_RELEASE_FAILED, r)) goto done;
    r->state = OMEGA_GPU_GPU_COMPLETE;
    r->completion_timestamp_ns = now_ns();
    possibly_active = false;
    rc = visibility_barrier();
    if (rc != 0) { failure(r, OMEGA_GPU_STAGE_BARRIER, OMEGA_GPU_ERR_VISIBILITY_FAILED, rc, 0); goto done; }
    r->state = OMEGA_GPU_OUTPUT_VISIBLE;
    const uint32_t *device_output = c_mem.cpu;
    for (size_t i = 0; i < job->word_count; i++) {
        OmegaUnwrittenReport report;
        omega_unwritten_scan(device_output + i, 1, job->output_poison[i], 1, &report);
        if (report.count) {
            if (!r->poison_count) r->first_poison_index = i;
            r->poison_count++;
        }
    }
    if (r->poison_count) {
        failure(r, OMEGA_GPU_STAGE_OUTPUT, OMEGA_GPU_ERR_OUTPUT_NOT_WRITTEN, 0, 0); goto done;
    }
    r->state = OMEGA_GPU_OUTPUT_PRODUCED;
    memcpy(job->output, c_mem.cpu, output_bytes);

done:
    /* Never destroy a channel or free buffers while work may remain active. */
    if (possibly_active) {
        quarantined = true;
        r->resource_retained = true;
    } else if (opened) {
        errno = 0;
        r->cleanup_rc = m16_native_close(&ctx);
        r->cleanup_errno = errno;
        if (r->cleanup_rc != 0) {
            snprintf(r->cleanup_error, sizeof r->cleanup_error, "%s", ctx.rm.err);
            quarantined = true;
            r->resource_retained = true;
            if (r->error == OMEGA_GPU_OK)
                failure(r, OMEGA_GPU_STAGE_CLEANUP, OMEGA_GPU_ERR_CLEANUP, r->cleanup_rc, r->cleanup_errno);
        }
    }
    if (r->error == OMEGA_GPU_OK) r->state = OMEGA_GPU_SUCCESS;
    atomic_flag_clear_explicit(&engine_lock, memory_order_release);
    return r->error;
}
