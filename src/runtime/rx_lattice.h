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
 * rx_lattice.h -- sparse meaning x method x realization candidate lattice
 * (omega-mv #698, SI LATTICE L1).
 *
 * For one bounded typed objective graph, the lattice enumerates the sparse
 * set C[i,j,k]: obligation i, qualified method j, reported realization k.
 * Only combinations declared supported are enumerated. A coordinate is a
 * PROPOSAL. It authorizes nothing: admission records why each coordinate is
 * refused, and the caller still runs work only through its own verified
 * execution path and only after the receipt check here.
 *
 * Cost and uncertainty come only from rx_costmodel (rx_cm_predict): the
 * method's arm, the realization's core class and pressure are its features.
 * There is no second cost model here. An operation rx_costmodel does not
 * cover has no cost prediction, so only its qualified incumbent may run, and
 * its estimate is the obligation's declared work cap, never zero.
 *
 * Like rx_costmodel this is a calculator: it never runs, maps or forks code,
 * never reads or mints authority and does no I/O. Whoever runs the work
 * reports a sealed receipt; the lattice commits a node only when the receipt
 * binds the dispatched candidate and its independent verification passed.
 * Effects are recorded at most once per transaction identity.
 *
 * Three dispatch policies over the same admitted set and the same lanes:
 *   SERIAL  one node at a time in topological order, incumbent method,
 *           first admitted realization of the incumbent;
 *   LCF     dependency-driven list scheduling: the ready node with the
 *           lowest predicted cost first, its lowest predicted-cost admitted
 *           candidate on any free lane;
 *   Q3D     longest remaining predicted path first; the candidate that
 *           minimizes predicted finish time (lane availability + upper
 *           confidence cost); a node waits for a busy lane when that is
 *           predicted to finish first; ties go to the incumbent, then to the
 *           lane already holding the node's weights, then lowest index.
 * A realization offers a fixed number of lanes; a single-owner device has
 * one. Nothing is dispatched to a busy lane, so concurrency never exceeds
 * what the realization declares.
 *
 * Numbering follows rx_graph.h: kinds are AgKind values and graph errors
 * reuse the AG_E_* values.
 */
#ifndef RX_LATTICE_H
#define RX_LATTICE_H

#include "rx_costmodel.h"

#include <stddef.h>
#include <stdint.h>

#define RX_LAT_MAX_NODES   32u
#define RX_LAT_MAX_DEPS     4u
#define RX_LAT_MAX_METHODS RX_CM_ARMS
#define RX_LAT_MAX_REALS    4u
#define RX_LAT_MAX_LANES    4u
#define RX_LAT_MAX_CANDS  256u
#define RX_LAT_MAX_CACHE   64u
#define RX_LAT_MAX_LEDGER  64u
#define RX_LAT_MAX_SCOPES   8u

/* Obligation kinds, rx_graph.h AgKind values. */
#define RX_LAT_KIND_CONST    1u   /* AG_CONST */
#define RX_LAT_KIND_PHYSICAL 7u   /* AG_PHYSICAL */
#define RX_LAT_KIND_EFFECT  10u   /* AG_EFFECT_PERFORM */
#define RX_LAT_KIND_JOIN    13u   /* AG_JOIN */

/* Operation classes. MATVEC is rx_costmodel's RX_CM_OP_MATVEC. */
enum { RX_LAT_OP_MATVEC = RX_CM_OP_MATVEC, RX_LAT_OP_SOURCE, RX_LAT_OP_JOIN, RX_LAT_OP_EFFECT,
       RX_LAT_OP_COUNT };

/* Rights a node is granted by the objective's approval, and a method needs. */
#define RX_LAT_RIGHT_COMPUTE 1u
#define RX_LAT_RIGHT_MODEL   2u
#define RX_LAT_RIGHT_EFFECT  4u

/* Numeric contracts. */
#define RX_LAT_PREC_EXACT_U64 1u
#define RX_LAT_PREC_APPROX    2u

/* Graph errors (rx_graph.h AG_E_* values, then lattice-specific ones). */
#define RX_LAT_OK          0
#define RX_LAT_E_ARG      -1
#define RX_LAT_E_FULL     -2
#define RX_LAT_E_TYPE     -3   /* AG_E_TYPE: wrong dependency, arity, weight shape */
#define RX_LAT_E_CYCLE    -4   /* AG_E_CYCLE */
#define RX_LAT_E_EFFECT   -8   /* AG_E_EFFECT_ORDER: effect not a sink or without EFFECT right */
#define RX_LAT_E_SCOPE   -12   /* data would flow into a less private scope */
#define RX_LAT_E_STATE   -13   /* call out of order */
#define RX_LAT_E_RECEIPT -14   /* receipt does not bind the dispatched candidate */
#define RX_LAT_E_REPLAY  -15   /* effect transaction already committed */
#define RX_LAT_E_REFUSED -16   /* cache entry refused */

/* Admission: 0 admitted, else the first reason found, in this order. */
enum {
    RX_LAT_ADMITTED = 0,
    RX_LAT_R_PRECISION,      /* method or realization lacks the required numeric contract */
    RX_LAT_R_STALE,          /* method revision differs from the revision its evidence measured */
    RX_LAT_R_PRIVILEGE,      /* method needs a right the node was not granted */
    RX_LAT_R_SCOPE,          /* realization may not hold this privacy scope */
    RX_LAT_R_REALIZATION,    /* FORGE realization unverified or descriptor changed */
    RX_LAT_R_SEAT,           /* model-call method with no admitted model seat */
    RX_LAT_R_UNVERIFIED,     /* method has not passed independent verification */
    RX_LAT_R_EVIDENCE,       /* cost-model evidence differs from the pinned digest */
    RX_LAT_R_UNKNOWN_COST,   /* no prediction and not the incumbent (or no cap for it) */
    RX_LAT_R_FAILED,         /* predicted failure probability above policy */
    RX_LAT_R_BUDGET,         /* upper confidence cost above the node's work cap */
    RX_LAT_R_COUNT
};

enum { RX_LAT_PENDING = 0, RX_LAT_RUNNING, RX_LAT_DONE, RX_LAT_FAILED, RX_LAT_REFUSED };
enum { RX_LAT_POLICY_SERIAL = 0, RX_LAT_POLICY_LCF, RX_LAT_POLICY_Q3D, RX_LAT_POLICY_COUNT };

typedef struct {
    uint32_t kind;                  /* RX_LAT_KIND_* */
    uint32_t op;                    /* RX_LAT_OP_* */
    uint32_t M, N;                  /* MATVEC shape; SOURCE/JOIN: N = output length */
    uint32_t weight;                /* MATVEC: shared weight identity (same id, same M x N) */
    uint32_t scope;                 /* 0 public, 1.. tenant-private */
    uint32_t rights;                /* RX_LAT_RIGHT_* granted */
    uint32_t precision;             /* RX_LAT_PREC_* required */
    uint32_t n_deps;
    uint16_t dep[RX_LAT_MAX_DEPS];
    uint64_t budget_ps;             /* work cap; 0 = none */
    uint64_t txid;                  /* EFFECT: transaction identity */
    uint64_t gen;                   /* input generation, binds cache entries */
} RxLatNode;

typedef struct {
    uint32_t op;
    uint32_t arm;                   /* rx_costmodel arm (MATVEC only) */
    uint32_t rights;                /* needed */
    uint32_t precision;
    uint32_t revision;              /* current source / model revision */
    uint32_t evidence_rev;          /* revision the cost evidence was measured with */
    uint32_t needs_seat;            /* model call: needs an admitted model seat */
    uint32_t incumbent;             /* the known qualified default for its op */
    uint32_t verified;              /* passed independent differential verification */
    uint8_t source[32];             /* source digest */
} RxLatMethod;

typedef struct {
    uint32_t core;                  /* RX_CM_CORE_* feature */
    uint32_t lanes;                 /* independent lanes; 1 = single submission owner */
    uint32_t scopes;                /* bit s: may hold scope s */
    uint32_t precisions;            /* bit p: supports RX_LAT_PREC p */
    uint32_t forge_verified;        /* FORGE reported a verified realization */
    uint32_t pressure;              /* RX_CM pressure bucket */
    uint64_t cache_bytes;           /* last private cache, rx_costmodel feature */
    uint8_t descriptor[32];         /* descriptor digest the lattice was built against */
    uint8_t live[32];               /* descriptor digest observed now */
} RxLatReal;

typedef struct {
    uint16_t node;
    uint8_t method, real;
    uint32_t reason;                /* RX_LAT_ADMITTED or RX_LAT_R_* */
    uint32_t n_obs;                 /* observations behind the prediction */
    double mean_log2_ps, sd_log2;   /* rx_cm prediction; 0 when none */
    uint64_t est_ps;                /* 2^mean, or the work cap for an unmodelled incumbent */
    uint64_t ucb_ps;                /* 2^(mean + z sd), or the work cap */
    uint8_t id[32];                 /* SHA-256 of the coordinate's complete identity */
} RxLatCand;

typedef struct {
    uint32_t n_nodes, n_methods, n_reals, n_cands;
    RxLatNode node[RX_LAT_MAX_NODES];
    RxLatMethod method[RX_LAT_MAX_METHODS];
    RxLatReal real[RX_LAT_MAX_REALS];
    uint8_t supported[RX_LAT_MAX_METHODS][RX_LAT_MAX_REALS];
    RxLatCand cand[RX_LAT_MAX_CANDS];
    uint16_t cand_first[RX_LAT_MAX_NODES], cand_count[RX_LAT_MAX_NODES];
    uint16_t topo[RX_LAT_MAX_NODES];        /* a topological order (set by validate) */
    uint32_t seat_up;                        /* a model seat is admitted (Core #735) */
    double z;                                /* upper confidence width, in sd units */
    uint8_t cm_digest[32];                   /* pinned cost-model evidence */
    uint32_t pinned;
    uint32_t reasons[RX_LAT_R_COUNT];        /* candidates per admission outcome */
} RxLattice;

/* A completion report from whoever ran the work. */
typedef struct {
    uint16_t node, cand;
    uint32_t verdict;                       /* 0: independent verification passed */
    uint64_t elapsed_ps;                    /* as measured by the runner */
    uint8_t output[32];                     /* digest of the produced output */
    uint8_t binding[32];                    /* rx_lat_receipt_seal */
} RxLatReceipt;

typedef struct {
    uint32_t node, cand, real, lane;
} RxLatPick;

typedef struct {
    uint32_t policy;
    uint64_t now_ps;                        /* the caller's clock; set before pick/finish */
    uint8_t status[RX_LAT_MAX_NODES];
    uint16_t chosen[RX_LAT_MAX_NODES];
    uint8_t lane_of[RX_LAT_MAX_NODES];
    uint64_t ready_ps[RX_LAT_MAX_NODES], start_ps[RX_LAT_MAX_NODES], finish_ps[RX_LAT_MAX_NODES];
    uint64_t rank_ps[RX_LAT_MAX_NODES];
    uint8_t lane_busy[RX_LAT_MAX_REALS][RX_LAT_MAX_LANES];
    uint64_t lane_until[RX_LAT_MAX_REALS][RX_LAT_MAX_LANES];  /* predicted free time */
    uint32_t lane_weight[RX_LAT_MAX_REALS][RX_LAT_MAX_LANES]; /* resident weight id + 1 */
    uint8_t out[RX_LAT_MAX_NODES][32];      /* committed output digests */
    uint32_t in_flight, done, failed, refused;
    uint32_t dispatched[RX_LAT_MAX_REALS];
    uint32_t weight_switches;
    uint32_t receipt_refusals;
} RxLatRun;

typedef struct {
    uint32_t n;
    uint64_t txid[RX_LAT_MAX_LEDGER];
    uint8_t value[RX_LAT_MAX_LEDGER][32];
    uint32_t replays;
} RxLatLedger;

typedef struct {
    uint8_t key[32];
    uint32_t scope;
    uint64_t gen;
    uint8_t output[32];
    uint8_t binding[32];
} RxLatCacheEntry;

typedef struct {
    uint32_t n;
    RxLatCacheEntry e[RX_LAT_MAX_CACHE];
    uint32_t hits, misses, refusals;
} RxLatCache;

/* ---- building ---- */
void rx_lat_init(RxLattice *L);
int rx_lat_add_node(RxLattice *L, const RxLatNode *n);           /* index or RX_LAT_E_* */
int rx_lat_add_method(RxLattice *L, const RxLatMethod *m);
int rx_lat_add_real(RxLattice *L, const RxLatReal *r);
int rx_lat_support(RxLattice *L, uint32_t method, uint32_t real);

/* Acyclic, dependencies in range and of a consumable type, arity per op,
 * one shape per weight id, no flow into a less private scope, effects are
 * sinks holding the EFFECT right. Sets L->topo. */
int rx_lat_validate(RxLattice *L);

/* Validate, then enumerate the sparse candidate set. Returns its size. */
int rx_lat_build(RxLattice *L);

/* Pin the cost-model evidence the lattice will admit against. */
void rx_lat_pin(RxLattice *L, const RxCostModel *m);

/* Admission and estimates for every candidate. Returns the number of nodes
 * left with no admitted candidate. */
int rx_lat_admit(RxLattice *L, const RxCostModel *m, const RxCmPolicy *p);

/* ---- one run ---- */
int rx_lat_run_init(const RxLattice *L, RxLatRun *r, uint32_t policy);
/* 1: out is dispatched (node RUNNING, lane held); 0: nothing may start now. */
int rx_lat_pick(const RxLattice *L, RxLatRun *r, RxLatPick *out);
/* Verify-before-commit. RX_LAT_OK commits the node; otherwise the node fails
 * and its dependents are refused. */
int rx_lat_finish(const RxLattice *L, RxLatRun *r, const RxLatReceipt *rc);
/* 1 when nothing is pending or running. */
int rx_lat_settled(const RxLattice *L, const RxLatRun *r);

void rx_lat_receipt_seal(const RxLattice *L, RxLatReceipt *rc);

/* Exactly-once effect: requires the node to be an EFFECT obligation with the
 * EFFECT right, RUNNING, every dependency DONE, and a transaction id never
 * committed before. value: SHA-256 of the dependency outputs, in dep order. */
int rx_lat_effect_commit(RxLatLedger *g, const RxLattice *L, const RxLatRun *r, uint32_t node,
                         uint8_t value[32]);

/* Pure-node output reuse. The key covers the node's semantics and its
 * dependency outputs, not the method or realization. A hit requires the same
 * scope, the same generation and an intact binding; anything else is refused. */
int rx_lat_cache_key(const RxLattice *L, const RxLatRun *r, uint32_t node, uint8_t key[32]);
int rx_lat_cache_put(RxLatCache *c, const uint8_t key[32], uint32_t scope, uint64_t gen,
                     const uint8_t output[32]);
/* 0 hit (output filled), 1 miss, RX_LAT_E_REFUSED for an illegitimate entry. */
int rx_lat_cache_get(RxLatCache *c, const uint8_t key[32], uint32_t scope, uint64_t gen,
                     uint8_t output[32]);

/* Longest predicted path from any source to any sink (lower bound on the
 * makespan with unlimited lanes), using each node's best admitted estimate. */
uint64_t rx_lat_critical_path_ps(const RxLattice *L);

const char *rx_lat_reason_name(uint32_t reason);
const char *rx_lat_policy_name(uint32_t policy);

#endif /* RX_LATTICE_H */
