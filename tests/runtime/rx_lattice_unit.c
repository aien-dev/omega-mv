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
 * rx_lattice unit test (omega-mv #698). AArch64 host: the emitted matvec
 * arms are real machine code, verified in a forked child before use.
 *
 * Time here is MODELED: each dispatched coordinate's duration comes from a
 * fixed synthetic truth table (held-out noise, not the training noise), so
 * schedules are deterministic and replayable. Every output is nevertheless
 * computed for real by the chosen arm and checked independently before its
 * receipt is sealed; the outputs of every policy are compared with each other
 * and with an independent reference-only oracle that does not use the
 * lattice. Measured wall time is rx_lattice_bench's job.
 *
 * Mutants, each on a fresh fixture: cycle, wrong dependencies, stale model
 * revision and precision, illegitimate cached output, privilege escalation,
 * budget starvation, model worker down, self-modified receipt and cost
 * evidence, invalid realization, transaction replay, unknown cost.
 */
#include "rx_lattice_fixture.h"

#include <math.h>

static int g_checks, g_fail;
#define CHECK(cond, ...) do {                                            \
        g_checks++;                                                      \
        if (!(cond)) {                                                   \
            g_fail++;                                                    \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                \
            fprintf(stderr, "\n");                                       \
        }                                                                \
    } while (0)

/* ---- synthetic cost truth (log2 picoseconds) ---- */

static double truth_l2(uint32_t arm, uint32_t M, uint32_t N, uint32_t core) {
    double b = 10.0 + 0.95 * log2((double)M * N + 1.0);
    if (core == RX_CM_CORE_A725) b += 0.6;
    if (arm == ARM_REF) b += 0.3;
    if (arm == ARM_SCALAR) b += 0.5;
    if (arm == ARM_QUAD4) b += core == RX_CM_CORE_OTHER ? -1.6 : -0.4;
    if (arm == ARM_MODEL) b += 3.0;
    return b;
}

