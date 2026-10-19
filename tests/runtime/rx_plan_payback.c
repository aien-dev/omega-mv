/*
 * OMEGA_PLAN_REUSE, E03 extension (aien-architecture#246, "Hardware-first
 * learning extension", 2026-10-19): does retained experience repay its full
 * cost? Lifecycle payback, retention and reinvestment accounting on the
 * EXISTING rx_plan cache. Protocol: evidence/PLAN_REUSE/e03-payback/00-frozen-protocol.md.
 *
 * This program drives only existing paths (rx_plan.c, rx_plan_arrange.c); it
 * builds no second cache and changes no gate. The World scaffolding (one
 * World and one native AIENOS authority per instance) is the one of
 * tests/runtime/rx_plan_reuse.c, kept identical so the two tests measure the
 * same thing.
 *
 * Two arms on one task stream:
 *   A  no reuse: plan with A* every time, execute, verify (never touches the cache)
 *   B  existing reuse: retrieve, bind, applicability, execute, verify, record;
 *      on a miss plan, generalise, identify, store, publish the index
 * Every task writes one trace row per arm; the ledger is the per-phase sum of
 * the rows and must reconcile with them exactly.
 *
 * Modes: `dev` runs the development partition and writes the prediction;
 * `heldout` runs the frozen held-out streams and writes the receipt.
 * PAYBACK_MUTANT=M1|M2|M3 breaks one check on purpose (see protocol); a
 * mutant run must report bad rows > 0 or the check cannot fail.
 */
#include "runtime/aienos_cap.h"
#include "runtime/rx_plan.h"
#include "runtime/rx_plan_arrange.h"
#include "runtime/rx_world.h"
#include "omega_evidence.h"
#include "sha256.h"

#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

enum { SUBJ_EXTERNAL = 100, ISSUER = 3, SUBJ_PLAN = 71 };

#define RES_CELL     0xB000001ull
#define RES_RUN      0xB000002ull
#define RES_DECISION 0xB000003ull
#define RES_INDEX    0xB000004ull
#define RES_ENV      0xB000005ull
#define RES_UNIT     0xB000100ull

#define WORLD_GEN    7u
#define COG_GEN      3u
#define MAX_EXPAND   2000000u

/* Frozen horizons (protocol). */
#define H_DEV        12u
#define H_HELD       40u
#define SEED_DEV     200u
#define SEED_HELD    300u
#define MAX_ROWS     4096u

static int g_checks, g_fail, g_bad_rows, g_no_adapt;
static const char *g_mutant = "";

#define CHECK(cond, ...) do {                                            \
        g_checks++;                                                      \
        if (!(cond)) {                                                   \
            g_fail++;                                                    \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                \
            fputc('\n', stderr);                                         \
        }                                                                \
    } while (0)

static int mutant(const char *m) { return strcmp(g_mutant, m) == 0; }

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t cpu_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void loadavg(char *out, size_t n) {
    FILE *f = fopen("/proc/loadavg", "r");
    strcpy(out, "unavailable");
    if (!f) return;
    if (fgets(out, (int)n, f)) { char *e = strchr(out, '\n'); if (e) *e = 0; }
    fclose(f);
}

/* ---- environment (identical to rx_plan_reuse.c) ---- */

enum { SHAPE_REVERSE = 0, SHAPE_MERGE = 1, SHAPE_OTHER = 2 };

typedef enum {
    PERT_NONE = 0, PERT_BINDING, PERT_GOAL, PERT_WORLD_GEN, PERT_COG_GEN, PERT_ENV,
    PERT_STATE_SWAP, PERT_STATE_OUTSIDER, PERT_AUTHORITY, PERT_RESOURCES
} Pert;

typedef struct {
    uint32_t shape, seed;
    Pert pert;
    uint64_t world_gen, cog_gen, max_steps;
} Spec;

typedef struct {
    AienosCapAdmin *admin;
    AienosCapView *view;
    RxWorld w;
    PlEnv env;
    RxObjRef decision, index;
    uint32_t n_units, n_goal_units;
    RxObjRef unit[PLA_MAX_UNITS];
    RxCapRef unit_cap[PLA_MAX_UNITS];
    RxCapRef ext_unit[PLA_MAX_UNITS];
    RxCapRef ext_run, ext_decision, ext_index;
    AgCapTable caps;
    PlExecCtx x;
    PlRunStore rs;
    uint64_t seq;
} Env;

static AgSkillTable g_skills;
static uint64_t g_core_class;

