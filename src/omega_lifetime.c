/*
 * omega_lifetime.c: live intervals, the shared plan, the two safety rules and the storage map (see the header).
 */
#include "omega_lifetime.h"
#include "omega_gpu_fmt.h"
#include <string.h>

static uint64_t up(uint64_t v, uint64_t a) { return (v + a - 1u) / a * a; }
static int pow2(uint32_t a) { return a != 0 && (a & (a - 1u)) == 0; }
static int has(const OmegaLtBuf *b) { return b->first != OMEGA_LT_NONE; }
static int live_overlap(const OmegaLtBuf *a, const OmegaLtBuf *b) {
    return has(a) && has(b) && a->first <= b->last && b->first <= a->last;
}
static int addr_overlap(uint64_t ao, uint64_t an, uint64_t bo, uint64_t bn) { return ao < bo + bn && bo < ao + an; }
/* holds a value from before the list: LIVE_IN, PINNED, or kept after the list (LIVE_OUT) but never written in it */
static int from_before(const OmegaLt *t, uint32_t i) {
    if (t->buf[i].flags & (OMEGA_LT_LIVE_IN | OMEGA_LT_PINNED)) return 1;
    if (!(t->buf[i].flags & OMEGA_LT_LIVE_OUT)) return 0;
    for (uint32_t k = 0; k < t->n_acc; k++) if (t->acc[k].buf == i && t->acc[k].write) return 0;
    return 1;
}

void omega_lt_init(OmegaLt *t, OmegaLtAccess *acc, uint32_t acc_cap, const char **op_name, uint32_t op_cap) {
    if (!t) return;
    memset(t, 0, sizeof *t);
    t->acc = acc; t->acc_cap = acc ? acc_cap : 0;
    t->op_name = op_name; t->op_cap = op_name ? op_cap : 0xffffffffu;
}

int omega_lt_buf(OmegaLt *t, const char *name, uint64_t bytes, uint32_t align, uint32_t flags) {
    if (!t || t->overflow || t->n_buf >= OMEGA_LT_MAX_BUFS || bytes == 0 || !pow2(align)) return -1;
    if (flags & OMEGA_LT_TAIL) {
        if (t->n_buf == 0) return -1;
        const OmegaLtBuf *h = &t->buf[t->n_buf - 1];
        if (h->bytes % align != 0) return -1; /* the tail starts exactly where its head ends */
    }
    OmegaLtBuf *b = &t->buf[t->n_buf];
    memset(b, 0, sizeof *b);
    b->name = name ? name : "?"; b->bytes = bytes; b->align = align; b->flags = flags;
    b->first = b->last = OMEGA_LT_NONE;
    return (int)t->n_buf++;
}

int omega_lt_op(OmegaLt *t, const char *name) {
    if (!t || t->overflow) return -1;
    if (t->n_op >= t->op_cap) { t->overflow = 1; return -1; }
    if (t->op_name) t->op_name[t->n_op] = name ? name : "?";
    t->n_op++;
    return 0;
}

int omega_lt_use(OmegaLt *t, int id, int write) {
    if (!t || t->overflow || t->n_op == 0 || id < 0 || (uint32_t)id >= t->n_buf) return -1;
    if (t->n_acc >= t->acc_cap) { t->overflow = 1; return -1; }
    OmegaLtAccess *a = &t->acc[t->n_acc++];
    a->step = t->n_op - 1u; a->buf = (uint16_t)id; a->write = write ? 1u : 0u; a->pad = 0;
    return 0;
}

int omega_lt_live(OmegaLt *t) {
    if (!t || t->overflow) return -1;
    for (uint32_t i = 0; i < t->n_buf; i++) t->buf[i].first = t->buf[i].last = OMEGA_LT_NONE;
    for (uint32_t k = 0; k < t->n_acc; k++) {
        OmegaLtBuf *b = &t->buf[t->acc[k].buf];
        const uint32_t s = t->acc[k].step;
        if (b->first == OMEGA_LT_NONE || s < b->first) b->first = s;
        if (b->last == OMEGA_LT_NONE || s > b->last) b->last = s;
    }
    for (uint32_t i = 0; i < t->n_buf; i++) {
        OmegaLtBuf *b = &t->buf[i];
        if (from_before(t, i)) { b->first = 0; if (b->last == OMEGA_LT_NONE) b->last = 0; }
        if (b->flags & (OMEGA_LT_LIVE_OUT | OMEGA_LT_PINNED)) { b->last = t->n_op; }
    }
    return 0;
}

