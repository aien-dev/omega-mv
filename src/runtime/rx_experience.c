/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * rx_experience.c -- verified experience on the composition accept chain
 * (rx_experience.h). Every write goes through the composition's attached
 * Cortex link (rx_cortex_append_in); this file adds no writer, World,
 * scheduler or store.
 */
#include "rx_experience.h"

#include "rx_cortex_record.h"
#include "sha256.h"

#include <stdlib.h>
#include <string.h>

#define EXP_SKILL_ID 0x699u
#define EXP_NOW_US 1000000ull

const char *rx_exp_strerror(int rc) {
    switch (rc) {
    case RX_EXP_OK: return "ok";
    case RX_EXP_ERR_ARG: return "bad argument";
    case RX_EXP_ERR_UNOBSERVED: return "no environment receipt: not an observation";
    case RX_EXP_ERR_FORGED: return "receipt not written by the World recorder for a live commit";
    case RX_EXP_ERR_MISMATCH: return "observed state differs from the receipt";
    case RX_EXP_ERR_ORDER: return "not the pending transition of this environment";
    case RX_EXP_ERR_DUPLICATE: return "receipt already used by an experience";
    case RX_EXP_ERR_REPLAY: return "transition does not replay";
    case RX_EXP_ERR_UNVERIFIED: return "no AEGIS pass, promotion or matching goal";
    case RX_EXP_ERR_CONSENT: return "missing owner, consent, privacy or split";
    case RX_EXP_ERR_OWNER: return "revocation by someone other than the owner";
    case RX_EXP_ERR_HYPOTHESIS: return "hypothesis is not one for this prior and action";
    case RX_EXP_ERR_REFUSED: return "environment refused the action";
    case RX_EXP_ERR_WORLD: return "composition, World or Cortex failure";
    case RX_EXP_ERR_RETIRED: return "state object retired in the World";
    case RX_EXP_ERR_TENANT: return "owner is not the tenant that took the action";
    case RX_EXP_ERR_TAINTED: return "hypothesis basis imported, private into non-private, or too large";
    case RX_EXP_ERR_CAPABILITY: return "no live capability of the tenant to act in this family";
    case RX_EXP_ERR_TRACE: return "episode trace full: reset to start the next episode";
    default: return "unknown";
    }
}

static void le64(uint8_t *b, uint64_t v) { for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i)); }

static void to_words(const uint8_t d[32], uint64_t *w) {
    for (int i = 0; i < 4; i++) {
        w[i] = 0;
        for (int b = 0; b < 8; b++) w[i] |= (uint64_t)d[8 * i + b] << (8 * b);
    }
}

static void state_digest(const RxExpFamily *f, uint64_t state, uint8_t out[32]) {
    static const char tag[] = "omega.exp.state.v1";
    uint8_t b[sizeof tag - 1 + 24];
    memcpy(b, tag, sizeof tag - 1);
    le64(b + sizeof tag - 1, f->family);
    le64(b + sizeof tag - 1 + 8, f->version);
    le64(b + sizeof tag - 1 + 16, state);
    sha256_hash(b, sizeof b, out);
}

static void seed_commit(uint64_t seed, uint8_t out[32]) {
    static const char tag[] = "omega.exp.seed.v1";
    uint8_t b[sizeof tag - 1 + 8];
    memcpy(b, tag, sizeof tag - 1);
    le64(b + sizeof tag - 1, seed);
    sha256_hash(b, sizeof b, out);
}

static void skill_identity(const RxExpFamily *f, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"omega.exp.family.v1", 19);
    sha256_update(&c, (const uint8_t *)f->name, strlen(f->name));
    uint8_t b[16];
    le64(b, f->family);
    le64(b + 8, f->version);
    sha256_update(&c, b, sizeof b);
    sha256_final(&c, out);
}

static SrRequirement requirement(uint32_t op) {
    SrRequirement q;
    memset(&q, 0, sizeof q);
    q.need.semantic_operation = op;
    q.need.accepted_input_types = ~0ull;
    q.need.effect_class = 0x1FF;
    q.need.authority_ceiling.resource_hi = UINT64_MAX;
    q.need.authority_ceiling.rights = RX_RIGHT_READ | RX_RIGHT_WRITE;
    q.t.n_order = 1;
    q.t.order[0] = CQ_DIM_COST;
    q.t.k = CQ_MAX_K;
    q.local_only = 1;
    return q;
}