static RxCapRef mint(Env *e, uint32_t subject, uint64_t resource, uint32_t rights) {
    AienosCapRef office;
    aienos_cap_office(e->admin, &office);
    AienosCapMint m = { ISSUER, subject, resource, rights, 0, { UINT32_MAX, 0 }, office };
    AienosCapRef r = { UINT32_MAX, 0 };
    if (aienos_cap_mint(e->admin, &m, &r) != 0) r = (AienosCapRef){ UINT32_MAX, 0 };
    return (RxCapRef){ r.cap_id, r.generation };
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static int put_unit(Env *e, uint32_t k, const RxObjRef *on) {
    RxMutation m[2] = { { e->unit[k], PLA_F_SUPPORT, on ? pl_ref_pack(*on) : 0 },
                        { e->unit[k], PLA_F_SELF, pl_ref_pack(e->unit[k]) } };
    return rx_world_publish_external(&e->w, e->ext_unit[k], m, 2) > 0 ? 0 : -1;
}

static uint32_t goal_units(uint32_t shape) { return shape == SHAPE_OTHER ? 5 : 6; }

static int env_build(Env *e, const Spec *sp) {
    memset(e, 0, sizeof *e);
    if (aienos_cap_start(&e->admin, &e->view) != 0) return -1;
    if (rx_world_init_native(&e->w, e->view, 4, 1u << 16) != RX_OK) return -1;
    e->w.external_subject = SUBJ_EXTERNAL;
    const uint32_t R_ = RX_RIGHT_READ, W = RX_RIGHT_WRITE, RW = R_ | W;
    uint32_t seed = sp->seed * 2654435761u + 17u;

    uint32_t nf = 1 + lcg(&seed) % 9;
    RxObjRef fill[16];
    for (uint32_t i = 0; i < nf; i++)
        if (rx_world_create(&e->w, 0x5B0F, RX_PERSIST_EPHEMERAL, RES_ENV, NULL, &fill[i]) != RX_OK)
            return -1;
    for (uint32_t i = 0; i < nf; i += 2) rx_world_retire(&e->w, fill[i]);

    e->n_goal_units = goal_units(sp->shape);
    e->n_units = e->n_goal_units + 4;
    uint32_t order[PLA_MAX_UNITS];
    for (uint32_t k = 0; k < e->n_units; k++) order[k] = k;
    for (uint32_t k = e->n_units; k > 1; k--) {
        uint32_t j = lcg(&seed) % k, t = order[k - 1];
        order[k - 1] = order[j];
        order[j] = t;
    }
    for (uint32_t i = 0; i < e->n_units; i++) {
        uint32_t k = order[i];
        uint64_t f[RX_MAX_FIELDS] = { 0, 0x100u + k };
        if (rx_world_create(&e->w, PL_OT_UNIT, RX_PERSIST_RESIDENT, RES_UNIT + k, f, &e->unit[k]) != RX_OK)
            return -1;
    }
    for (uint32_t k = 0; k < e->n_units; k++) e->ext_unit[k] = mint(e, SUBJ_EXTERNAL, RES_UNIT + k, W);

    const RxObjRef *U = e->unit;
    uint32_t g = e->n_goal_units;
    if (sp->shape == SHAPE_MERGE) {
        put_unit(e, 0, NULL); put_unit(e, 1, &U[0]); put_unit(e, 2, &U[1]);
        put_unit(e, 3, NULL); put_unit(e, 4, &U[3]); put_unit(e, 5, &U[4]);
    } else {
        put_unit(e, 0, NULL);
        for (uint32_t k = 1; k < g; k++) put_unit(e, k, &U[k - 1]);
    }
    put_unit(e, g, NULL); put_unit(e, g + 1, &U[g]); put_unit(e, g + 2, NULL); put_unit(e, g + 3, NULL);
    uint32_t top = sp->shape == SHAPE_MERGE ? 2 : g - 1;
    if (sp->pert == PERT_STATE_SWAP) {
        put_unit(e, top, &U[top - 2]);
        put_unit(e, top - 1, &U[top]);
    }
    if (sp->pert == PERT_STATE_OUTSIDER) put_unit(e, g + 2, &U[top]);

    uint64_t facts[PLA_MAX_FACTS] = { 0 };
    uint32_t nfct = 0;
    if (sp->shape == SHAPE_MERGE) {
        uint32_t seqv[6] = { 0, 3, 1, 4, 2, 5 };
        facts[nfct++] = pla_fact(U[seqv[0]], NULL);
        for (uint32_t i = 1; i < 6; i++) facts[nfct++] = pla_fact(U[seqv[i]], &U[seqv[i - 1]]);
    } else {
        facts[nfct++] = pla_fact(U[g - 1], NULL);
        for (uint32_t k = 0; k + 1 < g; k++) facts[nfct++] = pla_fact(U[k], &U[k + 1]);
    }
    for (uint32_t k = nfct; k > 1; k--) {
        uint32_t j = lcg(&seed) % k;
        uint64_t t = facts[k - 1];
        facts[k - 1] = facts[j];
        facts[j] = t;
    }
    if (sp->pert == PERT_BINDING) {
        uint32_t k = g - 1;
        RxObjRef old = U[k];
        uint64_t f[RX_MAX_FIELDS] = { 0, 0x100u + k };
        if (rx_world_retire(&e->w, old) != RX_OK ||
            rx_world_create(&e->w, PL_OT_UNIT, RX_PERSIST_RESIDENT, RES_UNIT + k, f, &e->unit[k]) != RX_OK)
            return -1;
        put_unit(e, k, &U[k - 1]);
    }

    uint64_t z[RX_MAX_FIELDS] = { 0 };
    uint64_t wg[RX_MAX_FIELDS] = { sp->world_gen };
    uint64_t cg[RX_MAX_FIELDS] = { sp->cog_gen, 0xA1E0 };
    uint64_t st[RX_MAX_FIELDS] = { sp->pert == PERT_ENV ? 0u : 1u };
    RxObjRef fr;
    if (rx_world_create(&e->w, PL_OT_WORLDGEN, RX_PERSIST_RESIDENT, RES_ENV, wg, &e->env.obj[PL_ENV_WORLDGEN]) != RX_OK ||
        rx_world_create(&e->w, PL_OT_COGNITION, RX_PERSIST_RESIDENT, RES_ENV, cg, &e->env.obj[PL_ENV_COGNITION]) != RX_OK ||
        rx_world_create(&e->w, PL_OT_STATION, RX_PERSIST_RESIDENT, RES_ENV, st, &e->env.obj[PL_ENV_STATION]) != RX_OK ||
        rx_world_create(&e->w, PL_OT_GOALFACTS, RX_PERSIST_RESIDENT, RES_ENV, facts, &fr) != RX_OK)
        return -1;
    uint64_t gl[RX_MAX_FIELDS] = { 1000u + sp->seed, PLA_GOAL_ARRANGE, sp->max_steps, 1000, pl_ref_pack(fr) };
    if (rx_world_create(&e->w, PL_OT_GOAL, RX_PERSIST_RESIDENT, RES_ENV, gl, &e->env.obj[PL_ENV_GOAL]) != RX_OK ||
        rx_world_create(&e->w, PL_OT_DECISION, RX_PERSIST_RESIDENT, RES_DECISION, z, &e->decision) != RX_OK ||
        rx_world_create(&e->w, PL_OT_INDEX, RX_PERSIST_RESIDENT, RES_INDEX, z, &e->index) != RX_OK)
        return -1;
    e->env.obj[PL_ENV_FACTS] = fr;

    e->caps.subject = SUBJ_PLAN;
    for (uint32_t k = 0; k < e->n_units; k++) {
        e->unit_cap[k] = mint(e, SUBJ_PLAN, RES_UNIT + k, RW);
        e->caps.cap[e->caps.n].ref = e->unit_cap[k];
        e->caps.cap[e->caps.n].resource = RES_UNIT + k;
        e->caps.cap[e->caps.n].rights = RW;
        e->caps.n++;
    }
    e->ext_run = mint(e, SUBJ_EXTERNAL, RES_RUN, W);
    e->ext_decision = mint(e, SUBJ_EXTERNAL, RES_DECISION, W);
    e->ext_index = mint(e, SUBJ_EXTERNAL, RES_INDEX, W);
    e->x.cell_cap = mint(e, SUBJ_PLAN, RES_CELL, RW);
    e->x.run_cap = mint(e, SUBJ_PLAN, RES_RUN, R_);
    e->x.ext_run_cap = e->ext_run;
    e->x.cell_res = RES_CELL;
    e->x.run_res = RES_RUN;
    e->x.skills = &g_skills;
    e->x.core_class = g_core_class;
    rx_world_wait_quiescent(&e->w, 30000);
    return 0;
}

static void env_stop(Env *e) {
    rx_world_wait_quiescent(&e->w, 30000);
    rx_world_destroy(&e->w);
    aienos_cap_stop(e->admin, e->view);
}

static Spec spec(uint32_t shape, uint32_t seed, Pert p) {
    return (Spec){ shape, seed, p, WORLD_GEN, COG_GEN, 32 };
}

/* ---- one task, with every lifecycle phase timed ---- */

enum { PATH_FIRST = 1, PATH_REUSE, PATH_ADAPT, PATH_REFUSED };
enum { ARM_A = 0, ARM_B = 1 };

typedef struct {
    uint32_t n;
    PlanTemplate *t[PL_MAX_TEMPLATES];
    PlAxis ax[PL_MAX_TEMPLATES];
    int pred[PL_MAX_TEMPLATES];
    RxObjRef slots[PL_MAX_TEMPLATES][PL_MAX_SLOTS];
} Refusals;

/* Phase order is the ledger order. CPU phases are thread CPU ns of the
 * cognition thread; WALL phases are wall ns (execution runs on World threads). */
enum { PH_GOAL_READ = 0, PH_LOOKUP, PH_APPLIC, PH_SEARCH, PH_GENERALIZE, PH_IDENTIFY, PH_STORE,
       PH_DECISION, PH_INDEX, PH_COMPILE, PH_REFERENCE, PH_RUN, PH_VERIFY, PH_REPAIR,
       PH_SERIALIZE, PH_RELOAD, PH_COUNT };
static const char *PH_NAME[PH_COUNT] = { "goal_read_cpu", "lookup_cpu", "applicability_cpu", "search_cpu",
    "generalize_cpu", "identify_cpu", "store_cpu", "decision_cpu", "index_cpu", "compile_wall",
    "reference_wall", "run_wall", "verify_wall", "repair_cpu", "serialize_cpu", "reload_cpu" };

typedef struct {
    uint32_t stream, task, arm, shape, pert;
    uint64_t seed, world_gen;
    int path, axis, success, goal_ok, status_before, status_after, restart_before, rederived;
    uint32_t moves, cache_n, candidates, refused, stale_refusals;
    uint64_t ph[PH_COUNT];
    uint64_t e2e_wall, cog_cpu, cog_wall, ident_copy_cpu, hash_in_e2e;
    uint8_t semantic_id[32];
} Row;

static Row g_rows[MAX_ROWS];
static uint32_t g_nrows;
static PlView g_view, g_view2;
static PlanTemplate g_tmp, g_tmp2;
static PlCache g_cache;
static Env *g_a, *g_b;

/* Retrieval (timed as lookup), then binding + applicability (timed as applicability). */
static PlanTemplate *decide(Env *e, PlCache *c, const PlView *v, const PlaGoal *g, RxObjRef *slots,
                            PlCost *cost, Refusals *rf, Row *r) {
    PlanTemplate *cand[PL_MAX_TEMPLATES];
    uint64_t c0 = cpu_ns();
    uint32_t n = rx_plan_cache_retrieve(c, (uint32_t)g->kind, g->shape, cand, PL_MAX_TEMPLATES, cost);
    r->ph[PH_LOOKUP] += cpu_ns() - c0;
    r->candidates = n;
    rf->n = 0;
    RxObjRef s[PL_MAX_SLOTS];
    PlanTemplate *use = NULL;
    c0 = cpu_ns();
    for (uint32_t i = 0; i < n && !use; i++) {
        uint32_t k = rf->n;
        int seen = 0;
        for (uint32_t attempt = 0;; attempt++) {
            int rc = pla_bind(cand[i], v, g, attempt, s, cost);
            if (rc == 0) break;
            PlAxis ax = PL_AX_BINDING;
            int pr = -1;
            if (rc > 0) ax = rx_plan_applicable(cand[i], &e->w, v, &e->env, s, &e->caps, cost, &pr);
            if (ax == PL_AX_OK) {
                memcpy(slots, s, sizeof s);
                use = cand[i];
                break;
            }
            if (!seen || ax > rf->ax[k]) {
                rf->t[k] = cand[i];
                rf->ax[k] = ax;
                rf->pred[k] = pr;
                memcpy(rf->slots[k], s, sizeof s);
                seen = 1;
            }
        }
        if (seen && !use) {
            cand[i]->refusals[rf->ax[k]]++;
            if (rf->ax[k] == PL_AX_STALE) r->stale_refusals++;
            rf->n++;
        }
    }
    r->ph[PH_APPLIC] += cpu_ns() - c0;
    r->refused = rf->n;
    return use;
}

static PlAxis furthest(const Refusals *rf) {
    PlAxis a = PL_AX_SHAPE;
    for (uint32_t i = 0; i < rf->n; i++)
        if (rf->ax[i] > a) a = rf->ax[i];
    return a;
}

static int64_t record(Env *e, uint64_t goal_seq, Refusals *rf, const PlanTemplate *use) {
    int64_t last = 0;
    for (uint32_t i = 0; i < rf->n; i++)
        last = rx_plan_decide(&e->w, e->ext_decision, e->decision, goal_seq, rf->ax[i], rf->t[i], rf->pred[i],
                              ++e->seq, NULL);
    if (!rf->n && !use)
        last = rx_plan_decide(&e->w, e->ext_decision, e->decision, goal_seq, PL_AX_SHAPE, NULL, -1, ++e->seq, NULL);
    if (use)
        last = rx_plan_decide(&e->w, e->ext_decision, e->decision, goal_seq, PL_AX_OK, use, -1, ++e->seq, NULL);
    return last;
}

static int goal_holds(Env *e) {
    PlaGoal g;
    rx_plan_view(&e->w, &g_view2);
    if (pla_goal_read(&g_view2, &e->env, &g, NULL) != 0) return 0;
    for (uint32_t i = 0; i < g.n_facts; i++) {
        uint64_t f = g_view2.o[g.x[i].id].field[PLA_F_SUPPORT];
        uint64_t want = g.floor[i] ? 0 : pl_ref_pack((RxObjRef){ g.t[i].id, g_view2.o[g.t[i].id].generation });
        if (f != want) return 0;
    }
    return pla_legal(&g_view2);
}

/* Solve one task in one arm. Arm A never consults the cache. Arm B is the
 * existing reuse path with adaptation (the full path of rx_plan_reuse.c). */
static int solve(Env *e, int arm, Row *r) {
    PlCache *c = arm == ARM_B ? &g_cache : NULL;
    uint64_t e0 = now_ns(), c0 = cpu_ns(), p0;
    PlCost cost;
    memset(&cost, 0, sizeof cost);
    r->arm = (uint32_t)arm;
    r->cache_n = c ? c->n : 0;
    p0 = cpu_ns();
    rx_plan_view(&e->w, &g_view);
    PlaGoal g;
    if (pla_goal_read(&g_view, &e->env, &g, &cost) != 0) return -1;
    r->ph[PH_GOAL_READ] += cpu_ns() - p0;
    PlanTemplate *use = NULL;
    Refusals rf;
    rf.n = 0;
    RxObjRef slots[PL_MAX_SLOTS];
    memset(slots, 0, sizeof slots);
    if (c) {
        use = decide(e, c, &g_view, &g, slots, &cost, &rf, r);
        r->axis = use ? PL_AX_OK : (int)furthest(&rf);
        if (use) {
            r->path = PATH_REUSE;
            r->status_before = (int)use->status;
            memcpy(r->semantic_id, use->semantic_id, 32);
        }
    }
    const PlanTemplate *run = use;
    if (!use) {
        PlaTarget tg;
        PlaPlan plan;
        const uint8_t *parent = NULL;
        uint32_t origin = PL_ORIGIN_SEARCH;
        int adapted = 0;
        if (c && !g_no_adapt) {
            /* Repair: for each candidate refused only on required state, plan
             * a prefix that reaches that state and keep the shortest whole plan. */
            p0 = cpu_ns();
            for (uint32_t i = 0; i < rf.n; i++) {
                if (rf.ax[i] != PL_AX_STATE) continue;
                PlaTarget tt;
                PlaPlan prefix, rest;
                if (pla_target_from_template(rf.t[i], rf.slots[i], &tt) != 0) continue;
                if (pla_plan(&g_view, &g, &tt, MAX_EXPAND, &prefix, &cost) != 0) continue;
                if (pla_template_moves(rf.t[i], rf.slots[i], &rest) != 0) continue;
                if (prefix.n + rest.n > PLA_MAX_MOVES || (adapted && prefix.n + rest.n >= plan.n)) continue;
                plan = prefix;
                for (uint32_t k = 0; k < rest.n; k++) {
                    plan.mover[plan.n] = rest.mover[k];
                    plan.dest[plan.n] = rest.dest[k];
                    plan.to_floor[plan.n++] = rest.to_floor[k];
                }
                adapted = 1;
                origin = PL_ORIGIN_ADAPTED;
                parent = rf.t[i]->semantic_id;
            }
            r->ph[PH_REPAIR] += cpu_ns() - p0;
        }
        if (!adapted) {
            p0 = cpu_ns();
            pla_target_from_goal(&g, &tg);
            int prc = pla_plan(&g_view, &g, &tg, MAX_EXPAND, &plan, &cost);
            r->ph[PH_SEARCH] += cpu_ns() - p0;
            if (prc != 0) return -2;
        }
        const uint64_t *cf = g_view.o[e->env.obj[PL_ENV_COGNITION].id].field;
        p0 = cpu_ns();
        int grc = pla_generalize(&g_view, &g, &plan, origin, parent, cf[0], &g_tmp, slots);
        r->ph[PH_GENERALIZE] += cpu_ns() - p0;
        if (grc != 0) return -3;
        r->path = adapted ? PATH_ADAPT : PATH_FIRST;
        memcpy(r->semantic_id, g_tmp.semantic_id, 32);
        run = &g_tmp;
    }
    r->cog_cpu = cpu_ns() - c0;
    r->cog_wall = now_ns() - e0;
    if (c) {
        p0 = cpu_ns();
        int64_t crumb = record(e, g.seq, &rf, use);
        r->ph[PH_DECISION] += cpu_ns() - p0;
        CHECK(crumb > 0, "decision crumb");
    }
    e->x.reference_first = use && use->status == PL_STATUS_CANDIDATE;
    PlExecResult ex;
    rx_plan_execute(run, &e->w, &e->env, slots, &e->caps, &e->x, 1, &e->rs, pla_legal, &ex);
    r->ph[PH_COMPILE] += ex.compile_ns;
    r->ph[PH_REFERENCE] += ex.reference_ns;
    r->ph[PH_RUN] += ex.run_ns;
    r->ph[PH_VERIFY] += ex.verify_ns;
    r->success = ex.success;
    r->moves = run->n_steps;
    PlanTemplate *stored = NULL;
    if (use) {
        rx_plan_record_use(use, ex.success);
        r->status_after = (int)use->status;
        stored = use;
    } else if (c && ex.success) {
        rx_plan_view(&e->w, &g_view2);
        uint64_t wg = g_view2.o[e->env.obj[PL_ENV_WORLDGEN].id].field[0];
        uint64_t cg = g_view2.o[e->env.obj[PL_ENV_COGNITION].id].field[0];
        uint32_t n_before = c->n;
        p0 = cpu_ns();
        stored = rx_plan_cache_store(c, &g_tmp, wg, cg);
        r->ph[PH_STORE] += cpu_ns() - p0;
        r->status_after = stored ? (int)stored->status : 0;
        r->rederived = stored && c->n == n_before;   /* same meaning again: the record was renewed */
    }
    if (stored) {
        p0 = cpu_ns();
        rx_plan_publish_index(&e->w, e->ext_index, e->index, c, stored);
        r->ph[PH_INDEX] += cpu_ns() - p0;
    }
    r->goal_ok = goal_holds(e);
    r->e2e_wall = now_ns() - e0;
    if (ex.evidence_present != ex.evidence_required || (ex.fail & PL_FAIL_CRUMBS)) r->success = 0;
    if (run == &g_tmp) {
        /* pla_generalize hashes the identity inside (it is in e2e and in the
         * generalize phase of both arms). Measure that hash alone on a copy,
         * outside the e2e window. Arm A would not compute it: cost_A subtracts
         * it. Arm B counts it as acquisition: the generalize phase gives it up
         * to the identify phase so no phase holds it twice. */
        g_tmp2 = g_tmp;
        p0 = cpu_ns();
        rx_plan_identify(&g_tmp2);
        r->ident_copy_cpu = cpu_ns() - p0;
        uint64_t h = r->ident_copy_cpu < r->ph[PH_GENERALIZE] ? r->ident_copy_cpu : r->ph[PH_GENERALIZE];
        r->hash_in_e2e = h;             /* the hash e2e holds: never more than generalize held */
        r->ph[PH_GENERALIZE] -= h;
        if (c) r->ph[PH_IDENTIFY] += h;
    }
    return 0;
}

/* ---- restart: serialise with the existing encoder, free, reload ---- */

typedef struct {
    uint32_t n;
    struct { uint32_t len; uint8_t enc[1u << 14]; PlanTemplate raw; } t[PL_MAX_TEMPLATES];
} Snapshot;

static Snapshot g_snap;
static char g_outdir[512];

static int snapshot_write(PlCache *c, const char *path, uint64_t *cpu) {
    uint64_t p0 = cpu_ns();
    g_snap.n = c->n;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(&g_snap.n, sizeof g_snap.n, 1, f) == 1;
    for (uint32_t i = 0; i < c->n && ok; i++) {
        size_t n = rx_plan_encode(c->t[i], g_snap.t[i].enc, sizeof g_snap.t[i].enc);
        if (!n) { fclose(f); return -1; }
        g_snap.t[i].len = (uint32_t)n;
        g_snap.t[i].raw = *c->t[i];
        /* Only what a record is: its length, canonical bytes and the carried body. */
        ok = fwrite(&g_snap.t[i].len, sizeof g_snap.t[i].len, 1, f) == 1 &&
             fwrite(g_snap.t[i].enc, 1, n, f) == n &&
             fwrite(&g_snap.t[i].raw, sizeof g_snap.t[i].raw, 1, f) == 1;
    }
    ok &= fclose(f) == 0;
    *cpu += cpu_ns() - p0;
    return ok ? 0 : -1;
}

/* Reload one record through the existing rules: decode the semantic record,
 * re-identify the carried body, and refuse unless the body's canonical bytes
 * are the record's. A reloaded template is a CANDIDATE (store renews it), so
 * its next reuse runs the reference first. Returns 1 stored, 0 refused. */
static int reload_one(PlCache *c, const uint8_t *enc, uint32_t len, const PlanTemplate *raw,
                      const char **why) {
    PlanTemplate sem;
    if (rx_plan_decode(enc, len, &sem) != 0) { *why = "decode"; return 0; }
    PlanTemplate body = *raw;
    rx_plan_identify(&body);
    static uint8_t again[1u << 14];
    size_t n = rx_plan_encode(&body, again, sizeof again);
    int same = n == len && memcmp(again, enc, len) == 0;
    if (!same && !mutant("M2")) { *why = "canonical bytes differ from the carried body"; return 0; }
    if (!rx_plan_cache_store(c, &body, raw->verified_world_gen, raw->verified_cog_gen)) { *why = "store"; return 0; }
    *why = "stored";
    return 1;
}

static int snapshot_reload(PlCache *c, const char *path, uint64_t *cpu, uint32_t *stored, uint32_t *refused) {
    uint64_t p0 = cpu_ns();
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    memset(&g_snap, 0, sizeof g_snap.n);
    int ok = fread(&g_snap.n, sizeof g_snap.n, 1, f) == 1 && g_snap.n <= PL_MAX_TEMPLATES;
    for (uint32_t i = 0; i < g_snap.n && ok; i++) {
        ok = fread(&g_snap.t[i].len, sizeof g_snap.t[i].len, 1, f) == 1 && g_snap.t[i].len <= sizeof g_snap.t[i].enc &&
             fread(g_snap.t[i].enc, 1, g_snap.t[i].len, f) == g_snap.t[i].len &&
             fread(&g_snap.t[i].raw, sizeof g_snap.t[i].raw, 1, f) == 1;
    }
    fclose(f);
    if (!ok) return -1;
    *stored = *refused = 0;
    for (uint32_t i = 0; i < g_snap.n; i++) {
        const char *why;
        if (reload_one(c, g_snap.t[i].enc, g_snap.t[i].len, &g_snap.t[i].raw, &why)) (*stored)++;
        else (*refused)++;
    }
    *cpu += cpu_ns() - p0;
    return 0;
}

/* ---- streams ---- */

typedef enum { ST_S1_REV = 1, ST_S1_MER, ST_S2, ST_S2_WARMUP, ST_S3, ST_S4, ST_S5, ST_S6, ST_P0, ST_DEV_P0 } StreamKind;

static const char *stream_name(StreamKind k) {
    switch (k) {
    case ST_S1_REV: return "S1_reverse";
    case ST_S1_MER: return "S1_merge_P+";
    case ST_S2: return "S2_heldout_bindings_compositions";
    case ST_S2_WARMUP: return "S2_warmup_dev_seeds";
    case ST_S3: return "S3_interference";
    case ST_S4: return "S4_changed_preconditions";
    case ST_S5: return "S5_stale_generations";
    case ST_S6: return "S6_restart";
    case ST_P0: return "P0_no_payoff";
    case ST_DEV_P0: return "dev_P0_no_payoff";
    }
    return "?";
}

static Spec task_spec(StreamKind k, uint32_t i, uint32_t seed_base) {
    Spec s = spec(SHAPE_REVERSE, seed_base + i, PERT_NONE);
    switch (k) {
    case ST_S1_REV: case ST_S6: break;
    case ST_S1_MER: s.shape = SHAPE_MERGE; break;
    case ST_S2: s.shape = i % 3; break;
    case ST_S2_WARMUP: s.shape = i % 2; s.seed = seed_base + i / 2; break;   /* dev seeds, both shapes */
    case ST_S3: {
        static const uint32_t sh[5] = { SHAPE_REVERSE, SHAPE_MERGE, SHAPE_OTHER, SHAPE_REVERSE, SHAPE_REVERSE };
        static const Pert pt[5] = { PERT_NONE, PERT_NONE, PERT_NONE, PERT_STATE_SWAP, PERT_STATE_OUTSIDER };
        s.shape = sh[i % 5]; s.pert = pt[i % 5];
        break;
    }
    case ST_S4: {
        static const Pert pt[4] = { PERT_NONE, PERT_STATE_SWAP, PERT_NONE, PERT_STATE_OUTSIDER };
        s.pert = pt[i % 4];
        break;
    }
    case ST_S5: s.world_gen = WORLD_GEN + i / 4; break;
    case ST_P0: case ST_DEV_P0: s.world_gen = WORLD_GEN + i; break;
    }
    return s;
}

typedef struct {
    StreamKind kind;
    uint32_t H, seed_base, first_row, n_rows;
    int restart_at;         /* task index before which a restart happens (-1 none) */
    int keep_cache;         /* do not reset the cache at the start */
} Stream;

static void run_stream(Stream *st) {
    printf("[*] stream %s: %u tasks, seeds %u.., cache %s\n", stream_name(st->kind), st->H, st->seed_base,
           st->keep_cache ? "kept" : "fresh");
    if (!st->keep_cache) { rx_plan_cache_free(&g_cache); rx_plan_cache_init(&g_cache); }
    st->first_row = g_nrows;
    for (uint32_t i = 0; i < st->H; i++) {
        Spec sp = task_spec(st->kind, i, st->seed_base);
        uint64_t ser = 0, rel = 0;
        int restarted = 0;
        uint32_t stored = 0, refused = 0;
        if (st->restart_at == (int)i) {
            char path[640];
            snprintf(path, sizeof path, "%s/snapshot-%s-%u.bin", g_outdir, stream_name(st->kind), i);
            uint32_t had = g_cache.n;
            CHECK(snapshot_write(&g_cache, path, &ser) == 0, "snapshot write");
            rx_plan_cache_free(&g_cache);
            rx_plan_cache_init(&g_cache);
            CHECK(snapshot_reload(&g_cache, path, &rel, &stored, &refused) == 0, "snapshot reload");
            CHECK(stored == had && refused == 0, "restart reloads every record (%u of %u, %u refused)", stored, had, refused);
            for (uint32_t t = 0; t < g_cache.n; t++)
                CHECK(g_cache.t[t]->status == PL_STATUS_CANDIDATE, "reloaded record is a CANDIDATE");
            restarted = 1;
            printf("    restart before task %u: %u records serialised, %u reloaded, %u refused\n", i, had, stored, refused);
        }
        for (int arm = ARM_A; arm <= ARM_B; arm++) {
            if (g_nrows >= MAX_ROWS) { CHECK(0, "row table full"); return; }
            Row *r = &g_rows[g_nrows];
            memset(r, 0, sizeof *r);
            r->stream = (uint32_t)st->kind;
            r->task = i;
            r->shape = sp.shape;
            r->pert = (uint32_t)sp.pert;
            r->seed = sp.seed;
            r->world_gen = sp.world_gen;
            Env *e = arm == ARM_A ? g_a : g_b;
            CHECK(env_build(e, &sp) == 0, "build %s task %u arm %d", stream_name(st->kind), i, arm);
            int rc = solve(e, arm, r);
            CHECK(rc == 0 && r->success && r->goal_ok, "%s task %u arm %d: rc %d success %d goal %d path %d axis %s",
                  stream_name(st->kind), i, arm, rc, r->success, r->goal_ok, r->path,
                  rx_plan_axis_name((PlAxis)r->axis));
            if (arm == ARM_B && restarted) {
                r->ph[PH_SERIALIZE] += ser;
                r->ph[PH_RELOAD] += rel;
                r->restart_before = 1;
                CHECK(r->path == PATH_REUSE && r->status_before == PL_STATUS_CANDIDATE && r->ph[PH_REFERENCE] > 0,
                      "first reuse after restart revalidates (path %d status %d ref %llu)", r->path, r->status_before,
                      (unsigned long long)r->ph[PH_REFERENCE]);
            }
            env_stop(e);
            g_nrows++;
        }
        /* Floors F1..F4 per task, both arms over the same instance. */
        Row *a = &g_rows[g_nrows - 2], *b = &g_rows[g_nrows - 1];
        CHECK(a->goal_ok && b->goal_ok, "F1 goal holds (%s task %u)", stream_name(st->kind), i);
        CHECK(a->moves == b->moves, "F2 plan length %u vs %u (%s task %u, path %d)", a->moves, b->moves,
              stream_name(st->kind), i, b->path);
        CHECK(a->success && b->success, "F3/F4 verification (%s task %u)", stream_name(st->kind), i);
        if (b->path == PATH_REUSE) CHECK(b->success, "F4 accepted reuse verified (%s task %u)", stream_name(st->kind), i);
    }
    st->n_rows = g_nrows - st->first_row;
}

/* ---- controls: stale and corrupt records through existing rules ---- */

static struct { int stale_refused, stale_rederived, stale_revalidated; int corrupt_cases, corrupt_refused;
                int corrupt_stored; char corrupt_why[3][80]; } CTL;

static void t_stale_control(void) {
    printf("[*] control: a STALE template is refused on the stale axis, re-derived, then revalidated\n");
    rx_plan_cache_free(&g_cache);
    rx_plan_cache_init(&g_cache);
    Spec sp = spec(SHAPE_REVERSE, 250, PERT_NONE);
    Row r;
    memset(&r, 0, sizeof r);
    CHECK(env_build(g_b, &sp) == 0, "build");
    solve(g_b, ARM_B, &r);
    env_stop(g_b);
    CHECK(r.path == PATH_FIRST && g_cache.n == 1, "acquired");
    PlanTemplate *t = g_cache.t[0];
    rx_plan_record_use(t, 0);               /* the existing rule for a failed reuse */
    CHECK(t->status == PL_STATUS_STALE, "failed reuse marks STALE");
    sp.seed = 251;
    memset(&r, 0, sizeof r);
    CHECK(env_build(g_b, &sp) == 0, "build");
    solve(g_b, ARM_B, &r);
    env_stop(g_b);
    CTL.stale_refused = r.path != PATH_REUSE && r.axis == PL_AX_STALE && r.stale_refusals == 1;
    CTL.stale_rederived = r.path == PATH_FIRST && g_cache.n == 1 && t->rederivations == 1 && t->status == PL_STATUS_CANDIDATE;
    CHECK(CTL.stale_refused, "stale template refused on the stale axis (path %d axis %s)", r.path,
          rx_plan_axis_name((PlAxis)r.axis));
    CHECK(CTL.stale_rederived, "re-derived into the existing record as CANDIDATE");
    sp.seed = 252;
    memset(&r, 0, sizeof r);
    CHECK(env_build(g_b, &sp) == 0, "build");
    solve(g_b, ARM_B, &r);
    env_stop(g_b);
    CTL.stale_revalidated = r.path == PATH_REUSE && r.status_before == PL_STATUS_CANDIDATE && r.ph[PH_REFERENCE] > 0 &&
                            r.success && t->status == PL_STATUS_VERIFIED;
    CHECK(CTL.stale_revalidated, "next reuse revalidates (reference first) and promotes");
}

static void t_corrupt_control(void) {
    printf("[*] control: corrupt serialised records are refused on reload\n");
    rx_plan_cache_free(&g_cache);
    rx_plan_cache_init(&g_cache);
    Spec sp = spec(SHAPE_MERGE, 260, PERT_NONE);
    Row r;
    memset(&r, 0, sizeof r);
    CHECK(env_build(g_b, &sp) == 0, "build");
    solve(g_b, ARM_B, &r);
    env_stop(g_b);
    CHECK(r.path == PATH_FIRST && g_cache.n == 1, "acquired");
    uint64_t cpu = 0;
    char path[640];
    snprintf(path, sizeof path, "%s/snapshot-corrupt.bin", g_outdir);
    CHECK(snapshot_write(&g_cache, path, &cpu) == 0, "snapshot");
    for (int cse = 0; cse < 3; cse++) {
        PlCache c2;
        rx_plan_cache_init(&c2);
        uint8_t enc[1u << 14];
        memcpy(enc, g_snap.t[0].enc, g_snap.t[0].len);
        PlanTemplate raw = g_snap.t[0].raw;
        uint32_t len = g_snap.t[0].len;
        if (cse == 0) enc[0] ^= 0xFF;                       /* magic: decode must refuse */
        if (cse == 1) enc[len - 5] ^= 0x01;                 /* inside the graph digest: decode accepts, bytes differ */
        if (cse == 2) raw.graph.nodes[0].field ^= 1;        /* the carried body differs from the record */
        const char *why;
        int stored = reload_one(&c2, enc, len, &raw, &why);
        CTL.corrupt_cases++;
        CTL.corrupt_refused += !stored;
        CTL.corrupt_stored += stored;
        snprintf(CTL.corrupt_why[cse], sizeof CTL.corrupt_why[cse], "%s", why);
        CHECK(!stored, "corrupt record case %d was stored (%s)", cse, why);
        if (stored) g_bad_rows++;
        rx_plan_cache_free(&c2);
    }
    printf("    %d corrupt records: %d refused, %d stored\n", CTL.corrupt_cases, CTL.corrupt_refused, CTL.corrupt_stored);
}

/* ---- ledger, reconciliation, accounting ---- */

typedef struct {
    uint64_t ph[2][PH_COUNT];           /* per arm */
    uint64_t e2e[2], cog_cpu[2], ident_copy[2];
    uint32_t rows[2];
} Ledger;

static Ledger ledger_of(const Row *rows, uint32_t n) {
    Ledger L;
    memset(&L, 0, sizeof L);
    for (uint32_t i = 0; i < n; i++) {
        const Row *r = &rows[i];
        for (int p = 0; p < PH_COUNT; p++) {
            if (mutant("M1") && (p == PH_IDENTIFY || p == PH_STORE)) continue;
            L.ph[r->arm][p] += r->ph[p];
        }
        L.e2e[r->arm] += r->e2e_wall;
        L.cog_cpu[r->arm] += r->cog_cpu;
        L.ident_copy[r->arm] += r->ident_copy_cpu;
        L.rows[r->arm]++;
    }
    return L;
}

/* Reconcile: per phase, ledger == direct sum of rows; per row, e2e >= wall phases. */
static int reconcile(const Row *rows, uint32_t n, const Ledger *L, uint64_t *unattributed, int *bad) {
    *bad = 0;
    for (int arm = 0; arm < 2; arm++)
        for (int p = 0; p < PH_COUNT; p++) {
            uint64_t s = 0;
            for (uint32_t i = 0; i < n; i++) if ((int)rows[i].arm == arm) s += rows[i].ph[p];
            if (s != L->ph[arm][p]) (*bad)++;
        }
    *unattributed = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t wall = rows[i].ph[PH_COMPILE] + rows[i].ph[PH_REFERENCE] + rows[i].ph[PH_RUN] + rows[i].ph[PH_VERIFY];
        uint64_t cpu = 0;
        /* Serialize and reload happen between tasks, outside any task's e2e
         * window; cost_B carries them explicitly. Arm A's hash inside
         * generalize is in e2e but in no phase (cost_A subtracts it). */
        for (int p = 0; p < PH_COMPILE; p++) cpu += rows[i].ph[p];
        cpu += rows[i].ph[PH_REPAIR];
        uint64_t harness_hash = rows[i].arm == ARM_A ? rows[i].hash_in_e2e : 0;
        if (rows[i].e2e_wall < wall + cpu + harness_hash) (*bad)++;
        else *unattributed += rows[i].e2e_wall - wall - cpu - harness_hash;
    }
    return *bad == 0;
}

