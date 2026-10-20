/* omega_gpu_w4_api.c: MXFP4 GEMV entry points on the shared session (qwen38/w4-gemv). See the header.
 * Submission path and buffer policy are the FP8 API's (omega_gpu_fp8_api.c): weights and scales are written once by
 * the host and synced before any launch; staging buffers both sides touch are GPU-uncached (nvos.h
 * NVOS32_ATTR2_GPU_CACHEABLE: "For system memory this will not be coherent with direct CPU mappings"). */
#include "omega_gpu_w4_api.h"
#include "omega_gpu_fp8_api.h" /* omega_fp8_e4m3_ref, omega_fp8_pack_x_f32 */
#include "omega_blackwell_codegen_moe.h"
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include "omega_gpu_cmdlist.h"
#include "omega_blackwell_matmul.h" /* omega_bf16_to_fp32 */
#include <stdlib.h>
#include <string.h>

#define CACHE_SLOTS 16
#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)

struct OmegaGpuW4Tensor {
    uint32_t n, k;
    OmegaGpuMem w;  /* ONE buffer: n * k / 2 weight bytes, then the n * k / 32 scale bytes at s_off (one handle slot per tensor:
                     * the whole 27B with its FP8 copy beside it needs ~900 of the 1022 buffer slots, two per tensor did not fit) */
    size_t s_off;   /* wbytes rounded up to 4 KiB */
};

typedef struct {
    bool valid;
    OmegaW4GemvSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
} Slot;

static struct {
    OmegaGpuScratch x_buf, y_buf;
    OmegaGpuMem tab;
    uint32_t cta_budget, lane_bytes, rows, warps, rows_small;
    int sched, coal, pf, mutant;
    bool hooked;
    Slot cache[CACHE_SLOTS];
    unsigned next;
} g = { .sched = -1, .coal = -1, .pf = -1 };
static int g_rec;