/* The environment Skill is the only provider of the family operation. */
static int build_graph(RxExpEnv *e, const AienMachineId *self) {
    const RxExpFamily *f = e->fam;
    aien_mid_index_init(&e->ix, e->slots, 8);
    if (cq_catalog_init_canonical(&e->cat, &e->ix, self, 8, 8) != CQ_OK) return -1;
    uint32_t all = CQ_SRC(CQ_SRC_GRAPH) | CQ_SRC(CQ_SRC_SKILL) | CQ_SRC(CQ_SRC_FABRIC);
    if (cq_op_define(&e->cat, f->op, 0, all) != CQ_OK) return -1;
    e->skills.n = 1;
    e->skills.skill[0].id = EXP_SKILL_ID;
    e->skills.skill[0].fn = f->skill;
    skill_identity(f, e->skills.skill[0].identity);
    SrSkill s;
    memset(&s, 0, sizeof s);
    s.skill_id = EXP_SKILL_ID;
    s.version = f->version;
    memcpy(s.digest, e->skills.skill[0].identity, 32);
    s.machine = e->cat.self_machine;
    CqEntry p;
    memset(&p, 0, sizeof p);
    p.capability_id = 1;
    p.realization_id = 1;
    p.op = f->op;
    p.effects = CQ_FX_PURE;
    p.in_types = 0x1;
    p.out_types = 0x2;
    p.confidence_ppm = 1000000;
    p.reliability_ppm = 1000000;
    p.evidence_level = CQ_EV_MEASURED;
    p.evidence_ref = f->family;
    p.cost = 1;
    p.latency_us = 1;
    p.energy_uj = 1;
    p.live = CQ_LIVE_AVAILABLE;
    p.generation = 1;
    if (sr_register_skill(&e->cat, &s, &p, 1) != SR_OK) return -1;
    if (cq_catalog_build(&e->cat) != CQ_OK) return -1;
    e->router = (SrRouter){ &e->cat, &e->skills };
    return 0;
}

int rx_exp_open(RxExpEnv *e, const RxExpFamily *fam, const char *dir, const AienMachineId *self,
                uint64_t session, AienosCapAdmin *admin, AienosCapView *view) {
    if (!e || !fam || !dir || !self || !admin || !view) return RX_EXP_ERR_ARG;
    memset(e, 0, sizeof *e);
    e->fam = fam;
    e->view = view;
    e->now_us = EXP_NOW_US;
    if (build_graph(e, self) != 0) {
        cq_catalog_free(&e->cat);
        return RX_EXP_ERR_WORLD;
    }
    if (rx_compose_open(&e->c, dir, self, session, &e->router, fam->contract, admin, view, 1) !=
        RX_OK) {
        cq_catalog_free(&e->cat);
        return RX_EXP_ERR_WORLD;
    }
    e->open = 1;
    return RX_EXP_OK;
}

void rx_exp_close(RxExpEnv *e) {
    if (!e || !e->open) return;
    rx_compose_close(&e->c);
    cq_catalog_free(&e->cat);
    e->open = 0;
}

uint64_t rx_exp_state(RxExpEnv *e) {
    RxObject so;
    if (!e || !e->open || rx_world_read(e->c.world, e->c.state, &so) != RX_OK) return 0;
    return so.field[RXC_S_RESULT];
}

/* Append through the composition's Cortex writer link. */
static int put(RxExpEnv *e, CxHeader *h, const uint64_t *p, uint32_t n, uint64_t *id) {
    if (!e->c.attached) return RX_EXP_ERR_WORLD;
    h->t = rx_cortex_next_t_in(e->c.world, &e->c.cx);
    return rx_cortex_append_in(e->c.world, &e->c.cx, h, p, n, id) == RX_OK ? RX_EXP_OK
                                                                           : RX_EXP_ERR_WORLD;
}

static void drop_pending(RxExpEnv *e) {
    if (e->pend.live) e->dropped++;
    memset(&e->pend, 0, sizeof e->pend);
}