/* Arm A cost per the protocol: measured e2e minus the identity hash it would
 * not compute. Arm B cost: measured e2e plus restart costs it carries. */
static int64_t cost_a(const Row *r) { return (int64_t)r->e2e_wall - (int64_t)r->hash_in_e2e; }
static int64_t cost_b(const Row *r) { return (int64_t)r->e2e_wall + (int64_t)r->ph[PH_SERIALIZE] + (int64_t)r->ph[PH_RELOAD]; }

typedef struct {
    uint32_t H, hits, misses, acquisitions, rederivations, adaptations, stale_marks, forget_events,
             neg_transfer, refusals, stale_refusals, floors_ok, restarts, templates;
    int64_t net_cum[H_HELD + 1];         /* cumulative net after k tasks (ns) */
    int64_t net_final, saved_gross, spent_learning, reinvested, unfunded, spent_acq, spent_maint, spent_repair,
            spent_restart, cost_a_total, cost_b_total;
    int break_even;                      /* smallest H with net_cum >= 0, or -1 */
    uint64_t mem_bytes, lookup_cpu_first, lookup_cpu_last, applic_cpu_first, applic_cpu_last;
    double delta_med, delta_q1, delta_q3, p_hat, p_lo, p_hi, r_hat, C_ns, m_ns;
    double task_net_med, task_net_q1, task_net_q3;   /* robust: per-task (A - B) over every task after the first */
    int64_t net_robust;                               /* task_net_med x (H - 1) + task 0 net */
} Account;

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