static double gauss(uint64_t *s) {
    double u1 = ((double)(fx_xs(s) >> 11) + 1.0) / 9007199254740993.0;
    double u2 = (double)(fx_xs(s) >> 11) / 9007199254740992.0;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

static RxCmFeatures feat(uint32_t M, uint32_t N, uint32_t core, uint64_t cache) {
    RxCmFeatures f;
    memset(&f, 0, sizeof f);
    f.op = RX_CM_OP_MATVEC;
    f.M = M;
    f.N = N;
    f.state_bytes = ((uint64_t)M * N + M + N) * 8u;
    f.cache_bytes = cache;
    f.core = core;
    f.eligible = 0x7;
    f.need_evidence = 1;
    return f;
}

static void train(RxCostModel *m, uint32_t arm_mask) {
    static const uint32_t TM[] = { 16, 64, 128, 256, 512 };
    static const uint32_t TN[] = { 16, 33, 64, 129, 255, 256, 320, 384 };
    static const uint64_t CACHE[RX_CM_CORES] = { 2u << 20, 512u << 10, 1u << 20 };
    uint64_t s = 0x7A11u;
    rx_cm_init(m, N_ARMS);
    for (int rep = 0; rep < 2; rep++)
        for (uint32_t core = 0; core < RX_CM_CORES; core++)
            for (unsigned a = 0; a < sizeof TM / sizeof TM[0]; a++)
                for (unsigned b = 0; b < sizeof TN / sizeof TN[0]; b++)
                    for (uint32_t arm = 0; arm < N_ARMS; arm++) {
                        if (!(arm_mask & (1u << arm))) continue;
                        RxCmFeatures f = feat(TM[a], TN[b], core, CACHE[core]);
                        RxCmObservation o = { (uint64_t)exp2(truth_l2(arm, TM[a], TN[b], core) + 0.1 * gauss(&s)), 0 };
                        rx_cm_observe(m, &f, arm, &o);
                    }
}

/* Held-out duration of one coordinate: its own noise stream, never trained on. */
static uint64_t actual_ps(const RxLattice *L, uint32_t i, uint32_t c) {
    const RxLatNode *a = &L->node[i];
    if (a->op != RX_LAT_OP_MATVEC) return 200000ull + (uint64_t)a->N * 500u;
    const RxLatCand *k = &L->cand[c];
    uint64_t s = 0xD0E5u + i * 1000003ull + c * 7919ull;
    double l2 = truth_l2(L->method[k->method].arm, a->M, a->N, L->real[k->real].core) + 0.15 * gauss(&s);
    return (uint64_t)exp2(l2);
}

/* ---- reuse store: cached vectors addressed by output digest ---- */

static uint64_t st_vec[RX_LAT_MAX_CACHE][FX_MAXLEN];
static uint32_t st_len[RX_LAT_MAX_CACHE];
static uint8_t st_dig[RX_LAT_MAX_CACHE][32];
static uint32_t st_n;

static int st_find(const uint8_t d[32]) {
    for (uint32_t t = 0; t < st_n; t++)
        if (!memcmp(st_dig[t], d, 32)) return (int)t;
    return -1;
}

/* ---- modeled runner ---- */

typedef struct {
    uint64_t makespan_ps;
    uint64_t busy_ps[N_REALS];
    uint32_t max_conc[N_REALS];
    uint32_t arm_runs[N_ARMS];
    uint32_t cache_hits;
    uint8_t status[FX_NODES];
    uint8_t out[FX_NODES][32];
    uint32_t done, failed, refused, weight_switches, receipt_refusals;
    int effect_rc;
    uint64_t chosen_real_mask;
} Outcome;

typedef struct {
    int tamper_node;        /* flip a byte of this node's receipt output after sealing */
    int wrong_cand_node;    /* report a different candidate for this node */
    RxLatCache *cache;
} Hooks;

static const Hooks NO_HOOKS = { -1, -1, NULL };

static int run_modeled(const RxLattice *L, uint32_t policy, RxLatLedger *g, const Hooks *h, Outcome *o) {
    RxLatRun r;
    memset(&r, 0, sizeof r);
    memset(o, 0, sizeof *o);
    o->effect_rc = 1;   /* not attempted */
    if (rx_lat_run_init(L, &r, policy) != RX_LAT_OK) return -1;
    uint64_t fin[FX_NODES], dur[FX_NODES];
    RxLatReceipt rc[FX_NODES];
    uint8_t running[FX_NODES];
    uint32_t conc[N_REALS];
    memset(running, 0, sizeof running);
    memset(conc, 0, sizeof conc);
    for (;;) {
        RxLatPick p;
        while (rx_lat_pick(L, &r, &p)) {
            uint32_t i = p.node;
            const RxLatNode *a = &L->node[i];
            if (++conc[p.real] > o->max_conc[p.real]) o->max_conc[p.real] = conc[p.real];
            o->chosen_real_mask |= 1ull << (p.real * 8u + L->cand[p.cand].method);
            memset(&rc[i], 0, sizeof rc[i]);
            rc[i].node = (uint16_t)i;
            rc[i].cand = (uint16_t)p.cand;
            dur[i] = actual_ps(L, i, p.cand);
            int reused = 0;
            uint8_t key[32];
            if (a->op == RX_LAT_OP_EFFECT) {
                o->effect_rc = rx_lat_effect_commit(g, L, &r, i, rc[i].output);
                rc[i].verdict = o->effect_rc == RX_LAT_OK ? 0u : 1u;
            } else {
                if (h->cache && rx_lat_cache_key(L, &r, i, key) == RX_LAT_OK) {
                    uint8_t d[32];
                    int t = rx_lat_cache_get(h->cache, key, a->scope, a->gen, d) == 0 ? st_find(d) : -1;
                    if (t >= 0) {
                        fx_len[i] = st_len[t];
                        memcpy(fx_y[i], st_vec[t], (size_t)st_len[t] * 8u);
                        for (uint32_t q = 0; q < FX_GUARD; q++) fx_y[i][st_len[t] + q] = FX_CANARY;
                        fx_vec_digest(i, rc[i].output);
                        rc[i].verdict = memcmp(rc[i].output, d, 32) != 0;
                        dur[i] = 100000;   /* lookup */
                        reused = 1;
                        o->cache_hits++;
                    }
                }
                if (!reused) {
                    rc[i].verdict = fx_execute(L, i, p.cand);
                    if (a->op == RX_LAT_OP_MATVEC) o->arm_runs[L->method[L->cand[p.cand].method].arm]++;
                    fx_vec_digest(i, rc[i].output);
                    if (h->cache && rc[i].verdict == 0 && rx_lat_cache_key(L, &r, i, key) == RX_LAT_OK &&
                        st_n < RX_LAT_MAX_CACHE && rx_lat_cache_put(h->cache, key, a->scope, a->gen, rc[i].output) == 0) {
                        memcpy(st_vec[st_n], fx_y[i], (size_t)fx_len[i] * 8u);
                        st_len[st_n] = fx_len[i];
                        memcpy(st_dig[st_n], rc[i].output, 32);
                        st_n++;
                    }
                }
            }
            rc[i].elapsed_ps = dur[i];
            rx_lat_receipt_seal(L, &rc[i]);
            if (h->tamper_node == (int)i) rc[i].output[0] ^= 1u;
            if (h->wrong_cand_node == (int)i) rc[i].cand = (uint16_t)(rc[i].cand + 1u);
            fin[i] = r.now_ps + dur[i];
            running[i] = 1;
        }
        int next = -1;
        for (uint32_t i = 0; i < L->n_nodes; i++)
            if (running[i] && (next < 0 || fin[i] < fin[next])) next = (int)i;
        if (next < 0) break;
        uint32_t i = (uint32_t)next;
        running[i] = 0;
        r.now_ps = fin[i];
        uint32_t k = L->cand[r.chosen[i]].real;
        conc[k]--;
        o->busy_ps[k] += dur[i];
        (void)rx_lat_finish(L, &r, &rc[i]);   /* outcome lands in r.status / counters */
    }
    o->makespan_ps = r.now_ps;
    memcpy(o->status, r.status, sizeof o->status);
    memcpy(o->out, r.out, sizeof o->out);
    o->done = r.done;
    o->failed = r.failed;
    o->refused = r.refused;
    o->weight_switches = r.weight_switches;
    o->receipt_refusals = r.receipt_refusals;
    return rx_lat_settled(L, &r) ? 0 : -2;
}

/* ---- fixture with a trained cost model ---- */

static RxCostModel g_cm;
static RxCmPolicy g_pol;

static int fresh(RxLattice *L, int gpu_sim_verified) {
    fx_build(L, gpu_sim_verified);
    int n = rx_lat_build(L);
    if (n < 0) return n;
    rx_lat_pin(L, &g_cm);
    return rx_lat_admit(L, &g_cm, &g_pol);
}

static uint32_t count_reason(const RxLattice *L, uint32_t method, uint32_t reason) {
    uint32_t n = 0;
    for (uint32_t c = 0; c < L->n_cands; c++)
        if (L->cand[c].method == method && L->cand[c].reason == reason) n++;
    return n;
}

static uint8_t g_base[FX_NODES][32];
static uint8_t g_base_effect[32];

static int same_outputs(const Outcome *o, uint32_t skip_mask) {
    for (uint32_t i = 0; i < FX_NODES; i++) {
        if (skip_mask & (1u << i)) continue;
        if (o->status[i] != RX_LAT_DONE || memcmp(o->out[i], g_base[i], 32)) return 0;
    }
    return 1;
}

/* ---- tests ---- */

static void t_build(void) {
    RxLattice L, L2;
    int empty = fresh(&L, 1);
    /* MATVEC nodes: 11 x (ref 2 + scalar 2 + quad4 3 + planted 1 + model 1) = 99;
     * SOURCE 1, JOIN 4 x 2, EFFECT 1. */
    CHECK(L.n_cands == 99u + 1u + 8u + 1u, "sparse candidate count %u", L.n_cands);
    CHECK(L.n_cands < FX_NODES * N_METHODS * N_REALS, "sparser than the dense cube");
    CHECK(empty == 0, "every node has an admitted candidate (%d empty)", empty);
    int distinct = 1;
    for (uint32_t a = 0; a < L.n_cands; a++)
        for (uint32_t b = a + 1; b < L.n_cands; b++)
            if (!memcmp(L.cand[a].id, L.cand[b].id, 32)) distinct = 0;
    CHECK(distinct, "candidate identities distinct");
    fresh(&L2, 1);
    CHECK(!memcmp(L.cand, L2.cand, sizeof L.cand), "build and admission deterministic");
    CHECK(count_reason(&L, M_PLANTED, RX_LAT_R_UNVERIFIED) == 11, "planted arm refused: unverified");
    CHECK(count_reason(&L, M_MODEL, RX_LAT_R_SEAT) == 7 && count_reason(&L, M_MODEL, RX_LAT_R_SCOPE) == 4,
          "model-call refused: seat down or scope");
    uint32_t bscope = 0;
    for (uint32_t c = 0; c < L.n_cands; c++)
        if (L.cand[c].real == K_GPUSIM && L.node[L.cand[c].node].scope == 2)
            bscope += L.cand[c].reason == RX_LAT_R_SCOPE;
    CHECK(bscope == 4u * 2u, "tenant B never admitted onto tenant A's leased device (%u)", bscope);
    for (uint32_t c = 0; c < L.n_cands; c++)
        if (L.cand[c].reason == RX_LAT_ADMITTED)
            CHECK(L.cand[c].est_ps > 0 && L.cand[c].ucb_ps >= L.cand[c].est_ps, "estimates nonzero, ucb >= mean");
    printf("  build: %u candidates; admitted %u; refused unverified %u seat %u scope %u\n", L.n_cands,
           L.reasons[RX_LAT_ADMITTED], L.reasons[RX_LAT_R_UNVERIFIED], L.reasons[RX_LAT_R_SEAT],
           L.reasons[RX_LAT_R_SCOPE]);
}

static void t_policies(void) {
    RxLattice L;
    fresh(&L, 1);
    uint8_t oracle[FX_NODES][32];
    fx_oracle(&L, oracle);
    Outcome o[RX_LAT_POLICY_COUNT];
    for (uint32_t p = 0; p < RX_LAT_POLICY_COUNT; p++) {
        RxLatLedger g;
        memset(&g, 0, sizeof g);
        CHECK(run_modeled(&L, p, &g, &NO_HOOKS, &o[p]) == 0, "%s settles", rx_lat_policy_name(p));
        CHECK(o[p].done == FX_NODES && o[p].failed == 0 && o[p].refused == 0, "%s completes all", rx_lat_policy_name(p));
        CHECK(g.n == 1 && o[p].effect_rc == RX_LAT_OK, "%s commits the effect once", rx_lat_policy_name(p));
        for (uint32_t k = 0; k < N_REALS; k++)
            CHECK(o[p].max_conc[k] <= L.real[k].lanes, "%s never exceeds %s lanes", rx_lat_policy_name(p), REAL_NAME[k]);
        for (uint32_t i = 0; i < FX_NODES; i++)
            if (i != FX_EFFECT) CHECK(!memcmp(o[p].out[i], oracle[i], 32), "%s node %u equals oracle", rx_lat_policy_name(p), i);
        if (p == 0) {
            memcpy(g_base, o[0].out, sizeof g_base);
            memcpy(g_base_effect, g.value[0], 32);
        } else {
            CHECK(same_outputs(&o[p], 0), "%s outputs equal serial", rx_lat_policy_name(p));
            CHECK(!memcmp(g.value[0], g_base_effect, 32), "%s effect value equals serial", rx_lat_policy_name(p));
        }
    }
    CHECK(o[0].max_conc[K_X925] == 1 && o[0].max_conc[K_A725] == 0 && o[0].max_conc[K_GPUSIM] == 0,
          "serial: one node at a time on the incumbent realization");
    CHECK(o[0].arm_runs[ARM_REF] == 11, "serial runs the incumbent arm only");
    CHECK(o[RX_LAT_POLICY_Q3D].arm_runs[ARM_PLANTED] == 0 && o[RX_LAT_POLICY_LCF].arm_runs[ARM_PLANTED] == 0,
          "planted arm never runs");
    printf("  MODELED time (synthetic truth table, not a measurement):\n");
    printf("    predicted critical path %.1f us\n", (double)rx_lat_critical_path_ps(&L) / 1e6);
    for (uint32_t p = 0; p < RX_LAT_POLICY_COUNT; p++)
        printf("    %-6s makespan %8.1f us  busy x925 %7.1f a725 %7.1f gpu.sim %7.1f us  arms ref %u scalar %u quad4 %u  weight switches %u\n",
               rx_lat_policy_name(p), (double)o[p].makespan_ps / 1e6, (double)o[p].busy_ps[0] / 1e6,
               (double)o[p].busy_ps[1] / 1e6, (double)o[p].busy_ps[2] / 1e6, o[p].arm_runs[ARM_REF],
               o[p].arm_runs[ARM_SCALAR], o[p].arm_runs[ARM_QUAD4], o[p].weight_switches);
}

static void t_cycle(void) {
    RxLattice L;
    fx_build(&L, 1);
    L.node[1].dep[0] = 7;   /* 1 -> 2 -> 6 -> 7 -> 1 */
    CHECK(rx_lat_build(&L) == RX_LAT_E_CYCLE, "cycle refused");
    CHECK(rx_lat_run_init(&L, &(RxLatRun){ 0 }, RX_LAT_POLICY_Q3D) == RX_LAT_E_ARG, "no run without candidates");
    fx_build(&L, 1);
    L.node[3].dep[0] = 3;
    CHECK(rx_lat_build(&L) == RX_LAT_E_CYCLE, "self dependency refused as a cycle");
}

static void t_wrong_deps(void) {
    RxLattice L;
    fx_build(&L, 1);
    L.node[16].dep[1] = FX_EFFECT;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "consuming an effect receipt as a value refused");
    fx_build(&L, 1);
    L.node[9].dep[0] = 40;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "dependency out of range refused");
    fx_build(&L, 1);
    L.node[7].n_deps = 2;
    L.node[7].dep[1] = 1;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "matvec arity refused");
    fx_build(&L, 1);
    L.node[6].dep[1] = 2;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "duplicate dependency refused");
    fx_build(&L, 1);
    L.node[5].N = 321;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "two shapes for one weight id refused");
    fx_build(&L, 1);
    L.node[8].dep[1] = 2;   /* tenant B consumes tenant A's private output */
    CHECK(rx_lat_build(&L) == RX_LAT_E_SCOPE, "cross-tenant flow refused");
    fx_build(&L, 1);
    L.node[12].kind = RX_LAT_KIND_JOIN;
    CHECK(rx_lat_build(&L) == RX_LAT_E_TYPE, "kind / op mismatch refused");
}