static int run_goal(RxExpEnv *e, uint64_t input, RxcResult *r) {
    SrRequirement q = requirement(e->fam->op);
    return rx_compose_run(&e->c, input, &q, NULL, e->now_us, r) == RX_OK ? RX_EXP_OK
                                                                         : RX_EXP_ERR_WORLD;
}

int rx_exp_reset(RxExpEnv *e, uint64_t seed) {
    if (!e || !e->open) return RX_EXP_ERR_ARG;
    drop_pending(e);
    RxcResult r;
    if (run_goal(e, e->fam->reset_input(seed), &r) != RX_EXP_OK) return RX_EXP_ERR_WORLD;
    if (r.outcome == RXC_OUT_NO_WINNER || r.outcome == RXC_OUT_NOT_COMMITTED)
        return RX_EXP_ERR_REFUSED;
    if (r.outcome != RXC_OUT_COMMITTED) return RX_EXP_ERR_WORLD;
    e->seed = seed;
    seed_commit(seed, e->seed_commit);
    e->fam->objective(r.result, e->objective);
    e->episode++;
    e->step = 0;
    e->taken = 0;
    return RX_EXP_OK;
}

int rx_exp_hypothesis(RxExpEnv *e, uint64_t predictor, uint64_t action, uint64_t predicted,
                      uint64_t *out_id) {
    if (!e || !e->open) return RX_EXP_ERR_ARG;
    uint64_t p[RX_EXP_H_WORDS] = { RX_EXP_VERSION, e->fam->family, rx_exp_state(e), action,
                                   predicted, predictor };
    CxHeader h;
    memset(&h, 0, sizeof h);
    h.cls = CX_CLAIM;
    h.kind = CX_K_CANDIDATE;
    h.subject = RX_EXP_SUBJECT;
    h.tag = RX_EXP_TAG_HYPOTHESIS;
    return put(e, &h, p, RX_EXP_H_WORDS, out_id);
}

/* The state object is retired: its World reference is no longer live. */
static int subject_retired(RxExpEnv *e) {
    RxObject so;
    return rx_world_read(e->c.world, e->c.state, &so) != RX_OK;
}

int rx_exp_act(RxExpEnv *e, uint32_t tenant, AienosCapRef cap, uint64_t action,
               RxExpObservation *obs) {
    if (!e || !e->open || !obs) return RX_EXP_ERR_ARG;
    memset(obs, 0, sizeof *obs);
    /* Refusals before anything runs; a pending transition stays recordable. */
    AienosCapEntry ce;
    if (!tenant || aienos_cap_validate(e->view, cap, tenant, RX_EXP_RESOURCE(e->fam),
                                       RX_EXP_RIGHT_ACT, &ce) != 0)
        return RX_EXP_ERR_CAPABILITY;
    if (subject_retired(e)) return RX_EXP_ERR_RETIRED;
    if (e->taken >= RX_EXP_MAX_STEPS) return RX_EXP_ERR_TRACE;
    drop_pending(e);
    uint64_t prior = rx_exp_state(e);
    uint64_t input = e->fam->encode(prior, action);
    RxcResult r;
    if (run_goal(e, input, &r) != RX_EXP_OK) return RX_EXP_ERR_WORLD;
    if (r.outcome == RXC_OUT_NO_WINNER || r.outcome == RXC_OUT_NOT_COMMITTED)
        return RX_EXP_ERR_REFUSED;
    if (r.outcome != RXC_OUT_COMMITTED) return RX_EXP_ERR_WORLD;
    e->taken++;
    const CxObject *ev = cx_get(&e->c.cx, r.cx_evidence);
    if (!ev) return RX_EXP_ERR_WORLD;
    uint64_t next = 0, cost = 0;
    e->fam->step(input, &next, &cost);
    e->pend.live = 1;
    e->pend.prior = prior;
    e->pend.action = action;
    e->pend.input = input;
    e->pend.next = r.result;
    e->pend.cost = cost;
    e->pend.tenant = tenant;
    e->pend.goal_crumb = r.goal_crumb;
    e->pend.commit = ev->links[3];
    e->pend.evidence = r.cx_evidence;
    e->pend.promotion = r.cx_promotion;
    obs->prior = prior;
    obs->action = action;
    obs->next = r.result;
    obs->commit = e->pend.commit;
    obs->evidence = r.cx_evidence;
    obs->promotion = r.cx_promotion;
    return RX_EXP_OK;
}

