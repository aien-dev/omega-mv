/* omega_gpu_fx1_api.c: FX1 lossless FP8 weights, host encoder/decoder and the GEMV on the shared session (L274).
 * See omega_gpu_fx1_api.h and omega_blackwell_codegen_fx1.h. Submission and buffer policy follow omega_gpu_fp8_api.c:
 * x' and y are GPU-uncached session scratch (both sides touch them); the coded weights are written once by the host
 * and only read by the chip, so they may be GPU-cached like the raw weights (omega_gpu_fp8_weight_cache). */
#include "omega_gpu_fx1_api.h"
#include "omega_blackwell_codegen_fx1.h"
#include "omega_gpu_session.h"
#include "omega_gpu_mem_cached.h"
#include <stdlib.h>
#include <string.h>

#define MARKER_TIMEOUT_MS 20000ull
#define POISON_F32 (-999.0f)
#define FX_LANES 32u
#define FX_LB 16u

/* ---------------------------------------------------------------- host code */

/* Huffman lengths over the 16 exponent values, limited to OMEGA_FX1_LMAX (ITU T.81 Annex K.3 adjustment),
 * then the most frequent values get the shortest codes. */
static void fx_lengths(const uint64_t *hist, uint8_t *len) {
    int n = 0, sym[16], parent[32], alive[32], na, nodes;
    uint64_t w[32];
    for (int i = 0; i < 16; i++) { len[i] = 0; if (hist[i]) { sym[n] = i; w[n] = hist[i]; n++; } }
    if (n == 0) return;
    if (n == 1) { len[sym[0]] = 1; return; }
    nodes = n; na = n;
    for (int i = 0; i < n; i++) alive[i] = i;
    while (na > 1) {
        int a = 0, b = 1;
        if (w[alive[b]] < w[alive[a]]) { a = 1; b = 0; }
        for (int i = 2; i < na; i++) {
            if (w[alive[i]] < w[alive[a]]) { b = a; a = i; } else if (w[alive[i]] < w[alive[b]]) b = i;
        }
        int x = alive[a], y = alive[b];
        w[nodes] = w[x] + w[y]; parent[x] = nodes; parent[y] = nodes;
        alive[a] = nodes; alive[b] = alive[--na]; nodes++;
    }
    parent[nodes - 1] = -1;
    int bits[64] = {0};
    for (int i = 0; i < n; i++) { int d = 0; for (int v = i; parent[v] >= 0; v = parent[v]) d++; bits[d]++; }
    for (int i = 63; i > (int)OMEGA_FX1_LMAX; i--)
        while (bits[i] > 0) { int j = i - 2; while (bits[j] == 0) j--; bits[i] -= 2; bits[i - 1]++; bits[j + 1] += 2; bits[j]--; }
    int ord[16];
    for (int i = 0; i < n; i++) ord[i] = sym[i];
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (hist[ord[j]] > hist[ord[i]]) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int at = 0;
    for (int l = 1; l <= (int)OMEGA_FX1_LMAX; l++) for (int c = 0; c < bits[l]; c++) len[ord[at++]] = (uint8_t)l;
}