const char *omega_gpu_w4_last_error(void) { return omega_gpu_session_last_error(); }

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
void omega_gpu_w4_set_shape(uint32_t lb, uint32_t rows, uint32_t warps) {
    omega_gpu_session_lock(); g.lane_bytes = lb; g.rows = rows; g.warps = warps; cache_clear_locked(); omega_gpu_session_unlock();
}
void omega_gpu_w4_set_cta_budget(uint32_t ctas) { omega_gpu_session_lock(); g.cta_budget = ctas; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_w4_set_sched(int sched) { omega_gpu_session_lock(); g.sched = sched; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_w4_set_coal(int coal) { omega_gpu_session_lock(); g.coal = coal; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_w4_set_pf(int pf) { omega_gpu_session_lock(); g.pf = pf; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_w4_set_rows_small(uint32_t rows) { omega_gpu_session_lock(); g.rows_small = rows; cache_clear_locked(); omega_gpu_session_unlock(); }
void omega_gpu_w4_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; cache_clear_locked(); omega_gpu_session_unlock(); }

/* a small unsigned from the environment, read once per slot (spec_for and grid_for run on every record) */
static uint32_t env_u32(const char *name, int *seen, uint32_t *v) {
    if (!*seen) { const char *s = getenv(name); *v = s ? (uint32_t)strtoul(s, NULL, 10) : 0u; *seen = 1; }
    return *v;
}
static uint32_t env_rows_small(void) { static int seen; static uint32_t v; return env_u32("AIEN_OMEGA_W4_ROWS_SMALL", &seen, &v); }
static uint32_t env_ctas(void) { static int seen; static uint32_t v; return env_u32("AIEN_OMEGA_W4_CTAS", &seen, &v); }
/* pf default: AIEN_OMEGA_W4_PF=0|1|2 when set_pf was never called (read once), else 0 */
static uint32_t env_pf(void) {
    static int v = -1;
    if (v < 0) { const char *s = getenv("AIEN_OMEGA_W4_PF"); v = (s && s[0] >= '0' && s[0] <= '2' && s[1] == 0) ? s[0] - '0' : 0; }
    return (uint32_t)v;
}

/* Automatic shape: 16-byte lanes (one whole MX group per lane step), 2 rows and 8 warps (measured below; the FP8 big-k shape has 16,
 * omega_gpu_fp8_api.c), the coalesced store when the round fits a warp, the pipelined decode. Sweep receipts:
 * ~/handoffs/2026-10-19-ab-fp8head-4bit.md. */
void omega_gpu_w4_spec_for(uint32_t k, uint32_t n, OmegaW4GemvSpec *spec) {
    memset(spec, 0, sizeof *spec);
    spec->k = k;
    spec->lane_bytes = g.lane_bytes ? g.lane_bytes : (k % 1024u == 0 ? 16u : 8u);
    spec->rows = g.rows ? g.rows : 2u;
    /* speed3: n <= 8192 (o_proj, down: n 5120) can take finer row groups so the last round of warps is nearly full;
     * set_rows_small or AIEN_OMEGA_W4_ROWS_SMALL=1|2|4, only when the shape was not forced */
    if (!g.rows && n <= 8192u) { const uint32_t rs = g.rows_small ? g.rows_small : env_rows_small(); if (rs == 1u || rs == 2u || rs == 4u) spec->rows = rs; }
    /* 8 warps (2026-10-19, graph timeline, 2 layers, 3 interleaved rounds, runs/g4r): mix + o + gate_up + down median
     * 1975.1 us at 16,2,8 vs 2006.6 at 16,2,16, 2107.9 at 16,4,4 (coal off), 2046.3 at 16,2,8 (coal off) */
    spec->warps = g.warps ? g.warps : 8u;
    spec->sched = g.sched >= 0 ? (uint32_t)g.sched : 1u;
    spec->coal = (g.coal >= 0 ? g.coal : 1) && spec->warps * spec->rows <= 32u;
    spec->pf = g.pf >= 0 ? (uint32_t)g.pf : env_pf();
    spec->mutant = g.mutant;
}

static bool register_file_fits(uint32_t warps, uint32_t peak) {
    const uint32_t decl = omega_bw_gpr_declared(peak);
    if ((uint64_t)32u * warps * decl <= 65536u) return true;
    char e[160]; omega_gpu_fmt(e, sizeof e, "w4 kernel: %u warps x %u declared registers exceed the 65536-register file", warps, decl);
    omega_gpu_session_set_error(e);
    return false;
}

static int kernel_for(const OmegaW4GemvSpec *spec, Slot **out, bool *hit) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++) {
        Slot *s = &g.cache[i];
        if (s->valid && memcmp(&s->spec, spec, sizeof *spec) == 0) { *out = s; *hit = true; return OMEGA_GPU_W4_OK; }
    }
    Slot *s = &g.cache[g.next];
    g.next = (g.next + 1u) % CACHE_SLOTS;
    if (g_rec && s->valid) { omega_gpu_session_set_error("w4 record: kernel cache miss would evict a resident kernel (warm up before recording)"); return OMEGA_GPU_W4_TOO_LARGE; }
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) { omega_gpu_session_set_error("w4 codegen: host memory"); return OMEGA_GPU_W4_CODEGEN_FAIL; }
    int cg = omega_blackwell_codegen_w4_gemv_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("w4 codegen refused the shape"); return OMEGA_GPU_W4_CODEGEN_FAIL; }
    if (!register_file_fits(spec->warps, prog->regalloc.peak_gpr_usage)) { omega_gpu_host_free(prog); return OMEGA_GPU_W4_TOO_LARGE; }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_W4_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); return OMEGA_GPU_W4_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_W4_CHIP_FAIL; }
    if (s->valid) omega_gpu_session_free(&s->code);
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code;
    *out = s; *hit = false;
    return OMEGA_GPU_W4_OK;
}