static double quantile(int64_t *v, uint32_t n, double q) {
    if (!n) return 0;
    qsort(v, n, sizeof *v, cmp_i64);
    double pos = q * (double)(n - 1);
    uint32_t lo = (uint32_t)pos;
    uint32_t hi = lo + 1 < n ? lo + 1 : lo;
    return (double)v[lo] + (pos - (double)lo) * ((double)v[hi] - (double)v[lo]);
}

static void wilson(uint32_t k, uint32_t n, double *lo, double *hi) {
    if (!n) { *lo = *hi = 0; return; }
    double z = 1.96, p = (double)k / n;
    double d = 1 + z * z / n, c = p + z * z / (2 * n), s = z * sqrt(p * (1 - p) / n + z * z / (4.0 * n * n));
    *lo = (c - s) / d;
    *hi = (c + s) / d;
}

static Account account(const Row *rows, uint32_t n) {
    Account A;
    memset(&A, 0, sizeof A);
    A.break_even = -1;
    A.floors_ok = 1;
    int64_t deltas[H_HELD], tnets[H_HELD];
    uint32_t nd = 0, nt = 0;
    int64_t cum = 0, saved_cum = 0, net0 = 0;
    uint32_t opportunities = 0;
    int prev_usable = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        const Row *a = &rows[i], *b = &rows[i + 1];
        if (mutant("M3")) { const Row *t = a; a = b; b = t; }
        uint32_t k = i / 2;
        A.H++;
        int64_t ca = cost_a(a), cb = cost_b(b), net = ca - cb;
        A.cost_a_total += ca;
        A.cost_b_total += cb;
        if (k == 0) net0 = net;
        else if (nt < H_HELD) tnets[nt++] = net;
        if (!(a->goal_ok && b->goal_ok && a->success && b->success && a->moves == b->moves)) A.floors_ok = 0;
        if (b->path == PATH_REUSE && b->moves > a->moves) A.neg_transfer++;
        if (b->path == PATH_REUSE && !b->success) A.neg_transfer++;
        A.refusals += b->refused;
        A.stale_refusals += b->stale_refusals;
        if (b->restart_before) A.restarts++;
        int64_t learn = 0;
        if (b->path == PATH_REUSE) {
            A.hits++;
            if (nd < H_HELD) deltas[nd++] = net;
            A.saved_gross += net;
            saved_cum += net;
        } else {
            A.misses++;
            if (b->path == PATH_ADAPT) { A.adaptations++; learn += (int64_t)b->ph[PH_REPAIR]; A.spent_repair += (int64_t)b->ph[PH_REPAIR]; }
            int64_t acq = (int64_t)(b->ph[PH_IDENTIFY] + b->ph[PH_STORE] + b->ph[PH_INDEX]);
            int64_t maint = (int64_t)(b->ph[PH_LOOKUP] + b->ph[PH_APPLIC] + b->ph[PH_DECISION]);
            A.acquisitions++;
            A.rederivations += b->rederived ? 1u : 0u;
            learn += acq + maint;
            A.spent_acq += acq;
            A.spent_maint += maint;
        }
        int64_t rst = (int64_t)(b->ph[PH_SERIALIZE] + b->ph[PH_RELOAD]);
        learn += rst;
        A.spent_restart += rst;
        /* Reinvestment: learning paid for by savings already banked. */
        int64_t funded = saved_cum - A.reinvested - A.unfunded > 0 ? saved_cum - A.reinvested - A.unfunded : 0;
        int64_t re = learn < funded ? learn : funded;
        A.reinvested += re;
        A.unfunded += learn - re;
        A.spent_learning += learn;
        if (k > 0) {
            opportunities++;
            if (prev_usable && b->path != PATH_REUSE && b->stale_refusals) A.forget_events++;
        }
        prev_usable = b->path == PATH_REUSE || b->path == PATH_FIRST || b->path == PATH_ADAPT;
        if (b->path == PATH_REUSE && b->status_after == PL_STATUS_STALE) A.stale_marks++;
        cum += net;
        if (k < H_HELD) A.net_cum[k + 1] = cum;
        if (A.break_even < 0 && cum >= 0 && k > 0) A.break_even = (int)k + 1;
        if (k == 0) { A.lookup_cpu_first = b->ph[PH_LOOKUP]; A.applic_cpu_first = b->ph[PH_APPLIC]; }
        A.lookup_cpu_last = b->ph[PH_LOOKUP];
        A.applic_cpu_last = b->ph[PH_APPLIC];
    }
    A.net_final = cum;
    A.task_net_med = quantile(tnets, nt, 0.5);
    A.task_net_q1 = quantile(tnets, nt, 0.25);
    A.task_net_q3 = quantile(tnets, nt, 0.75);
    A.net_robust = net0 + (int64_t)(A.task_net_med * (double)nt);
    A.delta_med = quantile(deltas, nd, 0.5);
    A.delta_q1 = quantile(deltas, nd, 0.25);
    A.delta_q3 = quantile(deltas, nd, 0.75);
    A.p_hat = opportunities ? (double)A.hits / (double)opportunities : 0;
    wilson(A.hits, opportunities, &A.p_lo, &A.p_hi);
    A.r_hat = opportunities ? 1.0 - (double)A.forget_events / (double)opportunities : 1.0;
    A.templates = g_cache.n;
    A.mem_bytes = (uint64_t)g_cache.n * sizeof(PlanTemplate);
    return A;
}

