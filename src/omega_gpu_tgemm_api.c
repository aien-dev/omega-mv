/* omega_gpu_tgemm_api.c: the trainer's device-resident bf16 GEMM (lane E1, cuts 1 and 2). See the header. */
#include "omega_gpu_tgemm_api.h"
#include "omega_gpu_prefill.h"
#include <string.h>

#define TG_TIMEOUT_MS 20000u  /* the prefill module's launch limits (private there; same values) */
#define TG_SPIN_US 200000u
#define TG_SMS 48u            /* GB10 streaming multiprocessors (one CTA of 512 threads x 128 registers each) */
static struct { uint32_t rt, warps, aqn; int mutant, rmutant; OmegaGpuMem tab, part; } g;
void omega_gpu_tgemm_set_shape(uint32_t rt, uint32_t warps, uint32_t aqn) { g.rt = rt; g.warps = warps; g.aqn = aqn; }
void omega_gpu_tgemm_test_set_mutant(int m) { g.mutant = m; }
void omega_gpu_tgemm_test_set_reduce_mutant(int m) { g.rmutant = m; }
uint32_t omega_gpu_tgemm_bm(uint32_t n) { if (g.rt) return 16u * g.rt; const uint32_t w = g.warps ? g.warps : (n % 256u ? 8u : 16u); return w == 16u ? 128u : 64u; } /* mirrors omega_gpu_tgemm_spec */
_Static_assert(sizeof(OmegaTgemmTile) == OMEGA_TGEMM_TILE_BYTES, "tile entry");

static uint64_t a_bytes(const OmegaTgemmCall *c) { return (uint64_t)c->m * c->k * 2u; }
static uint64_t b_bytes(const OmegaTgemmCall *c) { return (uint64_t)c->k * c->n * 2u; }

int omega_gpu_tgemm_spec(const OmegaTgemmCall *c, OmegaTgemmSpec *out, uint32_t *splits_out) {
    if (!c || !out) return OMEGA_GPU_PF_BAD_ARGS;
    OmegaTgemmSpec s; memset(&s, 0, sizeof s);
    s.k = c->k; s.n = c->n; s.m = c->m; s.rt = g.rt ? g.rt : 8u; s.ct = 2; s.kc = 32; s.aqn = g.aqn ? g.aqn : 1u; s.layout = c->layout; s.epi = c->epi; s.mutant = g.mutant;
    s.warps = g.warps ? g.warps : (c->n % 256u ? 8u : 16u);
    if (!g.rt) s.rt = s.warps == 16u ? 8u : 4u; /* chip sweep TGEMM-CUT1: rt 8 x 16 warps fastest on every gate shape; rt 8 x 8 warps does not fit the register file */
    s.tfast = b_bytes(c) > a_bytes(c) ? 1u : 0u;
    if (c->splits > 16u || !c->a_va || !c->b_va || !c->c_va || c->m == 0 || c->k == 0 || c->k % 128u) return OMEGA_GPU_PF_BAD_ARGS;
    if (!omega_tgemm_spec_ok(&s)) return OMEGA_GPU_PF_BAD_ARGS;
    if (c->m % omega_tgemm_bm(&s)) return OMEGA_GPU_PF_BAD_ARGS;
    if ((uint64_t)c->m * c->n >= (1ull << 32)) return OMEGA_GPU_PF_TOO_LARGE; /* the kernel forms row * n + col in 32 bits before the wide scale (review of PR 487) */
    const uint32_t nt = c->m / omega_tgemm_bm(&s), gx = c->n / omega_tgemm_span(&s), nkb = c->k / 128u;
    uint32_t S = c->splits;
    if (S == 0) { S = 1; if ((uint64_t)nt * gx < 3u * TG_SMS) for (S = 2; S <= 16u; S *= 2) { if (nkb % S) { S /= 2; break; } if ((uint64_t)nt * gx * S >= 3u * TG_SMS) break; } if (S > 16u) S = 16; if (S == 0) S = 1; }
    if (S > 1u) {
        if (nkb % S) return OMEGA_GPU_PF_BAD_ARGS;
        if (((uint64_t)c->m * c->n) % OMEGA_TGEMM_REDUCE_WORDS_PER_CTA) return OMEGA_GPU_PF_BAD_ARGS;
        if ((uint64_t)c->m * c->n / OMEGA_TGEMM_REDUCE_WORDS_PER_CTA > 65535u) return OMEGA_GPU_PF_TOO_LARGE;
        s.epi = OMEGA_TGEMM_EPI_F32; /* partials; the reduce applies the call's epilogue */
    }
    if ((uint64_t)nt * S > 65535u) return OMEGA_GPU_PF_TOO_LARGE;
    *out = s;
    if (splits_out) *splits_out = S;
    return OMEGA_GPU_PF_OK;
}

