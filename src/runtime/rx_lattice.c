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

/* rx_lattice.c -- see rx_lattice.h. A calculator: no I/O, no code execution. */
#include "rx_lattice.h"
#include "sha256.h"

#include <math.h>
#include <string.h>

/* ---- small helpers ---- */

static void put_u64(sha256_ctx *c, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    sha256_update(c, b, 8);
}

static void put_tag(sha256_ctx *c, const char *tag) {
    sha256_update(c, (const uint8_t *)tag, strlen(tag) + 1u);
}

static uint64_t ps_of_log2(double l2) {
    if (!(l2 > 0.0)) return 1;
    if (l2 >= 63.0) return UINT64_MAX;
    double v = exp2(l2);
    return v < 1.0 ? 1 : (uint64_t)v;
}

static uint64_t arm_failures(const RxCostModel *m, uint32_t arm) {
    uint64_t f = 0;
    for (uint32_t c = 0; c < RX_CM_CELLS; c++) f += m->cell[c][arm].failures;
    return f;
}

static int has_dep(const RxLatNode *n, uint32_t d) {
    for (uint32_t k = 0; k < n->n_deps; k++)
        if (n->dep[k] == d) return 1;
    return 0;
}

/* ---- building ---- */

void rx_lat_init(RxLattice *L) {
    memset(L, 0, sizeof *L);
    L->z = 1.0;
}

int rx_lat_add_node(RxLattice *L, const RxLatNode *n) {
    if (!L || !n) return RX_LAT_E_ARG;
    if (L->n_nodes >= RX_LAT_MAX_NODES) return RX_LAT_E_FULL;
    L->node[L->n_nodes] = *n;
    return (int)L->n_nodes++;
}

int rx_lat_add_method(RxLattice *L, const RxLatMethod *m) {
    if (!L || !m) return RX_LAT_E_ARG;
    if (L->n_methods >= RX_LAT_MAX_METHODS) return RX_LAT_E_FULL;
    if (m->op == RX_LAT_OP_MATVEC && m->arm >= RX_CM_ARMS) return RX_LAT_E_ARG;
    L->method[L->n_methods] = *m;
    return (int)L->n_methods++;
}

int rx_lat_add_real(RxLattice *L, const RxLatReal *r) {
    if (!L || !r) return RX_LAT_E_ARG;
    if (L->n_reals >= RX_LAT_MAX_REALS) return RX_LAT_E_FULL;
    if (r->lanes == 0 || r->lanes > RX_LAT_MAX_LANES) return RX_LAT_E_ARG;
    L->real[L->n_reals] = *r;
    return (int)L->n_reals++;
}

int rx_lat_support(RxLattice *L, uint32_t method, uint32_t real) {
    if (!L || method >= L->n_methods || real >= L->n_reals) return RX_LAT_E_ARG;
    L->supported[method][real] = 1;
    return RX_LAT_OK;
}

static int kind_matches(const RxLatNode *n) {
    switch (n->op) {
    case RX_LAT_OP_MATVEC: return n->kind == RX_LAT_KIND_PHYSICAL;
    case RX_LAT_OP_SOURCE: return n->kind == RX_LAT_KIND_CONST;
    case RX_LAT_OP_JOIN:   return n->kind == RX_LAT_KIND_JOIN;
    case RX_LAT_OP_EFFECT: return n->kind == RX_LAT_KIND_EFFECT;
    default:               return 0;
    }
}

static int arity_ok(const RxLatNode *n) {
    switch (n->op) {
    case RX_LAT_OP_SOURCE: return n->n_deps == 0;
    case RX_LAT_OP_MATVEC: return n->n_deps == 1;
    case RX_LAT_OP_JOIN:   return n->n_deps >= 2;
    case RX_LAT_OP_EFFECT: return n->n_deps >= 1;
    default:               return 0;
    }
}