/* ---- receipt checks shared by record (live World) and dataset (store only) */

static const uint64_t *payload_n(const CxStore *s, uint64_t id, uint32_t cls, uint32_t kind,
                                 uint32_t min_words, CxObject *hdr) {
    CxRecord r;
    if (!id || cx_recall_id(s, id, &r) != CX_OK || !r.verified) return NULL;
    if (r.hdr.cls != cls || r.hdr.kind != kind || r.hdr.n < min_words) return NULL;
    if (hdr) *hdr = r.hdr;
    return r.payload;
}

/* The composition receipts of one transition, from the store. `next` and
 * `input` are what they must name. Returns RX_EXP_OK, RX_EXP_ERR_UNOBSERVED
 * (not a World commit record), RX_EXP_ERR_MISMATCH (commit record names
 * another state) or RX_EXP_ERR_UNVERIFIED. `wr` gets the decoded commit
 * record, `verdict` the AEGIS verdict World record the evidence names. */
static int check_chain(const CxStore *s, uint64_t commit, uint64_t evidence, uint64_t promotion,
                       uint64_t input, uint64_t next, CxWorldRecord *wr, uint64_t *verdict) {
    CxRecord cr;
    if (cx_recall_id(s, commit, &cr) != CX_OK || cx_world_decode(&cr, wr) != CX_OK ||
        wr->kind != CX_K_EXEC_COMMIT)
        return RX_EXP_ERR_UNOBSERVED;
    if (wr->field[RXC_S_RESULT] != next) return RX_EXP_ERR_MISMATCH;
    CxObject eh, ph, ch;
    const uint64_t *ep = payload_n(s, evidence, CX_EVIDENCE, CX_K_EVIDENCE_REF, RXC_EP_WORDS, &eh);
    if (!ep || !(eh.protect & CX_PROT_VERIFY_EVIDENCE) || eh.links[3] != commit)
        return RX_EXP_ERR_UNVERIFIED;
    uint64_t winner = ep[RXC_EP_WINNER];
    if (winner == RXC_NONE || winner >= RXC_K || !((ep[RXC_EP_PASSMASK] >> winner) & 1u) ||
        ep[RXC_EP_RESULT] != next)
        return RX_EXP_ERR_UNVERIFIED;
    *verdict = eh.links[0];
    /* The promotion (cx_promote) binds the winning claim and this evidence by digest. */
    const uint64_t *pp = payload_n(s, promotion, CX_EVIDENCE, CX_K_PROMOTION, 8, &ph);
    const uint64_t *cp = pp && ph.links[1] == evidence
                       ? payload_n(s, ph.links[0], CX_CLAIM, CX_K_CANDIDATE, RXC_CP_WORDS, &ch) : NULL;
    uint64_t dw[4];
    if (!cp) return RX_EXP_ERR_UNVERIFIED;
    to_words(ch.digest, dw);
    if (memcmp(dw, pp, sizeof dw) != 0) return RX_EXP_ERR_UNVERIFIED;
    to_words(eh.digest, dw);
    if (memcmp(dw, pp + 4, sizeof dw) != 0) return RX_EXP_ERR_UNVERIFIED;
    if (cp[RXC_CP_K] != winner || cp[RXC_CP_PASS] != 1 || cp[RXC_CP_RESULT] != next ||
        cp[RXC_CP_INPUT] != input)
        return RX_EXP_ERR_UNVERIFIED;
    CxRecord gr;
    CxWorldRecord g;
    if (cx_recall_id(s, ch.tag, &gr) != CX_OK || cx_world_decode(&gr, &g) != CX_OK ||
        g.kind != CX_K_WORK_ACCEPTED || g.field[RXC_G_INPUT] != input)
        return RX_EXP_ERR_UNVERIFIED;
    return RX_EXP_OK;
}