int omega_fx1_encode(uint32_t n, uint32_t k, const uint8_t *w, OmegaFx1Host *out) {
    if (!out) return OMEGA_GPU_FP8_BAD_ARGS;
    memset(out, 0, sizeof *out);
    if (!w || n == 0 || k == 0 || k % 512u || k > 32768u) return OMEGA_GPU_FP8_BAD_ARGS;
    const uint32_t steps = k / (FX_LANES * FX_LB), maxw = (k / FX_LANES * OMEGA_FX1_LMAX + 31u) / 32u;
    uint64_t hist[16] = {0};
    for (uint64_t i = 0; i < (uint64_t)n * k; i++) hist[(w[i] >> 3) & 15]++;
    fx_lengths(hist, out->len);
    uint32_t code[16] = {0}, c = 0;
    for (uint32_t l = 1; l <= OMEGA_FX1_LMAX; l++) { for (int s = 0; s < 16; s++) if (out->len[s] == l) code[s] = c++; c <<= 1; }
    for (int s = 0; s < 16; s++) {
        uint32_t l = out->len[s];
        if (!l) continue;
        uint32_t first = code[s] << (OMEGA_FX1_LMAX - l), cnt = 1u << (OMEGA_FX1_LMAX - l);
        for (uint32_t i = 0; i < cnt; i++) out->lut[first + i] = ((uint32_t)s << 23) | l;
    }
    out->n = n; out->k = k;
    out->nib = malloc((size_t)n * k / 2);
    out->rows = malloc(sizeof(uint32_t) * n);
    size_t cap = (size_t)n * maxw * FX_LANES + OMEGA_FX1_TAIL_WORD_ROWS * FX_LANES;
    out->exp = calloc(cap, sizeof(uint32_t));
    uint32_t *lw = malloc(sizeof(uint32_t) * maxw * FX_LANES);
    if (!out->nib || !out->rows || !out->exp || !lw) { free(lw); omega_fx1_host_free(out); return OMEGA_GPU_FP8_BAD_ARGS; }
    size_t at = 0;
    for (uint32_t r = 0; r < n; r++) {
        const uint8_t *row = w + (uint64_t)r * k;
        uint32_t wl[FX_LANES], LS = 0;
        for (uint32_t L = 0; L < FX_LANES; L++) {
            uint32_t *o = lw + L * maxw, nw = 0;
            uint64_t acc = 0;
            uint32_t nb = 0;
            for (uint32_t s = 0; s < steps; s++)
                for (uint32_t b = 0; b < FX_LB; b++) {
                    uint32_t e = (row[s * FX_LANES * FX_LB + L * FX_LB + b] >> 3) & 15;
                    acc = (acc << out->len[e]) | code[e];
                    nb += out->len[e];
                    if (nb >= 32) { o[nw++] = (uint32_t)(acc >> (nb - 32)); nb -= 32; acc &= (1ull << nb) - 1; }
                }
            if (nb) o[nw++] = (uint32_t)(acc << (32 - nb));
            wl[L] = nw;
            if (nw > LS) LS = nw;
        }
        out->rows[r] = (uint32_t)at;
        for (uint32_t j = 0; j < LS; j++)
            for (uint32_t L = 0; L < FX_LANES; L++) out->exp[at + (size_t)j * FX_LANES + L] = j < wl[L] ? lw[L * maxw + j] : 0;
        at += (size_t)LS * FX_LANES;
        for (uint32_t i = 0; i < k; i += 2)
            out->nib[((size_t)r * k + i) / 2] = (uint8_t)((((row[i] >> 4) & 8) | (row[i] & 7)) | ((((row[i + 1] >> 4) & 8) | (row[i + 1] & 7)) << 4));
    }
    free(lw);
    out->exp_words = at + OMEGA_FX1_TAIL_WORD_ROWS * FX_LANES; /* the tail stays zero (calloc) */
    uint32_t *shrunk = realloc(out->exp, out->exp_words * sizeof(uint32_t));
    if (shrunk) out->exp = shrunk;
    out->coded_bytes = (uint64_t)n * k / 2 + out->exp_words * 4u + (uint64_t)n * 4u + 256u * 4u;
    return OMEGA_GPU_FP8_OK;
}

/* Decode as a kernel lane does (omega_blackwell_codegen_fx1.h): groups of 4 codes, window from two words. */
int omega_fx1_decode_check(const OmegaFx1Host *h, const uint8_t *w, size_t *first_bad) {
    if (!h || !w || !h->nib || !h->exp) return -1;
    const uint32_t n = h->n, k = h->k, steps = k / (FX_LANES * FX_LB);
    for (uint32_t r = 0; r < n; r++) {
        const uint32_t *reg = h->exp + h->rows[r];
        for (uint32_t L = 0; L < FX_LANES; L++) {
            uint32_t p = 0;
            for (uint32_t s = 0; s < steps; s++)
                for (uint32_t g4 = 0; g4 < FX_LB; g4 += 4) {
                    uint32_t i = p >> 5, o = p & 31;
                    uint32_t w0 = reg[(size_t)i * FX_LANES + L], w1 = reg[(size_t)(i + 1) * FX_LANES + L];
                    uint32_t win = (w0 << o) + (uint32_t)(((uint64_t)w1 << o) >> 32);
                    for (uint32_t b = g4; b < g4 + 4; b++) {
                        uint32_t ent = h->lut[win >> (32 - OMEGA_FX1_LMAX)], l = ent & 0xffu;
                        win <<= l; p += l;
                        size_t c = (size_t)r * k + s * FX_LANES * FX_LB + L * FX_LB + b;
                        uint32_t q = (h->nib[c / 2] >> ((c & 1) * 4)) & 15u;
                        uint8_t v = (uint8_t)(((q & 8u) << 4) | (((ent >> 23) & 15u) << 3) | (q & 7u));
                        if (v != w[c]) { if (first_bad) *first_bad = c; return -1; }
                    }
                }
        }
    }
    return 0;
}