int rx_lat_validate(RxLattice *L) {
    if (!L || L->n_nodes == 0) return RX_LAT_E_ARG;
    uint32_t n = L->n_nodes;
    /* Types, arity, ranges. A self-dependency is a cycle, reported below. */
    for (uint32_t i = 0; i < n; i++) {
        const RxLatNode *a = &L->node[i];
        if (a->op >= RX_LAT_OP_COUNT || !kind_matches(a) || a->n_deps > RX_LAT_MAX_DEPS ||
            !arity_ok(a) || a->scope >= RX_LAT_MAX_SCOPES || a->precision >= 32u)
            return RX_LAT_E_TYPE;
        if (a->op == RX_LAT_OP_MATVEC && (a->M == 0 || a->N == 0 || a->M > 8192u || a->N > 8192u))
            return RX_LAT_E_TYPE;
        if ((a->op == RX_LAT_OP_SOURCE || a->op == RX_LAT_OP_JOIN) && (a->N == 0 || a->N > 8192u))
            return RX_LAT_E_TYPE;
        for (uint32_t k = 0; k < a->n_deps; k++) {
            uint32_t d = a->dep[k];
            if (d >= n) return RX_LAT_E_TYPE;
            if (d == i) continue;
            for (uint32_t q = 0; q < k; q++)
                if (a->dep[q] == d) return RX_LAT_E_TYPE;
            /* An effect yields a receipt, never a value another node computes on. */
            if (L->node[d].op == RX_LAT_OP_EFFECT) return RX_LAT_E_TYPE;
        }
        if (a->op == RX_LAT_OP_MATVEC)
            for (uint32_t j = 0; j < i; j++) {
                const RxLatNode *b = &L->node[j];
                if (b->op == RX_LAT_OP_MATVEC && b->weight == a->weight && (b->M != a->M || b->N != a->N))
                    return RX_LAT_E_TYPE;
            }
    }
    /* Acyclic: Kahn, lowest index first, so the order is deterministic. */
    uint32_t indeg[RX_LAT_MAX_NODES], placed = 0;
    uint8_t used[RX_LAT_MAX_NODES];
    memset(used, 0, sizeof used);
    for (uint32_t i = 0; i < n; i++) indeg[i] = L->node[i].n_deps;
    while (placed < n) {
        uint32_t pick = n;
        for (uint32_t i = 0; i < n && pick == n; i++)
            if (!used[i] && indeg[i] == 0) pick = i;
        if (pick == n) return RX_LAT_E_CYCLE;
        used[pick] = 1;
        L->topo[placed++] = (uint16_t)pick;
        for (uint32_t s = 0; s < n; s++)
            if (!used[s] && has_dep(&L->node[s], pick)) indeg[s]--;
    }
    /* Privacy: a node may consume public data or data of its own scope. */
    for (uint32_t i = 0; i < n; i++) {
        const RxLatNode *a = &L->node[i];
        for (uint32_t k = 0; k < a->n_deps; k++) {
            uint32_t s = L->node[a->dep[k]].scope;
            if (s != 0 && s != a->scope) return RX_LAT_E_SCOPE;
        }
    }
    /* Effects: explicit EFFECT right and a transaction identity. */
    for (uint32_t i = 0; i < n; i++) {
        const RxLatNode *a = &L->node[i];
        if (a->op == RX_LAT_OP_EFFECT && (!(a->rights & RX_LAT_RIGHT_EFFECT) || a->txid == 0))
            return RX_LAT_E_EFFECT;
    }
    return RX_LAT_OK;
}

static void cand_identity(const RxLattice *L, uint32_t i, uint32_t j, uint32_t k, uint8_t id[32]) {
    const RxLatNode *a = &L->node[i];
    const RxLatMethod *m = &L->method[j];
    const RxLatReal *r = &L->real[k];
    sha256_ctx c;
    sha256_init(&c);
    put_tag(&c, "rx_lattice.candidate.v1");
    put_u64(&c, i);
    put_u64(&c, a->kind);
    put_u64(&c, a->op);
    put_u64(&c, a->M);
    put_u64(&c, a->N);
    put_u64(&c, a->weight);
    put_u64(&c, a->scope);
    put_u64(&c, a->precision);
    put_u64(&c, a->gen);
    put_u64(&c, a->n_deps);
    for (uint32_t d = 0; d < a->n_deps; d++) put_u64(&c, a->dep[d]);
    put_u64(&c, j);
    put_u64(&c, m->op);
    put_u64(&c, m->arm);
    put_u64(&c, m->revision);
    put_u64(&c, m->precision);
    put_u64(&c, m->rights);
    put_u64(&c, m->needs_seat);
    sha256_update(&c, m->source, 32);
    put_u64(&c, k);
    put_u64(&c, r->core);
    put_u64(&c, r->lanes);
    sha256_update(&c, r->descriptor, 32);
    sha256_final(&c, id);
}