static int commit_used(const CxStore *s, uint64_t commit) {
    const CxIdList *l = &s->by_subject[RX_EXP_SUBJECT];
    for (uint32_t i = 0; i < l->n; i++) {
        const CxObject *o = cx_get(s, l->ids[i]);
        if (o && o->kind == CX_K_EXPERIENCE && o->links[0] == commit) return 1;
    }
    return 0;
}

/* RX_EXP_ERR_TAINTED rule (rx_experience.h, Taint): breadth-first over the
 * hypothesis and the claims it links, digests checked. */
static int tainted(const CxStore *s, uint64_t hyp, uint64_t privacy) {
    uint64_t q[RX_EXP_BASIS_MAX];
    uint32_t n = 0;
    q[n++] = hyp;
    for (uint32_t i = 0; i < n; i++) {
        CxRecord r;
        if (cx_recall_id(s, q[i], &r) != CX_OK || !r.verified) return 1;
        if (r.hdr.kind == CX_K_IMPORTED) return 1;
        if (r.hdr.kind == CX_K_EXPERIENCE) {
            RxExpRecord x;
            if (rx_exp_decode(s, q[i], &x) != RX_EXP_OK) return 1;
            if (x.w[RX_EXP_W_PRIVACY] != RX_EXP_PRIV_SYNTHETIC && privacy != RX_EXP_PRIV_PRIVATE)
                return 1;
            continue;
        }
        if (r.hdr.cls != CX_CLAIM) continue;
        for (uint32_t j = 0; j < CX_LINKS; j++) {
            uint64_t l = r.hdr.links[j];
            int seen = !l;
            for (uint32_t k = 0; k < n && !seen; k++) seen = q[k] == l;
            if (seen) continue;
            if (n == RX_EXP_BASIS_MAX) return 1;
            q[n++] = l;
        }
    }
    return 0;
}

static void prediction_error(const RxExpFamily *f, uint64_t pred, uint64_t obs, uint64_t *fields,
                             uint64_t *l1) {
    int64_t a[RX_EXP_MAX_FIELDS], b[RX_EXP_MAX_FIELDS];
    uint32_t n = f->unpack(pred, a);
    f->unpack(obs, b);
    *fields = 0;
    *l1 = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        (*fields)++;
        uint64_t d = a[i] > b[i] ? (uint64_t)a[i] - (uint64_t)b[i] : (uint64_t)b[i] - (uint64_t)a[i];
        *l1 = *l1 + d < *l1 ? UINT64_MAX : *l1 + d;
    }
}

