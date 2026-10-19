/*
 * omega_gpu_fnidx_api.c: kernel cache and launch of the Flash-Next indexer phases (see the header). Same structure as
 * omega_gpu_fnhc_api.c.
 */
#include "omega_gpu_fnidx_api.h"
#include "omega_gpu_session.h"
#include <stdbool.h>
#include <string.h>
#include "omega_gpu_cmdlist.h"

#define MARKER_TIMEOUT_MS 20000ull

typedef struct {
    bool valid;
    OmegaFnIdxSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
    uint32_t insns;
} Slot;

static struct { bool hooked; Slot slot[OMEGA_FNIDX_PHASES]; int mutant; } g;
static int g_rec;

static void slot_clear_locked(uint32_t i) { if (g.slot[i].valid) omega_gpu_session_free(&g.slot[i].code); memset(&g.slot[i], 0, sizeof g.slot[i]); }
static void on_session_close(void) { memset(g.slot, 0, sizeof g.slot); }
static OmegaGpuSession *dev_open_locked(void) {
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_session_close); g.hooked = true; }
    return omega_gpu_session_open();
}
void omega_gpu_fnidx_test_set_mutant(int mutant) {
    omega_gpu_session_lock();
    g.mutant = mutant;
    for (uint32_t i = 0; i < OMEGA_FNIDX_PHASES; i++) slot_clear_locked(i);
    omega_gpu_session_unlock();
}
const char *omega_gpu_fnidx_last_error(void) { return omega_gpu_session_last_error(); }

static int kernel_for(uint32_t phase, Slot **out, bool *hit) {
    OmegaFnIdxSpec spec; memset(&spec, 0, sizeof spec);
    spec.phase = phase;
    spec.mutant = g.mutant;
    { OmegaFnIdxSpec probe = spec; BlackwellIRProgram *pp = omega_gpu_host_calloc(1, sizeof *pp); if (!pp) return OMEGA_GPU_FNIDX_CODEGEN_FAIL;
      if (omega_blackwell_codegen_fnidx_prog(&probe, pp) != 0) spec.mutant = 0; /* a mutant belongs to one phase */
      omega_gpu_host_free(pp); }
    Slot *s = &g.slot[phase];
    if (s->valid && memcmp(&s->spec, &spec, sizeof spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_FNIDX_OK; }
    if (g_rec && s->valid) { omega_gpu_session_set_error("fnidx record: kernel cache miss would evict the resident kernel (prepare first)"); return OMEGA_GPU_FNIDX_CHIP_FAIL; }
    slot_clear_locked(phase);
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("fnidx codegen: host memory"); return OMEGA_GPU_FNIDX_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_fnidx_prog(&spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnidx codegen refused the spec"); return OMEGA_GPU_FNIDX_CODEGEN_FAIL; }
    if ((uint64_t)OMEGA_FNIDX_THREADS * omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage) > 65536u) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fnidx: register file"); return OMEGA_GPU_FNIDX_CODEGEN_FAIL; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_FNIDX_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    uint32_t insns = (uint32_t)prog->count;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); omega_gpu_session_set_error("fnidx encode"); return OMEGA_GPU_FNIDX_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_FNIDX_CHIP_FAIL; }
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = spec; s->kernel = kernel; s->code = code; s->insns = insns;
    *out = s; *hit = false;
    return OMEGA_GPU_FNIDX_OK;
}

static int core(struct OmegaGpuCmdList *rec, uint32_t phase, uint64_t a, uint64_t b, uint64_t c, uint32_t nb, uint32_t K, uint32_t vis, float scale, OmegaGpuFnIdxInfo *info) {
    uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (phase >= OMEGA_FNIDX_PHASES || !a || !b || nb == 0 || nb > OMEGA_FNIDX_MAX_BLOCKS) return OMEGA_GPU_FNIDX_BAD_ARGS;
    if (!rec) omega_gpu_session_lock();
#define UNLOCK() do { if (!rec) omega_gpu_session_unlock(); } while (0)
    if (!dev_open_locked()) { UNLOCK(); return OMEGA_GPU_FNIDX_CHIP_FAIL; }
    Slot *ks = NULL; bool hit = false;
    g_rec = rec != NULL;
    int rc = kernel_for(phase, &ks, &hit);
    g_rec = 0;
    if (rc != OMEGA_GPU_FNIDX_OK) { UNLOCK(); return rc; }
    uint32_t args[OMEGA_FNIDX_ARGS_WORDS]; memset(args, 0, sizeof args);
    uint64_t v[3] = { a, b, c };
    for (int i = 0; i < 3; i++) { args[2 * i] = (uint32_t)v[i]; args[2 * i + 1] = (uint32_t)(v[i] >> 32); }
    args[6] = nb; args[7] = K; args[8] = vis; memcpy(&args[9], &scale, 4);
    const uint32_t grid = omega_fnidx_grid(phase, nb);
    OmegaGpuLaunch L = {
        .code_va = ks->code.va, .gpr_count = omega_bw_gpr_declared(ks->kernel.gpr_count),
        .threads_x = OMEGA_FNIDX_THREADS, .threads_y = 1, .grid_x = grid, .grid_y = 1,
        .num_elements = (size_t)nb,
        .args = args, .n_args = OMEGA_FNIDX_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
        .shared_bytes = OMEGA_FNIDX_SHARED_BYTES,
    };
    if (info) { info->phase = phase; info->grid_x = grid; info->threads = OMEGA_FNIDX_THREADS; info->gprs = ks->kernel.gpr_count; info->insns = ks->insns; info->kernel_cache_hit = hit; }
    if (!omega_bw_gpr_declared_ok(L.gpr_count, ks->kernel.gpr_count)) { omega_gpu_session_set_error("declared registers below peak + 2: refusing to launch"); rc = OMEGA_GPU_FNIDX_CHIP_FAIL; }
    else if (rec) { if (omega_gpu_cmdlist_add(rec, &L) < 0) rc = OMEGA_GPU_FNIDX_CHIP_FAIL; }
    else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, NULL) != 0) rc = OMEGA_GPU_FNIDX_CHIP_FAIL;
    UNLOCK();