void omega_fx1_host_free(OmegaFx1Host *h) {
    if (!h) return;
    free(h->nib); free(h->exp); free(h->rows);
    memset(h, 0, sizeof *h);
}

/* ---------------------------------------------------------------- device */

struct OmegaGpuFx1Tensor {
    uint32_t n, k;
    uint64_t coded_bytes;
    OmegaGpuMem nib, exp, rows, lut, s;
};

typedef struct {
    bool valid;
    OmegaFx1GemvSpec spec;
    OmegaBlackwellKernel kernel;
    OmegaGpuMem code;
    uint32_t insns, gprs;
} Slot;

#define CACHE_SLOTS 8
static struct {
    OmegaGpuScratch x_buf, y_buf;
    uint32_t rows, warps, ctas;
    int mutant;
    Slot cache[CACHE_SLOTS];
    unsigned next;
    bool hooked;
} g;

static void on_session_close(void) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++) if (g.cache[i].valid) { omega_gpu_session_free(&g.cache[i].code); g.cache[i].valid = false; }
    omega_gpu_session_scratch_free(&g.x_buf);
    omega_gpu_session_scratch_free(&g.y_buf);
}

static OmegaGpuSession *dev_open_locked(void) {
    if (!g.hooked) { (void)omega_gpu_session_on_close(on_session_close); g.hooked = true; }
    return omega_gpu_session_open();
}

void omega_gpu_fx1_set_shape(uint32_t rows, uint32_t warps, uint32_t ctas) { omega_gpu_session_lock(); g.rows = rows; g.warps = warps; g.ctas = ctas; omega_gpu_session_unlock(); }
void omega_gpu_fx1_test_set_mutant(int m) { omega_gpu_session_lock(); g.mutant = m; omega_gpu_session_unlock(); }

static int kernel_for(const OmegaFx1GemvSpec *spec, Slot **out, bool *hit) {
    for (unsigned i = 0; i < CACHE_SLOTS; i++)
        if (g.cache[i].valid && memcmp(&g.cache[i].spec, spec, sizeof *spec) == 0) { *out = &g.cache[i]; *hit = true; return OMEGA_GPU_FP8_OK; }
    Slot *s = &g.cache[g.next];
    g.next = (g.next + 1) % CACHE_SLOTS;
    BlackwellIRProgram *prog = omega_gpu_host_calloc(1, sizeof *prog);
    if (!prog) return OMEGA_GPU_FP8_CODEGEN_FAIL;
    int cg = omega_blackwell_codegen_fx1_gemv_prog(spec, prog);
    size_t need = cg == 0 ? omega_bw_program_code_bytes(prog) : 0;
    if (cg != 0 || need == 0) { omega_gpu_host_free(prog); omega_gpu_session_set_error("fx1 codegen refused the shape"); return OMEGA_GPU_FP8_CODEGEN_FAIL; }
    uint32_t peak = prog->regalloc.peak_gpr_usage, count = (uint32_t)prog->count;
    if ((uint64_t)32u * spec->warps * omega_bw_gpr_declared(peak) > 65536u) {
        omega_gpu_host_free(prog); omega_gpu_session_set_error("fx1 kernel: register file exceeded"); return OMEGA_GPU_FP8_TOO_LARGE;
    }
    OmegaGpuMem code;
    if (omega_gpu_session_alloc_code(need, &code) != 0) { omega_gpu_host_free(prog); return OMEGA_GPU_FP8_CHIP_FAIL; }
    OmegaBlackwellKernel kernel;
    cg = omega_blackwell_kernel_encode_into(prog, code.cpu, need, &kernel);
    omega_gpu_host_free(prog);
    if (cg != 0 || !kernel.code || kernel.code_size != need) { omega_gpu_session_free(&code); return OMEGA_GPU_FP8_CODEGEN_FAIL; }
    if (omega_gpu_session_sync_to_gpu(&code) != 0) { omega_gpu_session_free(&code); return OMEGA_GPU_FP8_CHIP_FAIL; }
    if (s->valid) omega_gpu_session_free(&s->code);
    memset(s, 0, sizeof *s);
    s->valid = true; s->spec = *spec; s->kernel = kernel; s->code = code; s->insns = count; s->gprs = peak;
    *out = s; *hit = false;
    return OMEGA_GPU_FP8_OK;
}