int rx_exp_record(RxExpEnv *e, const RxExpDraft *d, uint64_t *out_id) {
    if (!e || !e->open || !d) return RX_EXP_ERR_ARG;
    const RxExpFamily *f = e->fam;
    CxStore *s = &e->c.cx;
    if (!d->owner || (d->consent & ~(uint64_t)RX_EXP_CONSENT_ALL) ||
        (d->privacy != RX_EXP_PRIV_PRIVATE && d->privacy != RX_EXP_PRIV_SYNTHETIC) ||
        (d->split != RX_EXP_SPLIT_TRAIN && d->split != RX_EXP_SPLIT_HOLDOUT))
        return RX_EXP_ERR_CONSENT;

    /* 1. The observation must stand on a World commit receipt. */
    const RxExpObservation *o = &d->obs;
    const CxObject *co = o->commit ? cx_get(s, o->commit) : NULL;
    if (!co || co->cls != CX_EXECUTION || co->kind != CX_K_EXEC_COMMIT || !o->evidence ||
        !o->promotion)
        return RX_EXP_ERR_UNOBSERVED;
    if (commit_used(s, o->commit)) return RX_EXP_ERR_DUPLICATE;

    /* 2. ... written by the World recorder for a live commit crumb of the
     * composition's commit step on the state object. */
    CxRecord cr;
    CxWorldRecord wr;
    if (cx_recall_id(s, o->commit, &cr) != CX_OK || cx_world_decode(&cr, &wr) != CX_OK)
        return RX_EXP_ERR_FORGED;
    const RxCrumb *k = rx_world_crumb(e->c.world, wr.crumb);
    if (wr.session != e->c.session || wr.obj != e->c.state.id ||
        wr.obj_gen != e->c.state.generation || wr.reaction != e->c.rx_commit ||
        rx_cortex_record_in(e->c.world, s, wr.crumb) != o->commit || !k ||
        k->kind != RX_CRUMB_COMMIT || memcmp(k->digest, wr.crumb_digest, 32) != 0)
        return RX_EXP_ERR_FORGED;
    if (subject_retired(e)) return RX_EXP_ERR_RETIRED;
    if (wr.field[RXC_S_RESULT] != o->next) return RX_EXP_ERR_MISMATCH;

    /* 3. It is the transition this environment is waiting to record. */
    if (!e->pend.live || o->commit != e->pend.commit || o->evidence != e->pend.evidence ||
        o->promotion != e->pend.promotion || d->prior != e->pend.prior ||
        d->action != e->pend.action || o->next != e->pend.next)
        return RX_EXP_ERR_ORDER;
    if (d->owner != e->pend.tenant) return RX_EXP_ERR_TENANT;

    /* 4. AEGIS passed it, it was promoted, the accepted goal was prior + action. */
    uint64_t input = f->encode(d->prior, d->action), verdict = 0;
    if (input != e->pend.input) return RX_EXP_ERR_ORDER;
    int rc = check_chain(s, o->commit, o->evidence, o->promotion, input, o->next, &wr, &verdict);
    if (rc != RX_EXP_OK) return rc == RX_EXP_ERR_UNOBSERVED ? RX_EXP_ERR_FORGED : rc;
    CxRecord vr;
    CxWorldRecord vw;
    if (cx_recall_id(s, verdict, &vr) != CX_OK || cx_world_decode(&vr, &vw) != CX_OK ||
        vw.kind != CX_K_EXEC_COMMIT || vw.reaction != e->c.rx_verify ||
        rx_cortex_record_in(e->c.world, s, vw.crumb) != verdict)
        return RX_EXP_ERR_UNVERIFIED;

    /* 5. Replay. */
    uint64_t next = 0, cost = 0;
    if (f->step(input, &next, &cost) != 0 || next != o->next) return RX_EXP_ERR_REPLAY;

    /* 6. The hypothesis, if any, is a claim about this prior and action. */
    uint64_t predicted = 0, predictor = 0;
    if (d->hypothesis) {
        CxObject hh;
        const uint64_t *hp = payload_n(s, d->hypothesis, CX_CLAIM, CX_K_CANDIDATE, RX_EXP_H_WORDS, &hh);
        if (!hp || hh.tag != RX_EXP_TAG_HYPOTHESIS || hp[RX_EXP_H_FAMILY] != f->family ||
            hp[RX_EXP_H_PRIOR] != d->prior || hp[RX_EXP_H_ACTION] != d->action)
            return RX_EXP_ERR_HYPOTHESIS;
        predicted = hp[RX_EXP_H_PREDICTED];
        predictor = hp[RX_EXP_H_PREDICTOR];
        if (tainted(s, d->hypothesis, d->privacy)) return RX_EXP_ERR_TAINTED;
    }

    uint64_t p[RX_EXP_W_WORDS];
    memset(p, 0, sizeof p);
    uint8_t dg[32];
    p[RX_EXP_W_VERSION] = RX_EXP_VERSION;
    p[RX_EXP_W_FAMILY] = f->family;
    p[RX_EXP_W_FAMILY_VERSION] = f->version;
    to_words(e->seed_commit, p + RX_EXP_W_SEED_COMMIT0);
    p[RX_EXP_W_EPISODE] = e->episode;
    p[RX_EXP_W_STEP] = e->step;
    static const uint8_t zero[32];
    to_words(memcmp(d->objective, zero, 32) ? d->objective : e->objective, p + RX_EXP_W_OBJECTIVE0);
    p[RX_EXP_W_PRIOR] = d->prior;
    state_digest(f, d->prior, dg);
    to_words(dg, p + RX_EXP_W_PRIOR_DIGEST0);
    p[RX_EXP_W_ACTION] = d->action;
    p[RX_EXP_W_INPUT] = input;
    p[RX_EXP_W_PRED_STATUS] = d->hypothesis ? RX_EXP_PRED_UNQUALIFIED : RX_EXP_PRED_NONE;
    p[RX_EXP_W_PREDICTED] = predicted;
    p[RX_EXP_W_PREDICTOR] = predictor;
    p[RX_EXP_W_NEXT] = o->next;
    state_digest(f, o->next, dg);
    to_words(dg, p + RX_EXP_W_NEXT_DIGEST0);
    p[RX_EXP_W_VERIFY] = RX_EXP_V_ALL;
    if (d->hypothesis)
        prediction_error(f, predicted, o->next, &p[RX_EXP_W_ERR_FIELDS], &p[RX_EXP_W_ERR_L1]);
    p[RX_EXP_W_COST] = cost;
    p[RX_EXP_W_COST_UNIT] = RX_EXP_COST_DECLARED_UNITS;
    p[RX_EXP_W_OWNER] = d->owner;
    p[RX_EXP_W_CONSENT] = d->consent;
    p[RX_EXP_W_LICENSE] = d->license;
    p[RX_EXP_W_PRIVACY] = d->privacy;
    p[RX_EXP_W_SPLIT] = d->split;
    p[RX_EXP_W_APPLY_OP] = f->op;
    p[RX_EXP_W_APPLY_DOMAIN] = f->domain;
    p[RX_EXP_W_SESSION] = e->c.session;

    CxHeader h;
    memset(&h, 0, sizeof h);
    h.cls = CX_EVIDENCE;
    h.kind = CX_K_EXPERIENCE;
    h.protect = CX_PROT_VERIFY_EVIDENCE;
    h.subject = RX_EXP_SUBJECT;
    h.tag = d->owner;
    h.links[0] = o->commit;
    h.links[1] = o->evidence;
    h.links[2] = o->promotion;
    h.links[3] = d->hypothesis;
    rc = put(e, &h, p, RX_EXP_W_WORDS, out_id);
    if (rc != RX_EXP_OK) return rc;
    memset(&e->pend, 0, sizeof e->pend);
    e->step++;
    return RX_EXP_OK;
}

