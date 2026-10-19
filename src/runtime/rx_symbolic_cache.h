/*
 * rx_symbolic_cache.h -- rewrite verdicts filed in the rx_semantic
 * content-addressed cache (see rx_symbolic_cache.c).
 */
#ifndef RX_SYMBOLIC_CACHE_H
#define RX_SYMBOLIC_CACHE_H

#include "rx_semantic.h"
#include "rx_symbolic.h"

typedef struct {
    RxSemEngine *e;
    uint64_t src[3], node;
    uint32_t branch;
    int (*cur_verify)(void *vctx, RxSymVerdict *v);
    void *cur_vctx;
    uint64_t lookups, hits, computes;
} RxSymCache;

int  rx_sym_cache_init(RxSymCache *c);
void rx_sym_cache_destroy(RxSymCache *c);
/* RxSymCacheFn: pass as RxSymPass.cache_fn with cache = the RxSymCache. */
int  rx_sym_cache_lookup(void *cache, const char *rule, const SemanticId *lhs, const SemanticId *rhs,
                         int (*verify)(void *vctx, RxSymVerdict *v), void *vctx, RxSymVerdict *out);

#endif
