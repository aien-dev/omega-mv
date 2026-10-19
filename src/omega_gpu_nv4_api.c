/* omega_gpu_nv4_api.c: grouped NVFP4 GEMV entry points on the shared session (flashnext/port). See the header.
 * Session, kernel cache, poison and launch plumbing follow omega_gpu_w4_api.c. */
#include "omega_gpu_nv4_api.h"
#include "omega_gpu_fp8_api.h" /* omega_fp8_pack_x_f32 */
#include "omega_blackwell_codegen_moe.h"
#include "omega_blackwell_codegen_w4.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_gpu_cmdlist.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SLOTS 8
#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)

struct OmegaGpuNv4Slab {
    uint32_t n, k, count;
    uint64_t block;  /* bytes per block */
    OmegaGpuMem mem; /* count blocks back to back */
};

typedef struct {
    bool valid;
    OmegaNv4GemvSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
} Slot;

static struct {
    OmegaGpuScratch x_buf, y_buf;
    OmegaGpuMem tab;
    uint32_t cta_budget, rows, warps;
    int sched, mutant;
    bool hooked;
    Slot cache[CACHE_SLOTS];
    unsigned next;
} g = { .sched = -1 };
static int g_rec;

const char *omega_gpu_nv4_last_error(void) { return omega_gpu_session_last_error(); }

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
void omega_gpu_nv4_set_shape(uint32_t rows, uint32_t warps) { omega_gpu_session_lock(); g.rows = rows; g.warps = warps; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_nv4_set_cta_budget(uint32_t ctas) { omega_gpu_session_lock(); g.cta_budget = ctas; omega_gpu_session_unlock(); }
void omega_gpu_nv4_set_sched(int sched) { omega_gpu_session_lock(); g.sched = sched; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_nv4_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; cache_clear_locked(); omega_gpu_session_unlock(); }

/* Automatic shape: 2 rows, 8 warps, pipelined decode (the W4 kernel's measured default; NOT re-measured for NV4 yet). */
void omega_gpu_nv4_spec_for(uint32_t k, uint32_t n, OmegaNv4GemvSpec *spec) {
    (void)n;
    memset(spec, 0, sizeof *spec);
    spec->k = k;
    spec->rows = g.rows ? g.rows : 2u;
    spec->warps = g.warps ? g.warps : 8u;
    spec->sched = g.sched >= 0 ? (uint32_t)g.sched : 1u;
    spec->mutant = g.mutant;
}

static bool register_file_fits(uint32_t warps, uint32_t peak) {
    const uint32_t decl = omega_bw_gpr_declared(peak);
    if ((uint64_t)32u * warps * decl <= 65536u) return true;
    char e[160]; omega_gpu_fmt(e, sizeof e, "nv4 kernel: %u warps x %u declared registers exceed the 65536-register file", warps, decl);
    omega_gpu_session_set_error(e);
    return false;
}

static int kernel_for(const OmegaNv4GemvSpec *spec, Slot **out, bool *hit) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++) {
        Slot *s = &g.cache[i];
        if (s->valid && memcmp(&s->spec, spec, sizeof *spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_NV4_OK; }
    }
    Slot *s = &g.cache[g.next];
    g.next = (g.next + 1u) % CACHE_SLOTS;
    if (g_rec && s->valid) { omega_gpu_session_set_error("nv4 record: kernel cache miss would evict a resident kernel (warm up before recording)"); return OMEGA_GPU_NV4_TOO_LARGE; }
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("nv4 codegen: host memory"); return OMEGA_GPU_NV4_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_nv4_gemv_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("nv4 codegen refused the shape"); return OMEGA_GPU_NV4_CODEGEN_FAIL; }
    if (!register_file_fits(spec->warps, prog->regalloc.peak_gpr_usage)) { omega_gpu_host_free(prog); return OMEGA_GPU_NV4_TOO_LARGE; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_NV4_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); return OMEGA_GPU_NV4_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_NV4_CHIP_FAIL; }
    if (s->valid) omega_gpu_session_free(&s->code);
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code;
    *out = s; *hit = false;
    return OMEGA_GPU_NV4_OK;
}

