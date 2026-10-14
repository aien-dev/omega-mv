/* omega_gpu_resident_api.c: host side of the resident FP8 GEMV chain (OWN-30B W-L2f). See the header. */
#include "omega_gpu_resident_api.h"
#include "omega_gpu_mem_cached.h"
#include "omega_blackwell_codegen_moe.h" /* OMEGA_FP8_BLOCK */
#include "omega_gpu_session.h"
#include "omega_gpu_fmt.h"
#include <string.h>

struct OmegaGpuResident {
    OmegaResidentSpec spec;
    uint32_t n_stages, peak, declared;
    uint32_t base;  /* barrier word value at the next launch start (sum of (n_stages - 1) x grid over earlier launches) */
    uint64_t stamp_va; /* W-L2g: stamp buffer for spec.stamps (0 = none) */
    int v2; uint32_t grid2; int batch_open; /* W-L2h resident2: fixed grid, batch of launches before one status() */
    OmegaGpuMem code, table, ctl, bar;
};

static struct { uint32_t sms, threads, blocks; } lim = { OMEGA_GPU_RESIDENT_SMS, OMEGA_GPU_RESIDENT_THREADS_PER_SM, OMEGA_GPU_RESIDENT_BLOCKS_PER_SM };

void omega_gpu_resident_set_limits(uint32_t sms, uint32_t threads_per_sm, uint32_t blocks_per_sm) {
    if (sms) lim.sms = sms;
    if (threads_per_sm) lim.threads = threads_per_sm;
    if (blocks_per_sm) lim.blocks = blocks_per_sm;
}

uint32_t omega_gpu_resident_capacity(const OmegaGpuResident *r) {
    if (!r) return 0;
    const uint32_t threads = 32u * r->spec.warps;
    uint32_t per = lim.blocks;
    if (lim.threads / threads < per) per = lim.threads / threads;
    if (65536u / (threads * r->declared) < per) per = 65536u / (threads * r->declared);
    return per * lim.sms;
}
uint32_t omega_gpu_resident_gpr_declared(const OmegaGpuResident *r) { return r ? r->declared : 0; }

void omega_gpu_resident_free(OmegaGpuResident *r) {
    if (!r) return;
    omega_gpu_session_free(&r->code);
    omega_gpu_session_free(&r->table);
    omega_gpu_session_free(&r->ctl);
    omega_gpu_session_free(&r->bar);
    omega_gpu_host_free(r);
}

/* A fresh barrier word holding 0. GPU scope: GPU-cached memory, written by the CPU only here, on a fresh allocation the GPU
 * never read (omega_gpu_mem_cached.h invariant), so it is never reset by the host again: launches continue from r->base.
 * System scope (r1 form): an uncached word. */