uint32_t omega_gpu_tgemm_tiles(const OmegaTgemmCall *c, uint64_t part_va, OmegaTgemmTile *tab, uint32_t max) {
    OmegaTgemmSpec s; uint32_t S;
    if (!tab || omega_gpu_tgemm_spec(c, &s, &S) != OMEGA_GPU_PF_OK) return 0;
    const uint32_t bm = omega_tgemm_bm(&s), nt = c->m / bm, nkb = c->k / 128u / S;
    if ((uint64_t)nt * S > max || (S > 1u && !part_va)) return 0;
    for (uint32_t sp = 0; sp < S; sp++) for (uint32_t t = 0; t < nt; t++) {
        OmegaTgemmTile *x = &tab[sp * nt + t];
        x->c_va = S > 1u ? part_va + (uint64_t)sp * c->m * c->n * 4u : c->c_va; x->row0 = t * bm; x->kb0 = sp * nkb; x->nkb = nkb; x->pad = 0; x->pad2 = 0;
    }
    return nt * S;
}

static int gen_tgemm(const OmegaPfKey *key, void *prog) { OmegaTgemmSpec s; memcpy(&s, key->words, sizeof s); return omega_blackwell_codegen_tgemm_prog(&s, prog); }
static int gen_reduce(const OmegaPfKey *key, void *prog) { OmegaTgemmReduceSpec r; memcpy(&r, key->words, sizeof r); return omega_blackwell_codegen_tgemm_reduce_prog(&r, prog); }

static int kernel_of(uint32_t kind, const void *spec, size_t len, int (*gen)(const OmegaPfKey *, void *), uint32_t threads, OmegaPfKernel *k) {
    OmegaPfKey key; memset(&key, 0, sizeof key); key.kind = kind; memcpy(key.words, spec, len);
    int rc = omega_gpu_pf_kernel_locked(&key, gen, k);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    if ((uint64_t)threads * k->gpr_declared > 65536u) return OMEGA_GPU_PF_TOO_LARGE;
    if (!omega_bw_gpr_declared_ok(k->gpr_declared, k->gpr_peak)) return OMEGA_GPU_PF_CHIP_FAIL;
    return OMEGA_GPU_PF_OK;
}
static void put_va(uint32_t *args, unsigned i, uint64_t va) { args[2 * i] = (uint32_t)va; args[2 * i + 1] = (uint32_t)(va >> 32); }

int omega_gpu_tgemm_fill_launch_locked(const OmegaTgemmCall *c, uint64_t tiles_va, uint32_t ntiles, OmegaGpuLaunch *L, uint32_t *args) {
    OmegaTgemmSpec s; uint32_t S;
    if (!L || !args || !tiles_va || !ntiles) return OMEGA_GPU_PF_BAD_ARGS;
    int rc = omega_gpu_tgemm_spec(c, &s, &S);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    _Static_assert(sizeof(OmegaTgemmSpec) <= sizeof ((OmegaPfKey *)0)->words, "spec fits the key");
    OmegaPfKernel k; const uint32_t threads = 32u * s.warps;
    rc = kernel_of(OMEGA_TGEMM_KIND, &s, sizeof s, gen_tgemm, threads, &k);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    put_va(args, 0, tiles_va); put_va(args, 1, c->a_va); put_va(args, 2, c->b_va);
    memset(L, 0, sizeof *L);
    L->code_va = k.code_va; L->gpr_count = k.gpr_declared; L->threads_x = threads; L->threads_y = 1;
    const uint32_t gcol = c->n / omega_tgemm_span(&s);
    L->grid_x = s.tfast ? ntiles : gcol; L->grid_y = s.tfast ? gcol : ntiles; L->num_elements = (size_t)threads * gcol * ntiles;
    L->args = args; L->n_args = OMEGA_TGEMM_ARGS_WORDS; L->timeout_ms = TG_TIMEOUT_MS; L->shared_bytes = omega_tgemm_shared_bytes(&s); L->spin_us = TG_SPIN_US;
    return OMEGA_GPU_PF_OK;
}