int omega_gpu_nv4_slab_alloc(uint32_t n, uint32_t k, uint32_t count, OmegaGpuNv4Slab **out) {
    if (out) *out = NULL;
    if (!out || n == 0 || count == 0 || k < 64u || k % 64u) return OMEGA_GPU_NV4_BAD_ARGS;
    if (k > OMEGA_NV4_MAX_K) return OMEGA_GPU_NV4_TOO_LARGE;
    OmegaGpuNv4Slab *s = omega_gpu_host_calloc(1, sizeof *s);
    if (!s) return OMEGA_GPU_NV4_BAD_ARGS;
    s->n = n; s->k = k; s->count = count;
    s->block = (omega_nv4_block_bytes(n, k) + 255u) & ~(uint64_t)255u; /* blocks 256-byte aligned */
    int rc = OMEGA_GPU_NV4_OK;
    omega_gpu_session_lock();
    if (!dev_open_locked() || omega_gpu_session_alloc((size_t)(s->block * count), &s->mem) != 0) rc = OMEGA_GPU_NV4_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc != OMEGA_GPU_NV4_OK) { omega_gpu_host_free(s); return rc; }
    memset(s->mem.cpu, 0, (size_t)(s->block * count));
    *out = s;
    return OMEGA_GPU_NV4_OK;
}
void omega_gpu_nv4_slab_free(OmegaGpuNv4Slab *s) {
    if (!s) return;
    omega_gpu_session_lock();
    if (s->mem.cpu) omega_gpu_session_free(&s->mem);
    omega_gpu_session_unlock();
    omega_gpu_host_free(s);
}
void omega_gpu_nv4_slab_shape(const OmegaGpuNv4Slab *s, uint32_t *n, uint32_t *k, uint32_t *count) {
    if (n) *n = s ? s->n : 0;
    if (k) *k = s ? s->k : 0;
    if (count) *count = s ? s->count : 0;
}
int omega_gpu_nv4_slab_set(OmegaGpuNv4Slab *s, uint32_t idx, const uint8_t *nib, const uint8_t *e4m3, const float *row_global) {
    if (!s || !nib || !e4m3 || !row_global || idx >= s->count) return OMEGA_GPU_NV4_BAD_ARGS;
    const size_t nb = (size_t)s->n * s->k / 2u, sb = (size_t)s->n * s->k / OMEGA_NV4_GROUP;
    for (size_t i = 0; i < sb; i++)
        if ((e4m3[i] & 0x7Fu) == 0x7Fu) {
            char e[120]; omega_gpu_fmt(e, sizeof e, "nv4 set: e4m3 NaN scale byte %02x at %zu", e4m3[i], i);
            omega_gpu_session_set_error(e);
            return OMEGA_GPU_NV4_BAD_ARGS;
        }
    for (uint32_t r = 0; r < s->n; r++)
        if (!(row_global[r] >= 0.0f) || !(row_global[r] < 1e30f)) { omega_gpu_session_set_error("nv4 set: row scale not finite and >= 0"); return OMEGA_GPU_NV4_BAD_ARGS; }
    uint8_t *b = (uint8_t *)s->mem.cpu + (size_t)s->block * idx;
    memcpy(b, nib, nb);
    memcpy(b + nb, e4m3, sb);
    float *rs = (float *)(b + nb + sb);
    for (uint32_t r = 0; r < s->n; r++) rs[r] = ldexpf(row_global[r], OMEGA_NV4_ROWSCALE_FOLD);
    return OMEGA_GPU_NV4_OK;
}
int omega_gpu_nv4_slab_commit(OmegaGpuNv4Slab *s) {
    if (!s) return OMEGA_GPU_NV4_BAD_ARGS;
    return omega_gpu_session_sync_to_gpu(&s->mem) == 0 ? OMEGA_GPU_NV4_OK : OMEGA_GPU_NV4_CHIP_FAIL;
}
int omega_gpu_nv4_slab_entry(const OmegaGpuNv4Slab *s, uint32_t idx, uint64_t *w_va, uint64_t *s_va) {
    if (!s || !w_va || !s_va || idx >= s->count) return OMEGA_GPU_NV4_BAD_ARGS;
    *w_va = s->mem.va + s->block * idx;
    *s_va = *w_va + (uint64_t)s->n * s->k / 2u;
    return OMEGA_GPU_NV4_OK;
}

/* grid: the CTA budget shared by the groups, every warp the same number of row groups, at least two waves per group */
static uint32_t grid_for(const OmegaNv4GemvSpec *spec, uint32_t n, uint32_t groups) {
    const uint32_t RG = n / spec->rows;
    uint32_t budget = (g.cta_budget ? g.cta_budget : OMEGA_GPU_NV4_CTAS) / (groups ? groups : 1u);
    if (budget < 1u) budget = 1u;
    uint32_t grid_x = (RG + spec->warps - 1u) / spec->warps;
    if (!g.cta_budget) { uint32_t waves = (grid_x + budget - 1u) / budget; if (waves < 2u) waves = 2u; grid_x = (grid_x + waves - 1u) / waves; }
    if (grid_x > budget) grid_x = budget;
    return grid_x ? grid_x : 1u;
}