uint64_t omega_lt_peak_live(const OmegaLt *t) {
    if (!t) return 0;
    uint64_t peak = 0;
    for (uint32_t s = 0; s <= t->n_op; s++) {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < t->n_buf; i++) if (has(&t->buf[i]) && t->buf[i].first <= s && s <= t->buf[i].last) sum += t->buf[i].bytes;
        if (sum > peak) peak = sum;
    }
    return peak;
}

uint64_t omega_lt_sum_bytes(const OmegaLt *t) {
    uint64_t s = 0;
    if (t) for (uint32_t i = 0; i < t->n_buf; i++) s += t->buf[i].bytes;
    return s;
}

int omega_lt_plan_flat(const OmegaLt *t, uint64_t *off, uint64_t *arena) {
    if (!t || !off || !arena || t->overflow) return -1;
    uint64_t o = 0, amax = 1;
    for (uint32_t i = 0; i < t->n_buf; i++) {
        const OmegaLtBuf *b = &t->buf[i];
        off[i] = (b->flags & OMEGA_LT_TAIL) ? off[i - 1] + t->buf[i - 1].bytes : up(o, b->align);
        o = off[i] + b->bytes;
        if (b->align > amax) amax = b->align;
    }
    *arena = up(o, amax);
    return 0;
}

/* a group: one head and the TAIL buffers declared right after it */
static uint32_t group_end(const OmegaLt *t, uint32_t h) {
    uint32_t e = h + 1u;
    while (e < t->n_buf && (t->buf[e].flags & OMEGA_LT_TAIL)) e++;
    return e;
}

int omega_lt_plan(const OmegaLt *t, uint64_t *off, uint64_t *arena) {
    if (!t || !off || !arena || t->overflow) return -1;
    uint32_t heads[OMEGA_LT_MAX_BUFS], nh = 0;
    uint64_t gbytes[OMEGA_LT_MAX_BUFS], rel[OMEGA_LT_MAX_BUFS];
    uint8_t placed[OMEGA_LT_MAX_BUFS];
    memset(placed, 0, sizeof placed);
    for (uint32_t i = 0; i < t->n_buf; i = group_end(t, i)) {
        uint64_t r = 0;
        for (uint32_t j = i; j < group_end(t, i); j++) { rel[j] = r; r += t->buf[j].bytes; }
        heads[nh] = i; gbytes[nh] = r; nh++;
    }
    /* largest group first; ties in declaration order (insertion sort, n <= 48) */
    for (uint32_t a = 1; a < nh; a++)
        for (uint32_t b = a; b > 0 && gbytes[b] > gbytes[b - 1]; b--) {
            uint32_t th = heads[b]; heads[b] = heads[b - 1]; heads[b - 1] = th;
            uint64_t tg = gbytes[b]; gbytes[b] = gbytes[b - 1]; gbytes[b - 1] = tg;
        }
    uint64_t end = 0;
    for (uint32_t gi = 0; gi < nh; gi++) {
        const uint32_t h = heads[gi], e = group_end(t, h);
        const uint64_t al = t->buf[h].align;
        uint64_t best = UINT64_MAX;
        /* candidates: 0, and every placed buffer's end minus each part's offset in the group, aligned up */
        for (uint32_t q = 0; q <= t->n_buf; q++) {
            if (q < t->n_buf && !placed[q]) continue;
            for (uint32_t p = h; p < e; p++) {
                uint64_t c;
                if (q == t->n_buf) { if (p != h) continue; c = 0; }
                else { const uint64_t qe = off[q] + t->buf[q].bytes; if (qe < rel[p]) continue; c = up(qe - rel[p], al); }
                if (c >= best) continue;
                int ok = 1;
                for (uint32_t pp = h; pp < e && ok; pp++)
                    for (uint32_t r = 0; r < t->n_buf && ok; r++)
                        if (placed[r] && live_overlap(&t->buf[pp], &t->buf[r]) &&
                            addr_overlap(c + rel[pp], t->buf[pp].bytes, off[r], t->buf[r].bytes)) ok = 0;
                if (ok) best = c;
            }
        }
        for (uint32_t p = h; p < e; p++) {
            off[p] = best + rel[p]; placed[p] = 1;
            if (off[p] + t->buf[p].bytes > end) end = off[p] + t->buf[p].bytes;
        }
    }
    uint64_t amax = 1;
    for (uint32_t i = 0; i < t->n_buf; i++) if (t->buf[i].align > amax) amax = t->buf[i].align;
    *arena = up(end, amax);
    /* greedy can lose to the plain layout in alignment corner cases: never plan worse than no sharing */
    uint64_t flat[OMEGA_LT_MAX_BUFS], farena = 0;
    if (omega_lt_plan_flat(t, flat, &farena) == 0 && farena < *arena) { memcpy(off, flat, (size_t)t->n_buf * sizeof off[0]); *arena = farena; }
    return 0;
}