static void t_stale(void) {
    RxLattice L;
    fx_build(&L, 1);
    L.method[M_QUAD4].revision = 2;   /* new code, evidence still from revision 1 */
    rx_lat_build(&L);
    rx_lat_pin(&L, &g_cm);
    CHECK(rx_lat_admit(&L, &g_cm, &g_pol) == 0, "stale model: every node still admissible");
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_STALE) == 33, "stale model revision refused");
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0),
          "stale model: same outputs");
    CHECK(o.arm_runs[ARM_QUAD4] == 0, "stale arm never runs");

    fx_build(&L, 1);
    L.method[M_SCALAR].precision = RX_LAT_PREC_APPROX;
    L.real[K_GPUSIM].precisions = 1u << RX_LAT_PREC_APPROX;
    rx_lat_build(&L);
    rx_lat_pin(&L, &g_cm);
    rx_lat_admit(&L, &g_cm, &g_pol);
    CHECK(count_reason(&L, M_SCALAR, RX_LAT_R_PRECISION) == 22, "approximate method refused for exact contract");
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_PRECISION) == 11, "realization without the exact contract refused");
    memset(&g, 0, sizeof g);
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0) &&
          o.arm_runs[ARM_SCALAR] == 0 && o.busy_ps[K_GPUSIM] == 0, "stale precision: same outputs, never placed");
}