int rx_exp_decode(const CxStore *s, uint64_t id, RxExpRecord *out) {
    if (!s || !out) return RX_EXP_ERR_ARG;
    CxObject h;
    const uint64_t *p = payload_n(s, id, CX_EVIDENCE, CX_K_EXPERIENCE, RX_EXP_W_WORDS, &h);
    if (!p || h.n != RX_EXP_W_WORDS || p[RX_EXP_W_VERSION] != RX_EXP_VERSION) return RX_EXP_ERR_ARG;
    memset(out, 0, sizeof *out);
    out->id = id;
    out->t = h.t;
    memcpy(out->w, p, sizeof out->w);
    out->commit = h.links[0];
    out->evidence = h.links[1];
    out->promotion = h.links[2];
    out->hypothesis = h.links[3];
    return RX_EXP_OK;
}

int rx_exp_revoke(RxExpEnv *e, uint64_t owner, uint64_t exp, uint64_t scope, uint64_t reason,
                  uint64_t *out_id) {
    if (!e || !e->open || !owner || !scope || (scope & ~(uint64_t)RX_EXP_CONSENT_ALL))
        return RX_EXP_ERR_ARG;
    if (exp) {
        RxExpRecord r;
        if (rx_exp_decode(&e->c.cx, exp, &r) != RX_EXP_OK) return RX_EXP_ERR_ARG;
        if (r.w[RX_EXP_W_OWNER] != owner) return RX_EXP_ERR_OWNER;
    }
    uint64_t p[RX_EXP_R_WORDS] = { RX_EXP_VERSION, owner, exp, scope, reason };
    CxHeader h;
    memset(&h, 0, sizeof h);
    h.cls = CX_EVIDENCE;
    h.kind = CX_K_CONSENT_REVOKED;
    h.protect = CX_PROT_AUTHORITY;
    h.subject = RX_EXP_SUBJECT;
    h.tag = owner;
    h.links[0] = exp;
    return put(e, &h, p, RX_EXP_R_WORDS, out_id);
}

/* ---- dataset view ------------------------------------------------------------- */

