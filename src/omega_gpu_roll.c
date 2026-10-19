/*
 * omega_gpu_roll.c: rolls, melodies, chords and harmony for the GPU engine (omega-mv#580). See the header.
 *
 * Slot layout (one GPU-uncached allocation per slot, voice-local VA):
 *   0x0000  marker page: first marker at +0, second marker at +0x10
 *   0x1000  node release semaphores, 16 bytes each (OMEGA_ROLL_MAX_NODES * 16 = 0x400)
 *   0x2000  shader scratch (0x4000, zeroed once; the QMD scratch window, as src/omega_gpu_cmdlist.c)
 *   0x6000  constant banks, 0x400 per node (driver words, arguments at 0x380)
 *   ...     QMDs, 0x400 per node (standalone QMD1 for SEND_PCAS)
 *   ...     method words of the group (the GPFIFO entry points here)
 *   ...     data: every own buffer of the group, 256-byte aligned
 */
#include "omega_gpu_roll.h"
#include "omega_blackwell_qmd.h"
#include "omega_blackwell_submit.h"
#include "omega_gpu_code_alloc.h"
#include "m16_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>

#define PAGE 0x1000u
#define MARK_OFF 0x0u
#define SEM_OFF 0x1000u
#define SCRATCH_OFF 0x2000u
#define SCRATCH_BYTES 0x4000u
#define BANK_OFF 0x6000u
#define BANK_BYTES 0x400u
#define QMD_BYTES 0x400u
#define MTHD_WAIT_FOR_IDLE 0x0110u          /* clcec0.h NVCEC0_WAIT_FOR_IDLE */
#define MTHD_SEND_PCAS_A 0x02b4u            /* clcec0.h NVCEC0_SEND_PCAS_A */
#define MTHD_SEND_SIGNALING_PCAS2_B 0x02c0u /* clcec0.h */
#define PCAS_ACTION_INVALIDATE_COPY_SCHEDULE 3u
#define SEM_ACQUIRE_WORDS 6u
#define TAIL_WORDS 17u
#define MAX_KERNELS 32u
#define DEFAULT_TIMEOUT_MS 20000ull

static const uint32_t SETUP_WORDS[18] = { /* src/omega_gpu_session.c SETUP_WORDS (compute class setup) */
    0x20012061, 0x0000cec0, 0x20012092, 0x00000001, 0x200120a8, 0x0000000f, 0x2001255d, 0x00000003,
    0x2001255e, 0x20000000, 0x2001255f, 0x000fffff, 0x20012557, 0x00000003, 0x20012558, 0x22000000,
    0x20012559, 0x00000000,
};

typedef struct {
    NvrmMem mem;
    uint32_t put_after;      /* GPFIFO put after this slot's entry: retire up to here */
    uint64_t submit_ns;
    uint32_t words;
    uint64_t tag;
    uint32_t n;
    int skip;
    OmegaRollNode nodes[OMEGA_ROLL_MAX_NODES];
    uint64_t buf_off[OMEGA_ROLL_MAX_NODES][OMEGA_ROLL_MAX_BUFS]; /* data offset of each buffer (aliases resolved) */
} Slot;

typedef struct {
    M16NativeContext *m;
    Slot *slots;
    uint32_t head, count;
    NvrmMem code[MAX_KERNELS];
} Voice;

struct OmegaRoll {
    OmegaRollConfig cfg;
    size_t bank_off, qmd_off, pb_off, pb_cap_words, data_off, slot_bytes;
    uint32_t nkernels;
    Voice v[OMEGA_ROLL_MAX_VOICES];
    char err[256];
};

static int g_latched;

static uint64_t now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void seterr(OmegaRoll *r, const char *fmt, ...) {
    if (!r) return;
    va_list ap; va_start(ap, fmt); vsnprintf(r->err, sizeof r->err, fmt, ap); va_end(ap);
}
static size_t up(size_t v, size_t a) { return (v + a - 1) / a * a; }
static uint32_t round8(uint32_t v) { return (v + 7u) / 8u * 8u; }
static inline uint32_t mthd(uint32_t subc, uint32_t m, uint32_t count) { return (2u << 28) | (count << 16) | (subc << 13) | (m >> 2); }