int omega_gpu_w4_tensor_upload(uint32_t n, uint32_t k, const uint8_t *w4, const uint8_t *e8, OmegaGpuW4Tensor **out) {
    if (out) *out = NULL;
    if (!w4 || !e8 || !out || n == 0 || k == 0 || k % 512u) return OMEGA_GPU_W4_BAD_ARGS;
    if (k > OMEGA_W4_MAX_K) return OMEGA_GPU_W4_TOO_LARGE;
    const size_t wbytes = (size_t)n * k / 2u, sbytes = (size_t)n * k / OMEGA_W4_GROUP;
    for (size_t i = 0; i < sbytes; i++)
        if (e8[i] > OMEGA_W4_E_MAX) {
            char e[120]; omega_gpu_fmt(e, sizeof e, "w4 upload: scale byte %u at %zu above %u", e8[i], i, OMEGA_W4_E_MAX);
            omega_gpu_session_set_error(e);
            return OMEGA_GPU_W4_BAD_ARGS;
        }
    OmegaGpuW4Tensor *t = omega_gpu_host_calloc(1, sizeof *t);
    if (!t) return OMEGA_GPU_W4_BAD_ARGS;
    t->n = n; t->k = k;
    int rc = OMEGA_GPU_W4_OK;
    omega_gpu_session_lock();
    t->s_off = (wbytes + 4095u) & ~(size_t)4095u;
    if (!dev_open_locked() || omega_gpu_session_alloc(t->s_off + sbytes, &t->w) != 0) rc = OMEGA_GPU_W4_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_W4_OK) {
        memcpy(t->w.cpu, w4, wbytes);
        memcpy((uint8_t *)t->w.cpu + t->s_off, e8, sbytes);
        if (omega_gpu_session_sync_to_gpu(&t->w) != 0) rc = OMEGA_GPU_W4_CHIP_FAIL;
    }
    if (rc != OMEGA_GPU_W4_OK) { omega_gpu_w4_tensor_free(t); return rc; }
    *out = t;
    return OMEGA_GPU_W4_OK;
}

void omega_gpu_w4_tensor_free(OmegaGpuW4Tensor *t) {
    if (!t) return;
    omega_gpu_session_lock();
    if (t->w.cpu) omega_gpu_session_free(&t->w);
    omega_gpu_session_unlock();
    omega_gpu_host_free(t);
}
void omega_gpu_w4_tensor_shape(const OmegaGpuW4Tensor *t, uint32_t *n, uint32_t *k) { if (n) *n = t ? t->n : 0; if (k) *k = t ? t->k : 0; }
size_t omega_gpu_w4_tensor_bytes(const OmegaGpuW4Tensor *t) { return t ? (size_t)t->n * t->k / 2u + (size_t)t->n * t->k / OMEGA_W4_GROUP : 0; }
int omega_gpu_w4_tensor_entry(const OmegaGpuW4Tensor *t, uint64_t *w_va, uint64_t *s_va) {
    if (!t || !w_va || !s_va) return OMEGA_GPU_W4_BAD_ARGS;
    *w_va = t->w.va; *s_va = t->w.va + t->s_off;
    return OMEGA_GPU_W4_OK;
}

/* grid: up to the CTA budget, split so every warp gets the same number of row groups, at least two */
static uint32_t grid_for(const OmegaW4GemvSpec *spec, uint32_t n) {
    const uint32_t forced = g.cta_budget ? g.cta_budget : env_ctas(); /* AIEN_OMEGA_W4_CTAS: whole-model sweeps */
    const uint32_t RG = n / spec->rows, budget = forced ? forced : OMEGA_GPU_W4_CTAS;
    uint32_t grid_x = (RG + spec->warps - 1u) / spec->warps;
    if (!forced) { uint32_t waves = (grid_x + budget - 1u) / budget; if (waves < 2u) waves = 2u; grid_x = (grid_x + waves - 1u) / waves; }
    if (grid_x > budget) grid_x = budget;
    return grid_x ? grid_x : 1u;
}

