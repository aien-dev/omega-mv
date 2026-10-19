/* omega_gpu_tvgemm.c: dense tree-verify / decode GEMM on the tensor cores (cascade/tree-verify-gemm). See the header. */
#include "omega_gpu_tvgemm.h"
#include "omega_gpu_session.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_fmt.h"
#include <string.h>

#define KIND_TV 0x60u
#define TAB_CAP 4096u
#define RIDX_N 128u

typedef struct { uint64_t w_va, s_va; uint32_t row0, cnt, pad[2]; } TileEntry;
_Static_assert(sizeof(TileEntry) == OMEGA_TCGEMM_TILE_BYTES, "tile entry layout");
typedef struct { uint64_t w_va, s_va; uint32_t rows, bm, at, nt; } TabKey;

static struct {
    uint32_t rt, ct, warps;
    int mutant;
    bool hooked;
    OmegaGpuMem tab, ridx;
    TabKey keys[TAB_CAP];
    uint32_t nkeys, used;
} g;

static void on_close(void) { omega_gpu_session_free(&g.tab); omega_gpu_session_free(&g.ridx); memset(&g.tab, 0, sizeof g.tab); memset(&g.ridx, 0, sizeof g.ridx); g.nkeys = g.used = 0; }

void omega_gpu_tv_set_shape(uint32_t rt, uint32_t ct, uint32_t w) { omega_gpu_session_lock(); g.rt = rt; g.ct = ct; g.warps = w; omega_gpu_session_unlock(); }
void omega_gpu_tv_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; omega_gpu_session_unlock(); }

int omega_gpu_tv_spec(uint32_t rows, uint32_t n, uint32_t k, OmegaTcGemmSpec *spec) {
    if (!spec || rows == 0 || rows > OMEGA_GPU_TV_MAX_ROWS) return -1;
    memset(spec, 0, sizeof *spec);
    spec->k = k;
    spec->rt = g.rt ? g.rt : rows <= 16u ? 1u : rows <= 32u ? 2u : 4u;
    /* rt * ct >= 4 (HMMA spacing); 8 warps unless the A staging slots do not divide (rt 1: 4 warps) */
    spec->ct = g.ct ? g.ct : spec->rt >= 4u ? 2u : 4u / spec->rt;
    spec->warps = g.warps ? g.warps : spec->rt == 1u ? 4u : 8u;
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
static int prepare_locked(const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va, OmegaPfKernel *k, OmegaTcGemmSpec *spec,
                          uint32_t *grid_x, uint32_t *grid_y, uint32_t args[OMEGA_TCGEMM_ARGS_WORDS]) {
    uint32_t experts = 0, n = 0, kk = 0;
    if (!t || !x_va || !y_va) return OMEGA_GPU_PF_BAD_ARGS;
    omega_gpu_fp8_tensor_shape(t, &experts, &n, &kk);
    if (experts != 1 || omega_gpu_tv_spec(rows, n, kk, spec) != 0) { omega_gpu_session_set_error("tv gemm: shape or row count refused"); return OMEGA_GPU_PF_BAD_ARGS; }
    int rc = omega_gpu_pf_open_locked();
    if (rc != OMEGA_GPU_PF_OK) return rc;
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_close); g.hooked = true; }
    if (!g.tab.cpu) {
        if (omega_gpu_session_alloc((size_t)TAB_CAP * OMEGA_TCGEMM_TILE_BYTES, &g.tab) != 0 || omega_gpu_session_alloc(RIDX_N * 4u, &g.ridx) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
        uint32_t *ri = g.ridx.cpu; for (uint32_t i = 0; i < RIDX_N; i++) ri[i] = i < OMEGA_GPU_TV_MAX_ROWS ? i : 0u;
        if (omega_gpu_session_sync_to_gpu(&g.ridx) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
    }
    uint64_t w_va = 0, s_va = 0; (void)omega_gpu_fp8_tensor_entry(t, 0, &w_va, &s_va);
    const uint32_t bm = 16u * spec->rt;
    TabKey *key = NULL;
    for (uint32_t i = 0; i < g.nkeys && !key; i++) if (g.keys[i].w_va == w_va && g.keys[i].s_va == s_va && g.keys[i].rows == rows && g.keys[i].bm == bm) key = &g.keys[i];
    if (!key) {
        const uint32_t nt = (rows + bm - 1u) / bm;
        if (g.nkeys >= TAB_CAP || g.used + nt > TAB_CAP) { omega_gpu_session_set_error("tv gemm: tile table full"); return OMEGA_GPU_PF_TOO_LARGE; }
        key = &g.keys[g.nkeys++]; key->w_va = w_va; key->s_va = s_va; key->rows = rows; key->bm = bm; key->at = g.used; key->nt = nt;
        TileEntry *te = (TileEntry *)g.tab.cpu + g.used;
        for (uint32_t i = 0; i < nt; i++) { memset(&te[i], 0, sizeof te[i]); te[i].w_va = w_va; te[i].s_va = s_va; te[i].row0 = i * bm; te[i].cnt = rows - i * bm < bm ? rows - i * bm : bm; }
        g.used += nt;
        if (omega_gpu_session_sync_to_gpu(&g.tab) != 0) return OMEGA_GPU_PF_CHIP_FAIL;
    }
    OmegaPfKey pk; memset(&pk, 0, sizeof pk); pk.kind = KIND_TV; memcpy(pk.words, spec, sizeof *spec);
    rc = omega_gpu_pf_kernel_locked(&pk, gen_tv, k);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    const uint64_t tab_va = g.tab.va + (uint64_t)key->at * OMEGA_TCGEMM_TILE_BYTES;
    const uint64_t va[4] = { tab_va, g.ridx.va, x_va, y_va };
    for (int i = 0; i < 4; i++) { args[2 * i] = (uint32_t)va[i]; args[2 * i + 1] = (uint32_t)(va[i] >> 32); }
    args[8] = n;
    *grid_x = n / omega_tcgemm_span(spec); *grid_y = key->nt;
    return OMEGA_GPU_PF_OK;
}

int omega_gpu_tv_gemm(const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va, uint64_t *chip_ns) {
    if (chip_ns) *chip_ns = 0;
    omega_gpu_session_lock();
    OmegaPfKernel k; OmegaTcGemmSpec spec; uint32_t gx = 0, gy = 0, args[OMEGA_TCGEMM_ARGS_WORDS];
    int rc = prepare_locked(t, rows, x_va, y_va, &k, &spec, &gx, &gy, args);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_launch_locked(&k, 32u * spec.warps, gx, gy, omega_tcgemm_shared_bytes(&spec), args, OMEGA_TCGEMM_ARGS_WORDS, chip_ns);
    omega_gpu_session_unlock();
    return rc;
}

int omega_gpu_tv_record_locked(OmegaGpuCmdList *cl, const OmegaGpuFp8Tensor *t, uint32_t rows, uint64_t x_va, uint64_t y_va) {
    OmegaPfKernel k; OmegaTcGemmSpec spec; uint32_t gx = 0, gy = 0, args[OMEGA_TCGEMM_ARGS_WORDS];
    int rc = prepare_locked(t, rows, x_va, y_va, &k, &spec, &gx, &gy, args);
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_pf_record_locked(cl, &k, 32u * spec.warps, gx, gy, omega_tcgemm_shared_bytes(&spec), args, OMEGA_TCGEMM_ARGS_WORDS);
    return rc;
}