int rx_lat_build(RxLattice *L) {
    int rc = rx_lat_validate(L);
    if (rc != RX_LAT_OK) return rc;
    L->n_cands = 0;
    for (uint32_t i = 0; i < L->n_nodes; i++) {
        L->cand_first[i] = (uint16_t)L->n_cands;
        L->cand_count[i] = 0;
        for (uint32_t j = 0; j < L->n_methods; j++) {
            if (L->method[j].op != L->node[i].op) continue;
            for (uint32_t k = 0; k < L->n_reals; k++) {
                if (!L->supported[j][k]) continue;
                if (L->n_cands >= RX_LAT_MAX_CANDS) return RX_LAT_E_FULL;
                RxLatCand *c = &L->cand[L->n_cands++];
                memset(c, 0, sizeof *c);
                c->node = (uint16_t)i;
                c->method = (uint8_t)j;
                c->real = (uint8_t)k;
                c->reason = RX_LAT_R_UNKNOWN_COST;   /* nothing admitted before rx_lat_admit */
                cand_identity(L, i, j, k, c->id);
                L->cand_count[i]++;
            }
        }
    }
    return (int)L->n_cands;
}

void rx_lat_pin(RxLattice *L, const RxCostModel *m) {
    rx_cm_digest(m, L->cm_digest);
    L->pinned = 1;
}

static uint32_t incumbent_arm(const RxLattice *L, uint32_t op) {
    for (uint32_t j = 0; j < L->n_methods; j++)
        if (L->method[j].op == op && L->method[j].incumbent) return L->method[j].arm;
    return 0;
}

static void features(const RxLattice *L, const RxLatNode *a, const RxLatReal *r, RxCmFeatures *f) {
    memset(f, 0, sizeof *f);
    f->op = RX_CM_OP_MATVEC;
    f->M = a->M;
    f->N = a->N;
    f->state_bytes = ((uint64_t)a->M * a->N + a->M + a->N) * 8u;
    f->cache_bytes = r->cache_bytes;
    f->core = r->core;
    f->pressure = r->pressure;
    f->current_arm = incumbent_arm(L, RX_LAT_OP_MATVEC);
    for (uint32_t j = 0; j < L->n_methods; j++)
        if (L->method[j].op == RX_LAT_OP_MATVEC && L->method[j].verified) f->eligible |= 1u << L->method[j].arm;
    f->need_evidence = 1;
    f->latency_budget_ps = a->budget_ps;
}