static void t_cache(void) {
    RxLattice L;
    fresh(&L, 1);
    RxLatCache c;
    memset(&c, 0, sizeof c);
    st_n = 0;
    Hooks h = { -1, -1, &c };
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &h, &o) == 0 && same_outputs(&o, 0), "cache fill run");
    CHECK(c.n == FX_NODES - 1u && o.cache_hits == 0, "pure nodes cached, effect never (%u)", c.n);
    /* Legitimate reuse: same objective again with a new transaction. */
    L.node[FX_EFFECT].txid = FX_TXID + 1;
    rx_lat_build(&L);
    rx_lat_admit(&L, &g_cm, &g_pol);
    /* The effect value binds the new transaction id, so it is compared by status only. */
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &h, &o) == 0 && same_outputs(&o, 1u << FX_EFFECT) &&
          o.status[FX_EFFECT] == RX_LAT_DONE && g.n == 2, "reuse run");
    CHECK(o.cache_hits == FX_NODES - 1u && o.arm_runs[ARM_REF] + o.arm_runs[ARM_SCALAR] + o.arm_runs[ARM_QUAD4] == 0,
          "every pure node reused, nothing recomputed (%u hits)", o.cache_hits);
    /* Illegitimate entries: tampered output, other tenant, stale generation. */
    c.e[2].output[5] ^= 0x40;              /* third entry (insertion order follows dispatch) */
    for (uint32_t t = 0; t < c.n; t++) {
        if (t == 3) c.e[t].scope ^= 3u;     /* claim another scope */
        if (t == 4) c.e[t].gen += 1;        /* stale generation */
    }
    uint32_t ref0 = c.refusals;
    L.node[FX_EFFECT].txid = FX_TXID + 2;
    rx_lat_build(&L);
    rx_lat_admit(&L, &g_cm, &g_pol);
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &h, &o) == 0 && same_outputs(&o, 1u << FX_EFFECT) &&
          o.status[FX_EFFECT] == RX_LAT_DONE && g.n == 3,
          "illegitimate entries: outputs still equal serial");
    CHECK(c.refusals - ref0 == 3, "three illegitimate entries refused (%u)", c.refusals - ref0);
    CHECK(o.cache_hits == FX_NODES - 1u - 3u, "the rest reused (%u)", o.cache_hits);
    RxLatRun r;
    memset(&r, 0, sizeof r);
    uint8_t key[32];
    CHECK(rx_lat_cache_key(&L, &r, FX_EFFECT, key) == RX_LAT_E_TYPE, "effects have no cache key");
}