int omega_gpu_roll_is_latched(void) { return __atomic_load_n(&g_latched, __ATOMIC_ACQUIRE); }
const char *omega_gpu_roll_error(const OmegaRoll *r) { return r ? r->err : "no roll"; }
uint32_t omega_gpu_roll_in_flight(const OmegaRoll *r, uint32_t voice) { return r && voice < r->cfg.voices ? r->v[voice].count : 0; }

/* ---- host-only: validation and the order words ---- */

int omega_gpu_roll_validate(const OmegaRollConfig *cfg, const OmegaRollGroup *g, const char **why) {
    const char *w = NULL;
    if (!cfg || !g || !g->nodes) w = "null config or group";
    else if (g->n == 0 || g->n > cfg->max_nodes || g->n > OMEGA_ROLL_MAX_NODES) w = "node count outside 1..max_nodes";
    else if (g->order < 0 || g->order >= OMEGA_ROLL_ORDER_COUNT) w = "unknown order";
    size_t data = 0;
    for (uint32_t i = 0; !w && i < g->n; i++) {
        const OmegaRollNode *nd = &g->nodes[i];
        if (!nd->k || nd->threads_x == 0 || nd->grid_x == 0 || nd->grid_y == 0 || nd->n_args == 0 || nd->n_args > OMEGA_ROLL_MAX_ARGS) { w = "node launch shape or argument count"; break; }
        if (nd->nbufs > OMEGA_ROLL_MAX_BUFS) { w = "too many buffers"; break; }
        if (i < 64 && (nd->deps >> i) != 0) { w = "a dependency on itself or a later node"; break; }
        if (nd->deps && g->order == OMEGA_ROLL_ORDER_NONE) { w = "ORDER_NONE with dependencies"; break; }
        for (uint32_t b = 0; b < nd->nbufs; b++) {
            const OmegaRollBuf *bf = &nd->buf[b];
            if (bf->arg_lo != OMEGA_ROLL_NO_ARG && (uint32_t)bf->arg_lo + 1u >= nd->n_args) { w = "buffer address argument past n_args"; break; }
            if (bf->from_node >= 0) {
                if ((uint32_t)bf->from_node >= i) { w = "alias to the same or a later node"; break; }
                const OmegaRollNode *src = &g->nodes[bf->from_node];
                if (bf->from_buf >= src->nbufs || src->buf[bf->from_buf].from_node >= 0) { w = "alias to a missing or aliased buffer"; break; }
                if (bf->in || bf->poisoned) { w = "an alias carries no input and no poison (the producer owns it)"; break; }
                if (bf->bytes > src->buf[bf->from_buf].bytes) { w = "alias larger than its source"; break; }
                if (g->order != OMEGA_ROLL_ORDER_SERIAL && !((nd->deps >> bf->from_node) & 1u)) { w = "a data alias without the matching dependency"; break; }
            } else {
                if (bf->bytes == 0 || (bf->bytes & 3u)) { w = "buffer size zero or not a multiple of 4"; break; }
                data += up(bf->bytes, 256);
            }
        }
    }
    if (!w && data > cfg->data_bytes) w = "buffers exceed data_bytes";
    if (why) *why = w;
    return w ? -1 : 0;
}