#undef UNLOCK
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}

static const float SCALE = 0.08838834764831845f; /* 1 / sqrt(128) */

int omega_gpu_fnidx_score_run(uint64_t q_va, uint64_t kb_va, uint64_t scores_va, uint32_t nb, OmegaGpuFnIdxInfo *info) {
    return core(NULL, OMEGA_FNIDX_SCORE, q_va, kb_va, scores_va, nb, 0, 0, SCALE, info);
}
int omega_gpu_fnidx_select_run(uint64_t scores_va, uint64_t plist_va, uint32_t nb, uint32_t vis, OmegaGpuFnIdxInfo *info) {
    if (nb == 0 || vis < nb * 4u || vis - nb * 4u > 3u) return OMEGA_GPU_FNIDX_BAD_ARGS;
    return core(NULL, OMEGA_FNIDX_SELECT, scores_va, plist_va, plist_va, nb, nb < OMEGA_FNIDX_BUDGET_BLOCKS ? nb : OMEGA_FNIDX_BUDGET_BLOCKS, vis, 0.0f, info);
}
int omega_gpu_fnidx_score_record(struct OmegaGpuCmdList *cl, uint64_t q_va, uint64_t kb_va, uint64_t scores_va, uint32_t nb) {
    if (!cl) return OMEGA_GPU_FNIDX_BAD_ARGS;
    return core(cl, OMEGA_FNIDX_SCORE, q_va, kb_va, scores_va, nb, 0, 0, SCALE, NULL);
}
int omega_gpu_fnidx_select_record(struct OmegaGpuCmdList *cl, uint64_t scores_va, uint64_t plist_va, uint32_t nb, uint32_t vis) {
    if (!cl || nb == 0 || vis < nb * 4u || vis - nb * 4u > 3u) return OMEGA_GPU_FNIDX_BAD_ARGS;
    return core(cl, OMEGA_FNIDX_SELECT, scores_va, plist_va, plist_va, nb, nb < OMEGA_FNIDX_BUDGET_BLOCKS ? nb : OMEGA_FNIDX_BUDGET_BLOCKS, vis, 0.0f, NULL);
}
int omega_gpu_fnidx_prepare(uint32_t phase) {
    if (phase >= OMEGA_FNIDX_PHASES) return OMEGA_GPU_FNIDX_BAD_ARGS;
    omega_gpu_session_lock();
    int rc = OMEGA_GPU_FNIDX_OK;
    if (!dev_open_locked()) rc = OMEGA_GPU_FNIDX_CHIP_FAIL;
    Slot *ks = NULL; bool hit = false;
    if (rc == OMEGA_GPU_FNIDX_OK) rc = kernel_for(phase, &ks, &hit);
    omega_gpu_session_unlock();
    return rc;
}
