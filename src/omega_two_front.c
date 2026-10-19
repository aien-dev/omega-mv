/*
 * omega_two_front.c: the bookkeeping of the two-front arena (omega#571); see omega_two_front.h. Host only, no device,
 * no libm. Items live in a fixed table; a slot is free when kind == 0.
 */
#include "omega_two_front.h"
#include "omega_blackwell_codegen_attn256.h"
#include "omega_gpu_fmt.h"
#include <string.h>

static int g_mutant;
void omega_tf_test_set_mutant(int mutant) { g_mutant = mutant; }

static uint64_t align_up(uint64_t x) { return (x + OMEGA_TF_ALIGN - 1u) / OMEGA_TF_ALIGN * OMEGA_TF_ALIGN; }
static uint64_t kv_bytes(const OmegaTf *tf, uint32_t positions) {
    return (uint64_t)((positions + OMEGA_TF_BLOCK_POSITIONS - 1u) / OMEGA_TF_BLOCK_POSITIONS) * tf->block_bytes;
}
static void note_gap(OmegaTf *tf) { uint64_t g = omega_tf_gap(tf); if (g < tf->min_gap) tf->min_gap = g; }

int omega_tf_init(OmegaTf *tf, uint64_t bytes, uint64_t block_bytes) {
    if (!tf) return OMEGA_TF_BAD_ARGS;
    memset(tf, 0, sizeof *tf);
    bytes = bytes / OMEGA_TF_ALIGN * OMEGA_TF_ALIGN;
    if (bytes == 0 || block_bytes == 0 || block_bytes % OMEGA_TF_ALIGN) return OMEGA_TF_BAD_ARGS;
    tf->bytes = bytes; tf->block_bytes = block_bytes; tf->low = 0; tf->high = bytes; tf->min_gap = bytes;
    return OMEGA_TF_OK;
}
uint64_t omega_tf_alloc_bytes(const OmegaTf *tf) { return tf ? align_up(tf->bytes + OMEGA_DATTN256_PLANE_SLACK) : 0; }
uint64_t omega_tf_gap(const OmegaTf *tf) { return tf && tf->high >= tf->low ? tf->high - tf->low : 0; }
const OmegaTfItem *omega_tf_item(const OmegaTf *tf, uint32_t id) {
    return tf && id < OMEGA_TF_MAX_ITEMS && tf->item[id].kind ? &tf->item[id] : NULL;
}

static int new_slot(OmegaTf *tf, uint32_t *id) {
    for (uint32_t i = 0; i < OMEGA_TF_MAX_ITEMS; i++) if (tf->item[i].kind == 0) { if (i >= tf->n) tf->n = i + 1; *id = i; return OMEGA_TF_OK; }
    omega_gpu_fmt(tf->err, sizeof tf->err, "item table full (%u items)", OMEGA_TF_MAX_ITEMS);
    return OMEGA_TF_FULL_TABLE;
}
static void set_name(OmegaTfItem *it, const char *name) {
    size_t k = 0;
    if (name) for (; name[k] && k + 1 < sizeof it->name; k++) it->name[k] = name[k];
    it->name[k] = 0;
}
/* the refusal every crossing gets: both fronts and the shortfall */
static int overflow(OmegaTf *tf, const char *what, uint64_t need) {
    tf->refusals++;
    omega_gpu_fmt(tf->err, sizeof tf->err, "two-front overflow: %s needs %llu bytes, gap %llu (low front at %llu, high front at %llu): short by %llu",
                  what, (unsigned long long)need, (unsigned long long)omega_tf_gap(tf), (unsigned long long)tf->low,
                  (unsigned long long)tf->high, (unsigned long long)(need - omega_tf_gap(tf)));
    return OMEGA_TF_OVERFLOW;
}
static int fits(const OmegaTf *tf, uint64_t need) { return g_mutant == 1 || need <= omega_tf_gap(tf); }

