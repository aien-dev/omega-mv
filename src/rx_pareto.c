/* rx_pareto.c -- see rx_pareto.h. No heap, no threads, no system calls. */
#include "rx_pareto.h"

#include <stddef.h>

int rx_pareto_within(uint64_t v, uint64_t best, uint32_t tol_ppm) {
    unsigned __int128 lim = (unsigned __int128)best * (RX_PARETO_PPM + (uint64_t)tol_ppm);
    return (unsigned __int128)v * RX_PARETO_PPM <= lim;
}

int rx_pareto_dominates(const void *items, uint32_t a, uint32_t b, const RxParetoRule *r,
                        RxParetoValueFn val, void *ctx) {
    int strict = 0;
    for (uint32_t i = 0; i < r->n_dims; i++) {
        uint64_t x = val(items, a, r->dims[i], ctx), y = val(items, b, r->dims[i], ctx);
        if (x > y) return 0;
        if (x < y) strict = 1;
    }
    return strict;
}

typedef struct {
    const void *items;
    const RxParetoRule *r;
    RxParetoValueFn val;
    RxParetoTieFn tie;
    void *ctx;
} Cmp;

/* Strict total order: dimensions, tie key, index. */
static int cmp_full(const Cmp *c, uint32_t a, uint32_t b) {
    if (a == b) return 0;
    for (uint32_t i = 0; i < c->r->n_dims; i++) {
        uint64_t x = c->val(c->items, a, c->r->dims[i], c->ctx);
        uint64_t y = c->val(c->items, b, c->r->dims[i], c->ctx);
        if (x != y) return x < y ? -1 : 1;
    }
    if (c->tie) {
        int t = c->tie(c->items, a, b, c->ctx);
        if (t) return t < 0 ? -1 : 1;
    }
    return a < b ? -1 : 1;
}

/* Heapsort on indices: O(n log n), no allocation; the order is total, so the
 * result is the same whatever the algorithm. */
static void sift(uint32_t *v, uint32_t start, uint32_t end, const Cmp *c) {
    uint32_t root = start;
    for (;;) {
        uint32_t child = 2 * root + 1;
        if (child >= end) return;
        uint32_t sw = root;
        if (cmp_full(c, v[sw], v[child]) < 0) sw = child;
        if (child + 1 < end && cmp_full(c, v[sw], v[child + 1]) < 0) sw = child + 1;
        if (sw == root) return;
        uint32_t t = v[root]; v[root] = v[sw]; v[sw] = t;
        root = sw;
    }
}

static void sort_idx(uint32_t *v, uint32_t n, const Cmp *c) {
    if (n < 2) return;
    for (uint32_t s = n / 2; s-- > 0;) sift(v, s, n, c);
    for (uint32_t end = n; end-- > 1;) {
        uint32_t t = v[0]; v[0] = v[end]; v[end] = t;
        sift(v, 0, end, c);
    }
}

int rx_pareto_rank(const void *items, uint32_t n, const RxParetoRule *r, RxParetoValueFn val,
                   RxParetoTieFn tie, void *ctx, RxParetoScratch *s, uint32_t *out_idx, uint32_t k,
                   uint32_t *front) {
    if (!r || !val || !r->dims || r->n_dims == 0 || r->n_dims > RX_PARETO_MAX_DIMS)
        return RX_PARETO_ERR_ARG;
    if (front) *front = 0;
    if (n == 0 || k == 0) return 0;
    if (!items || !s || !s->idx || !s->front || !s->mark || !s->live || !out_idx)
        return RX_PARETO_ERR_ARG;

    Cmp c = { items, r, val, tie, ctx };
    uint32_t *idx = s->idx, *fr = s->front;
    uint8_t *mark = s->mark, *live = s->live;
    for (uint32_t i = 0; i < n; i++) { idx[i] = i; mark[i] = 0; }
    sort_idx(idx, n, &c);

    /* Front: a dominator sorts earlier and dominance is transitive, so
     * checking against the front found so far is enough. */
    uint32_t nf = 0;
    for (uint32_t i = 0; i < n; i++) {
        int dom = 0;
        for (uint32_t j = 0; j < nf && !dom; j++)
            dom = rx_pareto_dominates(items, fr[j], idx[i], r, val, ctx);
        if (!dom) fr[nf++] = idx[i];
    }
    if (front) *front = nf;
    if (r->include_dominated) for (uint32_t i = 0; i < n; i++) mark[i] = 1;
    else for (uint32_t j = 0; j < nf; j++) mark[fr[j]] = 1;
    uint32_t pool = r->include_dominated ? n : nf;
    if (k > pool) k = pool;

    uint32_t got = 0;
    while (got < k) {
        for (uint32_t i = 0; i < n; i++) live[i] = mark[i] == 1;
        for (uint32_t d = 0; d < r->n_dims; d++) {
            uint64_t best = UINT64_MAX;
            for (uint32_t i = 0; i < n; i++)
                if (live[i]) {
                    uint64_t v = val(items, i, r->dims[d], ctx);
                    if (v < best) best = v;
                }
            uint32_t tol = r->tol_ppm ? r->tol_ppm[r->dims[d]] : 0;
            for (uint32_t i = 0; i < n; i++)
                if (live[i] && !rx_pareto_within(val(items, i, r->dims[d], ctx), best, tol)) live[i] = 0;
        }
        uint32_t pick = UINT32_MAX;
        for (uint32_t i = 0; i < n && pick == UINT32_MAX; i++)
            if (live[idx[i]]) pick = idx[i];
        if (pick == UINT32_MAX) break;
        out_idx[got++] = pick;
        mark[pick] = 2;
    }
    return (int)got;
}