static uint32_t admit_one(const RxLattice *L, RxLatCand *c, const RxCostModel *m, const RxCmPolicy *p,
                          int evidence_ok) {
    const RxLatNode *a = &L->node[c->node];
    const RxLatMethod *me = &L->method[c->method];
    const RxLatReal *r = &L->real[c->real];
    c->n_obs = 0;
    c->mean_log2_ps = c->sd_log2 = 0.0;
    c->est_ps = c->ucb_ps = 0;
    if (me->precision != a->precision || !(r->precisions & (1u << a->precision))) return RX_LAT_R_PRECISION;
    if (me->revision != me->evidence_rev) return RX_LAT_R_STALE;
    if (me->rights & ~a->rights) return RX_LAT_R_PRIVILEGE;
    if (!(r->scopes & (1u << a->scope))) return RX_LAT_R_SCOPE;
    if (!r->forge_verified || memcmp(r->descriptor, r->live, 32) != 0) return RX_LAT_R_REALIZATION;
    if (me->needs_seat && !L->seat_up) return RX_LAT_R_SEAT;
    if (!me->verified) return RX_LAT_R_UNVERIFIED;
    int have = 0;
    if (a->op == RX_LAT_OP_MATVEC) {
        if (!evidence_ok) {
            if (!me->incumbent) return RX_LAT_R_EVIDENCE;
        } else {
            RxCmFeatures f;
            RxCmPrediction pr;
            features(L, a, r, &f);
            rx_cm_predict(m, &f, me->arm, &pr);
            if (arm_failures(m, me->arm) > 0 && pr.fail_p > p->max_fail_p) return RX_LAT_R_FAILED;
            if (pr.n > 0) {
                have = 1;
                c->n_obs = pr.n;
                c->mean_log2_ps = pr.mean_log2_ps;
                c->sd_log2 = pr.sd_log2;
            }
        }
    }
    if (have) {
        c->est_ps = ps_of_log2(c->mean_log2_ps);
        c->ucb_ps = ps_of_log2(c->mean_log2_ps + L->z * c->sd_log2);
    } else if (me->incumbent && a->budget_ps) {
        /* No prediction: the incumbent may run, charged at its cap, never zero. */
        c->est_ps = c->ucb_ps = a->budget_ps;
    } else {
        return RX_LAT_R_UNKNOWN_COST;
    }
    if (a->budget_ps && c->ucb_ps > a->budget_ps) return RX_LAT_R_BUDGET;
    return RX_LAT_ADMITTED;
}

int rx_lat_admit(RxLattice *L, const RxCostModel *m, const RxCmPolicy *p) {
    uint8_t now[32];
    rx_cm_digest(m, now);
    int evidence_ok = L->pinned && memcmp(now, L->cm_digest, 32) == 0;
    memset(L->reasons, 0, sizeof L->reasons);
    for (uint32_t c = 0; c < L->n_cands; c++) {
        L->cand[c].reason = admit_one(L, &L->cand[c], m, p, evidence_ok);
        L->reasons[L->cand[c].reason]++;
    }
    int empty = 0;
    for (uint32_t i = 0; i < L->n_nodes; i++) {
        int any = 0;
        for (uint32_t c = L->cand_first[i]; c < (uint32_t)L->cand_first[i] + L->cand_count[i]; c++)
            if (L->cand[c].reason == RX_LAT_ADMITTED) any = 1;
        if (!any) empty++;
    }
    return empty;
}

/* ---- one run ---- */

static int admitted(const RxLattice *L, uint32_t c) {
    return L->cand[c].reason == RX_LAT_ADMITTED;
}

static uint64_t best_est(const RxLattice *L, uint32_t i) {
    uint64_t b = 0;
    for (uint32_t c = L->cand_first[i]; c < (uint32_t)L->cand_first[i] + L->cand_count[i]; c++)
        if (admitted(L, c) && (b == 0 || L->cand[c].est_ps < b)) b = L->cand[c].est_ps;
    return b;
}

static int incumbent_cand(const RxLattice *L, uint32_t i) {
    for (uint32_t c = L->cand_first[i]; c < (uint32_t)L->cand_first[i] + L->cand_count[i]; c++)
        if (admitted(L, c) && L->method[L->cand[c].method].incumbent) return (int)c;
    return -1;
}

static void propagate(const RxLattice *L, RxLatRun *r) {
    for (uint32_t t = 0; t < L->n_nodes; t++) {
        uint32_t i = L->topo[t];
        if (r->status[i] != RX_LAT_PENDING) continue;
        for (uint32_t k = 0; k < L->node[i].n_deps; k++) {
            uint8_t s = r->status[L->node[i].dep[k]];
            if (s == RX_LAT_FAILED || s == RX_LAT_REFUSED) {
                r->status[i] = RX_LAT_REFUSED;
                r->refused++;
                break;
            }
        }
    }
}

static int deps_done(const RxLattice *L, const RxLatRun *r, uint32_t i) {
    for (uint32_t k = 0; k < L->node[i].n_deps; k++)
        if (r->status[L->node[i].dep[k]] != RX_LAT_DONE) return 0;
    return 1;
}

