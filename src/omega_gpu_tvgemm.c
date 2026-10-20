/* omega_gpu_tvgemm.c: dense tree-verify / decode GEMM on the tensor cores (cascade/tree-verify-gemm). See the header. */
#include "omega_gpu_tvgemm.h"
#include "omega_gpu_session.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_fmt.h"
#include <string.h>
#include <stdlib.h>

#define KIND_TV 0x60u
#define TAB_CAP 32768u  /* tile-table keys (tensor x row count x row tile). speed5/batching: a serving batch changes its row count
                         * every time a conversation joins or leaves, so 257 tensors x 1..64 rows need ~16k keys (1024 was full at 4 rows) */
#define TILE_CAP 131072u /* tile entries over all keys (at most 4 per key at 64 rows) */
#define HASH_N 65536u    /* key lookup slots (power of two, > 2 x TAB_CAP) */
#define RIDX_N 128u /* per row count: rows 0 .. rows-1 then row 0 (the padding rows of a partial tile read row 0) */

typedef struct { uint64_t w_va, s_va; uint32_t row0, cnt, pad[2]; } TileEntry;
_Static_assert(sizeof(TileEntry) == OMEGA_TCGEMM_TILE_BYTES, "tile entry layout");
typedef struct { uint64_t w_va, s_va; uint32_t rows, bm, at, nt, ri; } TabKey;

static struct {
    uint32_t rt, ct, warps;
    int mutant;
    bool hooked;
    OmegaGpuMem tab, ridx;
    TabKey keys[TAB_CAP];
    uint32_t slot[HASH_N]; /* key index + 1, 0 = empty; open addressing */
    uint32_t nkeys, used;
    OmegaGpuMem hx, hy; /* omega_gpu_tv_f32's device buffers */
} g;

static void on_close(void) { omega_gpu_session_free(&g.hx); omega_gpu_session_free(&g.hy); memset(&g.hx, 0, sizeof g.hx); memset(&g.hy, 0, sizeof g.hy);
    omega_gpu_session_free(&g.tab); omega_gpu_session_free(&g.ridx); memset(&g.tab, 0, sizeof g.tab); memset(&g.ridx, 0, sizeof g.ridx); g.nkeys = g.used = 0; memset(g.slot, 0, sizeof g.slot); }

void omega_gpu_tv_set_shape(uint32_t rt, uint32_t ct, uint32_t w) { omega_gpu_session_lock(); g.rt = rt; g.ct = ct; g.warps = w; omega_gpu_session_unlock(); }
void omega_gpu_tv_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; omega_gpu_session_unlock(); }

int omega_gpu_tv_spec(uint32_t rows, uint32_t n, uint32_t k, OmegaTcGemmSpec *spec) { return omega_gpu_tv_spec_x(rows, OMEGA_GPU_TV_X_BF16P, n, k, spec); }
int omega_gpu_tv_spec_x(uint32_t rows, uint32_t xform, uint32_t n, uint32_t k, OmegaTcGemmSpec *spec) {
    if (!spec || rows == 0 || rows > OMEGA_GPU_TV_MAX_ROWS || xform > OMEGA_GPU_TV_X_F32) return -1;
    memset(spec, 0, sizeof *spec);
    spec->k = k;
    spec->xf32 = xform == OMEGA_GPU_TV_X_F32;
    spec->rt = g.rt ? g.rt : rows <= 16u ? 1u : (rows <= 32u || spec->xf32) ? 2u : 4u;
    /* rt * ct >= 4 (HMMA spacing); 8 warps unless the A staging slots do not divide (rt 1: 4 warps) */
    spec->ct = g.ct ? g.ct : spec->rt >= 4u ? 2u : 4u / spec->rt;
    spec->warps = g.warps ? g.warps : spec->rt == 1u ? 4u : 8u;
    if (!g.warps) {
        /* blocks = n / (warps * 8 * ct) on 48 SMs: a narrow output (down, o_proj, qkv, mixer) needs fewer warps per block so every SM has
           several blocks (measured, evidence tv8-down/amix-sweep: down 32 rows 583 -> 538 us, mixer 453 -> 414 us) */
        if (spec->rt == 1u) { if (n / (spec->warps * 8u * spec->ct) < 64u) spec->warps = 2u; }
        else while (spec->warps > 2u && n / (spec->warps * 8u * spec->ct) < 144u) spec->warps /= 2u;
    }
    spec->mutant = g.mutant;
    if (!omega_tcgemm_spec_ok(spec)) return -1;
    if (n % omega_tcgemm_span(spec) || n % 128u) return -1;
    return 0;
}

static int gen_tv(const OmegaPfKey *key, void *prog) {
    OmegaTcGemmSpec s; memcpy(&s, key->words, sizeof s);
    return omega_blackwell_codegen_tcgemm_prog(&s, (BlackwellIRProgram *)prog);
}