/* the KV item whose end is the low front and that is still live, or -1 */
static int top_kv(const OmegaTf *tf) {
    int best = -1;
    for (uint32_t i = 0; i < tf->n; i++) {
        const OmegaTfItem *it = &tf->item[i];
        if (it->kind != OMEGA_TF_KV || !it->live || it->off + it->bytes != tf->low) continue;
        if (best < 0 || it->off > tf->item[best].off || (it->off == tf->item[best].off && i > (uint32_t)best)) best = (int)i;
    }
    return best;
}
/* drop released items at the fronts' edges (a front retreats through its holes) */
static void retreat(OmegaTf *tf) {
    for (int moved = 1; moved;) {
        moved = 0;
        for (uint32_t i = 0; i < tf->n; i++) {
            OmegaTfItem *it = &tf->item[i];
            if (!it->kind || it->live) continue;
            if (it->kind == OMEGA_TF_KV && it->off + it->bytes == tf->low) { tf->low = it->off; memset(it, 0, sizeof *it); moved = 1; }
            else if (it->kind != OMEGA_TF_KV && it->off == tf->high) { tf->high = it->off + it->bytes; memset(it, 0, sizeof *it); moved = 1; }
        }
    }
    /* a zero-byte released KV extent below live ones never touches an edge: free it */
    for (uint32_t i = 0; i < tf->n; i++) if (tf->item[i].kind == OMEGA_TF_KV && !tf->item[i].live && tf->item[i].bytes == 0) memset(&tf->item[i], 0, sizeof tf->item[i]);
}

int omega_tf_kv_open(OmegaTf *tf, uint32_t owner, uint32_t positions, const char *name, uint32_t *id) {
    if (!tf || !id) return OMEGA_TF_BAD_ARGS;
    const uint64_t need = kv_bytes(tf, positions);
    if (!fits(tf, need)) return overflow(tf, "a new KV extent", need);
    uint32_t s; int rc = new_slot(tf, &s);
    if (rc != OMEGA_TF_OK) return rc;
    OmegaTfItem *it = &tf->item[s];
    it->off = tf->low; it->bytes = need; it->kind = OMEGA_TF_KV; it->owner = owner; it->live = 1;
    it->positions = (uint32_t)(need / tf->block_bytes) * OMEGA_TF_BLOCK_POSITIONS;
    set_name(it, name);
    tf->low += need;
    note_gap(tf);
    *id = s;
    return OMEGA_TF_OK;
}
int omega_tf_kv_grow(OmegaTf *tf, uint32_t id, uint32_t positions) {
    if (!tf || id >= OMEGA_TF_MAX_ITEMS || tf->item[id].kind != OMEGA_TF_KV || !tf->item[id].live) return OMEGA_TF_BAD_ARGS;
    OmegaTfItem *it = &tf->item[id];
    if (positions <= it->positions) return OMEGA_TF_OK;
    if (top_kv(tf) != (int)id) {
        omega_gpu_fmt(tf->err, sizeof tf->err, "KV extent %u (%s) is sealed under a newer extent: move it to the top first", id, it->name);
        return OMEGA_TF_NOT_TOP;
    }
    const uint64_t more = kv_bytes(tf, positions) - it->bytes;
    if (!fits(tf, more)) return overflow(tf, "growing the top KV extent", more);
    it->bytes += more; it->positions = (uint32_t)(it->bytes / tf->block_bytes) * OMEGA_TF_BLOCK_POSITIONS;
    tf->low += more;
    note_gap(tf);
    return OMEGA_TF_OK;
}
int omega_tf_kv_shrink(OmegaTf *tf, uint32_t id, uint32_t positions) {
    if (!tf || id >= OMEGA_TF_MAX_ITEMS || tf->item[id].kind != OMEGA_TF_KV || !tf->item[id].live) return OMEGA_TF_BAD_ARGS;
    OmegaTfItem *it = &tf->item[id];
    if (positions >= it->positions) return OMEGA_TF_OK;
    if (top_kv(tf) == (int)id) {  /* return whole blocks to the gap */
        const uint64_t keep = kv_bytes(tf, positions);
        tf->low -= it->bytes - keep; it->bytes = keep;
        it->positions = (uint32_t)(keep / tf->block_bytes) * OMEGA_TF_BLOCK_POSITIONS;
    } /* a sealed extent keeps its bytes (they are reclaimed when it closes) */
    return OMEGA_TF_OK;
}
int omega_tf_kv_move_top(OmegaTf *tf, uint32_t id, uint64_t *old_off, uint32_t *new_id) {
    if (!tf || !old_off || !new_id || id >= OMEGA_TF_MAX_ITEMS || tf->item[id].kind != OMEGA_TF_KV || !tf->item[id].live) return OMEGA_TF_BAD_ARGS;
    const OmegaTfItem was = tf->item[id];
    uint32_t s;
    int rc = omega_tf_kv_open(tf, was.owner, was.positions, was.name, &s);
    if (rc != OMEGA_TF_OK) return rc;
    tf->item[id].live = 0;  /* a hole: below the new extent, so its bytes stay until the low front retreats past it */
    *old_off = was.off; *new_id = s;
    return OMEGA_TF_OK;
}
int omega_tf_rec_push(OmegaTf *tf, uint32_t kind, uint32_t owner, uint64_t bytes, uint32_t position, const char *name, uint32_t *id) {
    if (!tf || !id || bytes == 0 || (kind != OMEGA_TF_GDN_LIVE && kind != OMEGA_TF_GDN_SNAPSHOT && kind != OMEGA_TF_SCRATCH)) return OMEGA_TF_BAD_ARGS;
    const uint64_t need = align_up(bytes);
    if (!fits(tf, need)) return overflow(tf, kind == OMEGA_TF_GDN_SNAPSHOT ? "a DeltaNet snapshot" : kind == OMEGA_TF_GDN_LIVE ? "a live DeltaNet set" : "a scratch record", need);
    uint32_t s; int rc = new_slot(tf, &s);
    if (rc != OMEGA_TF_OK) return rc;
    OmegaTfItem *it = &tf->item[s];
    tf->high -= need;
    it->off = tf->high; it->bytes = need; it->kind = kind; it->owner = owner; it->positions = position; it->live = 1;
    set_name(it, name);
    note_gap(tf);
    *id = s;
    return OMEGA_TF_OK;
}
int omega_tf_release(OmegaTf *tf, uint32_t id) {
    if (!tf || id >= OMEGA_TF_MAX_ITEMS || !tf->item[id].kind || !tf->item[id].live) return OMEGA_TF_BAD_ARGS;
    tf->item[id].live = 0;
    retreat(tf);
    return OMEGA_TF_OK;
}