int rx_lat_run_init(const RxLattice *L, RxLatRun *r, uint32_t policy) {
    if (!L || !r || policy >= RX_LAT_POLICY_COUNT || L->n_cands == 0) return RX_LAT_E_ARG;
    uint64_t now = r->now_ps;
    memset(r, 0, sizeof *r);
    r->policy = policy;
    r->now_ps = now;
    for (uint32_t i = 0; i < L->n_nodes; i++) {
        int ok = policy == RX_LAT_POLICY_SERIAL ? incumbent_cand(L, i) >= 0 : best_est(L, i) > 0;
        if (!ok) {
            r->status[i] = RX_LAT_REFUSED;
            r->refused++;
        }
    }
    propagate(L, r);
    /* Upward rank: own best estimate plus the longest successor rank. */
    for (uint32_t t = L->n_nodes; t-- > 0;) {
        uint32_t i = L->topo[t];
        uint64_t succ = 0;
        for (uint32_t s = 0; s < L->n_nodes; s++)
            if (has_dep(&L->node[s], i) && r->rank_ps[s] > succ) succ = r->rank_ps[s];
        r->rank_ps[i] = r->status[i] == RX_LAT_REFUSED ? 0 : best_est(L, i) + succ;
    }
    for (uint32_t i = 0; i < L->n_nodes; i++) r->ready_ps[i] = now;
    for (uint32_t k = 0; k < L->n_reals; k++)
        for (uint32_t l = 0; l < RX_LAT_MAX_LANES; l++) r->lane_until[k][l] = now;
    return RX_LAT_OK;
}

static uint64_t real_avail(const RxLattice *L, const RxLatRun *r, uint32_t k) {
    uint64_t best = UINT64_MAX;
    for (uint32_t l = 0; l < L->real[k].lanes; l++) {
        uint64_t t = r->lane_busy[k][l] && r->lane_until[k][l] > r->now_ps ? r->lane_until[k][l] : r->now_ps;
        if (t < best) best = t;
    }
    return best;
}

/* A free lane of k, preferring one that already holds the weights. -1 if none. */
static int free_lane(const RxLattice *L, const RxLatRun *r, uint32_t k, uint32_t weight_tag) {
    int any = -1;
    for (uint32_t l = 0; l < L->real[k].lanes; l++) {
        if (r->lane_busy[k][l]) continue;
        if (weight_tag && r->lane_weight[k][l] == weight_tag) return (int)l;
        if (any < 0) any = (int)l;
    }
    return any;
}

static uint32_t weight_tag(const RxLatNode *a) {
    return a->op == RX_LAT_OP_MATVEC ? a->weight + 1u : 0u;
}

static int has_resident(const RxLattice *L, const RxLatRun *r, uint32_t k, uint32_t tag) {
    if (!tag) return 0;
    for (uint32_t l = 0; l < L->real[k].lanes; l++)
        if (!r->lane_busy[k][l] && r->lane_weight[k][l] == tag) return 1;
    return 0;
}

static void dispatch(const RxLattice *L, RxLatRun *r, uint32_t i, uint32_t c, uint32_t lane, RxLatPick *out) {
    uint32_t k = L->cand[c].real;
    uint32_t tag = weight_tag(&L->node[i]);
    r->status[i] = RX_LAT_RUNNING;
    r->chosen[i] = (uint16_t)c;
    r->lane_of[i] = (uint8_t)lane;
    r->start_ps[i] = r->now_ps;
    r->lane_busy[k][lane] = 1;
    uint64_t span = r->policy == RX_LAT_POLICY_Q3D ? L->cand[c].ucb_ps : L->cand[c].est_ps;
    r->lane_until[k][lane] = r->now_ps + span;
    if (tag && r->lane_weight[k][lane] != tag) {
        r->weight_switches++;
        r->lane_weight[k][lane] = tag;
    }
    r->in_flight++;
    r->dispatched[k]++;
    out->node = i;
    out->cand = c;
    out->real = k;
    out->lane = lane;
}