/* lock held, device open: kernel, launch shape and argument words for one call */
static int prepare_locked(const OmegaGpuFp8Tensor *t, uint32_t rows, uint32_t xform, uint64_t x_va, uint64_t y_va, OmegaPfKernel *k, OmegaTcGemmSpec *spec,
                          uint32_t *grid_x, uint32_t *grid_y, uint32_t args[OMEGA_TCGEMM_ARGS_WORDS]) {
    uint32_t experts = 0, n = 0, kk = 0;
    if (!t || !x_va || !y_va) return OMEGA_GPU_PF_BAD_ARGS;
    omega_gpu_fp8_tensor_shape(t, &experts, &n, &kk);
    if (experts != 1 || omega_gpu_tv_spec_x(rows, xform, n, kk, spec) != 0) { omega_gpu_session_set_error("tv gemm: shape or row count refused"); return OMEGA_GPU_PF_BAD_ARGS; }
    int rc = omega_gpu_pf_open_locked();
    if (rc != OMEGA_GPU_PF_OK) return rc;
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_close); g.hooked = true; }
    if (!g.tab.cpu) {
        if (omega_gpu_session_alloc((size_t)TILE_CAP * OMEGA_TCGEMM_TILE_BYTES, &g.tab) != 0 || omega_gpu_session_alloc((size_t)OMEGA_GPU_TV_MAX_ROWS * RIDX_N * 4u, &g.ridx) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
        /* the row index depends on the row count only: one block per count 1 .. OMEGA_GPU_TV_MAX_ROWS, written once */
        for (uint32_t r = 1; r <= OMEGA_GPU_TV_MAX_ROWS; r++) {
            uint32_t *ri = (uint32_t *)g.ridx.cpu + (size_t)(r - 1u) * RIDX_N;
            for (uint32_t i = 0; i < RIDX_N; i++) ri[i] = i < r ? i : 0u;
        }
        if (omega_gpu_session_sync_to_gpu(&g.ridx) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
    }
    uint64_t w_va = 0, s_va = 0; (void)omega_gpu_fp8_tensor_entry(t, 0, &w_va, &s_va);
    const uint32_t bm = 16u * spec->rt;
    TabKey *key = NULL;
    uint32_t h = (uint32_t)((w_va >> 8) ^ (w_va >> 29) ^ (s_va >> 6) ^ ((uint64_t)rows * 0x9E3779B1u) ^ ((uint64_t)bm << 20)) & (HASH_N - 1u);
    for (;; h = (h + 1u) & (HASH_N - 1u)) {
        const uint32_t s = g.slot[h];
        if (!s) break;
        TabKey *c = &g.keys[s - 1u];
        if (c->w_va == w_va && c->s_va == s_va && c->rows == rows && c->bm == bm) { key = c; break; }
    }
    if (!key) {
        const uint32_t nt = (rows + bm - 1u) / bm;
        if (g.nkeys >= TAB_CAP || g.used + nt > TILE_CAP) { omega_gpu_session_set_error("tv gemm: tile table full"); return OMEGA_GPU_PF_TOO_LARGE; }
        key = &g.keys[g.nkeys++]; key->w_va = w_va; key->s_va = s_va; key->rows = rows; key->bm = bm; key->at = g.used; key->nt = nt; key->ri = rows - 1u;
        g.slot[h] = g.nkeys; /* h is the empty slot the probe stopped at */
        TileEntry *te = (TileEntry *)g.tab.cpu + g.used;
        for (uint32_t i = 0; i < nt; i++) { memset(&te[i], 0, sizeof te[i]); te[i].w_va = w_va; te[i].s_va = s_va; te[i].row0 = i * bm; te[i].cnt = rows - i * bm < bm ? rows - i * bm : bm; }
        g.used += nt;
        if (omega_gpu_session_sync_to_gpu(&g.tab) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
    }
    OmegaPfKey pk; memset(&pk, 0, sizeof pk); pk.kind = KIND_TV; memcpy(pk.words, spec, sizeof *spec);
    rc = omega_gpu_pf_kernel_locked(&pk, gen_tv, k);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    const uint64_t tab_va = g.tab.va + (uint64_t)key->at * OMEGA_TCGEMM_TILE_BYTES;
    const uint64_t va[4] = { tab_va, g.ridx.va + (uint64_t)key->ri * RIDX_N * 4u, x_va, y_va };
    for (int i = 0; i < 4; i++) { args[2 * i] = (uint32_t)va[i]; args[2 * i + 1] = (uint32_t)(va[i] >> 32); }
    args[8] = n;
    *grid_x = n / omega_tcgemm_span(spec); *grid_y = key->nt;
    return OMEGA_GPU_PF_OK;
}

int omega_gpu_tv_gemm(const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va, uint64_t *chip_ns) {
    return omega_gpu_tv_gemm_x(t, rows, OMEGA_GPU_TV_X_BF16P, x_va, y_va, chip_ns);
}
int omega_gpu_tv_gemm_x(const OmegaGpuFp8Tensor *t, uint32_t rows, uint32_t xform, uint64_t x_va, uint64_t y_va, uint64_t *chip_ns) {
    if (chip_ns) *chip_ns = 0;
    omega_gpu_session_lock();
    OmegaPfKernel k; OmegaTcGemmSpec spec; uint32_t gx = 0, gy = 0, args[OMEGA_TCGEMM_ARGS_WORDS];
    int rc = prepare_locked(t, rows, xform, x_va, y_va, &k, &spec, &gx, &gy, args);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_launch_locked(&k, 32u * spec.warps, gx, gy, omega_tcgemm_shared_bytes(&spec), args, OMEGA_TCGEMM_ARGS_WORDS, chip_ns);
    omega_gpu_session_unlock();
    return rc;
}

int omega_gpu_tv_record_locked(OmegaGpuCmdList *cl, const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va) {
    return omega_gpu_tv_record_x_locked(cl, t, rows, OMEGA_GPU_TV_X_BF16P, x_va, y_va);
}
int omega_gpu_tv_record_x_locked(OmegaGpuCmdList *cl, const OmegaGpuFp8Tensor *t, uint32_t rows, uint32_t xform, uint64_t x_va, uint64_t y_va) {
    OmegaPfKernel k; OmegaTcGemmSpec spec; uint32_t gx = 0, gy = 0, args[OMEGA_TCGEMM_ARGS_WORDS];
    int rc = prepare_locked(t, rows, xform, x_va, y_va, &k, &spec, &gx, &gy, args);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_record_locked(cl, &k, 32u * spec.warps, gx, gy, omega_tcgemm_shared_bytes(&spec), args, OMEGA_TCGEMM_ARGS_WORDS);
    return rc;
}

static int dev_grow(OmegaGpuMem *m, size_t bytes) {
    if (m->va && m->size >= bytes) return OMEGA_GPU_PF_OK;
    if (m->size) { omega_gpu_pf_free(m); memset(m, 0, sizeof *m); }
    return omega_gpu_pf_alloc_dev(bytes, m);
}
int omega_gpu_tv_f32(const OmegaGpuFp8Tensor *t, uint32_t rows, const float *x, float *y, OmegaGpuTvInfo *info) {
    const uint64_t t0 = omega_gpu_session_now_ns();
    if (info) memset(info, 0, sizeof *info);
    uint32_t experts = 0, n = 0, k = 0;
    if (!t || !x || !y) return OMEGA_GPU_PF_BAD_ARGS;
    omega_gpu_fp8_tensor_shape(t, &experts, &n, &k);
    OmegaTcGemmSpec spec;
    if (experts != 1 || omega_gpu_tv_spec(rows, n, k, &spec) != 0) return OMEGA_GPU_PF_BAD_ARGS;
    const size_t xb = (size_t)rows * k * 2u, yb = (size_t)rows * n * 4u;
    uint16_t *xs = malloc(xb); float *ys = malloc(yb);
    if (!xs || !ys) { free(xs); free(ys); return OMEGA_GPU_PF_CHIP_FAIL; }
    for (uint32_t r = 0; r < rows; r++) omega_tcgemm_permute_x(x + (size_t)r * k, k, xs + (size_t)r * k);
    for (size_t i = 0; i < (size_t)rows * n; i++) ys[i] = -999.0f;
    int rc = dev_grow(&g.hx, xb);
    if (rc == OMEGA_GPU_PF_OK) rc = dev_grow(&g.hy, yb);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_upload(&g.hx, 0, xs, xb);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_upload(&g.hy, 0, ys, yb);
    uint64_t ns = 0;
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_tv_gemm(t, rows, g.hx.va, g.hy.va, &ns);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_download(&g.hy, 0, ys, yb);
    if (rc == OMEGA_GPU_PF_OK) {
        for (size_t i = 0; i < (size_t)rows * n; i++) if (ys[i] == -999.0f) { omega_gpu_session_set_error("tv gemm: output poison survived"); rc = OMEGA_GPU_PF_CHIP_FAIL; break; }
        if (rc == OMEGA_GPU_PF_OK) memcpy(y, ys, yb);
    }
    free(xs); free(ys);
    if (info) { info->chip_ns = ns; info->rt = spec.rt; info->ct = spec.ct; info->warps = spec.warps; info->call_ns = omega_gpu_session_now_ns() - t0; }
    return rc;
}