static int gemv_core(uint32_t n, uint32_t k, uint32_t groups, const OmegaGpuNv4Slab *s, const uint32_t *ids, const float *x, uint32_t x_rows,
                     float *y, OmegaGpuNv4Info *info, OmegaGpuCmdList *rec, uint64_t dev_tab, uint64_t dev_x, uint32_t dev_xs, uint64_t dev_y) {
    const uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (groups == 0 || groups > OMEGA_GPU_NV4_MAX_GROUPS) return OMEGA_GPU_NV4_BAD_ARGS;
    if (rec ? (!dev_tab || !dev_x || !dev_y) : (!s || !ids || !x || !y || (x_rows != 1u && x_rows != groups))) return OMEGA_GPU_NV4_BAD_ARGS;
    if (!rec) for (uint32_t i = 0; i < groups; i++) if (ids[i] >= s->count) return OMEGA_GPU_NV4_BAD_ARGS;
    OmegaNv4GemvSpec spec; omega_gpu_nv4_spec_for(k, n, &spec);
    if (n % spec.rows || !omega_nv4_gemv_spec_ok(&spec)) return OMEGA_GPU_NV4_CODEGEN_FAIL;
    const uint32_t RG = n / spec.rows, grid_x = grid_for(&spec, n, groups);
    if (!rec) omega_gpu_session_lock();
#define UNLOCK() do { if (!rec) omega_gpu_session_unlock(); } while (0)
    int rc = OMEGA_GPU_NV4_OK;
    if (!dev_open_locked()) { UNLOCK(); return OMEGA_GPU_NV4_CHIP_FAIL; }
    if (!rec) {
        if (!g.tab.cpu && omega_gpu_session_alloc(16u * OMEGA_GPU_NV4_MAX_GROUPS, &g.tab) != 0) { UNLOCK(); return OMEGA_GPU_NV4_CHIP_FAIL; }
        int src = omega_gpu_session_scratch(&g.x_buf, (size_t)x_rows * k * 4u);
        if (src == 0) src = omega_gpu_session_scratch(&g.y_buf, (size_t)groups * n * 4u);
        if (src != 0) { UNLOCK(); return src == OMEGA_GPU_SESSION_SCRATCH_RESERVED ? OMEGA_GPU_NV4_TOO_LARGE : OMEGA_GPU_NV4_CHIP_FAIL; }
    }
    Slot *ks = NULL; bool hit = false;
    g_rec = rec != NULL;
    rc = kernel_for(&spec, &ks, &hit);
    g_rec = 0;
    if (rc != OMEGA_GPU_NV4_OK) { UNLOCK(); return rc; }
    float *yd = NULL;
    if (!rec) {
        uint64_t *tab = g.tab.cpu;
        for (uint32_t i = 0; i < groups; i++) omega_gpu_nv4_slab_entry(s, ids[i], &tab[2 * i], &tab[2 * i + 1]);
        for (uint32_t r = 0; r < x_rows; r++) omega_fp8_pack_x_f32(x + (size_t)r * k, k, (float *)g.x_buf.mem.cpu + (size_t)r * k);
        yd = g.y_buf.mem.cpu;
        for (size_t i = 0; i < (size_t)groups * n; i++) yd[i] = POISON_F32;
        if (omega_gpu_session_sync_to_gpu(&g.tab) != 0 || omega_gpu_session_sync_to_gpu(&g.x_buf.mem) != 0 || omega_gpu_session_sync_to_gpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_NV4_CHIP_FAIL;
    }
    const uint64_t tab_va = rec ? dev_tab : g.tab.va, x_va = rec ? dev_x : g.x_buf.mem.va, y_va = rec ? dev_y : g.y_buf.mem.va;
    const uint32_t xs = rec ? dev_xs : (x_rows == 1u ? 0u : k * 4u);
    uint32_t args[OMEGA_FP8_ARGS_WORDS];
    memset(args, 0, sizeof args);
    args[0] = (uint32_t)tab_va; args[1] = (uint32_t)(tab_va >> 32);
    args[2] = (uint32_t)x_va; args[3] = (uint32_t)(x_va >> 32);
    args[4] = (uint32_t)y_va; args[5] = (uint32_t)(y_va >> 32);
    args[6] = RG; args[7] = xs; args[8] = n * 4u; args[9] = grid_x * spec.warps;
    if (info) {
        info->n = n; info->k = k; info->groups = groups; info->rows_per_warp = spec.rows; info->warps_per_cta = spec.warps; info->sched = spec.sched;
        info->grid_x = grid_x; info->weight_bytes = (uint64_t)groups * omega_nv4_block_bytes(n, k); info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_NV4_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = omega_bw_gpr_declared(ks->kernel.gpr_count),
            .threads_x = 32u * spec.warps, .threads_y = 1, .grid_x = grid_x, .grid_y = groups,
            .num_elements = (size_t)groups * n, .args = args, .n_args = OMEGA_FP8_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = 0,
        };
        if (rec) { if (omega_gpu_cmdlist_add(rec, &L) < 0) rc = OMEGA_GPU_NV4_CHIP_FAIL; }
        else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, NULL) != 0) rc = OMEGA_GPU_NV4_CHIP_FAIL;
    }
    if (rc == OMEGA_GPU_NV4_OK && !rec && omega_gpu_session_sync_to_cpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_NV4_CHIP_FAIL;
    if (rc == OMEGA_GPU_NV4_OK && !rec) {
        size_t unchanged = 0;
        for (size_t i = 0; i < (size_t)groups * n; i++) if (yd[i] == POISON_F32) unchanged++;
        if (unchanged) { omega_gpu_session_set_error("nv4: output poison survived"); rc = OMEGA_GPU_NV4_CHIP_FAIL; }
        else memcpy(y, yd, (size_t)groups * n * sizeof *y);
    }
    UNLOCK();
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}
#undef UNLOCK