static int pick_serial(const RxLattice *L, RxLatRun *r, RxLatPick *out) {
    if (r->in_flight) return 0;
    for (uint32_t t = 0; t < L->n_nodes; t++) {
        uint32_t i = L->topo[t];
        if (r->status[i] != RX_LAT_PENDING || !deps_done(L, r, i)) continue;
        int c = incumbent_cand(L, i);
        int lane = c < 0 ? -1 : free_lane(L, r, L->cand[c].real, weight_tag(&L->node[i]));
        if (lane < 0) continue;
        dispatch(L, r, i, (uint32_t)c, (uint32_t)lane, out);
        return 1;
    }
    return 0;
}

static int pick_lcf(const RxLattice *L, RxLatRun *r, RxLatPick *out) {
    int bi = -1, bc = -1;
    uint64_t be = 0;
    for (uint32_t t = 0; t < L->n_nodes; t++) {
        uint32_t i = L->topo[t];
        if (r->status[i] != RX_LAT_PENDING || !deps_done(L, r, i)) continue;
        int nc = -1;
        for (uint32_t c = L->cand_first[i]; c < (uint32_t)L->cand_first[i] + L->cand_count[i]; c++) {
            if (!admitted(L, c) || free_lane(L, r, L->cand[c].real, 0) < 0) continue;
            if (nc < 0 || L->cand[c].est_ps < L->cand[nc].est_ps) nc = (int)c;
        }
        if (nc < 0) continue;
        if (bi < 0 || L->cand[nc].est_ps < be) {
            bi = (int)i;
            bc = nc;
            be = L->cand[nc].est_ps;
        }
    }
    if (bi < 0) return 0;
    uint32_t i = (uint32_t)bi, c = (uint32_t)bc;
    dispatch(L, r, i, c, (uint32_t)free_lane(L, r, L->cand[c].real, weight_tag(&L->node[i])), out);
    return 1;
}

/* Q3D: is candidate a better than b for node i? */
static int q3d_better(const RxLattice *L, const RxLatRun *r, uint32_t a, uint32_t b, uint32_t tag) {
    uint64_t fa = real_avail(L, r, L->cand[a].real) + L->cand[a].ucb_ps;
    uint64_t fb = real_avail(L, r, L->cand[b].real) + L->cand[b].ucb_ps;
    if (fa != fb) return fa < fb;
    int ia = L->method[L->cand[a].method].incumbent, ib = L->method[L->cand[b].method].incumbent;
    if (ia != ib) return ia > ib;
    int ra = has_resident(L, r, L->cand[a].real, tag), rb = has_resident(L, r, L->cand[b].real, tag);
    if (ra != rb) return ra > rb;
    return a < b;
}

static int pick_q3d(const RxLattice *L, RxLatRun *r, RxLatPick *out) {
    uint8_t seen[RX_LAT_MAX_NODES];
    memset(seen, 0, sizeof seen);
    for (;;) {
        /* Highest rank among unseen ready nodes; topological position breaks ties. */
        int bi = -1;
        for (uint32_t t = 0; t < L->n_nodes; t++) {
            uint32_t i = L->topo[t];
            if (seen[i] || r->status[i] != RX_LAT_PENDING || !deps_done(L, r, i)) continue;
            if (bi < 0 || r->rank_ps[i] > r->rank_ps[bi]) bi = (int)i;
        }
        if (bi < 0) return 0;
        uint32_t i = (uint32_t)bi;
        seen[i] = 1;
        uint32_t tag = weight_tag(&L->node[i]);
        int bc = -1;
        for (uint32_t c = L->cand_first[i]; c < (uint32_t)L->cand_first[i] + L->cand_count[i]; c++)
            if (admitted(L, c) && (bc < 0 || q3d_better(L, r, c, (uint32_t)bc, tag))) bc = (int)c;
        if (bc < 0) continue;
        int lane = free_lane(L, r, L->cand[bc].real, tag);
        if (lane < 0) continue;   /* predicted to finish sooner by waiting for this lane */
        dispatch(L, r, i, (uint32_t)bc, (uint32_t)lane, out);
        return 1;
    }
}

