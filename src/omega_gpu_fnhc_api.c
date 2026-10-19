/*
 * omega_gpu_fnhc_api.c: kernel cache and launch of the Flash-Next 4-lane gated residual phases (see the header). The session,
 * kernel-slot and launch pattern is omega_gpu_router_fn_api.c's.
 */
#include "omega_gpu_fnhc_api.h"
#include "omega_gpu_session.h"
#include <stdbool.h>
#include <string.h>
#include "omega_gpu_cmdlist.h"

#define MARKER_TIMEOUT_MS 20000ull

typedef struct {
    bool valid;
    OmegaFnHcSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
    uint32_t insns;
} Slot;

static struct { bool hooked; Slot slot[OMEGA_FNHC_PHASES]; int mutant; } g;
static int g_rec;

static void slot_clear_locked(uint32_t i) { if (g.slot[i].valid) omega_gpu_session_free(&g.slot[i].code); memset(&g.slot[i], 0, sizeof g.slot[i]); }
static void on_session_close(void) { memset(g.slot, 0, sizeof g.slot); }
static OmegaGpuSession *dev_open_locked(void) {
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_session_close); g.hooked = true; }
    return omega_gpu_session_open();
}
void omega_gpu_fnhc_test_set_mutant(int mutant) {
    omega_gpu_session_lock();
    g.mutant = mutant;
    for (uint32_t i = 0; i < OMEGA_FNHC_PHASES; i++) slot_clear_locked(i);
    omega_gpu_session_unlock();
}
const char *omega_gpu_fnhc_last_error(void) { return omega_gpu_session_last_error(); }

static int kernel_for(uint32_t phase, Slot **out, bool *hit) {
    OmegaFnHcSpec spec; memset(&spec, 0, sizeof spec);
    spec.phase = phase;
    spec.mutant = g.mutant;
    /* a mutant belongs to one phase: the other phases run honestly */
    { OmegaFnHcSpec probe = spec; BlackwellIRProgram *pp = omega_gpu_host_calloc(1, sizeof *pp); if (!pp) return OMEGA_GPU_FNHC_CODEGEN_FAIL;
      if (omega_blackwell_codegen_fnhc_prog(&probe, pp) != 0) spec.mutant = 0;
      omega_gpu_host_free(pp); }
    Slot *s = &g.slot[phase];
    if (s->valid && memcmp(&s->spec, &spec, sizeof spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_FNHC_OK; }
    if (g_rec && s->valid) { omega_gpu_session_set_error("fnhc record: kernel cache miss would evict the resident kernel (prepare first)"); return OMEGA_GPU_FNHC_CHIP_FAIL; }
    slot_clear_locked(phase);
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("fnhc codegen: host memory"); return OMEGA_GPU_FNHC_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_fnhc_prog(&spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnhc codegen refused the spec"); return OMEGA_GPU_FNHC_CODEGEN_FAIL; }
    if ((uint64_t)OMEGA_FNHC_THREADS * omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage) > 65536u) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnhc: register file"); return OMEGA_GPU_FNHC_CODEGEN_FAIL; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_FNHC_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    uint32_t insns = (uint32_t)prog->count;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); omega_gpu_session_set_error("fnhc encode"); return OMEGA_GPU_FNHC_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_FNHC_CHIP_FAIL; }
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = spec; s->kernel = kernel; s->code = code; s->insns = insns;
    *out = s; *hit = false;
    return OMEGA_GPU_FNHC_OK;
}

static int core(struct OmegaGpuCmdList *rec, uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, float eps, OmegaGpuFnHcInfo *info) {
    uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (phase >= OMEGA_FNHC_PHASES || !a) return OMEGA_GPU_FNHC_BAD_ARGS;
    if (!rec) omega_gpu_session_lock();
#define UNLOCK() do { if (!rec) omega_gpu_session_unlock(); } while (0)
    if (!dev_open_locked()) { UNLOCK(); return OMEGA_GPU_FNHC_CHIP_FAIL; }
    Slot *ks = NULL; bool hit = false;
    g_rec = rec != NULL;
    int rc = kernel_for(phase, &ks, &hit);
    g_rec = 0;
    if (rc != OMEGA_GPU_FNHC_OK) { UNLOCK(); return rc; }
    uint32_t args[OMEGA_FNHC_ARGS_WORDS]; memset(args, 0, sizeof args);
    uint64_t v[5] = { a, b, c, d, e };
    for (int i = 0; i < 5; i++) { args[2 * i] = (uint32_t)v[i]; args[2 * i + 1] = (uint32_t)(v[i] >> 32); }
    memcpy(&args[10], &eps, 4);
    const uint32_t grid = omega_fnhc_grid(phase);
    OmegaGpuLaunch L = {
        .code_va = ks->code.va, .gpr_count = omega_bw_gpr_declared(ks->kernel.gpr_count),
        .threads_x = OMEGA_FNHC_THREADS, .threads_y = 1, .grid_x = grid, .grid_y = 1,
        .num_elements = (size_t)OMEGA_FNHC_L,
        .args = args, .n_args = OMEGA_FNHC_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
        .shared_bytes = OMEGA_FNHC_SHARED_BYTES,
    };
    if (info) { info->phase = phase; info->grid_x = grid; info->threads = OMEGA_FNHC_THREADS; info->gprs = ks->kernel.gpr_count; info->insns = ks->insns; info->kernel_cache_hit = hit; }
    if (!omega_bw_gpr_declared_ok(L.gpr_count, ks->kernel.gpr_count)) { omega_gpu_session_set_error("declared registers below peak + 2: refusing to launch"); rc = OMEGA_GPU_FNHC_CHIP_FAIL; }
    else if (rec) { if (omega_gpu_cmdlist_add(rec, &L) < 0) rc = OMEGA_GPU_FNHC_CHIP_FAIL; }
    else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, NULL) != 0) rc = OMEGA_GPU_FNHC_CHIP_FAIL;
    UNLOCK();
#undef UNLOCK
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}

int omega_gpu_fnhc_run(uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, float eps, OmegaGpuFnHcInfo *info) {
    return core(NULL, phase, a, b, c, d, e, eps, info);
}
int omega_gpu_fnhc_record(struct OmegaGpuCmdList *cl, uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, float eps) {
    if (!cl) return OMEGA_GPU_FNHC_BAD_ARGS;
    return core(cl, phase, a, b, c, d, e, eps, NULL);
}
int omega_gpu_fnhc_prepare(uint32_t phase) {
    if (phase >= OMEGA_FNHC_PHASES) return OMEGA_GPU_FNHC_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = OMEGA_GPU_FNHC_OK;
    if (!dev_open_locked()) rc = OMEGA_GPU_FNHC_CHIP_FAIL;
    Slot *ks = NULL; bool hit = false;
    if (rc == OMEGA_GPU_FNHC_OK) rc = kernel_for(phase, &ks, &hit);
    omega_gpu_session_unlock();
    return rc;
}