int omega_tf_check(OmegaTf *tf) {
    if (!tf) return -1;
    if (!(tf->low <= tf->high && tf->high <= tf->bytes)) {
        omega_gpu_fmt(tf->err, sizeof tf->err, "fronts crossed: low %llu high %llu bytes %llu", (unsigned long long)tf->low, (unsigned long long)tf->high, (unsigned long long)tf->bytes);
        return -1;
    }
    for (uint32_t i = 0; i < tf->n; i++) {
        const OmegaTfItem *a = &tf->item[i];
        if (!a->kind) continue;
        if (a->off % OMEGA_TF_ALIGN || a->bytes % OMEGA_TF_ALIGN) { omega_gpu_fmt(tf->err, sizeof tf->err, "item %u (%s) not page aligned", i, a->name); return -1; }
        if (a->kind == OMEGA_TF_KV ? a->off + a->bytes > tf->low : (a->off < tf->high || a->off + a->bytes > tf->bytes)) {
            omega_gpu_fmt(tf->err, sizeof tf->err, "item %u (%s) at %llu+%llu outside its front (low %llu high %llu)", i, a->name,
                          (unsigned long long)a->off, (unsigned long long)a->bytes, (unsigned long long)tf->low, (unsigned long long)tf->high);
            return -1;
        }
        if (a->kind == OMEGA_TF_KV && a->live && a->bytes < kv_bytes(tf, a->positions)) {
            omega_gpu_fmt(tf->err, sizeof tf->err, "KV extent %u (%s) holds %llu bytes for %u positions", i, a->name, (unsigned long long)a->bytes, a->positions);
            return -1;
        }
        for (uint32_t j = i + 1; j < tf->n; j++) {
            const OmegaTfItem *b = &tf->item[j];
            if (!b->kind || a->bytes == 0 || b->bytes == 0) continue;
            if (a->off < b->off + b->bytes && b->off < a->off + a->bytes) {
                omega_gpu_fmt(tf->err, sizeof tf->err, "items %u (%s) and %u (%s) share bytes", i, a->name, j, b->name);
                return -1;
            }
        }
    }
    return 0;
}