/* Store-only re-verification of a decoded experience. */
static int recheck(const CxStore *s, const RxExpFamily *f, const RxExpRecord *r) {
    const uint64_t *w = r->w;
    if (w[RX_EXP_W_FAMILY] != f->family || w[RX_EXP_W_FAMILY_VERSION] != f->version ||
        w[RX_EXP_W_VERIFY] != RX_EXP_V_ALL)
        return -1;
    if (f->encode(w[RX_EXP_W_PRIOR], w[RX_EXP_W_ACTION]) != w[RX_EXP_W_INPUT]) return -1;
    uint64_t next = 0, cost = 0;
    if (f->step(w[RX_EXP_W_INPUT], &next, &cost) != 0 || next != w[RX_EXP_W_NEXT] ||
        cost != w[RX_EXP_W_COST])
        return -1;
    uint8_t dg[32];
    uint64_t dw[4];
    state_digest(f, w[RX_EXP_W_PRIOR], dg);
    to_words(dg, dw);
    if (memcmp(dw, w + RX_EXP_W_PRIOR_DIGEST0, sizeof dw)) return -1;
    state_digest(f, w[RX_EXP_W_NEXT], dg);
    to_words(dg, dw);
    if (memcmp(dw, w + RX_EXP_W_NEXT_DIGEST0, sizeof dw)) return -1;
    CxWorldRecord wr;
    uint64_t verdict = 0;
    if (check_chain(s, r->commit, r->evidence, r->promotion, w[RX_EXP_W_INPUT], w[RX_EXP_W_NEXT],
                    &wr, &verdict) != RX_EXP_OK)
        return -1;
    if (wr.kind != CX_K_EXEC_COMMIT || wr.session != w[RX_EXP_W_SESSION]) return -1;
    return 0;
}

typedef struct { uint64_t t, owner, target, scope; } Revocation;

uint32_t rx_exp_dataset(const CxStore *s, const RxExpFamily *fam, uint64_t need, uint64_t splits,
                        RxExpRecord *out, uint32_t max, RxExpViewStats *st) {
    RxExpViewStats local;
    if (!st) st = &local;
    memset(st, 0, sizeof *st);
    if (!s || !fam || RX_EXP_SUBJECT >= s->n_subjects) return 0;
    const CxIdList *l = &s->by_subject[RX_EXP_SUBJECT];
    uint32_t n_rev = 0;
    Revocation *rev = NULL;
    for (uint32_t i = 0; i < l->n; i++) {
        const CxObject *o = cx_get(s, l->ids[i]);
        if (!o || o->kind != CX_K_CONSENT_REVOKED) continue;
        CxObject h;
        const uint64_t *p = payload_n(s, o->id, CX_EVIDENCE, CX_K_CONSENT_REVOKED, RX_EXP_R_WORDS, &h);
        if (!p) continue;   /* a revocation that fails its digest cannot widen eligibility */
        Revocation *nr = realloc(rev, (n_rev + 1) * sizeof *rev);
        if (!nr) { free(rev); return 0; }
        rev = nr;
        rev[n_rev++] = (Revocation){ h.t, p[RX_EXP_R_OWNER], p[RX_EXP_R_TARGET], p[RX_EXP_R_SCOPE] };
    }
    uint32_t n_out = 0;
    for (uint32_t i = 0; i < l->n; i++) {
        const CxObject *o = cx_get(s, l->ids[i]);
        if (!o || o->kind != CX_K_EXPERIENCE) continue;
        st->seen++;
        RxExpRecord r;
        if (rx_exp_decode(s, o->id, &r) != RX_EXP_OK || recheck(s, fam, &r) != 0) {
            st->invalid++;
            continue;
        }
        uint64_t owner = r.w[RX_EXP_W_OWNER], consent = r.w[RX_EXP_W_CONSENT], eff = consent;
        for (uint32_t j = 0; j < n_rev; j++)
            if (rev[j].owner == owner &&
                (rev[j].target == r.id || (rev[j].target == 0 && rev[j].t > r.t)))
                eff &= ~rev[j].scope;
        if ((consent & need) != need) { st->no_consent++; continue; }
        if ((eff & need) != need) { st->revoked++; continue; }
        if (!(r.w[RX_EXP_W_SPLIT] & splits)) { st->split++; continue; }
        st->admitted++;
        if (n_out < max && out) out[n_out++] = r;
    }
    free(rev);
    return n_out;
}
