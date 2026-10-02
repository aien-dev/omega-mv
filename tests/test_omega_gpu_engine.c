/* Host model for the production engine. No driver implementation is linked.
 * The model checks software sequencing; it makes no claim about chip timing. */
#include "omega_blackwell_engine.h"
#include "omega_blackwell_qmd.h"
#include "m16_native.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum Fault { GOOD, OPEN, CHANNEL, ALLOC, PREPARE, SUBMIT, WAIT0, WAIT1, WAIT2,
             WRONG0, WRONG1, WRONG2, BARRIER, UNWRITTEN, PARTIAL, CLOSE };
static enum Fault fault;
static Nvrm *rm_live;
static NvrmMem allocations[16];
static int allocation_count, open_count, close_count, submit_count, wait_count;
static int pre_barrier, post_barrier, parsed, flush_seen;
static uint32_t *device_output;
static volatile uint32_t *markers[3];
static uint32_t a[4] = {1, 2, 3, 4}, b[4] = {10, 20, 30, 40};
/* Arbitrary output, deliberately NOT a mathematical result of a and b. */
static uint32_t produced[4] = {0x55555555, 0, 0xffffffff, 19};
static uint32_t poison[4] = {0xaaaa0001, 0xaaaa0002, 0xaaaa0003, 0xaaaa0004};
static uint32_t output[4];
static const uint8_t code[16] = {1, 2, 3};
static uint64_t pair(const uint32_t *p) { return p[0] | ((uint64_t)p[1] << 32); }
static void *mapped(uint64_t address, size_t size) {
    for (int i = 0; i < allocation_count; i++)
        if (address >= allocations[i].va && size <= allocations[i].size &&
            address - allocations[i].va <= allocations[i].size - size)
            return (uint8_t *)allocations[i].cpu + address - allocations[i].va;
    assert(!"packet references unallocated memory"); return NULL;
}
static int fail_driver(void) {
    errno = EIO;
    if (rm_live) strcpy(rm_live->err, "injected lower-level failure");
    return -37;
}
int m16_native_open(M16NativeContext *ctx) {
    ++open_count; memset(ctx, 0, sizeof(*ctx)); rm_live = &ctx->rm;
    ctx->rm.sm_version = 0x0c01;
    return fault == OPEN ? fail_driver() : 0;
}
int m16_native_create_channel(M16NativeContext *ctx) {
    assert(&ctx->rm == rm_live); return fault == CHANNEL ? fail_driver() : 0;
}
int nvrm_alloc(Nvrm *rm, uint64_t bytes, NvrmMem *mem) {
    assert(rm == rm_live); assert(allocation_count < 16);
    if (fault == ALLOC && allocation_count == 3) return fail_driver();
    void *ptr = NULL; assert(posix_memalign(&ptr, 4096, bytes) == 0);
    memset(ptr, 0x69, bytes);
    *mem = (NvrmMem){.cpu = ptr, .va = (uintptr_t)ptr, .size = bytes};
    allocations[allocation_count++] = *mem;
    return 0;
}
int __real_omega_blackwell_verify_qmd_invariants(const uint32_t *qmd);
int __wrap_omega_blackwell_verify_qmd_invariants(const uint32_t *qmd) {
    if (fault == PREPARE) return fail_driver();
    return __real_omega_blackwell_verify_qmd_invariants(qmd);
}
int m16_native_submit_methods(M16NativeContext *ctx, const uint32_t *pb, size_t words) {
    assert(&ctx->rm == rm_live); ++submit_count;
    assert(pre_barrier == 1);
    int qmd_count = 0, marker_count = 0;
    for (size_t off = 0; off < words;) {
        uint32_t h = pb[off++], count = (h >> 16) & 0x1fff;
        uint32_t method = (h & 0x1fff) << 2, subchannel = (h >> 13) & 7;
        assert(count && off + count <= words);
        const uint32_t *p = pb + off;
        if (method == 0x318 && subchannel == 1) {
            assert(count == 98); ++qmd_count;
            const uint32_t *q = p + 2;
            if (qmd_count == 2) {
                assert(q[4] == 0x013f0000);
                uint64_t cb = ((uint64_t)q[42] << 6) | ((uint64_t)(q[43] & 0x7ffff) << 38);
                uint32_t *args = mapped(cb + 0x380, 28);
                assert(args[6] == 4);
                assert(memcmp(mapped(pair(args), 16), a, 16) == 0);
                assert(memcmp(mapped(pair(args + 2), 16), b, 16) == 0);
                device_output = mapped(pair(args + 4), 16);
                assert(memcmp(device_output, poison, 16) == 0);
                markers[2] = mapped(pair(q + 15), 4);
                assert(q[17] == 6);
            }
        }
        if (method == 0x5c && subchannel == 0) {
            assert(count == 5 && marker_count < 2 && qmd_count == 2);
            if (marker_count == 1) assert(flush_seen);
            assert(p[2] == (marker_count ? 0x46464646u : 0x44444444u));
            assert(p[3] == 0 && p[4] == (1u | (1u << 20)));
            markers[marker_count++] = mapped(pair(p), 4);
        }
        if (method == 0x28 && subchannel == 0) {
            assert(marker_count == 1 && count == 4);
            assert(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == (0x10u << 27));
            flush_seen = 1;
        }
        off += count;
    }
    assert(qmd_count == 2 && marker_count == 2 && flush_seen);
    parsed = 1;
    return fault == SUBMIT ? fail_driver() : 0;
}
int m16_native_wait_marker(volatile uint32_t *ptr, uint32_t expected, uint64_t timeout) {
    assert(parsed && wait_count < 3 && timeout == 123);
    int index = wait_count++;
    assert(ptr == markers[index]);
    assert(expected == (index == 0 ? 0x44444444u : index == 1 ? 0x46464646u : 6));
    if (fault == (enum Fault)(WAIT0 + index)) {
        fail_driver(); return -1; /* native polling API's timeout convention */
    }
    *ptr = fault == (enum Fault)(WRONG0 + index) ? 0 : expected;
    return 0;
}
int omega_gpu_engine_test_barrier(void) {
    if (!submit_count) { ++pre_barrier; return 0; }
    assert(wait_count == 3); ++post_barrier;
    if (fault == BARRIER) return fail_driver();
    if (fault != UNWRITTEN) {
        size_t n = fault == PARTIAL ? 3 : 4;
        memcpy(device_output, produced, n * sizeof(uint32_t));
    }
    return 0;
}
int m16_native_close(M16NativeContext *ctx) {
    assert(&ctx->rm == rm_live); ++close_count;
    /* Any close after uncertain execution is a test failure. */
    assert(!submit_count || (wait_count == 3 && fault != SUBMIT &&
           !(fault >= WAIT0 && fault <= WRONG2)));
    for (int i = 0; i < allocation_count; i++) free(allocations[i].cpu);
    strcpy(ctx->rm.err, "cleanup overwrote driver detail"); errno = EBADF;
    return fault == CLOSE ? -81 : 0;
}
static OmegaGpuJob job(void) {
    return (OmegaGpuJob){.code = code, .code_bytes = sizeof(code),
        .inputs = {a, b}, .output = output, .output_poison = poison,
        .word_count = 4, .timeout_ms = 123};
}
static void run_case(enum Fault f) {
    fault = f; memset(output, 0x91, sizeof(output));
    OmegaGpuJob j = job(); OmegaGpuResult r;
    int rc = omega_gpu_execute(&j, &r);
    static const OmegaGpuError errors[] = {
        OMEGA_GPU_OK, OMEGA_GPU_ERR_OPEN, OMEGA_GPU_ERR_CHANNEL, OMEGA_GPU_ERR_ALLOC,
        OMEGA_GPU_ERR_PREPARE, OMEGA_GPU_ERR_SUBMIT,
        OMEGA_GPU_ERR_COMPLETION_TIMEOUT, OMEGA_GPU_ERR_VISIBILITY_TIMEOUT, OMEGA_GPU_ERR_RELEASE_TIMEOUT,
        OMEGA_GPU_ERR_COMPLETION_FAILED, OMEGA_GPU_ERR_VISIBILITY_FAILED, OMEGA_GPU_ERR_RELEASE_FAILED,
        OMEGA_GPU_ERR_VISIBILITY_FAILED, OMEGA_GPU_ERR_OUTPUT_NOT_WRITTEN,
        OMEGA_GPU_ERR_OUTPUT_NOT_WRITTEN, OMEGA_GPU_ERR_CLEANUP};
    static const OmegaGpuStage stages[] = {
        OMEGA_GPU_STAGE_NONE, OMEGA_GPU_STAGE_OPEN, OMEGA_GPU_STAGE_CHANNEL, OMEGA_GPU_STAGE_ALLOC,
        OMEGA_GPU_STAGE_PREPARE, OMEGA_GPU_STAGE_SUBMIT, OMEGA_GPU_STAGE_COMPLETION,
        OMEGA_GPU_STAGE_VISIBILITY, OMEGA_GPU_STAGE_RELEASE, OMEGA_GPU_STAGE_COMPLETION,
        OMEGA_GPU_STAGE_VISIBILITY, OMEGA_GPU_STAGE_RELEASE, OMEGA_GPU_STAGE_BARRIER,
        OMEGA_GPU_STAGE_OUTPUT, OMEGA_GPU_STAGE_OUTPUT, OMEGA_GPU_STAGE_CLEANUP};
    if (rc != (int)errors[f] || r.error != errors[f] || r.failed_stage != stages[f]) {
        fprintf(stderr, "case %d: rc %d error %d stage %d expected %d/%d\n", f, rc, r.error, r.failed_stage, errors[f], stages[f]);
        assert(0);
    }
    if (f == GOOD) {
        assert(r.state == OMEGA_GPU_SUCCESS && close_count == 1);
        assert(wait_count == 3 && post_barrier == 1);
        assert(memcmp(output, produced, sizeof(output)) == 0);
        assert(r.poison_count == 0 && !r.resource_retained);
    }
    if (f >= SUBMIT && f <= WRONG2) {
        assert(close_count == 0 && r.resource_retained);
        assert(r.state != OMEGA_GPU_SUCCESS);
        int calls = open_count;
        assert(omega_gpu_execute(&j, &r) == OMEGA_GPU_ERR_QUARANTINED);
        assert(open_count == calls && close_count == 0);
    }
    if (f == CHANNEL || f == ALLOC || f == PREPARE) {
        assert(close_count == 1 && r.native_rc == -37 && r.saved_errno == EIO);
        assert(strcmp(r.driver_error, "injected lower-level failure") == 0);
    }
    if (f == UNWRITTEN || f == PARTIAL) {
        assert(r.state == OMEGA_GPU_OUTPUT_VISIBLE && close_count == 1);
        assert(r.poison_count == (f == UNWRITTEN ? 4u : 1u));
        assert(r.first_poison_index == (f == UNWRITTEN ? 0u : 3u));
        for (size_t i = 0; i < 4; i++) assert(output[i] == 0x91919191);
    }
    if (f == CLOSE) assert(r.cleanup_rc == -81 && r.cleanup_errno == EBADF);
}
static void invalid_cases(void) {
    OmegaGpuJob j = job(); OmegaGpuResult r;
    assert(omega_gpu_execute(NULL, &r) == OMEGA_GPU_ERR_INVALID);
    assert(omega_gpu_execute(&j, NULL) == OMEGA_GPU_ERR_INVALID);
    j.word_count = 0;
    assert(omega_gpu_execute(&j, &r) == OMEGA_GPU_ERR_INVALID);
    j = job(); j.output_poison = NULL;
    assert(omega_gpu_execute(&j, &r) == OMEGA_GPU_ERR_INVALID);
    j = job(); j.code_bytes = 4097;
    assert(omega_gpu_execute(&j, &r) == OMEGA_GPU_ERR_INVALID);
    assert(open_count == 0);
}
int main(void) {
    invalid_cases();
    for (int f = GOOD; f <= CLOSE; f++) {
        pid_t child = fork(); assert(child >= 0);
        if (child == 0) { run_case((enum Fault)f); _exit(0); }
        int status; assert(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "engine host case %d FAILED\n", f); return 1;
        }
    }
    puts("GPU engine host: PASS (16 isolated fault/success cases plus invalid arguments)");
    return 0;
}