int rx_lat_pick(const RxLattice *L, RxLatRun *r, RxLatPick *out) {
    if (!L || !r || !out) return 0;
    switch (r->policy) {
    case RX_LAT_POLICY_SERIAL: return pick_serial(L, r, out);
    case RX_LAT_POLICY_LCF:    return pick_lcf(L, r, out);
    case RX_LAT_POLICY_Q3D:    return pick_q3d(L, r, out);
    default:                   return 0;
    }
}

static void binding_of(const RxLattice *L, const RxLatReceipt *rc, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    put_tag(&c, "rx_lattice.receipt.v1");
    put_u64(&c, rc->node);
    put_u64(&c, rc->cand);
    if (rc->cand < L->n_cands) sha256_update(&c, L->cand[rc->cand].id, 32);
    put_u64(&c, rc->verdict);
    put_u64(&c, rc->elapsed_ps);
    sha256_update(&c, rc->output, 32);
    sha256_final(&c, out);
}

void rx_lat_receipt_seal(const RxLattice *L, RxLatReceipt *rc) {
    binding_of(L, rc, rc->binding);
}

int rx_lat_finish(const RxLattice *L, RxLatRun *r, const RxLatReceipt *rc) {
    if (!L || !r || !rc || rc->node >= L->n_nodes) return RX_LAT_E_ARG;
    uint32_t i = rc->node;
    if (r->status[i] != RX_LAT_RUNNING) return RX_LAT_E_STATE;
    uint32_t c = r->chosen[i], k = L->cand[c].real, l = r->lane_of[i];
    r->lane_busy[k][l] = 0;
    r->lane_until[k][l] = r->now_ps;
    r->in_flight--;
    r->finish_ps[i] = r->now_ps;
    uint8_t want[32];
    binding_of(L, rc, want);
    int bound = rc->cand == c && memcmp(want, rc->binding, 32) == 0;
    if (!bound || rc->verdict != 0) {
        if (!bound) r->receipt_refusals++;
        r->status[i] = RX_LAT_FAILED;
        r->failed++;
        propagate(L, r);
        return bound ? 1 : RX_LAT_E_RECEIPT;
    }
    r->status[i] = RX_LAT_DONE;
    memcpy(r->out[i], rc->output, 32);
    r->done++;
    for (uint32_t s = 0; s < L->n_nodes; s++)
        if (r->status[s] == RX_LAT_PENDING && has_dep(&L->node[s], i) && deps_done(L, r, s))
            r->ready_ps[s] = r->now_ps;
    return RX_LAT_OK;
}

int rx_lat_settled(const RxLattice *L, const RxLatRun *r) {
    for (uint32_t i = 0; i < L->n_nodes; i++)
        if (r->status[i] == RX_LAT_PENDING || r->status[i] == RX_LAT_RUNNING) return 0;
    return 1;
}

int rx_lat_effect_commit(RxLatLedger *g, const RxLattice *L, const RxLatRun *r, uint32_t node,
                         uint8_t value[32]) {
    if (!g || !L || !r || node >= L->n_nodes) return RX_LAT_E_ARG;
    const RxLatNode *a = &L->node[node];
    if (a->op != RX_LAT_OP_EFFECT || !(a->rights & RX_LAT_RIGHT_EFFECT)) return RX_LAT_E_EFFECT;
    if (r->status[node] != RX_LAT_RUNNING || !deps_done(L, r, node)) return RX_LAT_E_STATE;
    for (uint32_t t = 0; t < g->n; t++)
        if (g->txid[t] == a->txid) {
            g->replays++;
            return RX_LAT_E_REPLAY;
        }
    if (g->n >= RX_LAT_MAX_LEDGER) return RX_LAT_E_FULL;
    sha256_ctx c;
    sha256_init(&c);
    put_tag(&c, "rx_lattice.effect.v1");
    put_u64(&c, a->txid);
    for (uint32_t k = 0; k < a->n_deps; k++) sha256_update(&c, r->out[a->dep[k]], 32);
    sha256_final(&c, value);
    g->txid[g->n] = a->txid;
    memcpy(g->value[g->n], value, 32);
    g->n++;
    return RX_LAT_OK;
}