/* net(H) diagnostic. Units: ns. H opportunities. */
static double net_model(double p, double Delta, double r, double C, double m, uint32_t H) {
    double geo;
    if (r >= 1.0) geo = (double)H;
    else if (r <= 0.0) geo = H ? 1.0 : 0.0;
    else geo = (1.0 - pow(r, (double)H)) / (1.0 - r);
    return p * Delta * geo - C - m * (double)H;
}

static double net_direct_sum(double p, double Delta, double r, double C, double m, uint32_t H) {
    double s = 0, rk = 1;
    for (uint32_t k = 1; k <= H; k++) { s += rk; rk *= r; }
    return p * Delta * s - C - m * (double)H;
}

/* ---- output ---- */

static void write_trace(const char *path, const char *load0, const char *load1) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# rx_plan_payback trace; loadavg_start=%s; loadavg_end=%s; mutant=%s\n", load0, load1, g_mutant);
    fprintf(f, "stream,task,arm,shape,pert,seed,world_gen,path,axis,success,goal_ok,moves,status_before,status_after,"
               "cache_n,candidates,refused,stale_refusals,restart_before,rederived,e2e_wall_ns,cog_cpu_ns,cog_wall_ns,ident_copy_cpu_ns");
    for (int p = 0; p < PH_COUNT; p++) fprintf(f, ",%s", PH_NAME[p]);
    fprintf(f, ",semantic_id_word0\n");
    for (uint32_t i = 0; i < g_nrows; i++) {
        const Row *r = &g_rows[i];
        fprintf(f, "%s,%u,%c,%u,%u,%llu,%llu,%d,%s,%d,%d,%u,%d,%d,%u,%u,%u,%u,%d,%d,%llu,%llu,%llu,%llu",
                stream_name((StreamKind)r->stream), r->task, r->arm ? 'B' : 'A', r->shape, r->pert,
                (unsigned long long)r->seed, (unsigned long long)r->world_gen, r->path,
                rx_plan_axis_name((PlAxis)r->axis), r->success, r->goal_ok, r->moves, r->status_before, r->status_after,
                r->cache_n, r->candidates, r->refused, r->stale_refusals, r->restart_before, r->rederived,
                (unsigned long long)r->e2e_wall, (unsigned long long)r->cog_cpu, (unsigned long long)r->cog_wall,
                (unsigned long long)r->ident_copy_cpu);
        for (int p = 0; p < PH_COUNT; p++) fprintf(f, ",%llu", (unsigned long long)r->ph[p]);
        fprintf(f, ",%016llx\n", (unsigned long long)pl_word(r->semantic_id, 0));
    }
    fclose(f);
}