static int bar_fresh(OmegaGpuResident *r) {
    omega_gpu_session_free(&r->bar);
    r->base = 0;
    int a = r->spec.scope_gpu ? omega_gpu_session_alloc_cached(64u, &r->bar) : omega_gpu_session_alloc(64u, &r->bar);
    if (a != 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
    memset(r->bar.cpu, 0, 64u);
    return omega_gpu_session_sync_to_gpu(&r->bar) == 0 ? OMEGA_GPU_RESIDENT_OK : OMEGA_GPU_RESIDENT_CHIP_FAIL;
}

int omega_gpu_resident_create(const OmegaResidentSpec *spec, const OmegaGpuResidentStage *stages, uint32_t n_stages, OmegaGpuResident **out) {
    if (!spec || !stages || !out || n_stages == 0 || n_stages > OMEGA_RES_MAX_STAGES) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    *out = NULL;
    for (uint32_t s = 0; s < n_stages; s++) {
        const OmegaGpuResidentStage *g = &stages[s];
        if (!g->w_va || !g->s_va || !g->x_va || !g->y_va || g->k == 0 || g->n == 0 || g->k % (32u * spec->lane_bytes) || g->k % OMEGA_FP8_BLOCK
            || g->n % OMEGA_FP8_BLOCK || (uint64_t)g->n * g->k >= (1ull << 32)) {
            omega_gpu_session_set_error("resident: stage shape refused (k multiple of 32 * lane_bytes and 128, n multiple of 128)");
            return OMEGA_GPU_RESIDENT_BAD_ARGS;
        }
    }
    OmegaGpuResident *r = omega_gpu_host_calloc(1, sizeof *r);
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!r || !prog) { omega_gpu_host_free(r); omega_gpu_host_free(prog); return OMEGA_GPU_RESIDENT_CHIP_FAIL; }
    r->spec = *spec; r->n_stages = n_stages;
    int rc = OMEGA_GPU_RESIDENT_OK;
    OmegaResidentPatches pt;
    if (omega_blackwell_codegen_resident_prog_p(spec, prog, &pt) != 0) { omega_gpu_session_set_error("resident codegen refused the spec"); rc = OMEGA_GPU_RESIDENT_CODEGEN_FAIL; }
    size_t need = rc == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (rc == 0) {
        r->peak = prog->regalloc.peak_gpr_usage;
        r->declared = omega_bw_gpr_declared(r->peak);
        if ((uint64_t)32u * spec->warps * r->declared > 65536u) { omega_gpu_session_set_error("resident: CTA exceeds the register file"); rc = OMEGA_GPU_RESIDENT_TOO_LARGE; }
    }
    if (rc == 0 && omega_gpu_session_alloc_code(need, &r->code) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) {
        OmegaBlackwellKernel k;
        if (omega_blackwell_kernel_encode_into(prog, r->code.cpu, need, &k) != 0 || k.code_size != need || omega_resident_apply_patches(r->code.cpu, need, &pt) != 0) rc = OMEGA_GPU_RESIDENT_CODEGEN_FAIL;
        else if (omega_gpu_session_sync_to_gpu(&r->code) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    }
    omega_gpu_host_free(prog);
    /* stage table: written once here, then only read by the GPU (GPU-cached, omega_gpu_mem_cached.h invariant) */
    if (rc == 0 && omega_gpu_session_alloc_cached((size_t)n_stages * OMEGA_RES_DESC_BYTES, &r->table) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) {
        uint8_t *t = r->table.cpu;
        memset(t, 0, (size_t)n_stages * OMEGA_RES_DESC_BYTES);
        for (uint32_t s = 0; s < n_stages; s++) {
            const OmegaGpuResidentStage *g = &stages[s];
            uint64_t q[4] = { g->w_va, g->s_va, g->x_va, g->y_va };
            uint32_t w[4] = { g->k, g->k / (32u * spec->lane_bytes), g->n / spec->rows, (g->k / OMEGA_FP8_BLOCK) * 4u };
            memcpy(t + (size_t)s * OMEGA_RES_DESC_BYTES, q, sizeof q);
            memcpy(t + (size_t)s * OMEGA_RES_DESC_BYTES + 32, w, sizeof w);
        }
        if (omega_gpu_session_sync_to_gpu(&r->table) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    }
    if (rc == 0 && omega_gpu_session_alloc(OMEGA_RES_CTL_BYTES, &r->ctl) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) rc = bar_fresh(r);
    if (rc != 0) { omega_gpu_resident_free(r); return rc; }
    *out = r;
    return OMEGA_GPU_RESIDENT_OK;
}

int omega_gpu_resident_record(OmegaGpuCmdList *cl, OmegaGpuResident *r, uint32_t grid, uint32_t spin_max, int force) {
    if (!cl || !r || spin_max == 0 || r->v2) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    const uint32_t cap = omega_gpu_resident_capacity(r);
    if (grid == 0) grid = cap;
    if (grid > cap && !force) {
        char e[160];
        omega_gpu_fmt(e, sizeof e, "resident: grid %u exceeds the co-resident capacity %u (refused, nothing recorded)", grid, cap);
        omega_gpu_session_set_error(e);
        return OMEGA_GPU_RESIDENT_TOO_LARGE;
    }
    if (r->spec.mutant == 0 && (uint64_t)r->base + (uint64_t)(r->n_stages - 1u) * grid >= OMEGA_RES_ABORT_UNIT) { int b = bar_fresh(r); if (b != 0) return b; }
    uint32_t *c = r->ctl.cpu;
    memset(c, 0, OMEGA_RES_CTL_BYTES);
    c[OMEGA_RES_CTL_MAGIC / 4] = OMEGA_RES_MAGIC; c[OMEGA_RES_CTL_VERSION / 4] = OMEGA_RES_VERSION;
    c[OMEGA_RES_CTL_NSTAGES / 4] = r->n_stages; c[OMEGA_RES_CTL_GRID / 4] = grid; c[OMEGA_RES_CTL_SPINMAX / 4] = spin_max;
    if (omega_gpu_session_sync_to_gpu(&r->ctl) != 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
    uint32_t args[OMEGA_RES_ARGS_WORDS];
    args[0] = (uint32_t)r->ctl.va; args[1] = (uint32_t)(r->ctl.va >> 32);
    args[2] = (uint32_t)r->table.va; args[3] = (uint32_t)(r->table.va >> 32);
    args[4] = r->n_stages; args[5] = grid; args[6] = grid * r->spec.warps; args[7] = spin_max;
    args[8] = (uint32_t)r->bar.va; args[9] = (uint32_t)(r->bar.va >> 32); args[10] = r->base;
    args[11] = 0; args[12] = (uint32_t)r->stamp_va; args[13] = (uint32_t)(r->stamp_va >> 32);
    if (r->spec.stamps && !r->stamp_va) { omega_gpu_session_set_error("resident: stamps spec without a stamp buffer"); return OMEGA_GPU_RESIDENT_BAD_ARGS; }
    OmegaGpuLaunch L = {
        .code_va = r->code.va, .gpr_count = r->declared, .threads_x = 32u * r->spec.warps, .threads_y = 1,
        .grid_x = grid, .grid_y = 1, .num_elements = (size_t)grid * 32u * r->spec.warps,
        .args = args, .n_args = OMEGA_RES_ARGS_WORDS, .timeout_ms = 20000, .shared_bytes = 0,
    };
    if (!omega_bw_gpr_declared_ok(L.gpr_count, r->peak)) { omega_gpu_session_set_error("resident: declared registers below peak + 2"); return OMEGA_GPU_RESIDENT_CHIP_FAIL; }
    if (omega_gpu_cmdlist_add(cl, &L) < 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (r->spec.mutant == 0) r->base += (r->n_stages - 1u) * grid;
    return OMEGA_GPU_RESIDENT_OK;
}

int omega_gpu_resident_status(OmegaGpuResident *r, uint32_t *progress, uint32_t *error) {
    if (!r) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    r->batch_open = 0;
    if (omega_gpu_session_sync_to_cpu(&r->ctl) != 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
    const volatile uint32_t *c = r->ctl.cpu;
    if (progress) *progress = c[OMEGA_RES_CTL_PROGRESS / 4];
    const uint32_t err = c[OMEGA_RES_CTL_ERROR / 4];
    if (error) *error = err;
    /* an aborted launch leaves the barrier word at an unknown count: start over on a fresh word */
    if (err != 0 || c[OMEGA_RES_CTL_CANCEL / 4] != 0) return bar_fresh(r);
    return OMEGA_GPU_RESIDENT_OK;
}

void omega_gpu_resident_set_stamps(OmegaGpuResident *r, uint64_t va) { if (r) r->stamp_va = va; }

int omega_gpu_resident_cancel(OmegaGpuResident *r) {
    if (!r) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    volatile uint32_t *c = r->ctl.cpu;
    c[OMEGA_RES_CTL_CANCEL / 4] = 1u;
    return omega_gpu_session_sync_to_gpu(&r->ctl) == 0 ? OMEGA_GPU_RESIDENT_OK : OMEGA_GPU_RESIDENT_CHIP_FAIL;
}

/* ---- W-L2h: decode-stage kernel ---- */

uint32_t omega_gpu_resident2_grid(const OmegaGpuResident *r) { return r && r->v2 ? r->grid2 : 0; }

static int stage2_ok(const OmegaGpuResident2Stage *g) {
    switch (g->kind) {
    case OMEGA_RES2_GEMV16: case OMEGA_RES2_GEMV16_ADD: case OMEGA_RES2_GEMV8: {
        const uint32_t lb = g->kind == OMEGA_RES2_GEMV8 ? 8u : 16u;
        if (!g->x || !g->y || g->k == 0 || g->n == 0 || g->groups == 0 || g->groups > OMEGA_FP8_MAX_GROUPS) return 0;
        if (g->k % (32u * lb) || g->k % OMEGA_FP8_BLOCK || g->n % OMEGA_FP8_BLOCK || (uint64_t)g->n * g->k >= (1ull << 32)) return 0;
        if (!g->tab && (g->groups != 1 || !g->w || !g->s)) return 0;
        if (g->kind == OMEGA_RES2_GEMV16_ADD && !g->add) return 0;
        return 1; }
    case OMEGA_RES2_SWIGLU: return g->x && g->y && g->n && g->groups && (uint64_t)g->n * g->groups < (1ull << 31);
    case OMEGA_RES2_RESNORM: return g->x && g->y && g->dn && g->wts && g->nw;
    default: return 0;
    }
}

int omega_gpu_resident2_create(const OmegaResident2Spec *spec, const OmegaGpuResident2Stage *stages, uint32_t n_stages, uint32_t grid, OmegaGpuResident **out) {
    if (!spec || !stages || !out || n_stages == 0 || n_stages > OMEGA_RES_MAX_STAGES || grid == 0 || spec->warps != 8u) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    *out = NULL;
    for (uint32_t s = 0; s < n_stages; s++)
        if (!stage2_ok(&stages[s]) || (stages[s].kind != OMEGA_RES2_SWIGLU && stages[s].kind != OMEGA_RES2_RESNORM && stages[s].groups > grid * spec->warps)) {
            char e[128]; omega_gpu_fmt(e, sizeof e, "resident2: stage %u refused (kind %u: fields or shape)", s, stages[s].kind);
            omega_gpu_session_set_error(e); return OMEGA_GPU_RESIDENT_BAD_ARGS;
        }
    OmegaGpuResident *r = omega_gpu_host_calloc(1, sizeof *r);
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!r || !prog) { omega_gpu_host_free(r); omega_gpu_host_free(prog); return OMEGA_GPU_RESIDENT_CHIP_FAIL; }
    /* the legacy spec fields describe the fixed production barrier (capacity uses warps) */
    r->spec.lane_bytes = 16; r->spec.rows = 1; r->spec.warps = spec->warps; r->spec.mutant = spec->mutant;
    r->spec.scope_gpu = 1; r->spec.fence = 0; r->spec.abort = 1;
    r->n_stages = n_stages; r->v2 = 1; r->grid2 = grid;
    int rc = OMEGA_GPU_RESIDENT_OK;
    OmegaResidentPatches pt;
    if (omega_blackwell_codegen_resident2_prog_p(spec, prog, &pt) != 0) { omega_gpu_session_set_error("resident2 codegen refused the spec"); rc = OMEGA_GPU_RESIDENT_CODEGEN_FAIL; }
    size_t need = rc == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (rc == 0) {
        r->peak = prog->regalloc.peak_gpr_usage;
        r->declared = omega_bw_gpr_declared(r->peak);
        if ((uint64_t)32u * spec->warps * r->declared > 65536u) { omega_gpu_session_set_error("resident2: CTA exceeds the register file"); rc = OMEGA_GPU_RESIDENT_TOO_LARGE; }
    }
    if (rc == 0 && grid > omega_gpu_resident_capacity(r)) {
        char e[128]; omega_gpu_fmt(e, sizeof e, "resident2: grid %u exceeds the co-resident capacity %u (%u registers)", grid, omega_gpu_resident_capacity(r), r->declared);
        omega_gpu_session_set_error(e); rc = OMEGA_GPU_RESIDENT_TOO_LARGE;
    }
    if (rc == 0 && omega_gpu_session_alloc_code(need, &r->code) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) {
        OmegaBlackwellKernel k;
        if (omega_blackwell_kernel_encode_into(prog, r->code.cpu, need, &k) != 0 || k.code_size != need || omega_resident_apply_patches(r->code.cpu, need, &pt) != 0) rc = OMEGA_GPU_RESIDENT_CODEGEN_FAIL;
        else if (omega_gpu_session_sync_to_gpu(&r->code) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    }
    omega_gpu_host_free(prog);
    if (rc == 0 && omega_gpu_session_alloc_cached((size_t)n_stages * OMEGA_RES2_DESC_BYTES, &r->table) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) {
        uint8_t *t = r->table.cpu;
        memset(t, 0, (size_t)n_stages * OMEGA_RES2_DESC_BYTES);
        for (uint32_t s = 0; s < n_stages; s++) {
            const OmegaGpuResident2Stage *g = &stages[s];
            uint8_t *d = t + (size_t)s * OMEGA_RES2_DESC_BYTES;
            const uint64_t dva = r->table.va + (uint64_t)s * OMEGA_RES2_DESC_BYTES;
#define P64(off, v) do { uint64_t q_ = (v); memcpy(d + (off), &q_, 8); } while (0)
#define P32(off, v) do { uint32_t q_ = (v); memcpy(d + (off), &q_, 4); } while (0)
#define PF(off, v) do { float q_ = (v); memcpy(d + (off), &q_, 4); } while (0)
            P32(OMEGA_RES2_D_KIND, g->kind);
            if (g->kind == OMEGA_RES2_SWIGLU) {
                P64(OMEGA_RES2_D_X, g->x); P64(OMEGA_RES2_D_Y, g->y); P32(OMEGA_RES2_D_G, g->groups);
                PF(OMEGA_RES2_D_F0, g->scale); P32(OMEGA_RES2_D_F1, g->n);
            } else if (g->kind == OMEGA_RES2_RESNORM) {
                P64(OMEGA_RES2_D_TAB, g->x); P64(OMEGA_RES2_D_X, g->dn); P64(OMEGA_RES2_D_Y, g->y); P64(OMEGA_RES2_D_ADD, g->wts);
                P64(OMEGA_RES2_D_ENTRY, g->nw); PF(OMEGA_RES2_D_F0, g->eps); PF(OMEGA_RES2_D_F1, g->scale);
            } else {
                const uint32_t lb = g->kind == OMEGA_RES2_GEMV8 ? 8u : 16u;
                P64(OMEGA_RES2_D_TAB, g->tab ? g->tab : dva + OMEGA_RES2_D_ENTRY);
                P64(OMEGA_RES2_D_X, g->x); P64(OMEGA_RES2_D_Y, g->y); P64(OMEGA_RES2_D_ADD, g->add);
                P32(OMEGA_RES2_D_K, g->k); P32(OMEGA_RES2_D_KS, g->k / (32u * lb)); P32(OMEGA_RES2_D_N, g->n); P32(OMEGA_RES2_D_KB4, (g->k / OMEGA_FP8_BLOCK) * 4u);
                P32(OMEGA_RES2_D_G, g->groups); P32(OMEGA_RES2_D_XGS, g->xgs); P32(OMEGA_RES2_D_WPG, grid * spec->warps / g->groups);
                P64(OMEGA_RES2_D_ENTRY, g->w); P64(OMEGA_RES2_D_ENTRY + 8u, g->s);
            }
#undef P64
#undef P32
#undef PF
        }
        if (omega_gpu_session_sync_to_gpu(&r->table) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    }
    if (rc == 0 && omega_gpu_session_alloc(OMEGA_RES_CTL_BYTES, &r->ctl) != 0) rc = OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (rc == 0) rc = bar_fresh(r);
    if (rc != 0) { omega_gpu_resident_free(r); return rc; }
    *out = r;
    return OMEGA_GPU_RESIDENT_OK;
}

int omega_gpu_resident2_record_range(OmegaGpuCmdList *cl, OmegaGpuResident *r, uint32_t first, uint32_t count, uint32_t spin_max) {
    if (!cl || !r || !r->v2 || spin_max == 0 || count == 0 || first >= r->n_stages || count > r->n_stages - first) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    const uint32_t grid = r->grid2;
    if (!r->batch_open) {
        /* a new batch: the previous one was closed by status(), so nothing in flight still uses the barrier word */
        if (r->base >= OMEGA_RES_ABORT_UNIT / 2u) { int b = bar_fresh(r); if (b != 0) return b; }
        uint32_t *c = r->ctl.cpu;
        memset(c, 0, OMEGA_RES_CTL_BYTES);
        c[OMEGA_RES_CTL_MAGIC / 4] = OMEGA_RES_MAGIC; c[OMEGA_RES_CTL_VERSION / 4] = OMEGA_RES_VERSION;
        c[OMEGA_RES_CTL_NSTAGES / 4] = r->n_stages; c[OMEGA_RES_CTL_GRID / 4] = grid; c[OMEGA_RES_CTL_SPINMAX / 4] = spin_max;
        if (omega_gpu_session_sync_to_gpu(&r->ctl) != 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
        r->batch_open = 1;
    }
    if ((uint64_t)r->base + (uint64_t)(count - 1u) * grid >= OMEGA_RES_ABORT_UNIT) {
        omega_gpu_session_set_error("resident2: barrier word would reach the abort unit inside one batch (submit and status() first)");
        return OMEGA_GPU_RESIDENT_BAD_ARGS;
    }
    const uint64_t tva = r->table.va + (uint64_t)first * OMEGA_RES2_DESC_BYTES;
    uint32_t args[OMEGA_RES_ARGS_WORDS];
    args[0] = (uint32_t)r->ctl.va; args[1] = (uint32_t)(r->ctl.va >> 32);
    args[2] = (uint32_t)tva; args[3] = (uint32_t)(tva >> 32);
    args[4] = count; args[5] = grid; args[6] = grid * r->spec.warps; args[7] = spin_max;
    args[8] = (uint32_t)r->bar.va; args[9] = (uint32_t)(r->bar.va >> 32); args[10] = r->base;
    args[11] = 0; args[12] = 0; args[13] = 0;
    OmegaGpuLaunch L = {
        .code_va = r->code.va, .gpr_count = r->declared, .threads_x = 32u * r->spec.warps, .threads_y = 1,
        .grid_x = grid, .grid_y = 1, .num_elements = (size_t)grid * 32u * r->spec.warps,
        .args = args, .n_args = OMEGA_RES_ARGS_WORDS, .timeout_ms = 20000, .shared_bytes = OMEGA_RES2_SHARED_BYTES,
    };
    if (!omega_bw_gpr_declared_ok(L.gpr_count, r->peak)) { omega_gpu_session_set_error("resident2: declared registers below peak + 2"); return OMEGA_GPU_RESIDENT_CHIP_FAIL; }
    if (omega_gpu_cmdlist_add(cl, &L) < 0) return OMEGA_GPU_RESIDENT_CHIP_FAIL;
    if (r->spec.mutant == 0) r->base += (count - 1u) * grid;
    return OMEGA_GPU_RESIDENT_OK;
}

/* After a failed or abandoned batch (recorded launches dropped, or a submit that failed): the barrier count is unknown, so
 * start over on a fresh word (nothing of this object may be in flight). */
int omega_gpu_resident2_reset(OmegaGpuResident *r) {
    if (!r) return OMEGA_GPU_RESIDENT_BAD_ARGS;
    r->batch_open = 0;
    return bar_fresh(r);
}