static int gemv_core(const OmegaGpuW4Tensor *t, const float *x, float *y, OmegaGpuW4Info *info,
                     OmegaGpuCmdList *rec, uint64_t dev_tab, uint64_t dev_x, uint64_t dev_y) {
    const uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    if (!t || !t->w.cpu || (rec && (!dev_tab || !dev_x || !dev_y)) || (!rec && (!x || !y))) return OMEGA_GPU_W4_BAD_ARGS;
    const uint32_t n = t->n, k = t->k;
    OmegaW4GemvSpec spec; omega_gpu_w4_spec_for(k, n, &spec);
    if (n % spec.rows || k % (64u * spec.lane_bytes)) return OMEGA_GPU_W4_CODEGEN_FAIL;
    const uint32_t RG = n / spec.rows, grid_x = grid_for(&spec, n);
    if (!rec) omega_gpu_session_lock();
#define UNLOCK() do { if (!rec) omega_gpu_session_unlock(); } while (0)
    int rc = OMEGA_GPU_W4_OK;
    if (!dev_open_locked()) { UNLOCK(); return OMEGA_GPU_W4_CHIP_FAIL; }
    if (!rec) {
        if (!g.tab.cpu && omega_gpu_session_alloc(16u, &g.tab) != 0) { UNLOCK(); return OMEGA_GPU_W4_CHIP_FAIL; }
        int src = omega_gpu_session_scratch(&g.x_buf, (size_t)k * 4u);
        if (src == 0) src = omega_gpu_session_scratch(&g.y_buf, (size_t)n * 4u);
        if (src != 0) { UNLOCK(); return src == OMEGA_GPU_SESSION_SCRATCH_RESERVED ? OMEGA_GPU_W4_TOO_LARGE : OMEGA_GPU_W4_CHIP_FAIL; }
    }
    Slot *ks = NULL; bool hit = false;
    g_rec = rec != NULL;
    rc = kernel_for(&spec, &ks, &hit);
    g_rec = 0;
    if (rc != OMEGA_GPU_W4_OK) { UNLOCK(); return rc; }
    float *yd = NULL;
    if (!rec) {
        uint64_t *tab = g.tab.cpu; tab[0] = t->w.va; tab[1] = t->w.va + t->s_off;
        omega_fp8_pack_x_f32(x, k, g.x_buf.mem.cpu);
        yd = g.y_buf.mem.cpu;
        for (uint32_t i = 0; i < n; i++) yd[i] = POISON_F32;
        if (omega_gpu_session_sync_to_gpu(&g.tab) != 0 || omega_gpu_session_sync_to_gpu(&g.x_buf.mem) != 0 || omega_gpu_session_sync_to_gpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_W4_CHIP_FAIL;
    }
    const uint64_t tab_va = rec ? dev_tab : g.tab.va, x_va = rec ? dev_x : g.x_buf.mem.va, y_va = rec ? dev_y : g.y_buf.mem.va;
    uint32_t args[OMEGA_FP8_ARGS_WORDS];
    args[0] = (uint32_t)tab_va; args[1] = (uint32_t)(tab_va >> 32);
    args[2] = (uint32_t)x_va; args[3] = (uint32_t)(x_va >> 32);
    args[4] = (uint32_t)y_va; args[5] = (uint32_t)(y_va >> 32);
    args[6] = RG; args[7] = 0u; args[8] = n * 4u; args[9] = grid_x * spec.warps;
    if (info) {
        info->n = n; info->k = k; info->lane_bytes = spec.lane_bytes; info->rows_per_warp = spec.rows; info->warps_per_cta = spec.warps;
        info->coal = spec.coal; info->sched = spec.sched; info->grid_x = grid_x; info->steps = omega_w4_gemv_steps(&spec);
        info->weight_bytes = (uint64_t)n * k / 2u; info->scale_bytes = (uint64_t)n * k / OMEGA_W4_GROUP; info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_W4_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = omega_bw_gpr_declared(ks->kernel.gpr_count),
            .threads_x = 32u * spec.warps, .threads_y = 1, .grid_x = grid_x, .grid_y = 1,
            .num_elements = n, .args = args, .n_args = OMEGA_FP8_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = omega_w4_gemv_shared_bytes(&spec),
        };
        if (rec) { if (omega_gpu_cmdlist_add(rec, &L) < 0) rc = OMEGA_GPU_W4_CHIP_FAIL; }
        else if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, NULL) != 0) rc = OMEGA_GPU_W4_CHIP_FAIL;
    }
    if (rc == OMEGA_GPU_W4_OK && !rec && omega_gpu_session_sync_to_cpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_W4_CHIP_FAIL;
    if (rc == OMEGA_GPU_W4_OK && !rec) {
        size_t unchanged = 0;
        for (uint32_t i = 0; i < n; i++) if (yd[i] == POISON_F32) unchanged++;
        if (unchanged) { omega_gpu_session_set_error("w4: output poison survived"); rc = OMEGA_GPU_W4_CHIP_FAIL; }
        else memcpy(y, yd, (size_t)n * sizeof *y);
    }
    UNLOCK();
    if (info) info->call_ns = omega_gpu_session_now_ns() - t0;
    return rc;
}
#undef UNLOCK