static void t_privilege(void) {
    RxLattice L;
    fx_build(&L, 1);
    L.method[M_QUAD4].rights |= RX_LAT_RIGHT_EFFECT;   /* compute method asking for the effect right */
    rx_lat_build(&L);
    rx_lat_pin(&L, &g_cm);
    rx_lat_admit(&L, &g_cm, &g_pol);
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_PRIVILEGE) == 33, "escalating method refused everywhere");
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0) &&
          o.arm_runs[ARM_QUAD4] == 0, "escalating method never runs; outputs equal");
    fx_build(&L, 1);
    L.node[FX_EFFECT].rights = RX_LAT_RIGHT_COMPUTE;
    CHECK(rx_lat_build(&L) == RX_LAT_E_EFFECT, "effect without the EFFECT right refused");
    fresh(&L, 1);
    RxLatRun r;
    memset(&r, 0, sizeof r);
    rx_lat_run_init(&L, &r, RX_LAT_POLICY_Q3D);
    uint8_t v[32];
    CHECK(rx_lat_effect_commit(&g, &L, &r, 7, v) == RX_LAT_E_EFFECT, "compute node cannot commit an effect");
    CHECK(rx_lat_effect_commit(&g, &L, &r, FX_EFFECT, v) == RX_LAT_E_STATE, "effect before its inputs refused");
}