static const char *kind_name(uint32_t k) {
    return k == OMEGA_TF_KV ? "kv" : k == OMEGA_TF_GDN_LIVE ? "gdn-live" : k == OMEGA_TF_GDN_SNAPSHOT ? "gdn-snapshot" : k == OMEGA_TF_SCRATCH ? "scratch" : "?";
}
static const char *lifetime_name(uint32_t k) {
    return k == OMEGA_TF_GDN_SNAPSHOT ? "turn" : k == OMEGA_TF_SCRATCH ? "engine" : "conversation";
}
size_t omega_tf_map(const OmegaTf *tf, char *buf, size_t cap) {
    if (!tf || !buf || cap == 0) return 0;
    size_t len = 0;
    buf[0] = 0;
#define PUT(...) do { if (len + 1 < cap) { omega_gpu_fmt(buf + len, cap - len, __VA_ARGS__); while (len + 1 < cap && buf[len]) len++; } } while (0)
    PUT("two-front arena: %llu bytes (+ %llu read slack), KV block %llu bytes per %u positions\n", (unsigned long long)tf->bytes,
        (unsigned long long)(omega_tf_alloc_bytes(tf) - tf->bytes), (unsigned long long)tf->block_bytes, OMEGA_TF_BLOCK_POSITIONS);
    PUT("%s	%s	%s	%s	%s	%s	%s\n", "offset", "bytes", "kind", "lifetime", "owner", "position", "name");
    uint32_t ord[OMEGA_TF_MAX_ITEMS], k = 0;
    for (uint32_t i = 0; i < tf->n; i++) if (tf->item[i].kind) ord[k++] = i;
    for (uint32_t i = 1; i < k; i++) {  /* insertion sort by (offset, table index): address order */
        uint32_t v = ord[i], j = i;
        while (j > 0 && (tf->item[ord[j - 1]].off > tf->item[v].off || (tf->item[ord[j - 1]].off == tf->item[v].off && ord[j - 1] > v))) { ord[j] = ord[j - 1]; j--; }
        ord[j] = v;
    }
    uint64_t used[2] = { 0, 0 };
    int gap_done = 0;
    for (uint32_t x = 0; x <= k; x++) {
        const OmegaTfItem *it = x < k ? &tf->item[ord[x]] : NULL;
        if (!gap_done && (!it || it->kind != OMEGA_TF_KV)) {  /* KV items first (low front), then the gap, then the records */
            PUT("%llu	%llu	%s	%s	%s	%s	%s\n", (unsigned long long)tf->low, (unsigned long long)omega_tf_gap(tf), "GAP", "-", "-", "-", "free: the only slack");
            gap_done = 1;
        }
        if (!it) break;
        PUT("%llu	%llu	%s	%s	%u	%u	%s%s\n", (unsigned long long)it->off, (unsigned long long)it->bytes, kind_name(it->kind),
            lifetime_name(it->kind), it->owner, it->positions, it->name, it->live ? "" : " (hole)");
        if (it->live) used[it->kind == OMEGA_TF_KV ? 0 : 1] += it->bytes;
    }
    PUT("low front %llu bytes (live KV %llu), high front %llu bytes (live records %llu), gap %llu, smallest gap so far %llu, refusals %u\n",
        (unsigned long long)tf->low, (unsigned long long)used[0], (unsigned long long)(tf->bytes - tf->high), (unsigned long long)used[1],
        (unsigned long long)omega_tf_gap(tf), (unsigned long long)tf->min_gap, tf->refusals);
#undef PUT
    return len;
}

const char *omega_tf_rc_name(int rc) {
    switch (rc) {
    case OMEGA_TF_OK: return "OK";
    case OMEGA_TF_BAD_ARGS: return "BAD_ARGS";
    case OMEGA_TF_OVERFLOW: return "OVERFLOW";
    case OMEGA_TF_NOT_TOP: return "NOT_TOP";
    case OMEGA_TF_FULL_TABLE: return "FULL_TABLE";
    default: return "?";
    }
}

uint64_t omega_tf_fixed_conv_bytes(uint32_t attn_layers, uint32_t max_pos, uint64_t gdn_set_bytes) {
    const uint64_t plane = (uint64_t)OMEGA_DATTN256_KV_HEADS * max_pos * OMEGA_DATTN256_ROW_BYTES + OMEGA_DATTN256_PLANE_SLACK;
    return (uint64_t)attn_layers * 2u * plane + gdn_set_bytes;
}
uint64_t omega_tf_arena_conv_bytes(uint32_t attn_layers, uint32_t positions, uint64_t gdn_set_bytes, uint32_t snapshots) {
    const uint64_t blocks = (positions + OMEGA_TF_BLOCK_POSITIONS - 1u) / OMEGA_TF_BLOCK_POSITIONS;
    return blocks * attn_layers * (2ull * OMEGA_DATTN256_KV_HEADS * OMEGA_DATTN256_BLOCK_PLANE_BYTES) + align_up(gdn_set_bytes) * (1u + snapshots);
}