int omega_gpu_nv4_gemv_grouped_f32(const OmegaGpuNv4Slab *s, const uint32_t *ids, uint32_t groups, const float *x, uint32_t x_rows,
                                   float *y, OmegaGpuNv4Info *info) {
    if (!s) return OMEGA_GPU_NV4_BAD_ARGS;
    return gemv_core(s->n, s->k, groups, s, ids, x, x_rows, y, info, NULL, 0, 0, 0, 0);
}
int omega_gpu_nv4_gemv_record(OmegaGpuCmdList *cl, uint32_t n, uint32_t k, uint32_t groups, uint64_t tab_va, uint64_t x_va,
                              uint32_t x_stride_bytes, uint64_t y_va) {
    if (!cl) return OMEGA_GPU_NV4_BAD_ARGS;
    return gemv_core(n, k, groups, NULL, NULL, NULL, 1, NULL, NULL, cl, tab_va, x_va, x_stride_bytes, y_va);
}
int omega_gpu_nv4_prepare(uint32_t k, uint32_t n) {
    OmegaNv4GemvSpec spec; omega_gpu_nv4_spec_for(k, n, &spec);
    omega_gpu_session_lock();
    int rc = dev_open_locked() ? OMEGA_GPU_NV4_OK : OMEGA_GPU_NV4_CHIP_FAIL;
    Slot *s = NULL; bool hit = false;
    if (rc == OMEGA_GPU_NV4_OK) rc = kernel_for(&spec, &s, &hit);
    omega_gpu_session_unlock();
    return rc;
}

/* ---- host ---- */
double omega_nv4_e4m3_ref(uint8_t b) {
    const int e = (b >> 3) & 15, m = b & 7;
    const double v = e == 0 ? ldexp((double)m, -9) : ldexp(1.0 + m / 8.0, e - 7);
    return (b & 0x80u) ? -v : v;
}
void omega_nv4_gemv_oracle(const uint8_t *nib, const uint8_t *e4m3, const float *row_global, uint32_t n, uint32_t k, const float *x, double *y) {
    const uint32_t gpr = k / OMEGA_NV4_GROUP;
    for (uint32_t r = 0; r < n; r++) {
        const uint8_t *row = nib + (size_t)r * (k / 2u);
        double acc = 0.0;
        for (uint32_t gi = 0; gi < gpr; gi++) {
            double d = 0.0;
            for (uint32_t c = gi * OMEGA_NV4_GROUP; c < (gi + 1u) * OMEGA_NV4_GROUP; c++) d += (double)omega_w4_e2m1_ref(OMEGA_W4_NIBBLE(row, c)) * x[c];
            acc += d * omega_nv4_e4m3_ref(e4m3[(size_t)r * gpr + gi]);
        }
        y[r] = acc * (double)row_global[r];
    }
}
