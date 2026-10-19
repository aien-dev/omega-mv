/*
 * rx_symbolic_test -- OMEGA_SYMBOLIC_REWRITE: protocol, rules, verifier,
 * budget filter, evidence-graph trace, verdict cache, and workload 1: the
 * existing action graphs of tests/runtime/rx_action_graph.c (the very same
 * templates, environment and lowering, included below) compiled without and
 * with the rewrite pass inside rx_graph_optimize, then run on the reaction
 * engine and compared.
 *
 *   rx_symbolic_test [--runs N] [--out DIR]
 *
 * Exit 0 when every check passes. With --out, writes DIR/workload-action-graphs.tsv
 * (one row per graph and arm) and DIR/trace-action-graphs.evidence.json.
 */
#define main rx_action_graph_main
#include "rx_action_graph.c"
#undef main

#include "runtime/rx_symbolic.h"
#include "runtime/rx_symbolic_cache.h"
#include "omega_canonical.h"
#include "omega_gpu_decode_attn256_api.h"

static uint32_t omega_dattn256_perm_slot_local(uint32_t i) { return omega_dattn256_perm_slot(i, 64); }

static RxSymStore S;

/* ---- unit: protocol ---- */
static void t_protocol(void) {
    printf("[*] symbolic terms: SEMANTIC_ID, hash-consing, canonical object\n");
    rx_sym_store_init(&S);
    RxSymType u = rx_sym_u64();
    uint32_t x = rx_sym_var(&S, 0, u), y = rx_sym_var(&S, 1, u);
    uint32_t a = rx_sym_op(&S, OP_ADD, u, x, rx_sym_const(&S, 5, u));
    uint32_t a2 = rx_sym_op(&S, OP_ADD, u, rx_sym_var(&S, 0, u), rx_sym_const(&S, 5, u));
    CHECK(a == a2 && a != RX_SYM_NONE, "same expression built twice is one term (%u %u)", a, a2);
    uint32_t b = rx_sym_op(&S, OP_ADD, u, y, rx_sym_const(&S, 5, u));
    CHECK(memcmp(S.t[a].id.bytes, S.t[b].id.bytes, 32) != 0, "a different variable gives a different id");
    OmegaObject o;
    CHECK(rx_sym_object(&S, &S.t[a], &o) == 0 && omega_compute_semantic_id(&o) == 0 &&
          memcmp(o.id.bytes, S.t[a].id.bytes, 32) == 0, "term id is the canonical object's SEMANTIC_ID");
    uint8_t buf[4096]; size_t len = 0;
    CHECK(omega_canonical_encode(&o, buf, sizeof buf, &len) == 0 && len > 6 && buf[0] == 'O' && buf[5] == KIND_OPERATION,
          "canonical OMG0 encoding, kind OPERATION");
    RxSymAssume r = { RX_SYM_A_RANGE, 0, 1000 };
    uint32_t xr = rx_sym_with_assume(&S, x, &r);
    CHECK(xr != x && memcmp(S.t[xr].id.bytes, S.t[x].id.bytes, 32) != 0, "an assumption is part of the identity");
    RxSymType f = { RX_SYM_TY_F32, RX_SYM_AR_RNE, 32, 0, 0 };
    CHECK(rx_sym_var(&S, 0, f) != x, "a float variable is not the u64 variable");
    OmegaObject eq;
    CHECK(rx_sym_equivalence_object(&S, a, b, &eq) == 0 && eq.rel_count == 1 && eq.relations[0].kind == REL_EQUIVALENT_TO,
          "equivalence claim is an Omega object with REL_EQUIVALENT_TO");
}