/* Barrier and launch words for every node. qmd_va(i) = qmd_base + i * QMD_BYTES, sem_va(i) = sem_base + i * 16. */
static int emit_nodes(const OmegaRollGroup *g, uint64_t qmd_base, uint64_t sem_base, uint32_t *pb, uint32_t cap) {
    uint32_t n = 0;
    uint64_t wave = 0;
#define PUT(x) do { if (n >= cap) return -1; pb[n++] = (x); } while (0)
    for (uint32_t i = 0; i < g->n; i++) {
        const OmegaRollNode *nd = &g->nodes[i];
        const uint64_t deps = g->test_ignore_deps ? 0u : nd->deps;
        switch (g->order) {
        case OMEGA_ROLL_ORDER_SERIAL:
            if (i > 0) { PUT(mthd(1, MTHD_WAIT_FOR_IDLE, 1)); PUT(0); PUT(mthd(1, OMEGA_BW_MTHD_INVALIDATE_SHADER_CACHES, 1)); PUT(OMEGA_BW_INVALIDATE_SHADER_CACHES_ALL); }
            break;
        case OMEGA_ROLL_ORDER_SEM:
            for (uint32_t d = 0; d < i; d++) {
                if (!((deps >> d) & 1u)) continue;
                const uint64_t va = sem_base + (uint64_t)d * 16u;
                /* host semaphore ACQUIRE (clc96f.h: SEM_ADDR_LO 0x5c .. SEM_EXECUTE 0x6c, OPERATION_ACQUIRE 0, 32-bit payload):
                 * the channel's method stream stops here until node d's QMD released DONE */
                PUT(mthd(0, 0x005c, 5)); PUT((uint32_t)va); PUT((uint32_t)(va >> 32)); PUT(OMEGA_BW_SEMAPHORE_INTERMEDIATE_DONE); PUT(0); PUT(0);
            }
            break;
        case OMEGA_ROLL_ORDER_WAVE:
            if (deps & wave) { PUT(mthd(1, MTHD_WAIT_FOR_IDLE, 1)); PUT(0); wave = 0; }
            wave |= 1ull << i;
            break;
        default: break; /* NONE */
        }
        if ((int)i == g->skip_node) continue; /* TEST ONLY: the launch is dropped, its barrier words stay */
        const uint64_t q = qmd_base + (uint64_t)i * QMD_BYTES;
        PUT(mthd(1, MTHD_SEND_PCAS_A, 1)); PUT((uint32_t)(q >> 8));
        PUT(mthd(1, MTHD_SEND_SIGNALING_PCAS2_B, 1)); PUT(PCAS_ACTION_INVALIDATE_COPY_SCHEDULE);
    }
#undef PUT
    return (int)n;
}

int omega_gpu_roll_order_words(const OmegaRollGroup *g, uint32_t *pb, uint32_t cap) {
    if (!g || !g->nodes || !pb) return -1;
    return emit_nodes(g, 0x100000u, 0x200000u, pb, cap);
}

/* ---- device ---- */

static size_t pb_cap_words(uint32_t max_nodes) {
    return 18u + 4u + (size_t)max_nodes * (4u + 4u + SEM_ACQUIRE_WORDS * max_nodes) + TAIL_WORDS + 16u;
}