static void json_ledger(FILE *f, const Ledger *L) {
    for (int arm = 0; arm < 2; arm++) {
        fprintf(f, "      \"arm_%c\": {\"rows\": %u, \"e2e_wall_ns\": %llu, \"cognition_cpu_ns\": %llu, \"identity_hash_on_copy_ns\": %llu",
                arm ? 'B' : 'A', L->rows[arm], (unsigned long long)L->e2e[arm], (unsigned long long)L->cog_cpu[arm],
                (unsigned long long)L->ident_copy[arm]);
        for (int p = 0; p < PH_COUNT; p++) fprintf(f, ", \"%s\": %llu", PH_NAME[p], (unsigned long long)L->ph[arm][p]);
        fprintf(f, "}%s\n", arm ? "" : ",");
    }
}

static void json_account(FILE *f, const char *name, const Account *A) {
    fprintf(f, "    \"%s\": {\n      \"H\": %u, \"hits\": %u, \"misses\": %u, \"acquisitions\": %u, \"rederivations\": %u, "
               "\"adaptations\": %u, \"refusals\": %u, \"stale_refusals\": %u, \"stale_marks\": %u, \"forget_events\": %u, "
               "\"negative_transfer\": %u, \"restarts\": %u, \"floors_ok\": %s,\n",
            name, A->H, A->hits, A->misses, A->acquisitions, A->rederivations, A->adaptations, A->refusals,
            A->stale_refusals, A->stale_marks, A->forget_events, A->neg_transfer, A->restarts, A->floors_ok ? "true" : "false");
    fprintf(f, "      \"cost_A_total_ns\": %lld, \"cost_B_total_ns\": %lld, \"net_final_ns\": %lld, \"break_even_tasks\": %d,\n"
               "      \"robust\": {\"task_net_median_ns\": %.0f, \"task_net_q1_ns\": %.0f, \"task_net_q3_ns\": %.0f, \"net_robust_ns\": %lld, "
               "\"note\": \"net_final is the frozen cumulative sum of per-task wall differences and carries every outlier; net_robust is task-0 net plus the median per-task net times the remaining tasks\"},\n",
            (long long)A->cost_a_total, (long long)A->cost_b_total, (long long)A->net_final, A->break_even,
            A->task_net_med, A->task_net_q1, A->task_net_q3, (long long)A->net_robust);
    fprintf(f, "      \"reinvestment_ledger_ns\": {\"saved_gross\": %lld, \"spent_learning\": %lld, \"spent_acquisition\": %lld, "
               "\"spent_maintenance\": %lld, \"spent_repair\": %lld, \"spent_restart\": %lld, \"reinvested\": %lld, \"unfunded\": %lld, "
               "\"net\": %lld},\n",
            (long long)A->saved_gross, (long long)A->spent_learning, (long long)A->spent_acq, (long long)A->spent_maint,
            (long long)A->spent_repair, (long long)A->spent_restart, (long long)A->reinvested, (long long)A->unfunded,
            (long long)(A->saved_gross - A->spent_learning));
    fprintf(f, "      \"estimates\": {\"p_hat\": %.4f, \"p_wilson95\": [%.4f, %.4f], \"r_hat\": %.4f, \"delta_ns\": {\"median\": %.0f, \"q1\": %.0f, \"q3\": %.0f}},\n",
            A->p_hat, A->p_lo, A->p_hi, A->r_hat, A->delta_med, A->delta_q1, A->delta_q3);
    fprintf(f, "      \"memory\": {\"templates\": %u, \"bytes\": %llu, \"lookup_cpu_ns_first\": %llu, \"lookup_cpu_ns_last\": %llu, "
               "\"applicability_cpu_ns_first\": %llu, \"applicability_cpu_ns_last\": %llu},\n",
            A->templates, (unsigned long long)A->mem_bytes, (unsigned long long)A->lookup_cpu_first,
            (unsigned long long)A->lookup_cpu_last, (unsigned long long)A->applic_cpu_first, (unsigned long long)A->applic_cpu_last);
    fprintf(f, "      \"net_cumulative_ns\": [");
    for (uint32_t k = 1; k <= A->H && k <= H_HELD; k++) fprintf(f, "%s%lld", k > 1 ? ", " : "", (long long)A->net_cum[k]);
    fprintf(f, "]\n    }");
}

static void binary_digest(char out[65]) {
    strcpy(out, "unavailable");
    FILE *fp = fopen("/proc/self/exe", "rb");
    if (!fp) return;
    sha256_ctx c;
    sha256_init(&c);
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) sha256_update(&c, buf, n);
    fclose(fp);
    uint8_t d[32];
    sha256_final(&c, d);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
}

static void json_head(FILE *f, const char *mode, const char *load0, const char *load1) {
    char commit[41], digest[65];
    memset(commit, 0, sizeof commit);
    if (!omega_evidence_run_commit(commit)) memcpy(commit, "unknown", 8);
    binary_digest(digest);
    struct utsname u;
    memset(&u, 0, sizeof u);
    uname(&u);
    fprintf(f, "{\n  \"schema\": \"OMEGA_PLAN_REUSE_PAYBACK_V1\",\n  \"mode\": \"%s\",\n  \"mutant\": \"%s\",\n  \"arm_B_variant\": \"%s\",\n", mode, g_mutant,
            g_no_adapt ? "reuse only (no adaptation/repair)" : "existing reuse with adaptation/repair (frozen protocol)");
    fprintf(f, "  \"run_commit\": \"%s\",\n  \"tree_dirty\": %s,\n  \"test_binary_sha256\": \"%s\",\n", commit,
            omega_evidence_tree_dirty() ? "true" : "false", digest);
    fprintf(f, "  \"host\": {\"sysname\": \"%s\", \"release\": \"%s\", \"machine\": \"%s\", \"core_class_midr_part\": \"0x%llx\", "
               "\"loadavg_start\": \"%s\", \"loadavg_end\": \"%s\"},\n",
            u.sysname, u.release, u.machine, (unsigned long long)g_core_class, load0, load1);
    fprintf(f, "  \"checks\": %d,\n  \"failures\": %d,\n  \"bad_rows\": %d,\n", g_checks, g_fail, g_bad_rows);
    fprintf(f, "  \"units\": \"nanoseconds; cognition phases are thread CPU time, execution phases and e2e are wall time\",\n");
    fprintf(f, "  \"arms\": \"A no reuse (e2e minus the identity hash it would not compute); B existing rx_plan reuse with adaptation (e2e plus restart costs). Arm C (#584 learning arm): not available at run time, two arms only.\",\n");
    fprintf(f, "  \"joules\": {\"measured\": false, \"why\": \"no on-device package counter on this kernel (aien_spbm hwmon absent); UPS wall meter (upsc, 10 W x 2 s steps) cannot resolve a sub-millijoule per-task delta on a loaded box\"},\n");
    fprintf(f, "  \"scope\": \"host processor; arrangement domain (blocks-world lifted onto World objects); explicit A* planner; no general-agent claim\",\n");
}