static int up(OmegaGpuMem *m, const void *src, size_t bytes, size_t alloc_bytes) {
    int (*const alloc)(size_t, OmegaGpuMem *) = omega_gpu_fp8_weight_cache() ? omega_gpu_session_alloc_cached : omega_gpu_session_alloc;
    if (alloc(alloc_bytes, m) != 0) return -1;
    memset(m->cpu, 0, alloc_bytes);
    if (src) memcpy(m->cpu, src, bytes);
    return 0;
}

int omega_gpu_fx1_tensor_upload(const OmegaFx1Host *h, const uint16_t *scale, OmegaGpuFx1Tensor **out) {
    if (out) *out = NULL;
    if (!h || !h->nib || !h->exp || !scale || !out || h->n % 128u || h->k % 512u) return OMEGA_GPU_FP8_BAD_ARGS;
    OmegaGpuFx1Tensor *t = omega_gpu_host_calloc(1, sizeof *t);
    if (!t) return OMEGA_GPU_FP8_BAD_ARGS;
    t->n = h->n; t->k = h->k; t->coded_bytes = h->coded_bytes;
    const size_t scount = (size_t)(h->n / 128u) * (h->k / 128u);
    int rc = OMEGA_GPU_FP8_OK;
    omega_gpu_session_lock();
    if (!dev_open_locked() ||
        up(&t->nib, h->nib, (size_t)h->n * h->k / 2, (size_t)h->n * h->k / 2) != 0 ||
        up(&t->exp, h->exp, h->exp_words * 4u, h->exp_words * 4u) != 0 ||
        up(&t->rows, h->rows, (size_t)h->n * 4u, (size_t)h->n * 4u) != 0 ||
        up(&t->lut, h->lut, 256u * 4u, (size_t)omega_fx1_lut_words(32) * 4u) != 0 ||
        up(&t->s, NULL, 0, scount * 4u) != 0) rc = OMEGA_GPU_FP8_CHIP_FAIL;
    omega_gpu_session_unlock();
    if (rc == OMEGA_GPU_FP8_OK && omega_fp8_pack_scales(scale, scount, t->s.cpu, 0) != 0) { omega_gpu_session_set_error("fx1 upload: bad scale"); rc = OMEGA_GPU_FP8_BAD_ARGS; }
    if (rc == OMEGA_GPU_FP8_OK) {
        OmegaGpuMem *m[5] = { &t->nib, &t->exp, &t->rows, &t->lut, &t->s };
        for (int i = 0; i < 5 && rc == OMEGA_GPU_FP8_OK; i++) if (omega_gpu_session_sync_to_gpu(m[i]) != 0) rc = OMEGA_GPU_FP8_CHIP_FAIL;
    }
    if (rc != OMEGA_GPU_FP8_OK) { omega_gpu_fx1_tensor_free(t); return rc; }
    *out = t;
    return OMEGA_GPU_FP8_OK;
}

void omega_gpu_fx1_tensor_free(OmegaGpuFx1Tensor *t) {
    if (!t) return;
    omega_gpu_session_lock();
    OmegaGpuMem *m[5] = { &t->nib, &t->exp, &t->rows, &t->lut, &t->s };
    for (int i = 0; i < 5; i++) if (m[i]->cpu) omega_gpu_session_free(m[i]);
    omega_gpu_session_unlock();
    omega_gpu_host_free(t);
}