int omega_gpu_roll_open(const OmegaRollConfig *cfg, OmegaRoll **out) {
    if (!out) return OMEGA_ROLL_ERR;
    *out = NULL;
    if (omega_gpu_roll_is_latched()) return OMEGA_ROLL_LATCHED;
    if (!cfg || cfg->voices == 0 || cfg->voices > OMEGA_ROLL_MAX_VOICES || cfg->depth == 0 || cfg->depth > OMEGA_ROLL_MAX_DEPTH ||
        cfg->max_nodes == 0 || cfg->max_nodes > OMEGA_ROLL_MAX_NODES || cfg->data_bytes == 0) return OMEGA_ROLL_ERR;
    if (cfg->depth >= 1000u) return OMEGA_ROLL_ERR; /* nvrm GPFIFO: 1024 entries */
    OmegaRoll *r = calloc(1, sizeof *r);
    if (!r) return OMEGA_ROLL_ERR;
    r->cfg = *cfg;
    if (!r->cfg.timeout_ms) r->cfg.timeout_ms = DEFAULT_TIMEOUT_MS;
    r->bank_off = BANK_OFF;
    r->qmd_off = up(r->bank_off + (size_t)cfg->max_nodes * BANK_BYTES, PAGE);
    r->pb_off = up(r->qmd_off + (size_t)cfg->max_nodes * QMD_BYTES, PAGE);
    r->pb_cap_words = pb_cap_words(cfg->max_nodes);
    r->data_off = up(r->pb_off + r->pb_cap_words * 4u, PAGE);
    r->slot_bytes = up(r->data_off + cfg->data_bytes, PAGE);
    for (uint32_t v = 0; v < cfg->voices; v++) {
        Voice *vo = &r->v[v];
        vo->m = calloc(1, sizeof *vo->m);
        vo->slots = calloc(cfg->depth, sizeof *vo->slots);
        if (!vo->m || !vo->slots) { seterr(r, "host memory"); goto fail; }
        if (m16_native_open(vo->m) != 0) { seterr(r, "voice %u: open: %s", v, vo->m->rm.err); free(vo->m); vo->m = NULL; goto fail; }
        if (m16_native_create_channel(vo->m) != 0) { seterr(r, "voice %u: channel: %s", v, vo->m->rm.err); goto fail; }
        for (uint32_t s = 0; s < cfg->depth; s++) {
            Slot *sl = &vo->slots[s];
            if (nvrm_alloc_gpu_uncached(&vo->m->rm, r->slot_bytes, &sl->mem) != 0 || !sl->mem.cpu) {
                seterr(r, "voice %u slot %u: alloc %zu bytes: %s", v, s, r->slot_bytes, vo->m->rm.err); goto fail;
            }
            memset(sl->mem.cpu, 0, r->data_off); /* markers, sems, scratch, banks, QMDs, words */
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
    *out = r;
    return OMEGA_ROLL_OK;
fail:
    fprintf(stderr, "omega_gpu_roll_open: %s\n", r->err);
    for (uint32_t v = 0; v < cfg->voices; v++) {
        if (r->v[v].m) { (void)m16_native_close(r->v[v].m); free(r->v[v].m); }
        free(r->v[v].slots);
    }
    free(r);
    return OMEGA_ROLL_ERR;
}

int omega_gpu_roll_kernel(OmegaRoll *r, const uint8_t *code, size_t bytes, uint32_t gpr, OmegaRollKernel *out) {
    if (!r || !code || !bytes || !out) return OMEGA_ROLL_ERR;
    if (omega_gpu_roll_is_latched()) return OMEGA_ROLL_LATCHED;
    if (r->nkernels >= MAX_KERNELS) { seterr(r, "kernel table full"); return OMEGA_ROLL_ERR; }
    const size_t alloc = omega_gpu_code_alloc_bytes(bytes);
    if (!alloc) { seterr(r, "code size"); return OMEGA_ROLL_ERR; }
    memset(out, 0, sizeof *out);
    const uint32_t k = r->nkernels;
    for (uint32_t v = 0; v < r->cfg.voices; v++) {
        NvrmMem *m = &r->v[v].code[k];
        if (nvrm_alloc_gpu_uncached(&r->v[v].m->rm, up(alloc, PAGE), m) != 0) { seterr(r, "code alloc: %s", r->v[v].m->rm.err); return OMEGA_ROLL_ERR; }
        memset(m->cpu, 0, m->size);
        memcpy(m->cpu, code, bytes);
        out->va[v] = m->va;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    out->gpr = gpr > 64u ? round8(gpr) : 64u;
    out->code_bytes = bytes;
    out->index = (int)k;
    r->nkernels++;
    return OMEGA_ROLL_OK;
}

int omega_gpu_roll_submit(OmegaRoll *r, uint32_t voice, const OmegaRollGroup *g) {
    if (!r || voice >= r->cfg.voices) return OMEGA_ROLL_ERR;
    if (omega_gpu_roll_is_latched()) { seterr(r, "latched"); return OMEGA_ROLL_LATCHED; }
    const char *why = NULL;
    if (omega_gpu_roll_validate(&r->cfg, g, &why) != 0) { seterr(r, "group refused: %s", why); return OMEGA_ROLL_ERR; }
    Voice *vo = &r->v[voice];
    if (vo->count >= r->cfg.depth) return OMEGA_ROLL_FULL;
    Slot *sl = &vo->slots[(vo->head + vo->count) % r->cfg.depth];
    uint8_t *base = sl->mem.cpu;
    const uint64_t va = sl->mem.va;
    sl->n = g->n; sl->tag = g->tag; sl->skip = g->skip_node;
    memcpy(sl->nodes, g->nodes, (size_t)g->n * sizeof *g->nodes);

    /* data: own buffers bump-allocated in node order, aliases resolved to the producer's offset */
    size_t doff = r->data_off;
    for (uint32_t i = 0; i < g->n; i++) {
        const OmegaRollNode *nd = &g->nodes[i];
        for (uint32_t b = 0; b < nd->nbufs; b++) {
            const OmegaRollBuf *bf = &nd->buf[b];
            if (bf->from_node >= 0) { sl->buf_off[i][b] = sl->buf_off[bf->from_node][bf->from_buf]; continue; }
            sl->buf_off[i][b] = doff;
            uint8_t *dst = base + doff;
            if (bf->poisoned) { uint32_t *w = (uint32_t *)dst; for (size_t x = 0; x < bf->bytes / 4u; x++) w[x] = bf->poison; }
            else if (bf->in) memcpy(dst, bf->in, bf->bytes);
            doff += up(bf->bytes, 256);
        }
    }
    /* banks, QMDs, semaphores */
    const uint64_t sem_base = va + SEM_OFF, qmd_base = va + r->qmd_off;
    for (uint32_t i = 0; i < g->n; i++) {
        const OmegaRollNode *nd = &g->nodes[i];
        uint32_t args[OMEGA_ROLL_MAX_ARGS];
        memcpy(args, nd->args, (size_t)nd->n_args * 4u);
        for (uint32_t b = 0; b < nd->nbufs; b++) {
            if (nd->buf[b].arg_lo == OMEGA_ROLL_NO_ARG) continue;
            const uint64_t bva = va + sl->buf_off[i][b];
            args[nd->buf[b].arg_lo] = (uint32_t)bva; args[nd->buf[b].arg_lo + 1u] = (uint32_t)(bva >> 32);
        }
        const uint32_t ty = nd->threads_y ? nd->threads_y : 1u;
        const uint64_t bank_va = va + r->bank_off + (uint64_t)i * BANK_BYTES;
        uint8_t *bank = base + r->bank_off + (size_t)i * BANK_BYTES;
        uint32_t drv[OMEGA_BW_CBANK_DRIVER_WORDS];
        omega_blackwell_build_cbank_driver_2d(drv, bank_va, nd->threads_x, ty, nd->grid_x, nd->grid_y);
        memcpy(bank, drv, sizeof drv);
        memset(bank + sizeof drv, 0, 0x380 - sizeof drv);
        memcpy(bank + 0x380, args, (size_t)nd->n_args * 4u);
        const uint64_t q1_va = qmd_base + (uint64_t)i * QMD_BYTES;
        OmegaBlackwellQmdConfig cfg = {
            .code_va = nd->k->va[voice], .cbank_va = bank_va, .scratch_va = va + SCRATCH_OFF, .sem_va = sem_base + (uint64_t)i * 16u,
            .qmd0_va = q1_va + 0x200, .qmd1_va = q1_va, .num_elements = nd->num_elements,
            .threads_per_block = nd->threads_x, .grid_width = nd->grid_x * nd->grid_y, .threads_x = nd->threads_x, .threads_y = ty,
            .grid_x = nd->grid_x, .grid_y = nd->grid_y, .gpr_count = nd->k->gpr, .shared_bytes = nd->shared_bytes,
        };
        uint32_t q1[OMEGA_BW_QMD_WORDS];
        if (omega_blackwell_build_qmd1(q1, &cfg) != 0 || omega_blackwell_verify_qmd_invariants(q1) != 0) { seterr(r, "node %u: qmd refused", i); return OMEGA_ROLL_ERR; }
        q1[10] = 0; q1[12] = 0; /* standalone grid (PCAS), as src/omega_gpu_cmdlist.c */
        memcpy(base + r->qmd_off + (size_t)i * QMD_BYTES, q1, sizeof q1);
        *(volatile uint32_t *)(base + SEM_OFF + (size_t)i * 16u) = OMEGA_BW_SEMAPHORE_INTERMEDIATE_INIT;
    }
    volatile uint32_t *mk = (volatile uint32_t *)(base + MARK_OFF);
    mk[0] = 0; mk[4] = 0;

    /* method words, written straight into the slot (uncached), pointed at by one GPFIFO entry */
    uint32_t *pb = (uint32_t *)(base + r->pb_off);
    uint32_t n = 0;
    memcpy(pb, SETUP_WORDS, sizeof SETUP_WORDS); n += 18;
    pb[n++] = mthd(1, MTHD_WAIT_FOR_IDLE, 1); pb[n++] = 0;
    pb[n++] = mthd(1, OMEGA_BW_MTHD_INVALIDATE_SHADER_CACHES, 1); pb[n++] = OMEGA_BW_INVALIDATE_SHADER_CACHES_ALL;
    int w = emit_nodes(g, qmd_base, sem_base, pb + n, (uint32_t)(r->pb_cap_words - n - TAIL_WORDS));
    if (w < 0) { seterr(r, "method words exceed the slot"); return OMEGA_ROLL_ERR; }
    n += (uint32_t)w;
    const uint64_t m_va = va + MARK_OFF;
    pb[n++] = mthd(0, 0x005c, 5); pb[n++] = (uint32_t)m_va; pb[n++] = (uint32_t)(m_va >> 32);
    pb[n++] = OMEGA_BW_MARKER_COMPLETION_PAYLOAD; pb[n++] = 0; pb[n++] = 0x1u | (1u << 20);      /* RELEASE | WFI */
    pb[n++] = mthd(0, 0x0028, 4); pb[n++] = 0; pb[n++] = 0; pb[n++] = 0; pb[n++] = (0x10u << 27); /* L2_FLUSH_DIRTY */
    pb[n++] = mthd(0, 0x005c, 5); pb[n++] = (uint32_t)(m_va + 0x10); pb[n++] = (uint32_t)((m_va + 0x10) >> 32);
    pb[n++] = OMEGA_BW_MARKER2_PAYLOAD; pb[n++] = 0; pb[n++] = 0x1u | (1u << 20);
    sl->words = n;
    __asm__ volatile("dsb sy" ::: "memory");
    if (nvrm_enqueue(&vo->m->rm, &sl->mem, (uint32_t)r->pb_off, n) != 0) { seterr(r, "enqueue: %s", vo->m->rm.err); return OMEGA_ROLL_ERR; }
    sl->put_after = vo->m->rm.put;
    sl->submit_ns = now_ns();
    nvrm_ring(&vo->m->rm);
    vo->count++;
    return OMEGA_ROLL_OK;
}

static int wait_eq(volatile uint32_t *w, uint32_t want, uint64_t timeout_ms) {
    if (*w == want) return 0;
    const uint64_t end = now_ns() + timeout_ms * 1000000ull;
    uint32_t spins = 0;
    for (;;) {
        if (*w == want) return 0;
        if (++spins % 256u == 0) {
            if (now_ns() > end) return *w == want ? 0 : -1;
            if (spins > 4096u) sched_yield();
        }
        __asm__ volatile("yield" ::: "memory");
    }
}

int omega_gpu_roll_retire(OmegaRoll *r, uint32_t voice, OmegaRollResult *res) {
    if (!r || voice >= r->cfg.voices) return OMEGA_ROLL_ERR;
    if (omega_gpu_roll_is_latched()) return OMEGA_ROLL_LATCHED;
    Voice *vo = &r->v[voice];
    if (vo->count == 0) return OMEGA_ROLL_EMPTY;
    Slot *sl = &vo->slots[vo->head];
    uint8_t *base = sl->mem.cpu;
    volatile uint32_t *mk = (volatile uint32_t *)(base + MARK_OFF);
    OmegaRollResult rr; memset(&rr, 0, sizeof rr);
    rr.tag = sl->tag; rr.voice = voice; rr.nodes = sl->n; rr.first_missing = -1; rr.first_unwritten = -1; rr.words = sl->words;
    rr.submit_ns = sl->submit_ns;
    int a = wait_eq(&mk[0], OMEGA_BW_MARKER_COMPLETION_PAYLOAD, r->cfg.timeout_ms);
    int b = a == 0 ? wait_eq(&mk[4], OMEGA_BW_MARKER2_PAYLOAD, r->cfg.timeout_ms) : -1;
    rr.marker = mk[0]; rr.marker2 = mk[4];
    if (a != 0 || b != 0) {
        __atomic_store_n(&g_latched, 1, __ATOMIC_RELEASE); /* uncertain completion: keep everything, run nothing more */
        seterr(r, "voice %u tag %llu: %s wait timed out (latched; marker 0x%08x marker2 0x%08x)", voice, (unsigned long long)sl->tag,
               a ? "first marker" : "second marker", rr.marker, rr.marker2);
        if (res) *res = rr;
        return OMEGA_ROLL_LATCHED;
    }
    rr.done_ns = now_ns();
    __asm__ volatile("dsb sy" ::: "memory");
    for (uint32_t i = 0; i < sl->n; i++) {
        const uint32_t s = *(volatile uint32_t *)(base + SEM_OFF + (size_t)i * 16u);
        if (s == OMEGA_BW_SEMAPHORE_INTERMEDIATE_DONE) rr.sems_done++;
        else if (rr.first_missing < 0) rr.first_missing = (int32_t)i;
        const OmegaRollNode *nd = &sl->nodes[i];
        for (uint32_t bb = 0; bb < nd->nbufs; bb++) {
            const OmegaRollBuf *bf = &nd->buf[bb];
            const uint8_t *src = base + sl->buf_off[i][bb];
            if (bf->poisoned) {
                const uint32_t *wv = (const uint32_t *)src; uint64_t u = 0;
                for (size_t x = 0; x < bf->bytes / 4u; x++) u += wv[x] == bf->poison;
                if (u && rr.first_unwritten < 0) rr.first_unwritten = (int32_t)i;
                rr.unwritten_words += u;
            }
            if (bf->out) memcpy(bf->out, src, bf->bytes);
        }
    }
    nvrm_retire(&vo->m->rm, sl->put_after);
    vo->head = (vo->head + 1u) % r->cfg.depth;
    vo->count--;
    rr.rc = (rr.first_missing >= 0 || rr.unwritten_words) ? OMEGA_ROLL_CHECK : OMEGA_ROLL_OK;
    if (res) *res = rr;
    return rr.rc;
}

int omega_gpu_roll_close(OmegaRoll *r) {
    if (!r) return OMEGA_ROLL_OK;
    if (omega_gpu_roll_is_latched()) { seterr(r, "latched: nothing freed, nothing closed"); return OMEGA_ROLL_ERR; }
    int rc = OMEGA_ROLL_OK;
    for (uint32_t v = 0; v < r->cfg.voices; v++) {
        while (r->v[v].count) {
            int x = omega_gpu_roll_retire(r, v, NULL);
            if (x == OMEGA_ROLL_LATCHED) return OMEGA_ROLL_ERR;
        }
    }
    for (uint32_t v = 0; v < r->cfg.voices; v++) {
        if (r->v[v].m && m16_native_close(r->v[v].m) != 0) rc = OMEGA_ROLL_ERR;
        free(r->v[v].m);
        free(r->v[v].slots);
    }
    free(r);
    return rc;
}