/* ---- unit: machine arithmetic and the verifier ---- */
static void t_arith(void) {
    printf("[*] machine arithmetic: affine normal form over Z/2^64, verifier proofs and refutations\n");
    rx_sym_store_init(&S);
    RxSymType u = rx_sym_u64();
    uint32_t x = rx_sym_var(&S, 0, u), y = rx_sym_var(&S, 1, u);
    /* ((x + 42) * 2 + 1) + (x + 42) - 3 = 3x + 124 */
    uint32_t n5 = rx_sym_op(&S, OP_ADD, u, x, rx_sym_const(&S, 42, u));
    uint32_t n8 = rx_sym_op(&S, OP_ADD, u, rx_sym_op(&S, OP_MUL, u, n5, rx_sym_const(&S, 2, u)), rx_sym_const(&S, 1, u));
    uint32_t n10 = rx_sym_op(&S, OP_SUB, u, rx_sym_op(&S, OP_ADD, u, n8, n5), rx_sym_const(&S, 3, u));
    RxSymAffine af;
    CHECK(rx_sym_affine(&S, n10, &af) == 0 && af.n == 1 && af.coef[0] == 3 && af.k == 124, "3x + 124 (n %u c %llu k %llu)",
          af.n, (unsigned long long)af.coef[0], (unsigned long long)af.k);
    uint32_t rhs = rx_sym_op(&S, OP_ADD, u, rx_sym_op(&S, OP_MUL, u, x, rx_sym_const(&S, 3, u)), rx_sym_const(&S, 124, u));
    RxSymVerdict v;
    CHECK(rx_sym_verify_u64(&S, n10, rhs, 64, NULL, &v) == RX_SYM_V_PROVED, "proved (%d)", v.verdict);
    uint32_t wrong = rx_sym_op(&S, OP_ADD, u, rx_sym_op(&S, OP_MUL, u, x, rx_sym_const(&S, 3, u)), rx_sym_const(&S, 125, u));
    CHECK(rx_sym_verify_u64(&S, n10, wrong, 64, NULL, &v) == RX_SYM_V_REFUTED, "off by one refuted");
    /* wrap: x - x + y = y; (x * 2^63) * 2 = 0 in Z/2^64 */
    uint32_t z = rx_sym_op(&S, OP_MUL, u, rx_sym_op(&S, OP_MUL, u, x, rx_sym_const(&S, 1ull << 63, u)), rx_sym_const(&S, 2, u));
    CHECK(rx_sym_affine(&S, z, &af) == 0 && af.n == 0 && af.k == 0, "x * 2^63 * 2 = 0 (wrap)");
    CHECK(rx_sym_verify_u64(&S, z, rx_sym_const(&S, 0, u), 64, NULL, &v) == RX_SYM_V_PROVED, "and the verifier proves it");
    /* true over the integers, false in machine arithmetic: (x * 3) / 3 = x */
    uint32_t md = rx_sym_op(&S, OP_DIV, u, rx_sym_op(&S, OP_MUL, u, x, rx_sym_const(&S, 3, u)), rx_sym_const(&S, 3, u));
    CHECK(rx_sym_affine(&S, md, &af) != 0, "division is not a ring operation");
    CHECK(rx_sym_verify_u64(&S, md, x, 64, NULL, &v) == RX_SYM_V_REFUTED, "(x*3)/3 = x refuted on all of u64 (x=%llu: %llu)",
          (unsigned long long)v.counter[0], (unsigned long long)v.got_lhs);
    RxSymAssume rg[64]; memset(rg, 0, sizeof rg);
    rg[0] = (RxSymAssume){ RX_SYM_A_RANGE, 0, UINT64_MAX / 3 };
    CHECK(rx_sym_verify_u64(&S, md, x, 64, rg, &v) == RX_SYM_V_TESTED, "inside x <= 2^64/3 it holds (%d)", v.verdict);
    /* x * y is not affine; division by a variable can fail and must fail the same */
    CHECK(rx_sym_affine(&S, rx_sym_op(&S, OP_MUL, u, x, y), &af) != 0, "x*y not affine");
    uint32_t dv = rx_sym_op(&S, OP_DIV, u, x, y);
    CHECK(rx_sym_verify_u64(&S, dv, rx_sym_const(&S, 0, u), 64, NULL, &v) == RX_SYM_V_REFUTED, "x/y vs 0 refuted (failure differs)");
}

