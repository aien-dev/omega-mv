/* omega_gpu_fp4_api.c: FP4 expert GEMV entry points on the shared session (OWN-30B cut L3). See omega_gpu_fp4_api.h.
 * Submission path and buffer policy are the FP8 API's: every buffer both sides touch is GPU-uncached (nvos.h
 * NVOS32_ATTR2_GPU_CACHEABLE: "For system memory this will not be coherent with direct CPU mappings"), one launch =
 * omega_gpu_session_launch. Declared registers = peak + 2 rounded to 8 (omega_bw_gpr_declared; peak + 1 hung the chip). */
#include "omega_gpu_fp4_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include <string.h>

#define CACHE_SLOTS 8
#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)

struct OmegaGpuFp4Tensor {
    uint32_t experts, n, k;
    OmegaGpuMem w; /* experts * n * k / 2 code bytes */
    OmegaGpuMem s; /* experts * n * k / 16 scale bytes */
    float *t2;     /* experts: tensor scale * 2^120 (host copy; goes into the group table) */
};

typedef struct {
    bool valid;
    OmegaFp4GemvSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
} Slot;

static struct {
    OmegaGpuScratch x_buf, y_buf;
    OmegaGpuMem tab;
    uint32_t cta_budget; int cta_budget_set;
    uint32_t lpa, rows, warps;
    int mutant, no_fold;
    bool hooked;
    Slot cache[CACHE_SLOTS];
    unsigned next;
} g = { .cta_budget = 256u };

const char *omega_gpu_fp4_last_error(void) { return omega_gpu_session_last_error(); }
const char *omega_gpu_fp4_rc_name(int rc) {
    switch (rc) {
    case OMEGA_GPU_FP4_OK: return "OK";
    case OMEGA_GPU_FP4_BAD_ARGS: return "BAD_ARGS";
    case OMEGA_GPU_FP4_TOO_LARGE: return "TOO_LARGE";
    case OMEGA_GPU_FP4_CODEGEN_FAIL: return "CODEGEN_FAIL";
    case OMEGA_GPU_FP4_CHIP_FAIL: return "CHIP_FAIL";
    case OMEGA_GPU_FP4_BAD_SCALE: return "BAD_SCALE";
    default: return "UNKNOWN";
    }
}

