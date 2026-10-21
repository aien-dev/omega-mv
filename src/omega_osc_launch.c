/* omega_osc_launch.c -- the launcher for Omegese kernels (see omega_osc_launch.h, docs/omegese/GPU.md s11).
 *
 * The checks and the argument words are omega_kernel_launch_words (omega_osc_launch_args.c, no device
 * calls); this file adds the status buffer and the launch through the shared session. */
#include "omega_osc_launch.h"
#include <stdio.h>
#include <string.h>

static int refuse(char *err, size_t errn, const char *msg) {
    if (err && errn) snprintf(err, errn, "%s", msg);
    return OSC_LAUNCH_REFUSED;
}

static OmegaGpuScratch g_status;
static bool g_hooked;

void omega_kernel_launch_release(void) { omega_gpu_session_scratch_free(&g_status); }

int omega_kernel_load(const OscGpuKernel *k, OmegaGpuMem *code) {
    if (!k || !k->code || !k->code_size || !code) return OSC_LAUNCH_REFUSED;
    if (omega_gpu_session_alloc_code(k->code_size, code) != 0) return OSC_LAUNCH_CHIP;
    memcpy(code->cpu, k->code, k->code_size);
    if (omega_gpu_session_sync_to_gpu(code) != 0) { omega_gpu_session_free(code); return OSC_LAUNCH_CHIP; }
    return OSC_LAUNCH_OK;
}

int omega_kernel_launch(const OscGpuKernel *k, const OmegaGpuMem *code, const OscGpuArg *args, unsigned nargs,
                        uint32_t blocks, uint32_t batches, uint32_t timeout_ms, OscGpuRun *run,
                        char *err, size_t errn) {
    if (!run) return refuse(err, errn, "no run record");
    memset(run, 0, sizeof *run);
    if (!omega_gpu_session_is_open()) return refuse(err, errn, "session not open");
    if (timeout_ms == 0) return refuse(err, errn, "a launch needs a stated timeout");
    if (!g_hooked) { (void)omega_gpu_session_on_close(omega_kernel_launch_release); g_hooked = true; }
    if (omega_gpu_session_scratch(&g_status, OSC_GPU_STATUS_WORDS * 4u) != 0) {
        if (err && errn) snprintf(err, errn, "status buffer: %s", omega_gpu_session_last_error());
        return OSC_LAUNCH_CHIP;
    }
    uint32_t words[OSC_GPU_MAX_ARG_WORDS];
    int rc = omega_kernel_launch_words(k, code, args, nargs, blocks, batches, g_status.mem.va, words, err, errn);
    if (rc != OSC_LAUNCH_OK) return rc;
    /* the status buffer must not be one of the caller's ranges either */
    for (unsigned i = 0; i < nargs; i++) {
        if ((args[i].kind != OSC_GPU_P_VIEW && args[i].kind != OSC_GPU_P_EDIT) || !args[i].len) continue;
        uint64_t s = args[i].mem->va + args[i].byte_off, e = s + 4ull * args[i].len;
        if (s < g_status.mem.va + g_status.mem.size && g_status.mem.va < e)
            { if (err && errn) snprintf(err, errn, "%s argument %u overlaps the launcher's status buffer", k->name, i); return OSC_LAUNCH_REFUSED; }
    }
    memset(g_status.mem.cpu, 0, OSC_GPU_STATUS_WORDS * 4u);
    if (omega_gpu_session_sync_to_gpu(&g_status.mem) != 0) {
        if (err && errn) snprintf(err, errn, "status sync: %s", omega_gpu_session_last_error());
        return OSC_LAUNCH_CHIP;
    }
    OmegaGpuLaunch L = {
        .code_va = code->va, .gpr_count = k->gpr_count, .threads_x = k->threads, .threads_y = 1,
        .grid_x = blocks, .grid_y = batches, .num_elements = (size_t)k->threads * blocks * batches,
        .args = words, .n_args = k->arg_words, .timeout_ms = timeout_ms, .shared_bytes = k->shared_bytes,
    };
    uint32_t marker = 0;
    if (omega_gpu_session_launch(&L, &run->elapsed_ns, &marker) != 0) {
        if (err && errn) snprintf(err, errn, "%s launch: %s", k->name, omega_gpu_session_last_error());
        return OSC_LAUNCH_CHIP;
    }
    if (omega_gpu_session_sync_to_cpu(&g_status.mem) != 0) {
        if (err && errn) snprintf(err, errn, "status readback: %s", omega_gpu_session_last_error());
        return OSC_LAUNCH_CHIP;
    }
    memcpy(run->status, g_status.mem.cpu, sizeof run->status);
    for (unsigned c = 1; c < OSC_GPU_STATUS_WORDS; c++)
        if (run->status[c]) { run->trap = c; break; }
    return OSC_LAUNCH_OK;
}