static void t_budget(void) {
    RxLattice L;
    fx_build(&L, 1);
    L.node[10].budget_ps = 1000;   /* 1 ns: nothing can fit */
    rx_lat_build(&L);
    rx_lat_pin(&L, &g_cm);
    CHECK(rx_lat_admit(&L, &g_cm, &g_pol) == 1, "one node left without a candidate");
    for (uint32_t c = L.cand_first[10]; c < (uint32_t)L.cand_first[10] + L.cand_count[10]; c++)
        CHECK(L.cand[c].reason != RX_LAT_ADMITTED, "starved node: nothing admitted");
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0, "settles");
    CHECK(o.status[10] == RX_LAT_REFUSED && o.status[14] == RX_LAT_REFUSED && o.status[FX_EFFECT] == RX_LAT_REFUSED,
          "starved node and its dependents refused");
    CHECK(g.n == 0, "no effect committed on a refused objective");
    CHECK(same_outputs(&o, (1u << 10) | (1u << 14) | (1u << FX_EFFECT)), "independent work completes, equal outputs");
    CHECK(o.done == FX_NODES - 3u, "done %u", o.done);
}

static void t_seat(void) {
    RxLattice L;
    RxCostModel m;
    train(&m, 0x1Fu);   /* evidence for the model-call arm too */
    fx_build(&L, 1);
    L.method[M_MODEL].verified = 1;   /* suppose it were qualified */
    rx_lat_build(&L);
    rx_lat_pin(&L, &m);
    rx_lat_admit(&L, &m, &g_pol);
    CHECK(count_reason(&L, M_MODEL, RX_LAT_R_SEAT) == 7, "worker down: model-call refused (seat)");
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0),
          "worker down: math-only path, same outputs");
    L.seat_up = 1;
    rx_lat_admit(&L, &m, &g_pol);
    CHECK(count_reason(&L, M_MODEL, RX_LAT_ADMITTED) == 7, "seat up: proposal admissible (not run here)");
}