static void cache_clear_locked(void) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++) {
        if (!g.cache[i].valid) continue;
        omega_gpu_session_free(&g.cache[i].code);
        memset(&g.cache[i], 0, sizeof g.cache[i]);
    }
    g.next = 0;
}
static void on_session_close(void) {
    cache_clear_locked();
    omega_gpu_session_scratch_free(&g.x_buf);
    omega_gpu_session_scratch_free(&g.y_buf);
    omega_gpu_session_free(&g.tab);
}
static OmegaGpuSession *dev_open_locked(void) {
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_session_close); g.hooked = true; }
    return omega_gpu_session_open();
}
void omega_gpu_fp4_cache_clear(void) { omega_gpu_session_lock(); cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_fp4_set_shape(uint32_t lpa, uint32_t rows, uint32_t warps) { omega_gpu_session_lock(); g.lpa = lpa; g.rows = rows; g.warps = warps; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_fp4_set_cta_budget(uint32_t ctas) { omega_gpu_session_lock(); g.cta_budget_set = ctas != 0; g.cta_budget = ctas ? ctas : 256u; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_fp4_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_fp4_test_set_no_fold(int on) { g.no_fold = on ? 1 : 0; }

/* LPA 32 when the row has a multiple of 32 blocks, else 16; two rows per slot; the CTA width is the largest of
 * 16, 12, 8, ... warps whose threads divide k (x staging rounds are exact). */
void omega_gpu_fp4_shape_for(uint32_t k, uint32_t groups, uint32_t *lpa, uint32_t *rows, uint32_t *warps) {
    uint32_t l = k % 512u == 0 ? 32u : 16u, w = 0;
    static const uint32_t cand[] = { 16, 12, 8, 6, 4, 3, 2, 1 };
    for (unsigned i = 0; i < sizeof cand / sizeof cand[0] && !w; i++) if (k % (32u * cand[i]) == 0) w = cand[i];
    (void)groups;
    if (lpa) *lpa = g.lpa ? g.lpa : l;
    if (rows) *rows = g.rows ? g.rows : 2u;
    if (warps) *warps = g.warps ? g.warps : w;
}

static uint32_t gpr_declared(uint32_t peak) { return omega_bw_gpr_declared(peak); }

static bool register_file_fits(uint32_t warps, uint32_t peak) {
    uint32_t decl = gpr_declared(peak);
    if ((uint64_t)32u * warps * decl <= 65536u) return true;
    char e[160]; omega_gpu_fmt(e, sizeof e, "fp4 kernel: %u warps x %u declared registers exceed the 65536-register file", warps, decl);
    omega_gpu_session_set_error(e);
    return false;
}

static int kernel_for(const OmegaFp4GemvSpec *spec, Slot **out, bool *hit) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++) {
        Slot *s = &g.cache[i];
        if (s->valid && memcmp(&s->spec, spec, sizeof *spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_FP4_OK; }
    }
    Slot *s = &g.cache[g.next % CACHE_SLOTS]; g.next = (g.next + 1) % CACHE_SLOTS;
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("fp4 codegen: host memory"); return OMEGA_GPU_FP4_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_fp4_gemv_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fp4 codegen refused the shape"); return OMEGA_GPU_FP4_CODEGEN_FAIL; }
    if (!register_file_fits(spec->warps, prog->regalloc.peak_gpr_usage)) { omega_gpu_host_free(prog); return OMEGA_GPU_FP4_TOO_LARGE; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_FP4_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); return OMEGA_GPU_FP4_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_FP4_CHIP_FAIL; }
    if (s->valid) omega_gpu_session_free(&s->code);
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code;
    *out = s; *hit = false;
    return OMEGA_GPU_FP4_OK;
}

int omega_gpu_fp4_tensor_upload(uint32_t experts, uint32_t n, uint32_t k, const uint8_t *w4, const uint8_t *scales, const float *ts, OmegaGpuFp4Tensor **out) {
    if (out) *out = NULL;
    if (!w4 || !scales || !ts || !out || experts == 0 || n == 0 || k == 0 || k % 256u || n % 8u) return OMEGA_GPU_FP4_BAD_ARGS;
    if (k > OMEGA_FP4_MAX_K) return OMEGA_GPU_FP4_TOO_LARGE;
    const size_t wbytes = (size_t)experts * n * (k / 2u), sbytes = (size_t)experts * n * (k / 16u);
    size_t bad = omega_fp4_scan_scales(scales, sbytes);
    if (bad) {
        char e[120]; omega_gpu_fmt(e, sizeof e, "fp4 upload: illegal scale byte at index %zu", bad - 1);
        omega_gpu_session_set_error(e);
        return OMEGA_GPU_FP4_BAD_SCALE;
    }
    OmegaGpuFp4Tensor *t = omega_gpu_host_calloc(1, sizeof *t);
    if (!t) return OMEGA_GPU_FP4_BAD_ARGS;
    t->experts = experts; t->n = n; t->k = k;
    t->t2 = omega_gpu_host_calloc(experts, sizeof(float));
    if (!t->t2 || omega_fp4_pack_tscale(ts, experts, t->t2, g.no_fold) != 0) {
        omega_gpu_session_set_error("fp4 upload: tensor scale not finite, not positive, or overflows after the 2^120 fold");
        if (t->t2) omega_gpu_host_free(t->t2);
        omega_gpu_host_free(t);
        return OMEGA_GPU_FP4_BAD_SCALE;
    }
    int rc = OMEGA_GPU_FP4_OK;
    omega_gpu_session_lock();
    if (!dev_open_locked() || omega_gpu_session_alloc(wbytes, &t->w) != 0 || omega_gpu_session_alloc(sbytes, &t->s) != 0) rc = OMEGA_GPU_FP4_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_FP4_OK) {
        memcpy(t->w.cpu, w4, wbytes);
        memcpy(t->s.cpu, scales, sbytes);
        if (omega_gpu_session_sync_to_gpu(&t->w) != 0 || omega_gpu_session_sync_to_gpu(&t->s) != 0) rc = OMEGA_GPU_FP4_CHIP_FAIL;
    }
    if (rc != OMEGA_GPU_FP4_OK) { omega_gpu_fp4_tensor_free(t); return rc; }
    *out = t;
    return OMEGA_GPU_FP4_OK;
}

