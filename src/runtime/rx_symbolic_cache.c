/*
 * rx_symbolic_cache.c -- rewrite verdicts in Omega's content-addressed
 * semantic cache (rx_semantic.h). Not a second cache: one RxSemEngine, one
 * derivation "omega.symbolic.verify/1" over three sources (left side id,
 * right side id, rule digest). A question asked again, in any branch, is
 * served from the shared cache with its provenance; a new question runs the
 * verifier once and files the verdict.
 */
#include "rx_symbolic_cache.h"

#include <stdlib.h>
#include <string.h>

#define CACHE_OBJ_BASE (RX_MAX_OBJECTS - 4u)

static void words_of(const uint8_t *b32, uint64_t w[4]) {
    for (int i = 0; i < 4; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | b32[i * 8 + j];
        w[i] = v;
    }
}

static int verify_fn(const RxSemValue *const *in, uint32_t n_in, RxSemValue *out, void *user) {
    (void)in; (void)n_in;
    RxSymCache *c = user;
    RxSymVerdict v; memset(&v, 0, sizeof v);
    if (!c->cur_verify) return -1;
    c->cur_verify(c->cur_vctx, &v);
    out->n_words = 5;
    out->value[0] = (uint64_t)v.verdict;
    out->value[1] = v.vectors;
    out->value[2] = v.counter[0];
    out->value[3] = v.got_lhs;
    out->value[4] = v.got_rhs;
    out->confidence = RX_SEM_CONFIDENCE_ONE;
    c->computes++;
    return 0;
}

int rx_sym_cache_init(RxSymCache *c) {
    memset(c, 0, sizeof *c);
    c->e = calloc(1, sizeof *c->e);
    if (!c->e || rx_sem_init(c->e, 4096) != RX_SEM_OK) return -1;
    rx_sem_set_mode(c->e, RX_SEM_GRAIN_FIELD, true, false);
    RxSemFnDesc d; memset(&d, 0, sizeof d);
    d.name = "omega.symbolic.verify/1";
    d.out_type = 0x5359u;      /* 'SY' */
    d.generation = 1;
    rx_sem_digest_impl("rx_symbolic.c rx_sym_verify_u64 v1: affine proof + 4096 vectors", &d.impl_digest);
    d.fn = verify_fn;
    d.user = c;
    uint32_t fn;
    if (rx_sem_add_fn(c->e, &d, &fn) != RX_SEM_OK) return -1;
    for (uint32_t i = 0; i < 3; i++) {
        RxObjRef o = { CACHE_OBJ_BASE + i, 0 };
        if (rx_sem_add_source(c->e, o, 0, 0x5359u, false, &c->src[i]) != RX_SEM_OK) return -1;
    }
    if (rx_sem_add_node(c->e, fn, c->src, 3, &c->node) != RX_SEM_OK) return -1;
    if (rx_sem_branch_root(c->e, 1, 1, &c->branch) != RX_SEM_OK) return -1;
    return 0;
}

void rx_sym_cache_destroy(RxSymCache *c) {
    if (c && c->e) { rx_sem_destroy(c->e); free(c->e); c->e = NULL; }
}

int rx_sym_cache_lookup(void *cache, const char *rule, const SemanticId *lhs, const SemanticId *rhs,
                        int (*verify)(void *vctx, RxSymVerdict *v), void *vctx, RxSymVerdict *out) {
    RxSymCache *c = cache;
    memset(out, 0, sizeof *out);
    uint64_t w[4];
    RxSemDigest rd;
    rx_sem_digest_impl(rule, &rd);
    words_of(lhs->bytes, w);
    rx_sem_set_source(c->e, c->branch, c->src[0], 0, w, 4, RX_SEM_CONFIDENCE_ONE, NULL);
    words_of(rhs->bytes, w);
    rx_sem_set_source(c->e, c->branch, c->src[1], 0, w, 4, RX_SEM_CONFIDENCE_ONE, NULL);
    words_of(rd.b, w);
    rx_sem_set_source(c->e, c->branch, c->src[2], 0, w, 4, RX_SEM_CONFIDENCE_ONE, NULL);
    c->cur_verify = verify; c->cur_vctx = vctx;
    RxSemValue v; RxSemServed served = 0;
    int rc = rx_sem_get(c->e, c->branch, c->node, &v, &served);
    c->cur_verify = NULL; c->cur_vctx = NULL;
    if (rc != RX_SEM_OK) { verify(vctx, out); return out->verdict; }
    out->verdict = (int)v.value[0];
    out->vectors = (uint32_t)v.value[1];
    out->counter[0] = v.value[2];
    out->got_lhs = v.value[3];
    out->got_rhs = v.value[4];
    out->cache_hit = served != RX_SEM_SERVED_COMPUTED;
    c->lookups++;
    c->hits += out->cache_hit;
    return out->verdict;
}