int rx_lat_cache_key(const RxLattice *L, const RxLatRun *r, uint32_t node, uint8_t key[32]) {
    if (!L || !r || node >= L->n_nodes) return RX_LAT_E_ARG;
    const RxLatNode *a = &L->node[node];
    if (a->op == RX_LAT_OP_EFFECT) return RX_LAT_E_TYPE;   /* effects are never reused */
    sha256_ctx c;
    sha256_init(&c);
    put_tag(&c, "rx_lattice.cache.v1");
    put_u64(&c, a->op);
    put_u64(&c, a->M);
    put_u64(&c, a->N);
    put_u64(&c, a->weight);
    put_u64(&c, a->precision);
    for (uint32_t k = 0; k < a->n_deps; k++) sha256_update(&c, r->out[a->dep[k]], 32);
    sha256_final(&c, key);
    return RX_LAT_OK;
}

static void cache_binding(const RxLatCacheEntry *e, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    put_tag(&c, "rx_lattice.cache.entry.v1");
    sha256_update(&c, e->key, 32);
    put_u64(&c, e->scope);
    put_u64(&c, e->gen);
    sha256_update(&c, e->output, 32);
    sha256_final(&c, out);
}

int rx_lat_cache_put(RxLatCache *c, const uint8_t key[32], uint32_t scope, uint64_t gen,
                     const uint8_t output[32]) {
    if (!c) return RX_LAT_E_ARG;
    if (c->n >= RX_LAT_MAX_CACHE) return RX_LAT_E_FULL;
    RxLatCacheEntry *e = &c->e[c->n++];
    memcpy(e->key, key, 32);
    e->scope = scope;
    e->gen = gen;
    memcpy(e->output, output, 32);
    cache_binding(e, e->binding);
    return RX_LAT_OK;
}

int rx_lat_cache_get(RxLatCache *c, const uint8_t key[32], uint32_t scope, uint64_t gen,
                     uint8_t output[32]) {
    if (!c) return RX_LAT_E_ARG;
    int found = 0;
    for (uint32_t t = 0; t < c->n; t++) {
        const RxLatCacheEntry *e = &c->e[t];
        if (memcmp(e->key, key, 32) != 0) continue;
        found = 1;
        uint8_t b[32];
        cache_binding(e, b);
        if (e->scope == scope && e->gen == gen && memcmp(b, e->binding, 32) == 0) {
            memcpy(output, e->output, 32);
            c->hits++;
            return 0;
        }
    }
    if (found) {
        c->refusals++;
        return RX_LAT_E_REFUSED;
    }
    c->misses++;
    return 1;
}

uint64_t rx_lat_critical_path_ps(const RxLattice *L) {
    uint64_t fin[RX_LAT_MAX_NODES], cp = 0;
    for (uint32_t t = 0; t < L->n_nodes; t++) {
        uint32_t i = L->topo[t];
        uint64_t start = 0;
        for (uint32_t k = 0; k < L->node[i].n_deps; k++)
            if (fin[L->node[i].dep[k]] > start) start = fin[L->node[i].dep[k]];
        fin[i] = start + best_est(L, i);
        if (fin[i] > cp) cp = fin[i];
    }
    return cp;
}

const char *rx_lat_reason_name(uint32_t reason) {
    static const char *const names[RX_LAT_R_COUNT] = {
        "admitted", "precision", "stale", "privilege", "scope", "realization", "seat",
        "unverified", "evidence", "unknown_cost", "failed", "budget",
    };
    return reason < RX_LAT_R_COUNT ? names[reason] : "?";
}

const char *rx_lat_policy_name(uint32_t policy) {
    static const char *const names[RX_LAT_POLICY_COUNT] = { "serial", "lcf", "q3d" };
    return policy < RX_LAT_POLICY_COUNT ? names[policy] : "?";
}