int omega_gpu_tgemm_fill_reduce_launch_locked(const OmegaTgemmCall *c, uint64_t part_va, OmegaGpuLaunch *L, uint32_t *args) {
    OmegaTgemmSpec s; uint32_t S;
    if (!L || !args || !part_va) return OMEGA_GPU_PF_BAD_ARGS;
    int rc = omega_gpu_tgemm_spec(c, &s, &S);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    if (S < 2u) return OMEGA_GPU_PF_BAD_ARGS;
    OmegaTgemmReduceSpec r; memset(&r, 0, sizeof r);
    r.splits = S; r.epi = c->epi; r.pstride = c->m * c->n * 4u; r.mutant = g.rmutant;
    if (!omega_tgemm_reduce_spec_ok(&r)) return OMEGA_GPU_PF_BAD_ARGS;
    OmegaPfKernel k;
    rc = kernel_of(OMEGA_TGEMM_REDUCE_KIND, &r, sizeof r, gen_reduce, OMEGA_TGEMM_REDUCE_THREADS, &k);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    put_va(args, 0, c->c_va); put_va(args, 1, part_va);
    memset(L, 0, sizeof *L);
    L->code_va = k.code_va; L->gpr_count = k.gpr_declared; L->threads_x = OMEGA_TGEMM_REDUCE_THREADS; L->threads_y = 1;
    L->grid_x = (uint32_t)(((uint64_t)c->m * c->n) / OMEGA_TGEMM_REDUCE_WORDS_PER_CTA); L->grid_y = 1; L->num_elements = (size_t)OMEGA_TGEMM_REDUCE_THREADS * L->grid_x;
    L->args = args; L->n_args = OMEGA_TGEMM_REDUCE_ARGS_WORDS; L->timeout_ms = TG_TIMEOUT_MS; L->shared_bytes = 0; L->spin_us = TG_SPIN_US;
    return OMEGA_GPU_PF_OK;
}

static int grow_locked(OmegaGpuMem *m, size_t need) {
    if (m->size >= need) return OMEGA_GPU_PF_OK;
    omega_gpu_session_free(m);
    return omega_gpu_session_alloc(need, m) == 0 ? OMEGA_GPU_PF_OK : OMEGA_GPU_PF_CHIP_FAIL;
}

int omega_gpu_tgemm(const OmegaTgemmCall *c, uint64_t *ns) {
    OmegaTgemmSpec s; uint32_t S;
    int rc = omega_gpu_tgemm_spec(c, &s, &S);
    if (rc != OMEGA_GPU_PF_OK) return rc;
    const uint32_t nt = c->m / omega_tgemm_bm(&s) * S;
    const size_t need = ((size_t)nt * OMEGA_TGEMM_TILE_BYTES + 4095u) & ~(size_t)4095u;
    const size_t pneed = S > 1u ? (((size_t)S * c->m * c->n * 4u + 4095u) & ~(size_t)4095u) : 0u;
    OmegaGpuLaunch L, R; uint32_t args[OMEGA_TGEMM_ARGS_WORDS], rargs[OMEGA_TGEMM_REDUCE_ARGS_WORDS];
    /* a bigger partial buffer is allocated outside the lock (the cached allocator takes the lock itself) and swapped in under it;
     * the launches run under the lock, so no kernel can still be reading the buffer that is freed */
    OmegaGpuMem fresh; memset(&fresh, 0, sizeof fresh);
    if (pneed && g.part.size < pneed) { rc = omega_gpu_pf_alloc_dev(pneed, &fresh); if (rc != OMEGA_GPU_PF_OK) return rc; }
    /* the module table and partials are shared state: (re)built only under the session lock, which also serializes the launches */
    omega_gpu_session_lock();
    rc = omega_gpu_pf_open_locked();
    if (fresh.size) { if (g.part.size < pneed) { omega_gpu_session_free(&g.part); g.part = fresh; } else omega_gpu_session_free(&fresh); }
    if (rc == OMEGA_GPU_PF_OK) rc = grow_locked(&g.tab, need);
    if (rc == OMEGA_GPU_PF_OK && omega_gpu_tgemm_tiles(c, pneed ? g.part.va : 0, g.tab.cpu, nt) != nt) rc = OMEGA_GPU_PF_BAD_ARGS;
    if (rc == OMEGA_GPU_PF_OK && omega_gpu_session_sync_to_gpu(&g.tab) != 0) rc = OMEGA_GPU_PF_CHIP_FAIL;
    if (rc == OMEGA_GPU_PF_OK) rc = omega_gpu_tgemm_fill_launch_locked(c, g.tab.va, nt, &L, args);
    if (rc == OMEGA_GPU_PF_OK && S > 1u) rc = omega_gpu_tgemm_fill_reduce_launch_locked(c, g.part.va, &R, rargs);
    if (rc == OMEGA_GPU_PF_OK) {
        uint64_t t = 0;
        if (omega_gpu_session_launch(&L, &t, NULL) != 0) rc = OMEGA_GPU_PF_CHIP_FAIL;
        else if (ns) *ns += t;
    }
    if (rc == OMEGA_GPU_PF_OK && S > 1u) {
        uint64_t t = 0;
        if (omega_gpu_session_launch(&R, &t, NULL) != 0) rc = OMEGA_GPU_PF_CHIP_FAIL;
        else if (ns) *ns += t;
    }
    omega_gpu_session_unlock();
    return rc;
}