/* rule 2: replay the trace on the plan. Returns 0 or -1 with why; valid (if non-NULL) gets 1 per buffer still intact. */
static int replay(const OmegaLt *t, const uint64_t *off, char *why, size_t cap, uint8_t *valid) {
    uint8_t written[OMEGA_LT_MAX_BUFS];
    uint32_t clob[OMEGA_LT_MAX_BUFS], clob_step[OMEGA_LT_MAX_BUFS];
    for (uint32_t i = 0; i < t->n_buf; i++) {
        written[i] = (uint8_t)from_before(t, i);
        clob[i] = OMEGA_LT_NONE; clob_step[i] = 0;
    }
    int red = 0;
    for (uint32_t k = 0; k < t->n_acc && !red;) {
        const uint32_t s = t->acc[k].step;
        uint32_t e = k;
        while (e < t->n_acc && t->acc[e].step == s) e++;
        for (uint32_t j = k; j < e && !red; j++) {   /* an op reads its inputs before its outputs land */
            if (t->acc[j].write) continue;
            const uint32_t b = t->acc[j].buf;
            if (!written[b]) {
                if (why) omega_gpu_fmt(why, cap, "replay: %s read at step %u before any write", t->buf[b].name, s);
                red = 1;
            } else if (clob[b] != OMEGA_LT_NONE) {
                if (why) omega_gpu_fmt(why, cap, "replay: %s read at step %u after %s overwrote its bytes at step %u", t->buf[b].name, s,
                                       t->buf[clob[b]].name, clob_step[b]);
                red = 1;
            }
        }
        for (uint32_t j = k; j < e && !red; j++) {
            if (!t->acc[j].write) continue;
            const uint32_t b = t->acc[j].buf;
            written[b] = 1; clob[b] = OMEGA_LT_NONE;
            for (uint32_t c = 0; c < t->n_buf; c++)
                if (c != b && addr_overlap(off[b], t->buf[b].bytes, off[c], t->buf[c].bytes)) { clob[c] = b; clob_step[c] = s; }
        }
        k = e;
    }
    for (uint32_t i = 0; i < t->n_buf && !red; i++)
        if ((t->buf[i].flags & (OMEGA_LT_LIVE_OUT | OMEGA_LT_PINNED)) && clob[i] != OMEGA_LT_NONE) {
            if (why) omega_gpu_fmt(why, cap, "replay: %s (kept after the list) overwritten by %s at step %u", t->buf[i].name,
                                   t->buf[clob[i]].name, clob_step[i]);
            red = 1;
        }
    if (valid) for (uint32_t i = 0; i < t->n_buf; i++) valid[i] = written[i] && clob[i] == OMEGA_LT_NONE;
    return red ? -1 : 0;
}

int omega_lt_check(const OmegaLt *t, const uint64_t *off, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    if (!t || !off || t->overflow) { if (why) omega_gpu_fmt(why, why_cap, "check: no trace or plan"); return -1; }
    for (uint32_t i = 0; i < t->n_buf; i++) {
        const OmegaLtBuf *a = &t->buf[i];
        if (off[i] % a->align != 0 && !(a->flags & OMEGA_LT_TAIL)) {
            if (why) omega_gpu_fmt(why, why_cap, "check: %s at %llu breaks its %u-byte alignment", a->name, (unsigned long long)off[i], a->align);
            return -1;
        }
        if ((a->flags & OMEGA_LT_TAIL) && off[i] != off[i - 1] + t->buf[i - 1].bytes) {
            if (why) omega_gpu_fmt(why, why_cap, "check: tail %s not directly after %s", a->name, t->buf[i - 1].name);
            return -1;
        }
        for (uint32_t j = i + 1; j < t->n_buf; j++) {
            const OmegaLtBuf *b = &t->buf[j];
            if (live_overlap(a, b) && addr_overlap(off[i], a->bytes, off[j], b->bytes)) {
                if (why) omega_gpu_fmt(why, why_cap, "check: %s (live %u..%u) and %s (live %u..%u) share bytes", a->name, a->first, a->last,
                                       b->name, b->first, b->last);
                return -1;
            }
        }
    }
    return replay(t, off, why, why_cap, NULL);
}

