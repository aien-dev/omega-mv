/*
 * rx_pareto.h -- Omega's one Pareto front and deterministic selection rule.
 *
 * Shared by the capability query (rx_capq.c, candidates) and path evaluation
 * (src/path/rx_path_eval.c, whole trajectories), so there is exactly one
 * notion of dominance and one tie rule in the tree.
 *
 * Rule (unchanged from OMEGA_CAPABILITY_QUERY):
 *   1. Every item has a value to MINIMIZE on each named dimension.
 *   2. Front: an item is kept when no other item is <= on every named
 *      dimension and < on at least one (strict Pareto dominance).
 *   3. Order: on the first dimension keep everything within the band of the
 *      best (v * 1e6 <= best * (1e6 + tol_ppm)), the next dimension decides
 *      among those, and so on; a strict total order (dimensions, then the
 *      caller's tie key, then the index) breaks what remains. Repeat k times.
 *
 * This is a calculator over integers. It allocates nothing: the caller hands
 * in scratch arrays of n entries each. It is deterministic: same items, same
 * tradeoffs, same output, on every host.
 */
#ifndef RX_PARETO_H
#define RX_PARETO_H

#include <stdint.h>

#define RX_PARETO_PPM 1000000u
#define RX_PARETO_MAX_DIMS 16u

#define RX_PARETO_OK 0
#define RX_PARETO_ERR_ARG -1

/* Value of item i on dimension dim (to minimize). */
typedef uint64_t (*RxParetoValueFn)(const void *items, uint32_t i, uint32_t dim, void *ctx);
/* Strict order on the caller's identities: <0, 0, >0. 0 only for the same item. */
typedef int (*RxParetoTieFn)(const void *items, uint32_t a, uint32_t b, void *ctx);

/* Caller-owned scratch, n entries each. */
typedef struct {
    uint32_t *idx;
    uint32_t *front;
    uint8_t *mark;
    uint8_t *live;
} RxParetoScratch;

typedef struct {
    const uint8_t *dims;        /* dimension ids, most important first */
    uint32_t n_dims;            /* 1..RX_PARETO_MAX_DIMS */
    const uint32_t *tol_ppm;    /* indexed by dimension id; NULL = all 0 */
    int include_dominated;      /* 0: select from the front only */
} RxParetoRule;

/* 1 when a dominates b on the rule's dimensions. */
int rx_pareto_dominates(const void *items, uint32_t a, uint32_t b, const RxParetoRule *r,
                        RxParetoValueFn val, void *ctx);

/* 1 when v is within the band of best. */
int rx_pareto_within(uint64_t v, uint64_t best, uint32_t tol_ppm);

/* Rank n items. Writes at most k indices to out_idx in selection order and
 * returns how many (>= 0); *front (optional) gets the size of the Pareto
 * front. RX_PARETO_ERR_ARG on a bad rule. */
int rx_pareto_rank(const void *items, uint32_t n, const RxParetoRule *r, RxParetoValueFn val,
                   RxParetoTieFn tie, void *ctx, RxParetoScratch *s, uint32_t *out_idx, uint32_t k,
                   uint32_t *front);

#endif /* RX_PARETO_H */
