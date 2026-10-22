/* Host oracle only. Admission precedes the existing measured-cost control. */
#include "turing_gpu_replay.h"
#include "sha256.h"
#include "turing/select.h"
#include <stdlib.h>
#include <string.h>

static int present(const uint8_t p[32]) {
    uint8_t any=0;
    for (size_t j=0;j<32;++j) any|=p[j];
    return any!=0;
}
static int binding_valid(const tgr_binding *b) {
    if (!b->reuse) return 0;
    for (size_t j=0;j<TGR_BINDINGS;++j) if (!present(b->id[j])) return 0;
    return 1;
}
static int binding_equal(const tgr_binding *a, const tgr_binding *b) {
    return a->reuse==b->reuse && !memcmp(a->id,b->id,sizeof a->id);
}
static tgr_reason admit(const tgr_query *q, const tgr_candidate *c,
                        tgr_verifier verify, void *ctx, tgr_observation *o) {
    /* Snapshot once: the verifier sees exactly the bytes that were hashed. */
    if (!c->bytes || !c->size || c->size > 1024*1024 ||
        !present(c->receipt_sha256) || !present(c->binary)) return TGR_INVALID;
    uint8_t *snapshot=malloc(c->size), digest[32];
    if (!snapshot) return TGR_INVALID;
    memcpy(snapshot,c->bytes,c->size);
    sha256_hash(snapshot,c->size,digest);
    if (memcmp(digest,c->receipt_sha256,32)) { free(snapshot); return TGR_DIGEST; }
    int rc=verify ? verify(snapshot,c->size,o,ctx) : -1;
    free(snapshot);
    if (rc) return TGR_UNQUALIFIED;
    if (!binding_equal(&q->binding,&o->binding)) return TGR_BINDING;
    if (memcmp(c->binary,o->binary,32)) return TGR_BINARY;
    if (!o->measured_at || o->measured_at>q->now ||
        q->now-o->measured_at>q->max_age) return TGR_STALE;
    if (!o->registers || o->registers>q->max_registers ||
        o->resident_bytes>q->max_resident_bytes ||
        o->scratch_bytes>q->max_scratch_bytes) return TGR_RESOURCE;
    /* Exactly representable by the existing double-valued ranking core.
     * Unknown/zero/overflowing costs never enter the bucket. No extrapolation. */
    if (!o->samples || !o->total_ns || o->total_ns>((UINT64_C(1)<<53)-1) ||
        !o->submit_host_ns || !o->device_ns ||
        o->device_ns>o->submit_host_ns || o->submit_host_ns>o->total_ns) return TGR_COST;
    return TGR_ELIGIBLE;
}
int tgr_replay(const tgr_query *q, const tgr_candidate *c, size_t n,
               tgr_verifier verify, void *ctx, tgr_decision *d) {
    if (!d) return -1;
    memset(d,0,sizeof *d); d->chosen=-1; d->fallback=1;
    for(size_t i=0;i<TGR_MAX;++i) d->reason[i]=TGR_INVALID;
    if (!q || !c || !n || n>TGR_MAX || q->incumbent>=n ||
        !binding_valid(&q->binding) || !q->now || !q->max_age ||
        !q->max_registers || !q->max_resident_bytes) return -1;
    for(size_t i=0;i<n;++i) {
        if (!present(c[i].id)) return -1;
        for(size_t j=0;j<i;++j) if (!memcmp(c[i].id,c[j].id,32)) return -1;
    }
    turing_hist_bucket bucket;
    turing_bucket_reset(&bucket,0,0,0);
    int allowed[TGR_MAX]={0}; size_t original[TGR_MAX];
    /* Incumbent first makes an exact tie retain the qualified current method.
     * All other choices retain their frozen declared order. */
    original[0]=q->incumbent;
    for(size_t i=0,k=1;i<n;++i) if(i!=q->incumbent) original[k++]=i;
    for(size_t k=0;k<n;++k) {
        size_t i=original[k]; tgr_observation o={0};
        d->reason[i]=admit(q,&c[i],verify,ctx,&o);
        if(d->reason[i]==TGR_ELIGIBLE) {
            allowed[k]=1; d->total_ns[i]=o.total_ns;
            turing_bucket_observe(&bucket,k,o.total_ns);
        }
    }
    /* A comparison cannot promote anything without a qualified incumbent. */
    if(!allowed[0]) return 1;
    int best=turing_rank_min_cost(&bucket,allowed,n);
    if(best<0) return 1;
    d->chosen=(int)original[best];
    d->fallback=(d->chosen==(int)q->incumbent);
    return 0;
}