int omega_gpu_w4_gemv_f32(const OmegaGpuW4Tensor *t, const float *x, float *y, OmegaGpuW4Info *info) {
    return gemv_core(t, x, y, info, NULL, 0, 0, 0);
}
int omega_gpu_w4_gemv_record(OmegaGpuCmdList *cl, const OmegaGpuW4Tensor *t, uint64_t tab_va, uint64_t x_va, uint64_t y_va) {
    if (!cl) return OMEGA_GPU_W4_BAD_ARGS;
    return gemv_core(t, NULL, NULL, NULL, cl, tab_va, x_va, y_va);
}
int omega_gpu_w4_prepare(uint32_t k, uint32_t n) {
    OmegaW4GemvSpec spec; omega_gpu_w4_spec_for(k, n, &spec);
    omega_gpu_session_lock();
    int rc = dev_open_locked() ? OMEGA_GPU_W4_OK : OMEGA_GPU_W4_CHIP_FAIL;
    Slot *s = NULL; bool hit = false;
    if (rc == OMEGA_GPU_W4_OK) rc = kernel_for(&spec, &s, &hit);
    omega_gpu_session_unlock();
    return rc;
}

/* ---- host: converter and oracle ---- */

uint8_t omega_w4_e2m1_rne(double a) {
    static const double grid[8] = { 0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 };
    if (!(a < 6.0)) return 7u;
    unsigned i = 0;
    while (i < 7u && grid[i + 1] <= a) i++;
    const double mid = (grid[i] + grid[i + 1]) / 2.0;
    return (uint8_t)((a < mid || (a == mid && i % 2u == 0u)) ? i : i + 1u);
}

/* floor(log2 v) of a positive normal double: its exponent field (exact) */
static int floor_log2(double v) { uint64_t b; memcpy(&b, &v, 8); return (int)((b >> 52) & 0x7FFu) - 1023; }

/* one group at scale 2^e: the nibbles, the squared error and the count above 6 (clipped) */
static void w4_group_at(const double *v, int e, uint8_t *nib, double *err, uint64_t *sat) {
    double X = 1.0; for (int q = 0; q < (e < 0 ? -e : e); q++) X = e < 0 ? X / 2.0 : X * 2.0;
    double se = 0.0; uint64_t s = 0;
    for (uint32_t c = 0; c < OMEGA_W4_GROUP; c++) {
        const double a = (v[c] < 0 ? -v[c] : v[c]) / X;
        if (a > 6.0) s++;
        nib[c] = (uint8_t)(omega_w4_e2m1_rne(a) | (v[c] < 0 ? 8u : 0u));
        const double back = (double)omega_w4_e2m1_ref(nib[c]) * X;
        se += (back - v[c]) * (back - v[c]);
    }
    *err = se; *sat = s;
}

int omega_gpu_w4_from_fp8(uint32_t n, uint32_t k, const uint8_t *w8, const uint16_t *scale_bf16, uint32_t row0, uint32_t rows,
                          uint8_t *w4, uint8_t *e8, OmegaW4Stats *st) {
    return omega_gpu_w4_from_fp8_rule(n, k, w8, scale_bf16, row0, rows, OMEGA_W4_RULE_OCP, w4, e8, st);
}