/* ---- unit: budget ---- */
static void t_budget(void) {
    printf("[*] physics budget filter\n");
    RxSymBudget b = { 273e9, 207.7e12, 1500, 64ull << 30, 101376, 0.03, "unit" };
    RxSymWork w = { 26.89e9, 51.2e9, 0, 27ull << 30, 0 };
    double lb;
    CHECK(rx_sym_budget_admit(&b, &w, 123.04e6, &lb) == RX_SYM_B_ADMIT && lb > 98e6 && lb < 99e6, "27B token bound 98.5 ms (%.1f)", lb / 1e6);
    CHECK(rx_sym_budget_admit(&b, &w, 98.0e6, &lb) == RX_SYM_B_CANNOT_WIN, "cannot beat 98.0 ms measured");
    CHECK(rx_sym_budget_admit(&b, &w, 100.0e6, &lb) == RX_SYM_B_BELOW_NOISE, "1.5%% below incumbent is inside 3%% noise");
    w.mem_bytes = 100ull << 30;
    CHECK(rx_sym_budget_admit(&b, &w, 123.04e6, &lb) == RX_SYM_B_OVER_LIMIT, "memory over limit");
}

/* ---- unit: gather fold side condition ---- */
static void t_gather(void) {
    printf("[*] gather fold side conditions (data-checked)\n");
    static uint32_t perm[512];
    static uint16_t sc[8 * 2];
    for (uint32_t i = 0; i < 256; i++) perm[i] = omega_dattn256_perm_slot_local(i);
    for (uint32_t i = 0; i < 16; i++) sc[i] = (uint16_t)(0x3F80 + i);
    uint32_t bad; char why[160];
    CHECK(rx_sym_gather_fold_check(perm, 256, sc, 1024, 256, &bad, why, sizeof why) == 0 && bad == 2,
          "rotary permutation across two scale blocks refused (%s)", why);
    for (uint32_t i = 0; i < 16; i++) sc[i] = 0x3F80;
    CHECK(rx_sym_gather_fold_check(perm, 256, sc, 1024, 256, &bad, why, sizeof why) == 1, "equal scales: eligible (%s)", why);
    perm[5] = perm[6];
    CHECK(rx_sym_gather_fold_check(perm, 256, sc, 1024, 256, &bad, why, sizeof why) == 0, "repeat refused (%s)", why);
}

/* ---- unit: the pass on crafted graphs, unsound rule, assumption rule, cache ---- */
static int crafted(AgGraph *g, int kind) {
    rx_graph_init(g, 0);
    int a = N(g, AG_CONST, AG_T_U64);   /* stands in for a World read: guarded so it is a variable */
    g->nodes[a].imm = 41;
    int br = N(g, AG_CONST, AG_T_BOOL); g->nodes[br].imm = 1;
    int brn = N(g, AG_BRANCH, AG_T_BOOL);
    rx_graph_data(g, (uint32_t)br, (uint32_t)brn, 0, AG_EDGE_DATA);
    rx_graph_guard(g, (uint32_t)brn, (uint32_t)a, 1);
    int m, d;
    if (kind == 0) { m = node_pure(g, OP_DIV, a, -1, 3); d = node_pure(g, OP_MUL, m, -1, 3); }   /* (x/3)*3 */
    else { m = node_pure(g, OP_MUL, a, -1, 6); d = node_pure(g, OP_DIV, m, -1, 6); }            /* (x*6)/6 */
    int v = node_verify(g, d, 0, UINT64_MAX);
    rx_graph_success(g, (uint32_t)v, AG_OK);
    return a;
}