int omega_gpu_fx1_gemv_f32(const OmegaGpuFx1Tensor *t, const float *x, float *y, OmegaGpuFx1Info *info) {
    if (info) memset(info, 0, sizeof *info);
    if (!t || !x || !y) return OMEGA_GPU_FP8_BAD_ARGS;
    const uint32_t n = t->n, k = t->k;
    omega_gpu_session_lock();
    OmegaFx1GemvSpec spec = { .k = k, .rows = g.rows ? g.rows : 2u, .warps = g.warps ? g.warps : 8u, .mutant = g.mutant };
    const uint32_t RG = n / spec.rows;
    uint32_t grid_x = g.ctas ? g.ctas : (RG + spec.warps - 1) / spec.warps;
    if (grid_x > OMEGA_GPU_FP8_MAX_CTAS && !g.ctas) grid_x = OMEGA_GPU_FP8_MAX_CTAS;
    int rc = OMEGA_GPU_FP8_OK;
    if (!dev_open_locked()) { omega_gpu_session_unlock(); return OMEGA_GPU_FP8_CHIP_FAIL; }
    int src = omega_gpu_session_scratch(&g.x_buf, (size_t)k * 4u);
    if (src == 0) src = omega_gpu_session_scratch(&g.y_buf, (size_t)n * 4u);
    if (src != 0) { omega_gpu_session_unlock(); return OMEGA_GPU_FP8_CHIP_FAIL; }
    Slot *ks = NULL; bool hit = false;
    rc = kernel_for(&spec, &ks, &hit);
    if (rc != OMEGA_GPU_FP8_OK) { omega_gpu_session_unlock(); return rc; }
    float *xd = g.x_buf.mem.cpu, *yd = g.y_buf.mem.cpu;
    omega_fp8_pack_x_f32(x, k, xd);
    for (uint32_t i = 0; i < n; i++) yd[i] = POISON_F32;
    if (omega_gpu_session_sync_to_gpu(&g.x_buf.mem) != 0 || omega_gpu_session_sync_to_gpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_FP8_CHIP_FAIL;
    uint32_t args[OMEGA_FX1_ARGS_WORDS] = {0};
    const uint64_t va[7] = { t->nib.va, t->exp.va, t->rows.va, t->lut.va, t->s.va, g.x_buf.mem.va, g.y_buf.mem.va };
    for (int i = 0; i < 7; i++) { args[2 * i] = (uint32_t)va[i]; args[2 * i + 1] = (uint32_t)(va[i] >> 32); }
    args[14] = RG; args[15] = grid_x * spec.warps;
    if (info) {
        info->n = n; info->k = k; info->rows_per_warp = spec.rows; info->warps_per_cta = spec.warps; info->grid_x = grid_x;
        info->insns = ks->insns; info->gprs = ks->gprs; info->coded_bytes = t->coded_bytes; info->raw_bytes = (uint64_t)n * k;
        info->kernel_cache_hit = hit;
    }
    if (rc == OMEGA_GPU_FP8_OK) {
        OmegaGpuLaunch L = {
            .code_va = ks->code.va, .gpr_count = omega_bw_gpr_declared(ks->kernel.gpr_count),
            .threads_x = 32u * spec.warps, .threads_y = 1, .grid_x = grid_x, .grid_y = 1,
            .num_elements = n, .args = args, .n_args = OMEGA_FX1_ARGS_WORDS, .timeout_ms = MARKER_TIMEOUT_MS,
            .shared_bytes = omega_fx1_shared_bytes(&spec),
        };
        if (omega_gpu_session_launch(&L, info ? &info->elapsed_ns : NULL, NULL) != 0) rc = OMEGA_GPU_FP8_CHIP_FAIL;
    }
    if (rc == OMEGA_GPU_FP8_OK && omega_gpu_session_sync_to_cpu(&g.y_buf.mem) != 0) rc = OMEGA_GPU_FP8_CHIP_FAIL;
    if (rc == OMEGA_GPU_FP8_OK) {
        size_t unchanged = 0;
        for (uint32_t i = 0; i < n; i++) if (yd[i] == POISON_F32) unchanged++;
        if (unchanged) { omega_gpu_session_set_error("fx1: output poison survived"); rc = OMEGA_GPU_FP8_CHIP_FAIL; }
        else memcpy(y, yd, (size_t)n * sizeof *y);
    }
    omega_gpu_session_unlock();
    return rc;
}