int omega_gpu_w4_from_fp8_rule(uint32_t n, uint32_t k, const uint8_t *w8, const uint16_t *scale_bf16, uint32_t row0, uint32_t rows,
                               int rule, uint8_t *w4, uint8_t *e8, OmegaW4Stats *st) {
    if (!w8 || !scale_bf16 || !w4 || !e8 || n % 128u || k % 128u || k == 0 || (uint64_t)row0 + rows > n) return OMEGA_GPU_W4_BAD_ARGS;
    if (rule != OMEGA_W4_RULE_OCP && rule != OMEGA_W4_RULE_FIT && rule != OMEGA_W4_RULE_BEST) return OMEGA_GPU_W4_BAD_ARGS;
    const uint32_t kb = k / 128u, gpr = k / OMEGA_W4_GROUP;
    OmegaW4Stats loc; memset(&loc, 0, sizeof loc);
    for (uint32_t r = row0; r < row0 + rows; r++) {
        const uint8_t *src = w8 + (size_t)r * k;
        uint8_t *dst = w4 + (size_t)r * (k / 2u);
        for (uint32_t gi = 0; gi < gpr; gi++) {
            const double s_old = omega_bf16_to_fp32(scale_bf16[(size_t)(r / 128u) * kb + (gi * OMEGA_W4_GROUP) / 128u]);
            double v[OMEGA_W4_GROUP], amax = 0.0;
            for (uint32_t c = 0; c < OMEGA_W4_GROUP; c++) {
                v[c] = (double)omega_fp8_e4m3_ref(src[gi * OMEGA_W4_GROUP + c]) * s_old;
                const double a = v[c] < 0 ? -v[c] : v[c];
                if (!(a <= 1e300)) return OMEGA_GPU_W4_BAD_ARGS; /* NaN / inf */
                if (a > amax) amax = a;
                loc.sq_ref += v[c] * v[c];
            }
            loc.groups++;
            if (amax == 0.0) {
                e8[(size_t)r * gpr + gi] = 127u;
                for (uint32_t c = 0; c < OMEGA_W4_GROUP; c++) OMEGA_W4_SET_NIBBLE(dst, gi * OMEGA_W4_GROUP + c, 0u);
                loc.zero_groups++;
                continue;
            }
            /* OCP: X = 2^(floor(log2 amax) - 2), so amax / X is in [4, 8): a group's values above 6 X clip to 6 X.
             * FIT: one step up when that clips (amax / X <= 6, nothing clips). BEST: whichever of the two has less squared error. */
            int e = floor_log2(amax) - 2;
            uint8_t nib[OMEGA_W4_GROUP], nib2[OMEGA_W4_GROUP];
            double err = 0.0, err2 = 0.0; uint64_t sat = 0, sat2 = 0;
            w4_group_at(v, e, nib, &err, &sat);
            if (rule != OMEGA_W4_RULE_OCP && sat) {
                w4_group_at(v, e + 1, nib2, &err2, &sat2);
                if (rule == OMEGA_W4_RULE_FIT || err2 < err) { e++; memcpy(nib, nib2, sizeof nib); err = err2; sat = sat2; }
            }
            if (e + 127 < 0 || e + 127 > (int)OMEGA_W4_E_MAX) return OMEGA_GPU_W4_BAD_ARGS;
            e8[(size_t)r * gpr + gi] = (uint8_t)(e + 127);
            for (uint32_t c = 0; c < OMEGA_W4_GROUP; c++) OMEGA_W4_SET_NIBBLE(dst, gi * OMEGA_W4_GROUP + c, nib[c]);
            loc.sq_err += err; loc.saturated += sat;
        }
    }
    loc.weights = (uint64_t)rows * k;
    if (st) *st = loc;
    return OMEGA_GPU_W4_OK;
}

void omega_w4_gemv_oracle(const uint8_t *w4, const uint8_t *e8, uint32_t n, uint32_t k, const float *x, double *y) {
    const uint32_t gpr = k / OMEGA_W4_GROUP;
    for (uint32_t r = 0; r < n; r++) {
        const uint8_t *row = w4 + (size_t)r * (k / 2u);
        double acc = 0.0;
        for (uint32_t gi = 0; gi < gpr; gi++) {
            double d = 0.0;
            for (uint32_t c = gi * OMEGA_W4_GROUP; c < (gi + 1u) * OMEGA_W4_GROUP; c++) d += (double)omega_w4_e2m1_ref(OMEGA_W4_NIBBLE(row, c)) * x[c];
            int E = (int)e8[(size_t)r * gpr + gi] - 127; double s = 1.0;
            for (int q = 0; q < (E < 0 ? -E : E); q++) s = E < 0 ? s / 2.0 : s * 2.0;
            acc += d * s;
        }
        y[r] = acc;
    }
}