static void t_receipts(void) {
    RxLattice L;
    fresh(&L, 1);
    Outcome o;
    RxLatLedger g = { 0 };
    Hooks h = { 7, -1, NULL };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &h, &o) == 0, "settles");
    CHECK(o.status[7] == RX_LAT_FAILED && o.receipt_refusals == 1, "modified receipt refused");
    CHECK(o.status[10] == RX_LAT_REFUSED && o.status[14] == RX_LAT_REFUSED && g.n == 0,
          "dependents refused, no effect");
    memset(&g, 0, sizeof g);
    Hooks h2 = { -1, 9, NULL };
    CHECK(run_modeled(&L, RX_LAT_POLICY_LCF, &g, &h2, &o) == 0 && o.status[9] == RX_LAT_FAILED,
          "receipt for another candidate refused");
    /* A receipt replayed for a committed node. */
    RxLatRun r;
    memset(&r, 0, sizeof r);
    rx_lat_run_init(&L, &r, RX_LAT_POLICY_SERIAL);
    RxLatPick p;
    CHECK(rx_lat_pick(&L, &r, &p) == 1 && p.node == 0, "serial picks the source");
    RxLatReceipt rc = { .node = 0, .cand = (uint16_t)p.cand };
    fx_execute(&L, 0, p.cand);
    fx_vec_digest(0, rc.output);
    rx_lat_receipt_seal(&L, &rc);
    CHECK(rx_lat_finish(&L, &r, &rc) == RX_LAT_OK, "genuine receipt commits");
    CHECK(rx_lat_finish(&L, &r, &rc) == RX_LAT_E_STATE, "replayed receipt refused");
    /* Self-modified cost evidence after pinning. */
    RxCostModel m = g_cm;
    fx_build(&L, 1);
    rx_lat_build(&L);
    rx_lat_pin(&L, &m);
    RxCmFeatures f = feat(256, 256, RX_CM_CORE_X925, 2u << 20);
    RxCmObservation fake = { 1000, 0 };
    rx_cm_observe(&m, &f, ARM_SCALAR, &fake);
    rx_lat_admit(&L, &m, &g_pol);
    CHECK(count_reason(&L, M_SCALAR, RX_LAT_R_EVIDENCE) == 22 && count_reason(&L, M_QUAD4, RX_LAT_R_EVIDENCE) == 29,
          "changed evidence: non-incumbent proposals refused");
    CHECK(count_reason(&L, M_REF, RX_LAT_R_UNKNOWN_COST) == 22, "incumbent without prediction or cap refused");
    for (uint32_t i = 0; i < FX_NODES; i++)
        if (L.node[i].op == RX_LAT_OP_MATVEC) L.node[i].budget_ps = 400000000ull;   /* 400 us cap */
    rx_lat_admit(&L, &m, &g_pol);
    CHECK(count_reason(&L, M_REF, RX_LAT_ADMITTED) == 22, "with a cap the incumbent runs at its cap");
    memset(&g, 0, sizeof g);
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0) &&
          o.arm_runs[ARM_REF] == 11, "changed evidence: incumbent only, same outputs");
}