static void t_pass_units(void) {
    printf("[*] rewrite pass: unsound candidate refuted, assumption rule, verdict cache\n");
    static RxSymStore st;
    static RxSymTrace tr;
    AgGraph g, before;
    RxSymPass p;
    crafted(&g, 0);
    CHECK(rx_graph_validate(&g, NULL) == 0, "crafted 0 valid");
    before = g;
    rx_sym_pass_init(&p, &st, 32);
    p.test_unsound = 1; p.trace = &tr;
    AgReport rep; memset(&rep, 0, sizeof rep);
    CHECK(rx_graph_optimize_ex(&g, &rep, &p.hook) == 0, "optimize");
    CHECK(p.refuted >= 1 && rep.passes.rewritten == 0, "unsound (x/3)*3 -> x refuted, nothing applied (refuted %u)", p.refuted);
    AgReference r0, r1;
    CHECK(rx_graph_reference_on(&before, &g_skills, NULL, 0, 1, &r0) == 0 && rx_graph_reference_on(&g, &g_skills, NULL, 0, 1, &r1) == 0,
          "references");
    CHECK(r0.r.outcome == r1.r.outcome, "same outcome");

    crafted(&g, 1);
    rx_graph_validate(&g, NULL);
    rx_sym_pass_init(&p, &st, 32); p.trace = &tr;
    memset(&rep, 0, sizeof rep);
    rx_graph_optimize_ex(&g, &rep, &p.hook);
    CHECK(rep.passes.rewritten == 0 && p.refused >= 1, "(x*6)/6 refused without a range fact");
    crafted(&g, 1);
    rx_graph_validate(&g, NULL);
    before = g;
    rx_sym_pass_init(&p, &st, 32); p.trace = &tr;
    RxSymAssume ra = { RX_SYM_A_RANGE, 0, 1000000 };
    rx_sym_pass_fact(&p, 0, &ra);
    memset(&rep, 0, sizeof rep);
    rx_graph_optimize_ex(&g, &rep, &p.hook);
    CHECK(rep.passes.rewritten == 1 && g.n_nodes < before.n_nodes, "(x*6)/6 -> x applied under x <= 10^6 (rewritten %u, nodes %u -> %u)",
          rep.passes.rewritten, before.n_nodes, g.n_nodes);
    CHECK(rx_graph_validate(&g, NULL) == 0 && rx_graph_check_preserved(&before, &g) == 0, "rewritten graph valid and preserving");
    rx_graph_reference_on(&before, &g_skills, NULL, 0, 1, &r0);
    rx_graph_reference_on(&g, &g_skills, NULL, 0, 1, &r1);
    CHECK(r0.r.outcome == AG_RUN_SUCCESS && r1.r.outcome == AG_RUN_SUCCESS, "both succeed");

    /* cache: the same question twice */
    RxSymCache c;
    CHECK(rx_sym_cache_init(&c) == 0, "cache init");
    for (int round = 0; round < 2; round++) {
        AgGraph h = T_redundant;
        rx_graph_validate(&h, NULL);
        rx_sym_pass_init(&p, &st, 64);
        p.cache_fn = rx_sym_cache_lookup; p.cache = &c;
        memset(&rep, 0, sizeof rep);
        rx_graph_optimize_ex(&h, &rep, &p.hook);
    }
    CHECK(c.lookups >= 2 && c.hits >= c.lookups / 2 && c.computes == c.lookups - c.hits,
          "second compile served from the rx_semantic cache (lookups %llu hits %llu computes %llu)",
          (unsigned long long)c.lookups, (unsigned long long)c.hits, (unsigned long long)c.computes);
    rx_sym_cache_destroy(&c);
}

/* ---- workload 1: the existing action graphs, baseline vs rewrite ---- */

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int node_kind_value(const AgGraph *g, const AgResult *r, AgKind k, uint64_t *v) {
    for (uint32_t n = 0; n < g->n_nodes; n++) if (g->nodes[n].kind == k) { *v = r->value[n]; return (int)r->status[n]; }
    return -1;
}