int omega_lt_valid_at_end(const OmegaLt *t, const uint64_t *off, int id) {
    if (!t || !off || id < 0 || (uint32_t)id >= t->n_buf) return 0;
    uint8_t v[OMEGA_LT_MAX_BUFS];
    (void)replay(t, off, NULL, 0, v);
    return v[id];
}

int omega_lt_map(const OmegaLt *t, const uint64_t *off, char *out, size_t cap) {
    if (!t || !off || !out || cap == 0) return -1;
    uint32_t ord[OMEGA_LT_MAX_BUFS], slot[OMEGA_LT_MAX_BUFS];
    for (uint32_t i = 0; i < t->n_buf; i++) ord[i] = i;
    for (uint32_t a = 1; a < t->n_buf; a++)
        for (uint32_t b = a; b > 0 && off[ord[b]] < off[ord[b - 1]]; b--) { uint32_t x = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = x; }
    /* slots: runs of buffers (by offset) whose byte ranges chain together */
    uint32_t ns = 0; uint64_t run_end = 0;
    for (uint32_t k = 0; k < t->n_buf; k++) {
        const uint32_t i = ord[k];
        if (k == 0 || off[i] >= run_end) { ns++; run_end = 0; }
        slot[i] = ns - 1u;
        if (off[i] + t->buf[i].bytes > run_end) run_end = off[i] + t->buf[i].bytes;
    }
    size_t n = 0;
#define PUT(...) do { if (n < cap) n += (size_t)omega_gpu_fmt(out + n, cap - n, __VA_ARGS__); } while (0)
    PUT("storage map: %u buffers, %u slots, %u ops\n", t->n_buf, ns, t->n_op);
    for (uint32_t s = 0; s < ns; s++) {
        uint64_t lo = UINT64_MAX, hi = 0;
        for (uint32_t i = 0; i < t->n_buf; i++) if (slot[i] == s) {
            if (off[i] < lo) lo = off[i];
            if (off[i] + t->buf[i].bytes > hi) hi = off[i] + t->buf[i].bytes;
        }
        PUT("slot %u [%llu, %llu) %llu B\n", s, (unsigned long long)lo, (unsigned long long)hi, (unsigned long long)(hi - lo));
        for (uint32_t k = 0; k < t->n_buf; k++) {
            const uint32_t i = ord[k];
            if (slot[i] != s) continue;
            const OmegaLtBuf *b = &t->buf[i];
            if (has(b)) PUT("  %s @%llu %llu B live %u..%u%s%s%s%s writers:", b->name, (unsigned long long)off[i], (unsigned long long)b->bytes,
                            b->first, b->last, (b->flags & OMEGA_LT_LIVE_IN) ? " in" : "", (b->flags & OMEGA_LT_LIVE_OUT) ? " out" : "",
                            (b->flags & OMEGA_LT_PINNED) ? " pinned" : "", (b->flags & OMEGA_LT_TAIL) ? " tail" : "");
            else PUT("  %s @%llu %llu B never touched writers:", b->name, (unsigned long long)off[i], (unsigned long long)b->bytes);
            /* the danger list: every op kind that writes this buffer, with its count (names compared by pointer) */
            const char *seen[64]; uint32_t cnt[64], nseen = 0;
            for (uint32_t a = 0; a < t->n_acc; a++) {
                if (!t->acc[a].write || t->acc[a].buf != i) continue;
                const char *w = t->op_name ? t->op_name[t->acc[a].step] : "op";
                uint32_t q = 0;
                while (q < nseen && seen[q] != w) q++;
                if (q == nseen && nseen < 64) { seen[nseen] = w; cnt[nseen] = 0; nseen++; }
                if (q < nseen) cnt[q]++;
            }
            if (nseen == 0) PUT(" %s", (b->flags & (OMEGA_LT_LIVE_IN | OMEGA_LT_PINNED)) ? "host" : "none");
            for (uint32_t q = 0; q < nseen; q++) PUT(" %s x%u", seen[q], cnt[q]);
            PUT("\n");
        }
    }
#undef PUT
    return (int)(n < cap ? n : cap - 1);
}