void omega_gpu_fp4_tensor_free(OmegaGpuFp4Tensor *t) {
    if (!t) return;
    omega_gpu_session_lock();
    if (t->w.cpu) omega_gpu_session_free(&t->w);
    if (t->s.cpu) omega_gpu_session_free(&t->s);
    omega_gpu_session_unlock();
    if (t->t2) omega_gpu_host_free(t->t2);
    omega_gpu_host_free(t);
}

int omega_gpu_fp4_gemv_grouped_f32(const OmegaGpuFp4Tensor *t, uint32_t groups, const uint32_t *ids, const float *x, uint32_t x_rows, float *y, OmegaGpuFp4Info *info) {
    uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (!t || !t->w.cpu || !ids || !x || !y || groups == 0 || (x_rows != 1 && x_rows != groups)) return OMEGA_GPU_FP4_BAD_ARGS;
    if (groups > OMEGA_GPU_FP4_MAX_GROUPS) return OMEGA_GPU_FP4_TOO_LARGE;
    for (uint32_t gi = 0; gi < groups; gi++) if (ids[gi] >= t->experts) return OMEGA_GPU_FP4_BAD_ARGS;
    const uint32_t n = t->n, k = t->k;
    OmegaFp4GemvSpec spec; memset(&spec, 0, sizeof spec);
    spec.k = k; spec.mutant = g.mutant;
    omega_gpu_fp4_shape_for(k, groups, &spec.lpa, &spec.rows, &spec.warps);
    const uint32_t rpw = omega_fp4_gemv_rows_per_warp(&spec);
    if (!rpw || n % rpw) return OMEGA_GPU_FP4_CODEGEN_FAIL;
    const uint32_t RG = n / rpw;
    uint32_t grid_x = (RG + spec.warps - 1) / spec.warps;
    uint32_t budget = g.cta_budget_set ? g.cta_budget : (groups > 1u ? OMEGA_GPU_FP4_GROUPED_CTAS : 256u);
    if (grid_x > budget) grid_x = budget;
    if (grid_x == 0) grid_x = 1;

    omega_gpu_session_lock();
    int rc = OMEGA_GPU_FP4_OK;
    if (!dev_open_locked()) { omega_gpu_session_unlock(); return OMEGA_GPU_FP4_CHIP_FAIL; }
    if (!g.tab.cpu && omega_gpu_session_alloc((size_t)OMEGA_GPU_FP4_MAX_GROUPS * OMEGA_FP4_TAB_STRIDE, &g.tab) != 0) { omega_gpu_session_unlock(); return OMEGA_GPU_FP4_CHIP_FAIL; }
    int src = omega_gpu_session_scratch(&g.x_buf, (size_t)x_rows * k * 4u);
    if (src == 0) src = omega_gpu_session_scratch(&g.y_buf, (size_t)groups * n * 4u);
    if (src != 0) { omega_gpu_session_unlock(); return src == OMEGA_GPU_SESSION_SCRATCH_RESERVED ? OMEGA_GPU_FP4_TOO_LARGE : OMEGA_GPU_FP4_CHIP_FAIL; }
    Slot *ks = NULL; bool hit = false;
    rc = kernel_for(&spec, &ks, &hit);
    if (rc != OMEGA_GPU_FP4_OK) { omega_gpu_session_unlock(); return rc; }

    uint64_t *tab = g.tab.cpu;
    const uint64_t wstride = (uint64_t)n * (k / 2u), sstride = (uint64_t)n * (k / 16u);
    for (uint32_t gi = 0; gi < groups; gi++) {
        uint64_t *ent = tab + (size_t)gi * (OMEGA_FP4_TAB_STRIDE / 8u);
        ent[0] = t->w.va + ids[gi] * wstride; ent[1] = t->s.va + ids[gi] * sstride;
        uint32_t w2[2]; memcpy(w2, &t->t2[ids[gi]], 4); w2[1] = 0;
        memcpy(&ent[2], w2, 8); ent[3] = 0;
    }
    float *xd = g.x_buf.mem.cpu, *yd = g.y_buf.mem.cpu;
    for (uint32_t r = 0; r < x_rows; r++) omega_fp4_pack_x(x + (size_t)r * k, k, xd + (size_t)r * k);
    for (size_t i = 0; i < (size_t)groups * n; i++) yd[i] = POISON_F32;
    if (omega_gpu_session_sync_to_gpu(&g.tab) != 0 || omega_gpu_session_sync_to_gpu(&g.x_buf.mem) != 0 || omega_gpu_session_sync_to_gpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_FP4_CHIP_FAIL;

    uint32_t args[OMEGA_FP4_ARGS_WORDS];
    const uint64_t tab_va = g.tab.va, x_va = g.x_buf.mem.va;
    args[0] = (uint32_t)tab_va; args[1] = (uint32_t)(tab_va >> 32);
    args[2] = (uint32_t)x_va; args[3] = (uint32_t)(x_va >> 32);
    args[4] = (uint32_t)g.y_buf.mem.va; args[5] = (uint32_t)(g.y_buf.mem.va >> 32);
    args[6] = RG; args[7] = x_rows == 1 ? 0u : k * 4u; args[8] = n * 4u; args[9] = grid_x * spec.warps;
    if (info) {
        info->experts = t->experts; info->n = n; info->k = k; info->groups = groups;
        info->lpa = spec.lpa; info->rows = spec.rows; info->warps = spec.warps;
        info->grid_x = grid_x; info->grid_y = groups;
        info->weight_bytes = (uint64_t)groups * wstride; info->scale_bytes = (uint64_t)groups * sstride;
        info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_FP4_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = gpr_declared(ks->kernel.gpr_count),
            .threads_x = 32u * spec.warps, .threads_y = 1, .grid_x = grid_x, .grid_y = groups,
            .num_elements = (size_t)groups * n,
            .args = args, .n_args = OMEGA_FP4_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = omega_fp4_gemv_shared_bytes(&spec),
        };
        if (L.gpr_count < ks->kernel.gpr_count + 2u) { omega_gpu_session_set_error("declared registers below peak + 2: refusing to launch"); rc = OMEGA_GPU_FP4_CHIP_FAIL; }
        else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, info ? &info->completion_marker : NULL) != 0) rc = OMEGA_GPU_FP4_CHIP_FAIL;
    }
    if (rc == OMEGA_GPU_FP4_OK && omega_gpu_session_sync_to_cpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_FP4_CHIP_FAIL;
    if (rc == OMEGA_GPU_FP4_OK) {
        size_t unchanged = 0;
        for (size_t i = 0; i < (size_t)groups * n; i++) if (yd[i] == POISON_F32) unchanged++;
        if (unchanged) { omega_gpu_session_set_error("output poison survived"); rc = OMEGA_GPU_FP4_CHIP_FAIL; }
        else memcpy(y, yd, (size_t)groups * n * sizeof *y);
    }
    omega_gpu_session_unlock();
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}