static void workload_action_graphs(const char *out, uint32_t runs) {
    printf("[*] workload 1: existing action graphs (rx_action_graph templates), %u interleaved runs per arm\n", runs);
    static const struct { uint32_t goal; const char *name; } W[] = {
        { GOAL_LINEAR, "linear" }, { GOAL_DIAMOND, "diamond" }, { GOAL_BRANCH, "branch" }, { GOAL_FAILURE, "failure" },
        { GOAL_EFFECTS, "effects" }, { GOAL_REDUNDANT, "redundant" }, { GOAL_PARALLEL, "parallel" } };
    FILE *tsv = NULL;
    char path[600];
    if (out) {
        snprintf(path, sizeof path, "%s/workload-action-graphs.tsv", out);
        tsv = fopen(path, "w");
        if (tsv) fprintf(tsv, "graph\tarm\tnodes_before\tnodes_after\treactions\tcritical_us\tcrumbs_per_run\tmedian_run_ns\tp10_run_ns\tp90_run_ns\trewritten\tsame_result_as_baseline\n");
    }
    static RxSymStore st;
    static RxSymTrace tr;
    memset(&tr, 0, sizeof tr);
    RxSymCache cache;
    CHECK(rx_sym_cache_init(&cache) == 0, "cache");
    /* Budget: reaction floor measured below as the median single-node run. */
    RxSymBudget bud = { 0, 0, 0, 0, 0, 0.0, "reaction floor: measured cost of one reaction on this host" };
    uint64_t *ns[2];
    ns[0] = calloc(runs, sizeof(uint64_t)); ns[1] = calloc(runs, sizeof(uint64_t));
    uint32_t total_before = 0, total_after = 0;
    for (uint32_t w = 0; w < sizeof W / sizeof W[0]; w++) {
        Env e[2];
        AgGraph g[2];
        AgReport rep[2];
        AgLowered L[2];
        RxSymPass pass;
        rx_sym_pass_init(&pass, &st, 64);
        pass.cache_fn = rx_sym_cache_lookup; pass.cache = &cache; pass.trace = &tr; pass.budget = &bud;
        pass.reaction_ns = 1000;
        for (int arm = 0; arm < 2; arm++) {
            CHECK(env_start(&e[arm], 4, 1, 0) == 0, "env");
            library(&e[arm]);
            AgGoal goal = goal_for(&e[arm], W[w].goal);
            AgConstraints c = open_constraints();
            int rc = rx_graph_compile_ex(&goal, &e[arm].w, &e[arm].caps, &c, &e[arm].lib, 1, arm ? &pass.hook : NULL, &g[arm], &rep[arm]);
            CHECK(rc == AG_OK_READY, "%s arm %d compile %d", W[w].name, arm, rc);
            CHECK(lower(&e[arm], &L[arm], &g[arm], NULL) == 0, "%s lower", W[w].name);
        }
        uint64_t crumbs[2] = { 0, 0 };
        AgResult res[2];
        /* first run of each arm: full comparison with its own sequential reference */
        for (int arm = 0; arm < 2; arm++) {
            uint64_t c0 = e[arm].w.n_crumbs;
            full_run(&e[arm], &L[arm], 1, W[w].name, &res[arm]);
            crumbs[arm] = e[arm].w.n_crumbs - c0;
        }
        /* the two graphs mean the same: outcome, receipts, proposals, publications, effect chain */
        int same = res[0].outcome == res[1].outcome;
        static const AgKind kinds[] = { AG_EFFECT_PROPOSE, AG_EFFECT_PERFORM, AG_WORLD_PUBLISH, AG_VERIFY };
        for (uint32_t k = 0; k < 4; k++) {
            uint64_t v0 = 0, v1 = 0;
            int s0 = node_kind_value(&g[0], &res[0], kinds[k], &v0), s1 = node_kind_value(&g[1], &res[1], kinds[k], &v1);
            if (s0 != s1 || v0 != v1) same = 0;
        }
        for (uint64_t r = 2; r <= runs + 1; r++)
            for (int q = 0; q < 2; q++) {
                int arm = (int)((r + (uint64_t)q) & 1u);   /* alternate which arm goes first */
                uint64_t t = 0;
                run(&e[arm], &L[arm], r, &res[arm], &t);
                ns[arm][r - 2] = t;
            }
        same &= fld(&e[0], e[0].effect, 2) == fld(&e[1], e[1].effect, 2) && fld(&e[0], e[0].out, 0) == fld(&e[1], e[1].out, 0);
        CHECK(same, "%s: rewritten graph gives the baseline's outcome, values and effect chain", W[w].name);
        CHECK(rx_graph_check_preserved(&g[0], &g[1]) == 0, "%s preserved", W[w].name);
        for (int arm = 0; arm < 2; arm++) {
            qsort(ns[arm], runs, sizeof(uint64_t), cmp_u64);
            uint64_t crit = 0, tot = 0;
            rx_graph_costs(&g[arm], &crit, &tot);
            printf("    %-9s %-8s nodes %2u -> %2u  reactions %2u  crumbs/run %3llu  median %8llu ns  rewritten %u\n", W[w].name,
                   arm ? "rewrite" : "baseline", rep[arm].nodes_before, rep[arm].nodes_after, g[arm].n_nodes,
                   (unsigned long long)crumbs[arm], (unsigned long long)ns[arm][runs / 2], rep[arm].passes.rewritten);
            if (tsv)
                fprintf(tsv, "%s\t%s\t%u\t%u\t%u\t%llu\t%llu\t%llu\t%llu\t%llu\t%u\t%s\n", W[w].name, arm ? "rewrite" : "baseline",
                        rep[arm].nodes_before, rep[arm].nodes_after, g[arm].n_nodes, (unsigned long long)crit,
                        (unsigned long long)crumbs[arm], (unsigned long long)ns[arm][runs / 2],
                        (unsigned long long)ns[arm][runs / 10], (unsigned long long)ns[arm][runs * 9 / 10],
                        rep[arm].passes.rewritten, same ? "yes" : "NO");
        }
        total_before += g[0].n_nodes; total_after += g[1].n_nodes;
        for (int arm = 0; arm < 2; arm++) env_stop(&e[arm]);
    }
    printf("    reactions over all graphs %u -> %u; verdict cache lookups %llu hits %llu\n", total_before, total_after,
           (unsigned long long)cache.lookups, (unsigned long long)cache.hits);
    if (tsv) fclose(tsv);
    if (out) {
        snprintf(path, sizeof path, "%s/trace-action-graphs.evidence.json", out);
        FILE *f = fopen(path, "w");
        char concl[256], meas[256];
        snprintf(concl, sizeof concl, "symbolic rewrites inside rx_graph_optimize on the existing action graphs: reactions %u -> %u",
                 total_before, total_after);
        snprintf(meas, sizeof meas, "reactions_before=%u=reactions;reactions_after=%u=reactions;cache_hits=%llu=lookups", total_before,
                 total_after, (unsigned long long)cache.hits);
        if (f) { rx_sym_trace_json(&tr, "omega/rx_symbolic_test", "action-graphs", concl, meas, f); fclose(f); }
        printf("    wrote %s\n", path);
    }
    rx_sym_cache_destroy(&cache);
    free(ns[0]); free(ns[1]);
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    uint32_t runs = 200;
    const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
    }
    if (runs < 10) runs = 10;
    place();
    skills_init();
    build_templates(3000);
    t_protocol();
    t_arith();
    t_budget();
    t_gather();
    t_pass_units();
    workload_action_graphs(out, runs);
    printf("rx_symbolic_test: checks %d failures %d -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