/* ---- modes ---- */

static void place(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    int count = 0;
    long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long c = 0; c < n; c++) {
        char path[128];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%ld/regs/identification/midr_el1", c);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        unsigned long long midr = 0;
        int ok = fscanf(fp, "%llx", &midr) == 1;
        fclose(fp);
        if (ok && ((midr >> 4) & 0xfffu) == 0xd85u) { CPU_SET(c, &set); count++; g_core_class = 0xd85u; }
    }
    if (count && sched_setaffinity(0, sizeof set, &set) == 0) printf("[*] placed on %d Cortex-X925 cores\n", count);
}

static int write_file(const char *path, const char *buf, size_t n) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fwrite(buf, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static void json_model_block(FILE *f, const char *name, double p, double dlo, double d, double dhi, double plo, double phi,
                             double r, double C, double m, uint32_t Hopp, int acq_task) {
    double n = net_model(p, d, r, C, m, Hopp), ds = net_direct_sum(p, d, r, C, m, Hopp);
    double lo = net_model(plo, dlo, r, C, m, Hopp), hi = net_model(phi, dhi, r, C, m, Hopp);
    double per = p * d - m;
    int hstar = -1;
    if (per > 0) {
        if (r >= 1.0) hstar = (int)ceil(C / per) + acq_task;
        else {
            double arg = 1.0 - C * (1.0 - r) / (p * d);
            if (arg > 0 && arg <= 1) hstar = (int)ceil(log(arg) / log(r)) + acq_task;
        }
    }
    int no_payoff = r < 1.0 ? C >= p * d / (1.0 - r) : per <= 0;
    fprintf(f, "    \"%s\": {\"p\": %.4f, \"p_band\": [%.4f, %.4f], \"Delta_ns\": %.0f, \"Delta_band_ns\": [%.0f, %.0f], \"r\": %.4f, "
               "\"C_ns\": %.0f, \"m_ns\": %.0f, \"H_opportunities\": %u, \"acquisition_tasks\": %d,\n"
               "      \"net_closed_form_ns\": %.0f, \"net_direct_sum_ns\": %.0f, \"closed_form_equals_direct_sum\": %s,\n"
               "      \"net_band_ns\": [%.0f, %.0f], \"break_even_tasks\": %d, \"no_finite_payoff\": %s}",
            name, p, plo, phi, d, dlo, dhi, r, C, m, Hopp, acq_task, n, ds, fabs(n - ds) <= 1e-6 * fabs(n) + 1 ? "true" : "false",
            lo, hi, hstar, no_payoff ? "true" : "false");
}

static int run_dev(void) {
    char load0[128], load1[128];
    loadavg(load0, sizeof load0);
    Stream s1 = { ST_S1_REV, H_DEV, SEED_DEV, 0, 0, 6, 0 };     /* restart before task 6 in dev */
    Stream s2 = { ST_S1_MER, H_DEV, SEED_DEV, 0, 0, -1, 0 };
    Stream p0 = { ST_DEV_P0, 6, SEED_DEV + 50, 0, 0, -1, 0 };
    run_stream(&s1);
    Account a1 = account(&g_rows[s1.first_row], s1.n_rows);
    run_stream(&s2);
    Account a2 = account(&g_rows[s2.first_row], s2.n_rows);
    run_stream(&p0);
    Account a0 = account(&g_rows[p0.first_row], p0.n_rows);
    t_stale_control();
    t_corrupt_control();
    Ledger L = ledger_of(g_rows, g_nrows);
    uint64_t unatt;
    int bad;
    int ok = reconcile(g_rows, g_nrows, &L, &unatt, &bad);
    g_bad_rows += bad;
    CHECK(ok, "ledger reconciles with traces (%d bad)", bad);
    loadavg(load1, sizeof load1);

    /* Dev estimates. C: acquisition phases of the acquiring task (arm B task 0).
     * m: on P0 (no hit by rule) the per-task extra arm B pays beyond arm A. */
    const Row *acq1 = &g_rows[s1.first_row + 1], *acq2 = &g_rows[s2.first_row + 1];
    double C1 = (double)(acq1->ph[PH_IDENTIFY] + acq1->ph[PH_STORE] + acq1->ph[PH_INDEX] + acq1->ph[PH_DECISION] + acq1->ph[PH_LOOKUP]);
    double C2 = (double)(acq2->ph[PH_IDENTIFY] + acq2->ph[PH_STORE] + acq2->ph[PH_INDEX] + acq2->ph[PH_DECISION] + acq2->ph[PH_LOOKUP]);
    int64_t mv[8];
    uint32_t nm = 0;
    for (uint32_t k = 1; k < p0.H; k++) {
        const Row *a = &g_rows[p0.first_row + 2 * k], *b = &g_rows[p0.first_row + 2 * k + 1];
        mv[nm++] = cost_b(b) - cost_a(a);
    }
    double m0 = quantile(mv, nm, 0.5), m0lo = quantile(mv, nm, 0.25), m0hi = quantile(mv, nm, 0.75);
    double C0 = (double)(g_rows[p0.first_row + 1].ph[PH_IDENTIFY] + g_rows[p0.first_row + 1].ph[PH_STORE] +
                         g_rows[p0.first_row + 1].ph[PH_INDEX] + g_rows[p0.first_row + 1].ph[PH_DECISION] + g_rows[p0.first_row + 1].ph[PH_LOOKUP]);

    char path[640];
    snprintf(path, sizeof path, "%s/dev-trace.csv", g_outdir);
    write_trace(path, load0, load1);
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    json_head(f, "dev", load0, load1);
    fprintf(f, "  \"partition\": \"development: seeds %u..%u, H_dev %u per shape; P0 dev 6 tasks\",\n", SEED_DEV, SEED_DEV + H_DEV - 1, H_DEV);
    fprintf(f, "  \"ledger\": {\n");
    json_ledger(f, &L);
    fprintf(f, "  },\n  \"reconciliation\": {\"ok\": %s, \"bad_rows\": %d, \"unattributed_wall_ns\": %llu, \"rows\": %u},\n",
            ok ? "true" : "false", bad, (unsigned long long)unatt, g_nrows);
    fprintf(f, "  \"streams\": {\n");
    json_account(f, "dev_S1_reverse", &a1); fprintf(f, ",\n");
    json_account(f, "dev_S1_merge", &a2); fprintf(f, ",\n");
    json_account(f, "dev_P0_no_payoff", &a0); fprintf(f, "\n  },\n");
    fprintf(f, "  \"controls\": {\"stale_refused\": %s, \"stale_rederived\": %s, \"stale_revalidated\": %s, "
               "\"corrupt_cases\": %d, \"corrupt_refused\": %d, \"corrupt_stored\": %d, \"corrupt_why\": [\"%s\", \"%s\", \"%s\"],\n"
               "    \"sign_P+_dev\": \"%s\", \"sign_P0_dev\": \"%s\"},\n",
            CTL.stale_refused ? "true" : "false", CTL.stale_rederived ? "true" : "false", CTL.stale_revalidated ? "true" : "false",
            CTL.corrupt_cases, CTL.corrupt_refused, CTL.corrupt_stored, CTL.corrupt_why[0], CTL.corrupt_why[1], CTL.corrupt_why[2],
            a2.net_final > 0 ? "positive" : "negative", a0.net_final < 0 ? "negative" : "positive");
    CHECK(a2.net_final > 0, "P+ dev sign positive (net %lld ns)", (long long)a2.net_final);
    CHECK(a0.net_final < 0, "P0 dev sign negative (net %lld ns)", (long long)a0.net_final);
    if (a2.net_final <= 0) g_bad_rows++;
    if (a0.net_final >= 0) g_bad_rows++;
    /* Prediction for the held-out streams, from development numbers only. */
    fprintf(f, "  \"prediction_rule\": \"#262: p, r, Delta, C, m from the development partition only; held-out H=%u; net in ns over H-1 opportunities after one acquisition task (S1), or H opportunities with no acquisition (S2, sunk in dev)\",\n", H_HELD);
    fprintf(f, "  \"prediction\": {\n");
    json_model_block(f, "S1_reverse", a1.p_hat, a1.delta_q1, a1.delta_med, a1.delta_q3, a1.p_lo, a1.p_hi, a1.r_hat, C1, 0, H_HELD - 1, 1);
    fprintf(f, ",\n");
    json_model_block(f, "S1_merge_P+", a2.p_hat, a2.delta_q1, a2.delta_med, a2.delta_q3, a2.p_lo, a2.p_hi, a2.r_hat, C2, 0, H_HELD - 1, 1);
    fprintf(f, ",\n");
    json_model_block(f, "P0_no_payoff", 0, 0, 0, 0, 0, 0, a0.r_hat, C0, m0, H_HELD - 1, 1);
    fprintf(f, ",\n    \"P0_m_band_ns\": [%.0f, %.0f],\n", m0lo, m0hi);
    fprintf(f, "    \"derivation\": \"expected saving at opportunity k is p*Delta*r^(k-1); sum_{k=1..H} r^(k-1) = (1-r^H)/(1-r) for r<1, H for r=1, 1 for r=0 (0^0=1); no finite positive payoff when C >= p*Delta/(1-r); with maintenance m per opportunity the per-opportunity net is p*Delta-m\",\n");
    fprintf(f, "    \"model_check_r_lt_1\": {\"p\": 1, \"Delta_ns\": 1000000, \"r\": 0.9, \"C_ns\": 5000000, \"m_ns\": 0, \"H\": 39, \"closed_form_ns\": %.6f, \"direct_sum_ns\": %.6f, \"no_finite_payoff\": %s, \"sup_net_ns\": %.6f}\n",
            net_model(1, 1e6, 0.9, 5e6, 0, 39), net_direct_sum(1, 1e6, 0.9, 5e6, 0, 39), 5e6 >= 1e6 / 0.1 ? "true" : "false", 1e6 / 0.1 - 5e6);
    fprintf(f, "  }\n}\n");
    fclose(f);
    snprintf(path, sizeof path, "%s/dev-receipt.json", g_outdir);
    write_file(path, buf, len);
    free(buf);
    printf("checks %d failures %d bad_rows %d\n", g_checks, g_fail, g_bad_rows);
    printf("dev: S1_reverse p %.3f Delta %.0f ns [%.0f, %.0f] C %.0f ns; S1_merge p %.3f Delta %.0f ns [%.0f, %.0f] C %.0f ns; P0 m %.0f ns [%.0f, %.0f]\n",
           a1.p_hat, a1.delta_med, a1.delta_q1, a1.delta_q3, C1, a2.p_hat, a2.delta_med, a2.delta_q1, a2.delta_q3, C2, m0, m0lo, m0hi);
    printf("dev: net S1_reverse %lld ns, S1_merge %lld ns (P+ sign %s), P0 %lld ns (sign %s); reconciliation %s; stale %s/%s/%s; corrupt %d/%d refused\n",
           (long long)a1.net_final, (long long)a2.net_final, a2.net_final > 0 ? "positive" : "NEGATIVE",
           (long long)a0.net_final, a0.net_final < 0 ? "negative" : "POSITIVE", ok ? "OK" : "MISMATCH",
           CTL.stale_refused ? "refused" : "ACCEPTED", CTL.stale_rederived ? "rederived" : "NOT", CTL.stale_revalidated ? "revalidated" : "NOT",
           CTL.corrupt_refused, CTL.corrupt_cases);
    printf("receipt: %s\n", path);
    return g_fail ? 1 : 0;
}

static int run_heldout(void) {
    char load0[128], load1[128];
    loadavg(load0, sizeof load0);
    Stream st[] = {
        { ST_S1_REV, H_HELD, SEED_HELD, 0, 0, -1, 0 },
        { ST_S1_MER, H_HELD, SEED_HELD, 0, 0, -1, 0 },
        { ST_S2_WARMUP, 2 * H_DEV, SEED_DEV, 0, 0, -1, 0 },
        { ST_S2, H_HELD, SEED_HELD + 100, 0, 0, -1, 1 },
        { ST_S3, H_HELD, SEED_HELD + 200, 0, 0, -1, 0 },
        { ST_S4, H_HELD, SEED_HELD + 300, 0, 0, -1, 0 },
        { ST_S5, H_HELD, SEED_HELD + 400, 0, 0, -1, 0 },
        { ST_S6, H_HELD, SEED_HELD + 500, 0, 0, 20, 0 },
        { ST_P0, H_HELD, SEED_HELD + 600, 0, 0, -1, 0 },
    };
    const uint32_t NS = sizeof st / sizeof st[0];
    Account acc[sizeof st / sizeof st[0]];
    for (uint32_t i = 0; i < NS; i++) {
        run_stream(&st[i]);
        acc[i] = account(&g_rows[st[i].first_row], st[i].n_rows);
        printf("    %s: hits %u misses %u acq %u adapt %u refusals %u restarts %u; net %lld ns, break-even %d, floors %s, neg transfer %u\n",
               stream_name(st[i].kind), acc[i].hits, acc[i].misses, acc[i].acquisitions, acc[i].adaptations, acc[i].refusals,
               acc[i].restarts, (long long)acc[i].net_final, acc[i].break_even, acc[i].floors_ok ? "ok" : "VIOLATED", acc[i].neg_transfer);
    }
    t_stale_control();
    t_corrupt_control();
    Ledger L = ledger_of(g_rows, g_nrows);
    uint64_t unatt;
    int bad;
    int ok = reconcile(g_rows, g_nrows, &L, &unatt, &bad);
    g_bad_rows += bad;
    CHECK(ok, "ledger reconciles with traces (%d bad)", bad);
    loadavg(load1, sizeof load1);
    /* Signs. */
    const Account *pp = &acc[1], *p0 = &acc[NS - 1];
    CHECK(pp->net_final > 0, "P+ sign positive (net %lld ns)", (long long)pp->net_final);
    CHECK(p0->net_final < 0, "P0 sign negative (net %lld ns)", (long long)p0->net_final);
    if (pp->net_final <= 0) g_bad_rows++;
    if (p0->net_final >= 0) g_bad_rows++;

    char path[640];
    snprintf(path, sizeof path, "%s/heldout-trace.csv", g_outdir);
    write_trace(path, load0, load1);
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    json_head(f, "heldout", load0, load1);
    fprintf(f, "  \"partition\": \"held-out: seeds %u.. per stream (+100 per stream), H %u; S2 cache warmed on development seeds %u..%u (sunk cost counted in S2_warmup_dev_seeds)\",\n",
            SEED_HELD, H_HELD, SEED_DEV, SEED_DEV + H_DEV - 1);
    fprintf(f, "  \"ledger\": {\n");
    json_ledger(f, &L);
    fprintf(f, "  },\n  \"reconciliation\": {\"ok\": %s, \"bad_rows\": %d, \"unattributed_wall_ns\": %llu, \"rows\": %u},\n",
            ok ? "true" : "false", bad, (unsigned long long)unatt, g_nrows);
    fprintf(f, "  \"streams\": {\n");
    for (uint32_t i = 0; i < NS; i++) { json_account(f, stream_name(st[i].kind), &acc[i]); fprintf(f, "%s\n", i + 1 < NS ? "," : ""); }
    fprintf(f, "  },\n");
    fprintf(f, "  \"controls\": {\"stale_refused\": %s, \"stale_rederived\": %s, \"stale_revalidated\": %s, "
               "\"corrupt_cases\": %d, \"corrupt_refused\": %d, \"corrupt_stored\": %d, \"corrupt_why\": [\"%s\", \"%s\", \"%s\"],\n"
               "    \"sign_P+\": \"%s\", \"sign_P0\": \"%s\", \"expected\": \"P+ positive, P0 negative\"},\n",
            CTL.stale_refused ? "true" : "false", CTL.stale_rederived ? "true" : "false", CTL.stale_revalidated ? "true" : "false",
            CTL.corrupt_cases, CTL.corrupt_refused, CTL.corrupt_stored, CTL.corrupt_why[0], CTL.corrupt_why[1], CTL.corrupt_why[2],
            pp->net_final > 0 ? "positive" : "negative", p0->net_final < 0 ? "negative" : "positive");
    fprintf(f, "  \"measured_for_prediction\": {\n");
    const char *pn[3] = { "S1_reverse", "S1_merge_P+", "P0_no_payoff" };
    const Account *pa[3] = { &acc[0], &acc[1], p0 };
    for (int i = 0; i < 3; i++)
        fprintf(f, "    \"%s\": {\"net_final_ns\": %lld, \"net_robust_ns\": %lld, \"task_net_median_ns\": %.0f, \"break_even_tasks\": %d, \"p_hat\": %.4f, \"r_hat\": %.4f, \"delta_median_ns\": %.0f, \"delta_q1_ns\": %.0f, \"delta_q3_ns\": %.0f}%s\n",
                pn[i], (long long)pa[i]->net_final, (long long)pa[i]->net_robust, pa[i]->task_net_med, pa[i]->break_even, pa[i]->p_hat, pa[i]->r_hat, pa[i]->delta_med, pa[i]->delta_q1, pa[i]->delta_q3, i < 2 ? "," : "");
    fprintf(f, "  }\n}\n");
    fclose(f);
    snprintf(path, sizeof path, "%s/heldout-receipt.json", g_outdir);
    write_file(path, buf, len);
    free(buf);
    printf("checks %d failures %d bad_rows %d\n", g_checks, g_fail, g_bad_rows);
    printf("receipt: %s\n", path);
    return g_fail ? 1 : 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    const char *mode = argc > 1 ? argv[1] : "dev";
    const char *m = getenv("PAYBACK_MUTANT");
    g_mutant = m ? m : "";
    const char *out = getenv("PAYBACK_OUT");
    snprintf(g_outdir, sizeof g_outdir, "%s", out ? out : "build/payback");
    mkdir(g_outdir, 0775);
    g_no_adapt = getenv("PAYBACK_NO_ADAPT") != NULL;
    if (g_no_adapt) printf("[*] ablation: arm B without the adaptation (repair) path: reuse only\n");
    place();
    memset(&g_skills, 0, sizeof g_skills);
    rx_plan_cache_init(&g_cache);
    g_a = calloc(1, sizeof *g_a);
    g_b = calloc(1, sizeof *g_b);
    if (!g_a || !g_b) return 2;
    if (g_mutant[0]) printf("[*] MUTANT %s active: this run must report bad rows > 0\n", g_mutant);
    int rc = strcmp(mode, "heldout") == 0 ? run_heldout() : run_dev();
    rx_plan_cache_free(&g_cache);
    return rc;
}