static void t_realization(void) {
    RxLattice L;
    fresh(&L, 0);
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_REALIZATION) == 7, "unverified FORGE realization refused (%u)",
          count_reason(&L, M_QUAD4, RX_LAT_R_REALIZATION));
    fx_build(&L, 1);
    rx_lat_build(&L);
    L.real[K_X925].live[0] ^= 1;   /* descriptor changed since the lattice was built */
    rx_lat_pin(&L, &g_cm);
    rx_lat_admit(&L, &g_cm, &g_pol);
    uint32_t bad = 0;
    for (uint32_t c = 0; c < L.n_cands; c++)
        if (L.cand[c].real == K_X925) bad += L.cand[c].reason == RX_LAT_R_REALIZATION ||
                                             L.cand[c].reason == RX_LAT_R_UNVERIFIED;
    CHECK(bad == 11u * 4u + 1u + 4u + 1u, "changed descriptor: every x925 coordinate refused (%u)", bad);
    /* SOURCE and EFFECT exist only on x925: they are refused, so the objective is. */
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && o.done == 0 && g.n == 0,
          "objective refused when its source has no valid realization");
    fresh(&L, 0);
    memset(&g, 0, sizeof g);
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0) &&
          o.busy_ps[K_GPUSIM] == 0, "invalid gpu.sim: CPU only, same outputs");
}

static void t_replay(void) {
    RxLattice L;
    fresh(&L, 1);
    RxLatLedger g = { 0 };
    Outcome o;
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && g.n == 1, "first commit");
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0, "second run settles");
    CHECK(o.effect_rc == RX_LAT_E_REPLAY && g.n == 1 && g.replays == 1 && o.status[FX_EFFECT] == RX_LAT_FAILED,
          "replayed transaction refused, ledger unchanged");
}

static void t_unknown(void) {
    RxLattice L;
    RxCostModel empty;
    rx_cm_init(&empty, N_ARMS);
    fx_build(&L, 1);
    rx_lat_build(&L);
    rx_lat_pin(&L, &empty);
    CHECK(rx_lat_admit(&L, &empty, &g_pol) == 11, "no evidence and no cap: matvec nodes have no candidate");
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_UNKNOWN_COST) == 29, "unknown cost never admitted for a challenger");
    for (uint32_t i = 0; i < FX_NODES; i++)
        if (L.node[i].op == RX_LAT_OP_MATVEC) L.node[i].budget_ps = 400000000ull;
    CHECK(rx_lat_admit(&L, &empty, &g_pol) == 0, "with caps: the incumbent is the selection");
    Outcome o;
    RxLatLedger g = { 0 };
    CHECK(run_modeled(&L, RX_LAT_POLICY_Q3D, &g, &NO_HOOKS, &o) == 0 && same_outputs(&o, 0) &&
          o.arm_runs[ARM_REF] == 11, "unknown cost: incumbent only, same outputs");
}

static void t_failed_arm(void) {
    RxLattice L;
    RxCostModel m = g_cm;
    RxCmFeatures f = feat(64, 64, RX_CM_CORE_X925, 2u << 20);
    RxCmObservation bad = { 0, 1 };
    rx_cm_observe(&m, &f, ARM_QUAD4, &bad);
    fx_build(&L, 1);
    rx_lat_build(&L);
    rx_lat_pin(&L, &m);
    rx_lat_admit(&L, &m, &g_pol);
    CHECK(count_reason(&L, M_QUAD4, RX_LAT_R_FAILED) == 29, "an arm that produced a wrong result is excluded");
}

int main(void) {
    printf("rx_lattice unit (omega-mv #698)\n");
    if (fx_setup_arms() != 0) {
        fprintf(stderr, "arm setup failed\n");
        return 1;
    }
    for (uint32_t a = 0; a < N_ARMS; a++) printf("  arm %-13s %s\n", ARM_NAME[a], fx_verified[a] ? "verified" : "REFUSED");
    CHECK(fx_verified[ARM_REF] && fx_verified[ARM_SCALAR] && fx_verified[ARM_QUAD4], "real arms verified");
    CHECK(!fx_verified[ARM_PLANTED], "planted arm caught by the differential");
    train(&g_cm, (1u << ARM_REF) | (1u << ARM_SCALAR) | (1u << ARM_QUAD4));
    rx_cm_default_policy(&g_pol);
    t_build();
    t_policies();
    t_cycle();
    t_wrong_deps();
    t_stale();
    t_cache();
    t_privilege();
    t_budget();
    t_seat();
    t_receipts();
    t_realization();
    t_replay();
    t_unknown();
    t_failed_arm();
    printf("checks %d failures %d\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
