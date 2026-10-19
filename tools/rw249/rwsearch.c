/*
 * rwsearch -- bounded rewrite search for #249 E06, three arms under one budget (ledger L059).
 *
 * Read: aien-dev/aien-architecture-mv#249 (Wolfram-method extension, 2026-10-19); omega a8ffd2b9
 * src/omega_program.h (V0 body, omega_program_emit_schedule), src/runtime/rx_costmodel.h
 * (rx_cm_predict, `known`); aien-dev/omega-mv#583 OEV1 spec at omega-research-583 d8c74201.
 * Design note: docs/research/249-checked-elimination.md.
 *
 * Shares no source with rwcheck.c. Rules are implemented here and only labelled with a contract
 * id; rwcheck decides whether each labelled step really is an instance of that contract.
 *
 *   rwsearch gen       --seed S --n N                      expression set on stdout
 *   rwsearch calibrate --out BLOB [--obs K]                fit rx_costmodel arms from native timing
 *   rwsearch run       --arm M|G|B|Bi --set F --out DIR [--model BLOB] [--nodes N] [--evals N]
 *                      [--cpu-us N] [--inject-probe-rule]
 *   rwsearch exec      --dir DIR [--verdicts F] [--calls N]   time original vs accepted output
 *   rwsearch account   --dir DIR --verdicts F [--model BLOB] [--oev DIR] [--build SHA]
 *   rwsearch verify-accepted --dir DIR --verdicts F        independent end-to-end equivalence check
 *   rwsearch unknown-test                                  unknown cost is never ranked as free
 *
 * UNVERIFIED: the held-out generator's constant distribution (gen_const).
 * UNVERIFIED: additive per-step cost; core class reported as RX_CM_CORE_OTHER (not identified).
 * UNVERIFIED: whole-term breadth-first exploration in place of a subterm-sharing e-graph.
 */
#include "omega_program.h"
#include "runtime/rx_costmodel.h"

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

#pragma GCC diagnostic ignored "-Wformat-truncation"

#define MAXS 8           /* chain steps: rules never lengthen a chain; inputs have at most 6 */
#define MAXD 64          /* derivation steps kept per expression */
enum { O_ADD = 1, O_SUB, O_MUL, O_AND, O_OR };
static const char *ONAME[] = {"?", "add", "sub", "mul", "and", "or"};
static const uint8_t OMEGA_OP[] = {0, OP_ADD, OP_SUB, OP_MUL, OP_AND, OP_OR};

typedef struct {
    uint8_t n;
    uint8_t op[MAXS];
    uint64_t c[MAXS];
    uint16_t id[MAXS];   /* stable op ids for OEV1: original step index, kept through rewrites */
} Ch;

static uint64_t wmask(unsigned w) { return w >= 64 ? ~0ull : ((1ull << w) - 1); }
static double now(clockid_t k) { struct timespec t; clock_gettime(k, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }

static void die(const char *m) { fprintf(stderr, "rwsearch: %s\n", m); exit(2); }

static void ch_str(const Ch *c, char *o, size_t cap) {
    o[0] = 0;
    if (!c->n) { snprintf(o, cap, "id"); return; }
    for (int i = 0; i < c->n; ++i) {
        size_t l = strlen(o);
        snprintf(o + l, cap - l, "%s%s 0x%llx", i ? "; " : "", ONAME[c->op[i]], (unsigned long long)c->c[i]);
    }
}

static int ch_parse(const char *s, Ch *c) {
    memset(c, 0, sizeof *c);
    while (*s == ' ') s++;
    if (!strncmp(s, "id", 2)) return 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", s);
    char *save;
    for (char *p = strtok_r(buf, ";", &save); p; p = strtok_r(NULL, ";", &save)) {
        char o[8]; unsigned long long v;
        { char vs[40]; char *e; if (sscanf(p, " %7s %39s", o, vs) != 2) return -1; errno = 0; v = strtoull(vs, &e, 0); if (errno || *e) return -1; }
        int k = 0;
        for (int j = 1; j <= 5; ++j) if (!strcmp(o, ONAME[j])) k = j;
        if (!k || c->n >= MAXS) return -1;
        c->op[c->n] = (uint8_t)k; c->c[c->n] = v; c->id[c->n] = c->n; c->n++;
    }
    return 0;
}

static int ch_eq(const Ch *a, const Ch *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; ++i) if (a->op[i] != b->op[i] || a->c[i] != b->c[i]) return 0;
    return 1;
}

__attribute__((unused)) static uint64_t ch_eval(const Ch *c, unsigned w, uint64_t x) {
    uint64_t m = wmask(w);
    x &= m;
    for (int i = 0; i < c->n; ++i) {
        uint64_t k = c->c[i];
        switch (c->op[i]) {
        case O_ADD: x = (x + k) & m; break;
        case O_SUB: x = (x - k) & m; break;
        case O_MUL: x = (x * k) & m; break;
        case O_AND: x &= k; break;
        case O_OR: x |= k; break;
        }
    }
    return x;
}

/* ------------------------------------------------------------- rules -- */
/* Each rule rewrites the fragment starting at step `at` (innermost first). Returns 1 if it applied. */
static void del(Ch *c, int at) { for (int i = at; i + 1 < c->n; ++i) { c->op[i] = c->op[i + 1]; c->c[i] = c->c[i + 1]; c->id[i] = c->id[i + 1]; } c->n--; }
static uint64_t opk(int op, uint64_t a, uint64_t b, uint64_t m) {
    switch (op) { case O_ADD: return (a + b) & m; case O_SUB: return (a - b) & m; case O_MUL: return (a * b) & m;
    case O_AND: return a & b; default: return a | b; }
}

static int g_inject;   /* test only: a probe-agreeing bogus rule */

static int rule(int r, const Ch *in, int at, unsigned w, Ch *o) {
    const uint64_t m = wmask(w);
    *o = *in;
    int n = in->n;
    int two = at + 1 < n, three = at + 2 < n;
    uint8_t p = in->op[at], q = two ? in->op[at + 1] : 0;
    uint64_t a = in->c[at], b = two ? in->c[at + 1] : 0;
    switch (r) {
    case 1: if (!two || p != O_ADD || q != O_ADD) return 0; o->c[at] = (a + b) & m; del(o, at + 1); return 1;
    case 2: if (!two || p != O_SUB || q != O_SUB) return 0; o->c[at] = (a + b) & m; del(o, at + 1); return 1;
    case 3: if (!two || p != O_ADD || q != O_SUB) return 0; o->c[at] = (a - b) & m; del(o, at + 1); return 1;
    case 4: if (!two || p != O_SUB || q != O_ADD) return 0; o->op[at] = O_ADD; o->c[at] = (b - a) & m; del(o, at + 1); return 1;
    case 5: if (!two || p != O_MUL || q != O_MUL) return 0; o->c[at] = (a * b) & m; del(o, at + 1); return 1;
    case 6: if (!two || p != O_AND || q != O_AND) return 0; o->c[at] = a & b; del(o, at + 1); return 1;
    case 7: if (!two || p != O_OR || q != O_OR) return 0; o->c[at] = a | b; del(o, at + 1); return 1;
    case 8: if (p != O_ADD || a) return 0; del(o, at); return 1;
    case 9: if (p != O_SUB || a) return 0; del(o, at); return 1;
    case 10: if (p != O_MUL || a != 1) return 0; del(o, at); return 1;
    case 11: if (p != O_AND || a != m) return 0; del(o, at); return 1;
    case 12: if (p != O_OR || a) return 0; del(o, at); return 1;
    case 13: if (!two || q != O_AND || b) return 0; del(o, at); return 1;
    case 14: if (!two || q != O_MUL || b) return 0; del(o, at); return 1;
    case 15: if (!two || q != O_OR || b != m) return 0; del(o, at); return 1;
    case 16: if (!two || p != O_AND || a || q == O_OR) return 0; o->op[at + 1] = O_OR; o->c[at + 1] = opk(q, 0, b, m); return 1;
    case 17: if (!three || p != O_AND || a || q != O_OR) return 0;
             o->c[at + 1] = opk(in->op[at + 2], b, in->c[at + 2], m); del(o, at + 2); return 1;
    case 18: if (!two || p != O_ADD || q != O_MUL) return 0;
             o->op[at] = O_MUL; o->c[at] = b; o->id[at] = in->id[at + 1];
             o->op[at + 1] = O_ADD; o->c[at + 1] = (a * b) & m; o->id[at + 1] = in->id[at]; return 1;
    case 19: if (!two || p != O_OR || q != O_AND) return 0;
             o->op[at] = O_AND; o->c[at] = b; o->id[at] = in->id[at + 1];
             o->op[at + 1] = O_OR; o->c[at + 1] = a & b; o->id[at + 1] = in->id[at]; return 1;
    case 20: if (p != O_SUB) return 0; o->op[at] = O_ADD; o->c[at] = (0 - a) & m; return 1;
    case 21: if (p != O_ADD) return 0; o->op[at] = O_SUB; o->c[at] = (0 - a) & m; return 1;
    case 22: if (!two || p != O_AND || q != O_OR || (a | b) != m) return 0; del(o, at); return 1;
    case 23: if (!two || p != O_OR || q != O_AND || (a & b) != 0) return 0; del(o, at); return 1;
    case 24: if (!two || p != O_SUB || q != O_MUL) return 0;
             o->op[at] = O_MUL; o->c[at] = b; o->id[at] = in->id[at + 1];
             o->op[at + 1] = O_SUB; o->c[at + 1] = (a * b) & m; o->id[at + 1] = in->id[at]; return 1;
    case 25: if (!two || p != O_AND || q != O_OR) return 0;
             o->op[at] = O_OR; o->c[at] = b; o->id[at] = in->id[at + 1];
             o->op[at + 1] = O_AND; o->c[at + 1] = a | b; o->id[at + 1] = in->id[at]; return 1;
    case 99: if (!g_inject || w != 8 || p != O_AND || a != 63) return 0; del(o, at); return 1;   /* BOGUS: probe-only */
    }
    return 0;
}

static const int RULES_M[] = {1, 2, 5, 6, 7, 8, 9, 10, 11, 12};
static const int RULES_ALL[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 99};
#define NRULES_M (int)(sizeof RULES_M / sizeof RULES_M[0])
#define NRULES_ALL (int)(sizeof RULES_ALL / sizeof RULES_ALL[0])

/* --------------------------------------------------------------- cost -- */
static int body_of(const Ch *c, OmegaProgramBody *b) {
    memset(b, 0, sizeof *b);
    b->has_body = true;
    b->step_count = c->n;
    for (int i = 0; i < c->n; ++i) { b->steps[i].op = OMEGA_OP[c->op[i]]; b->steps[i].imm = c->c[i]; }
    return 0;
}

/* Arm G's ranking: emitted instruction count of the canonical (sequential) realization. */
static uint32_t insn_count(const Ch *c, unsigned w) {
    OmegaProgramBody b; body_of(c, &b);
    uint8_t code[4096]; size_t len = 0;
    if (omega_program_emit_schedule(&b, (uint16_t)w, OMEGA_SCHED_SEQUENTIAL, code, &len, sizeof code)) return UINT32_MAX;
    return (uint32_t)(len / 4);
}

/* rx_costmodel arms used as scalar step classes (stand-in; see the design note). */
enum { ARM_ADD = 0, ARM_SUB, ARM_MUL, ARM_AND, ARM_OR, ARM_MOVK, ARM_BASE, ARM_UNOBSERVED };
static int arm_of(int op) { return op == O_ADD ? ARM_ADD : op == O_SUB ? ARM_SUB : op == O_MUL ? ARM_MUL : op == O_AND ? ARM_AND : ARM_OR; }
static int movk_count(uint64_t c) { int k = 0; for (int s = 16; s < 64; s += 16) if ((c >> s) & 0xFFFF) k++; return k; }

static RxCostModel *g_model;
static RxCmFeatures cm_feat(void) {
    RxCmFeatures f; memset(&f, 0, sizeof f);
    f.op = RX_CM_OP_MATVEC; f.M = 1; f.N = 1; f.state_bytes = 8; f.cache_bytes = 1u << 20;
    f.core = RX_CM_CORE_OTHER; f.pressure = 0; f.eligible = 0xFF; f.need_evidence = 1;
    return f;
}
static int g_mutant_unknown_zero;   /* M8: unknown cost counted as zero */
/* predicted ps of one arm; returns 0 if unknown */
static int arm_ps(int arm, double *ps) {
    RxCmFeatures f = cm_feat();
    RxCmPrediction p;
    rx_cm_predict(g_model, &f, (uint32_t)arm, &p);
    if (!p.known) { if (g_mutant_unknown_zero) { *ps = 0; return 1; } return 0; }
    *ps = exp2(p.mean_log2_ps);
    return 1;
}
/* candidate cost in ps; returns 0 (ineligible) if any part is unknown */
static int cm_cost(const Ch *c, double *ps) {
    double s = 0, v;
    if (!arm_ps(ARM_BASE, &v)) return 0;
    s += v;
    for (int i = 0; i < c->n; ++i) {
        if (!arm_ps(arm_of(c->op[i]), &v)) return 0;
        s += v;
        int k = movk_count(c->c[i]);
        if (k) { if (!arm_ps(ARM_MOVK, &v)) return 0; s += k * v; }
    }
    *ps = s;
    return 1;
}

/* ------------------------------------------------------------- search -- */
typedef struct { Ch ch; int parent; int rule; } Node;
typedef struct {
    int nodes_max, evals_max;
    double cpu_ns_max;
} Budget;
typedef struct {
    Ch out;
    int nder;
    int der_rule[MAXD];
    Ch der_ch[MAXD];
    int nodes, evals, budget_hit, unranked;
    uint32_t insn_in, insn_out;
    double cost_in, cost_out;
    int cost_known_in, cost_known_out;
} Result;

static uint64_t ch_hash(const Ch *c) {
    uint64_t h = 1469598103934665603ull ^ c->n;
    for (int i = 0; i < c->n; ++i) { h = (h ^ c->op[i]) * 1099511628211ull; h = (h ^ c->c[i]) * 1099511628211ull; }
    return h;
}

static uint64_t g_match_attempts;
static int g_profile;
static volatile uint64_t g_sink;

static void finish(Result *r, const Ch *in, unsigned w) {
    r->insn_in = insn_count(in, w);
    r->insn_out = insn_count(&r->out, w);
    r->cost_known_in = g_model ? cm_cost(in, &r->cost_in) : 0;
    r->cost_known_out = g_model ? cm_cost(&r->out, &r->cost_out) : 0;
}

static void arm_manual(const Ch *in, unsigned w, const Budget *bg, Result *r) {
    Ch cur = *in, nx;
    double t0 = now(CLOCK_THREAD_CPUTIME_ID);
    r->nodes = 1;
    for (;;) {
        int applied = 0;
        for (int at = 0; at < cur.n && !applied; ++at)
            for (int k = 0; k < NRULES_M && !applied; ++k) {
                r->evals++;
                if (rule(RULES_M[k], &cur, at, w, &nx)) {
                    applied = 1;
                    if (r->nder < MAXD) { r->der_rule[r->nder] = RULES_M[k]; r->der_ch[r->nder] = nx; r->nder++; }
                    cur = nx; r->nodes++;
                }
            }
        if (!applied) break;
        if (r->evals >= bg->evals_max || r->nodes >= bg->nodes_max || now(CLOCK_THREAD_CPUTIME_ID) - t0 > bg->cpu_ns_max) { r->budget_hit = 1; break; }
    }
    r->out = cur;
}

static void arm_greedy(const Ch *in, unsigned w, const Budget *bg, Result *r) {
    Ch cur = *in, nx, best;
    double t0 = now(CLOCK_THREAD_CPUTIME_ID);
    r->nodes = 1;
    uint32_t ci = insn_count(&cur, w);
    for (;;) {
        uint32_t bi = ci; int brule = 0;
        for (int at = 0; at < cur.n; ++at)
            for (int k = 0; k < NRULES_ALL; ++k) {
                if (!rule(RULES_ALL[k], &cur, at, w, &nx)) continue;
                r->evals++;
                uint32_t ni = insn_count(&nx, w);
                if (ni < bi) { bi = ni; best = nx; brule = RULES_ALL[k]; }
            }
        if (!brule) break;
        if (r->nder < MAXD) { r->der_rule[r->nder] = brule; r->der_ch[r->nder] = best; r->nder++; }
        cur = best; ci = bi; r->nodes++;
        if (r->evals >= bg->evals_max || r->nodes >= bg->nodes_max || now(CLOCK_THREAD_CPUTIME_ID) - t0 > bg->cpu_ns_max) { r->budget_hit = 1; break; }
    }
    r->out = cur;
}

/* Bounded exploration of one equivalence class: breadth-first over whole terms. rank_cm: 1 cost
 * model (eligibility first), 0 instruction count. */
static Node *g_nodes;
static int *g_tab;
static void arm_bounded(const Ch *in, unsigned w, const Budget *bg, int rank_cm, Result *r) {
    int cap = bg->nodes_max, tsz = 1;
    while (tsz < 2 * cap) tsz <<= 1;
    for (int i = 0; i < tsz; ++i) g_tab[i] = -1;
    int nn = 0;
    g_nodes[nn].ch = *in; g_nodes[nn].parent = -1; g_nodes[nn].rule = 0;
    g_tab[ch_hash(in) & (uint64_t)(tsz - 1)] = nn++;
    double t0 = now(CLOCK_THREAD_CPUTIME_ID);
    int head = 0;
    while (head < nn) {
        if (r->evals >= bg->evals_max || now(CLOCK_THREAD_CPUTIME_ID) - t0 > bg->cpu_ns_max) { r->budget_hit = 1; break; }
        Ch cur = g_nodes[head].ch;
        for (int at = 0; at < cur.n; ++at)
            for (int k = 0; k < NRULES_ALL; ++k) {
                Ch nx;
                int ok = rule(RULES_ALL[k], &cur, at, w, &nx);
                g_match_attempts++;
                if (g_profile) { Ch dup; g_sink ^= (uint64_t)rule(RULES_ALL[k], &cur, at, w, &dup) + dup.n; }   /* profile: every match done twice; the time difference is the matching cost */
                if (!ok) continue;
                r->evals++;
                uint64_t h = ch_hash(&nx) & (uint64_t)(tsz - 1);
                int seen = 0;
                while (g_tab[h] >= 0) { if (ch_eq(&g_nodes[g_tab[h]].ch, &nx)) { seen = 1; break; } h = (h + 1) & (uint64_t)(tsz - 1); }
                if (seen) continue;
                if (nn >= cap) { r->budget_hit = 1; continue; }
                g_nodes[nn].ch = nx; g_nodes[nn].parent = head; g_nodes[nn].rule = RULES_ALL[k];
                g_tab[h] = nn++;
            }
        head++;
    }
    r->nodes = nn;
    /* extraction: eligibility first, then ranking */
    int best = 0;
    double bc = 0; uint32_t bi = insn_count(in, w);
    int have = 1;
    if (rank_cm) {
        if (!cm_cost(in, &bc)) { r->unranked = 1; r->out = *in; return; }   /* the input's own cost unknown: rank nothing */
    }
    for (int i = 1; i < nn && have; ++i) {
        uint32_t ii = insn_count(&g_nodes[i].ch, w);
        if (rank_cm) {
            double c;
            if (!cm_cost(&g_nodes[i].ch, &c)) continue;              /* unknown cost: ineligible, never free */
            if (c < bc || (c == bc && ii < bi)) { best = i; bc = c; bi = ii; }
        } else if (ii < bi) { best = i; bi = ii; }
    }
    r->out = g_nodes[best].ch;
    int path[512], np = 0;
    for (int i = best; i > 0 && np < 512; i = g_nodes[i].parent) path[np++] = i;
    for (int k = np - 1; k >= 0 && r->nder < MAXD; --k) {
        r->der_rule[r->nder] = g_nodes[path[k]].rule; r->der_ch[r->nder] = g_nodes[path[k]].ch; r->nder++;
    }
}

/* -------------------------------------------------------- expression set -- */
typedef struct { char id[16]; unsigned w; Ch ch; } Expr;

static uint64_t g_rng;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

static uint64_t gen_const(unsigned w, uint64_t prev) {
    uint64_t m = wmask(w), r = rnd() % 100;
    unsigned k = (unsigned)(rnd() % w);
    if (r < 15) return 0;
    if (r < 25) return 1;
    if (r < 45) return (2 + rnd() % 14) & m;
    if (r < 55) return m;
    if (r < 70) return ((1ull << k) - 1) & m;
    if (r < 80) return m ^ (((1ull << k) - 1) & m);
    if (r < 90) return (1ull << k) & m;
    return (~prev) & m;
}

static int load_set(const char *path, Expr **out) {
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open expression set");
    int cap = 1024, n = 0;
    Expr *e = calloc((size_t)cap, sizeof *e);
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (n >= cap) die("set too large");
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *ws = strstr(line, " w=");
        if (!ws) die("bad set line");
        *ws = 0;
        snprintf(e[n].id, sizeof e[n].id, "%s", line);
        e[n].w = (unsigned)strtoul(ws + 3, &ws, 10);
        if (ch_parse(ws, &e[n].ch)) die("bad chain in set");
        n++;
    }
    fclose(f);
    *out = e;
    return n;
}

/* ---------------------------------------------------------- native exec -- */
typedef uint64_t (*Fn)(uint64_t, uint64_t, uint64_t);
static Fn map_chain(const Ch *c, unsigned w, void **mem, size_t *sz) {
    OmegaProgramBody b; body_of(c, &b);
    uint8_t code[4096]; size_t len = 0;
    if (omega_program_emit_schedule(&b, (uint16_t)w, OMEGA_SCHED_SEQUENTIAL, code, &len, sizeof code)) die("emit refused");
    *sz = 4096;
    void *p = mmap(NULL, *sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) die("mmap");
    memcpy(p, code, len);
    __builtin___clear_cache((char *)p, (char *)p + len);
    if (mprotect(p, *sz, PROT_READ | PROT_EXEC)) die("mprotect");
    *mem = p;
    return (Fn)p;
}

/* ps per call, dependent chain x = f(x) over `calls` calls */
static double time_fn(Fn f, uint64_t m, uint32_t calls) {
    uint64_t x = 0x1234 & m;
    double t0 = now(CLOCK_MONOTONIC);
    for (uint32_t i = 0; i < calls; ++i) x = f((x + i) & m, 0, 0);
    double t1 = now(CLOCK_MONOTONIC);
    g_sink = x;
    return (t1 - t0) * 1000.0 / calls;
}

static void pin_cpu(void) {
    int cpu = sched_getcpu();
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    sched_setaffinity(0, sizeof s, &s);
}

/* ------------------------------------------------------------------ modes -- */
static int mode_gen(uint64_t seed, int n) {
    g_rng = seed * 0x9E3779B97F4A7C15ull + 1;
    static const unsigned W[4] = {8, 16, 32, 64};
    printf("# rw249 expression set: seed=%llu n=%d generator=gen_const v1 (UNVERIFIED distribution)\n", (unsigned long long)seed, n);
    for (int i = 0; i < n; ++i) {
        unsigned w = W[rnd() % 4];
        int len = 1 + (int)(rnd() % 6);
        Ch c; memset(&c, 0, sizeof c);
        uint64_t prev = 0;
        for (int k = 0; k < len; ++k) { c.op[k] = (uint8_t)(1 + rnd() % 5); c.c[k] = gen_const(w, prev); prev = c.c[k]; c.n++; }
        char s[512]; ch_str(&c, s, sizeof s);
        printf("S%llu-%04d w=%u %s\n", (unsigned long long)seed, i, w, s);
    }
    return 0;
}

static int mode_calibrate(const char *out, int obs) {
    pin_cpu();
    RxCostModel *m = calloc(1, sizeof *m);
    rx_cm_init(m, RX_CM_ARMS);
    RxCmFeatures f = cm_feat();
    const int K = 48;
    const uint32_t CALLS = 200000;
    Ch empty; memset(&empty, 0, sizeof empty);
    void *mem0; size_t sz0; Fn f0 = map_chain(&empty, 64, &mem0, &sz0);
    Fn fc[6]; void *memc[6]; size_t szc[6];
    for (int a = 0; a < 6; ++a) {
        /* K steps exceed MAXS: build the body directly */
        OmegaProgramBody b; memset(&b, 0, sizeof b); b.has_body = true; b.step_count = (uint16_t)K;
        for (int k = 0; k < K; ++k) {
            b.steps[k].op = a == ARM_MOVK ? OP_ADD : OMEGA_OP[a + 1];
            b.steps[k].imm = a == ARM_MOVK ? 0x0003000300030003ull : 3;
        }
        uint8_t code[4096]; size_t len = 0;
        if (omega_program_emit_schedule(&b, 64, OMEGA_SCHED_SEQUENTIAL, code, &len, sizeof code)) die("emit refused (calibration)");
        szc[a] = 4096;
        void *p = mmap(NULL, szc[a], PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) die("mmap");
        memcpy(p, code, len); __builtin___clear_cache((char *)p, (char *)p + len);
        if (mprotect(p, szc[a], PROT_READ | PROT_EXEC)) die("mprotect");
        memc[a] = p; fc[a] = (Fn)p;
    }
    printf("# calibration: K=%d steps per chain, %u calls per timing, %d observations per arm, cpu=%d\n", K, CALLS, obs, sched_getcpu());
    printf("arm,obs,ps_per_step\n");
    time_fn(f0, ~0ull, CALLS);   /* warm */
    for (int o = 0; o < obs; ++o) {
        double add_step = 0;
        for (int a = 0; a < 6; ++a) {
            double t0 = time_fn(f0, ~0ull, CALLS), tk = time_fn(fc[a], ~0ull, CALLS);
            double per = (tk - t0) / K;
            if (a == ARM_ADD) add_step = per;
            if (a == ARM_MOVK) per = (per - add_step) / 3.0;   /* three MOVK per step beyond the ADD step */
            if (per < 1.0) per = 1.0;
            RxCmObservation ob = {(uint64_t)llround(per), 0};
            if (rx_cm_observe(m, &f, (uint32_t)a, &ob) != RX_CM_OK) die("observe refused");
            printf("%d,%d,%.1f\n", a, o, per);
            if (a == 0) {
                double base = t0 < 1.0 ? 1.0 : t0;
                RxCmObservation bo = {(uint64_t)llround(base), 0};
                if (rx_cm_observe(m, &f, ARM_BASE, &bo) != RX_CM_OK) die("observe refused");
                printf("%d,%d,%.1f\n", ARM_BASE, o, base);
            }
        }
    }
    for (int a = 0; a < 6; ++a) munmap(memc[a], szc[a]);
    munmap(mem0, sz0);
    /* ARM_UNOBSERVED is left without observations on purpose. */
    size_t bl = rx_cm_blob_size(), got = 0;
    uint8_t *blob = malloc(bl);
    if (rx_cm_serialize(m, blob, bl, &got)) die("serialize");
    FILE *fo = fopen(out, "wb");
    if (!fo || fwrite(blob, 1, got, fo) != got) die("write blob");
    fclose(fo);
    uint8_t dg[32]; rx_cm_digest(m, dg);
    printf("# blob %s bytes=%zu digest=", out, got);
    for (int i = 0; i < 32; ++i) printf("%02x", dg[i]);
    printf("\n");
    return 0;
}

static RxCostModel *load_model(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open model blob");
    size_t bl = rx_cm_blob_size();
    uint8_t *b = malloc(bl);
    size_t got = fread(b, 1, bl, f);
    fclose(f);
    RxCostModel *m = calloc(1, sizeof *m);
    if (rx_cm_deserialize(m, b, got) != RX_CM_OK) die("model blob refused by rx_cm_deserialize");
    free(b);
    return m;
}

static long peak_rss_bytes(void) { struct rusage u; getrusage(RUSAGE_SELF, &u); return u.ru_maxrss * 1024L; }

static int mode_run(const char *arm, const char *set, const char *dir, const Budget *bg) {
    Expr *ex; int n = load_set(set, &ex);
    mkdir(dir, 0755);
    char p[600];
    snprintf(p, sizeof p, "%s/results.tsv", dir);
    FILE *fr = fopen(p, "w");
    snprintf(p, sizeof p, "%s/derivs.rwd", dir);
    FILE *fd = fopen(p, "w");
    if (!fr || !fd) die("cannot write results");
    fprintf(fd, "rw-derivation v1\nsemantics: omega.v0.wrap.1\n");
    fprintf(fr, "id\tw\tin\tout\tn_in\tn_out\tinsn_in\tinsn_out\tcost_ps_in\tcost_ps_out\tremoved_ids\tstatus\tnodes\tevals\tsearch_cpu_ns\n");
    int rank_cm = !strcmp(arm, "B");
    if (rank_cm && !g_model) die("arm B needs --model");
    g_nodes = calloc((size_t)bg->nodes_max + 1, sizeof *g_nodes);
    int tsz = 1; while (tsz < 2 * bg->nodes_max) tsz <<= 1;
    g_tab = calloc((size_t)tsz, sizeof *g_tab);
    double total_cpu = 0, w0 = now(CLOCK_MONOTONIC);
    int changed = 0, hit = 0, unranked = 0, removed_total = 0;
    for (int i = 0; i < n; ++i) {
        Result r; memset(&r, 0, sizeof r);
        double c0 = now(CLOCK_THREAD_CPUTIME_ID);
        if (!strcmp(arm, "M")) arm_manual(&ex[i].ch, ex[i].w, bg, &r);
        else if (!strcmp(arm, "G")) arm_greedy(&ex[i].ch, ex[i].w, bg, &r);
        else if (!strcmp(arm, "B") || !strcmp(arm, "Bi")) arm_bounded(&ex[i].ch, ex[i].w, bg, rank_cm, &r);
        else die("unknown arm");
        double cpu = now(CLOCK_THREAD_CPUTIME_ID) - c0;
        total_cpu += cpu;
        finish(&r, &ex[i].ch, ex[i].w);
        char si[512], so[512], rem[256] = "";
        ch_str(&ex[i].ch, si, sizeof si); ch_str(&r.out, so, sizeof so);
        for (int k = 0; k < ex[i].ch.n; ++k) {
            int kept = 0;
            for (int j = 0; j < r.out.n; ++j) if (r.out.id[j] == ex[i].ch.id[k]) kept = 1;
            if (!kept) { size_t l = strlen(rem); snprintf(rem + l, sizeof rem - l, "%s%d", l ? "," : "", k); removed_total++; }
        }
        int ch = !ch_eq(&r.out, &ex[i].ch);
        changed += ch; hit += r.budget_hit; unranked += r.unranked;
        fprintf(fr, "%s\t%u\t%s\t%s\t%d\t%d\t%u\t%u\t%.1f\t%.1f\t%s\t%s\t%d\t%d\t%.0f\n", ex[i].id, ex[i].w, si, so,
                ex[i].ch.n, r.out.n, r.insn_in, r.insn_out, r.cost_known_in ? r.cost_in : -1.0,
                r.cost_known_out ? r.cost_out : -1.0, rem[0] ? rem : "-",
                r.unranked ? "unranked" : r.budget_hit ? "best_found_under_budget" : "complete", r.nodes, r.evals, cpu);
        if (ch) {
            fprintf(fd, "expr: %s width=%u start=%s\n", ex[i].id, ex[i].w, si);
            for (int k = 0; k < r.nder; ++k) {
                char sd[512]; ch_str(&r.der_ch[k], sd, sizeof sd);
                if (r.der_rule[k] == 99) fprintf(fd, "step: R99 => %s\n", sd);
                else fprintf(fd, "step: R%02d => %s\n", r.der_rule[k], sd);
            }
            fprintf(fd, "end\n");
        }
    }
    fclose(fr); fclose(fd);
    snprintf(p, sizeof p, "%s/summary.txt", dir);
    FILE *fs = fopen(p, "w");
    fprintf(fs, "arm=%s set=%s n=%d changed=%d removed_ops=%d best_found_under_budget=%d unranked=%d "
            "search_cpu_ns=%.0f search_wall_ns=%.0f peak_rss_bytes=%ld nodes_max=%d evals_max=%d cpu_ns_max=%.0f ranking=%s\n",
            arm, set, n, changed, removed_total, hit, unranked, total_cpu, now(CLOCK_MONOTONIC) - w0, peak_rss_bytes(),
            bg->nodes_max, bg->evals_max, bg->cpu_ns_max, rank_cm ? "rx_costmodel" : !strcmp(arm, "Bi") || !strcmp(arm, "G") ? "insn_count" : "first_applicable");
    fprintf(fs, "profile=matching_doubled match_attempts=%llu\n", (unsigned long long)g_match_attempts);
    fclose(fs);
    printf("RWSEARCH arm=%s n=%d changed=%d removed_ops=%d best_found_under_budget=%d unranked=%d search_cpu_ns=%.0f peak_rss_bytes=%ld\n",
           arm, n, changed, removed_total, hit, unranked, total_cpu, peak_rss_bytes());
    return 0;
}

/* results.tsv row */
typedef struct { char id[32]; unsigned w; Ch in, out; char removed[256]; int accepted; double cost_out; } Row;

static int load_rows(const char *dir, const char *verdicts, Row **out) {
    char p[600]; snprintf(p, sizeof p, "%s/results.tsv", dir);
    FILE *f = fopen(p, "r");
    if (!f) die("cannot open results.tsv");
    Row *r = calloc(2048, sizeof *r);
    char line[2048]; int n = 0;
    if (!fgets(line, sizeof line, f)) die("empty results");
    while (fgets(line, sizeof line, f) && n < 2048) {
        char *col[16]; int nc = 0;
        char *save;
        for (char *t = strtok_r(line, "\t\n", &save); t && nc < 16; t = strtok_r(NULL, "\t\n", &save)) col[nc++] = t;
        if (nc < 15) die("bad results row");
        snprintf(r[n].id, sizeof r[n].id, "%s", col[0]);
        r[n].w = (unsigned)atoi(col[1]);
        if (ch_parse(col[2], &r[n].in) || ch_parse(col[3], &r[n].out)) die("bad chain in results");
        snprintf(r[n].removed, sizeof r[n].removed, "%s", col[10]);
        r[n].cost_out = atof(col[9]);
        r[n].accepted = ch_eq(&r[n].in, &r[n].out) ? -1 : 0;   /* -1 unchanged */
        n++;
    }
    fclose(f);
    if (verdicts) {
        FILE *v = fopen(verdicts, "r");
        if (!v) die("cannot open verdicts");
        int bypass = getenv("RW_MUTANT") && !strcmp(getenv("RW_MUTANT"), "bypass_checker");
        while (fgets(line, sizeof line, v)) {
            char id[64], verdict[32];
            if (sscanf(line, "RWDERIV %63s %31s", id, verdict) != 2) continue;
            for (int i = 0; i < n; ++i)
                if (!strcmp(r[i].id, id)) r[i].accepted = bypass || !strcmp(verdict, "accepted");
        }
        fclose(v);
        if (bypass) { fprintf(stderr, "rwsearch: MUTANT bypass_checker active\n"); for (int i = 0; i < n; ++i) if (r[i].accepted == 0) r[i].accepted = 1; }
    }
    *out = r;
    return n;
}

/* Independent end-to-end check: every accepted output equals its input on every input (w <= 16)
 * or on a sample (w >= 32), using omega's evaluator, not this file's. */
static int omega_eval(const Ch *c, unsigned w, const uint64_t *xs, size_t n, uint64_t *ys) {
    OmegaProgram *p = calloc(1, sizeof *p);
    omega_program_init(p, "rw249e2e");
    p->contract.input_type = p->contract.output_type = TYPE_UNSIGNED_INT;
    p->contract.input_width = p->contract.output_width = (uint16_t)w;
    snprintf(p->contract.precondition, sizeof p->contract.precondition, "x >= 0");
    omega_build_constraint_id(CONST_PRECONDITION, p->contract.precondition, &p->contract.precondition_id);
    snprintf(p->contract.postcondition, sizeof p->contract.postcondition, "rw249 e2e");
    omega_build_constraint_id(CONST_POSTCONDITION, p->contract.postcondition, &p->contract.postcondition_id);
    body_of(c, &p->body);
    int rc = omega_program_compute_id(p);
    if (!rc) rc = omega_program_eval(p, xs, n, ys);
    omega_program_destroy(p); free(p);
    return rc;
}

static int mode_verify(const char *dir, const char *verdicts) {
    Row *r; int n = load_rows(dir, verdicts, &r);
    uint64_t bad = 0; int checked = 0;
    for (int i = 0; i < n; ++i) {
        if (r[i].accepted != 1) continue;
        unsigned w = r[i].w;
        size_t cnt = w <= 16 ? (size_t)wmask(w) + 1 : 4096;
        uint64_t *xs = malloc(cnt * 8), *a = malloc(cnt * 8), *b = malloc(cnt * 8);
        g_rng = 0xE2E249ull;
        for (size_t k = 0; k < cnt; ++k) xs[k] = w <= 16 ? k : (k < 64 ? (k < 32 ? k : wmask(w) - (k - 32)) : rnd() & wmask(w));
        if (omega_eval(&r[i].in, w, xs, cnt, a) || omega_eval(&r[i].out, w, xs, cnt, b)) die("omega_program_eval refused");
        uint64_t rb = 0;
        for (size_t k = 0; k < cnt; ++k) if (a[k] != b[k]) rb++;
        if (rb) printf("E2E-BAD %s bad_rows=%llu\n", r[i].id, (unsigned long long)rb);
        bad += rb; checked++;
        free(xs); free(a); free(b);
    }
    printf("E2E accepted_checked=%d bad_rows=%llu %s\n", checked, (unsigned long long)bad, bad ? "RED" : "GREEN");
    return bad ? 1 : 0;
}

static int mode_exec(const char *dir, const char *verdicts, uint32_t calls) {
    pin_cpu();
    Row *r; int n = load_rows(dir, verdicts, &r);
    char p[600]; snprintf(p, sizeof p, "%s/exec.tsv", dir);
    FILE *fo = fopen(p, "w");
    fprintf(fo, "id\tw\taccepted\torig_ps\tnew_ps\tnull_ps\tsaving_ps\n");
    double sum_save = 0, sum_null = 0, wall0 = now(CLOCK_MONOTONIC), cpu0 = now(CLOCK_PROCESS_CPUTIME_ID);
    int timed = 0;
    for (int i = 0; i < n; ++i) {
        if (r[i].accepted != 1) { fprintf(fo, "%s\t%u\t%d\t-\t-\t-\t0\n", r[i].id, r[i].w, r[i].accepted); continue; }
        void *m1, *m2; size_t s1, s2;
        Fn fa = map_chain(&r[i].in, r[i].w, &m1, &s1), fb = map_chain(&r[i].out, r[i].w, &m2, &s2);
        uint64_t m = wmask(r[i].w);
        double a1 = 1e300, a2 = 1e300, b = 1e300;
        time_fn(fa, m, calls / 4); time_fn(fb, m, calls / 4);
        for (int k = 0; k < 3; ++k) {
            double t;
            t = time_fn(fa, m, calls); if (t < a1) a1 = t;
            t = time_fn(fb, m, calls); if (t < b) b = t;
            t = time_fn(fa, m, calls); if (t < a2) a2 = t;
        }
        double orig = a1 < a2 ? a1 : a2, null = fabs(a1 - a2), save = orig - b;
        fprintf(fo, "%s\t%u\t1\t%.1f\t%.1f\t%.1f\t%.1f\n", r[i].id, r[i].w, orig, b, null, save);
        sum_save += save; sum_null += null; timed++;
        munmap(m1, s1); munmap(m2, s2);
    }
    fclose(fo);
    snprintf(p, sizeof p, "%s/exec_summary.txt", dir);
    FILE *fs = fopen(p, "w");
    fprintf(fs, "timed=%d calls=%u sum_saving_ps_per_call=%.1f sum_null_ps=%.1f exec_wall_ns=%.0f exec_cpu_ns=%.0f peak_rss_bytes=%ld\n",
            timed, calls, sum_save, sum_null, now(CLOCK_MONOTONIC) - wall0, now(CLOCK_PROCESS_CPUTIME_ID) - cpu0, peak_rss_bytes());
    fclose(fs);
    printf("RWEXEC timed=%d sum_saving_ps_per_call=%.1f sum_null_ps=%.1f\n", timed, sum_save, sum_null);
    return 0;
}

/* OEV1 files (omega-mv#583 spec v1): one per accepted expression with removed ops. */
static const char *g_load1 = "unknown";
static const char *g_proof = "derivs.rwd";
static int mode_account(const char *dir, const char *verdicts, const char *oev, const char *build, const char *blob) {
    Row *r; int n = load_rows(dir, verdicts, &r);
    int acc = 0, rej = 0, unchanged = 0, removed = 0, files = 0;
    if (oev) mkdir(oev, 0755);
    for (int i = 0; i < n; ++i) {
        if (r[i].accepted == -1) { unchanged++; continue; }
        if (r[i].accepted == 0) { rej++; continue; }
        acc++;
        if (!strcmp(r[i].removed, "-")) continue;
        int nrem = 1; for (const char *q = r[i].removed; *q; ++q) nrem += *q == ',';
        removed += nrem;
        if (!oev) continue;
        char p[600]; snprintf(p, sizeof p, "%s/%s.oev", oev, r[i].id);
        FILE *f = fopen(p, "w");
        fprintf(f, "oev version=1\n");
        fprintf(f, "machine id=spark-b87b soc=GB10 cores=20 mem_bytes=137438953472 prov=specified src=omega-research-583/spec/offline-evidence-v1.md\n");
        fprintf(f, "build repo=omega-mv sha=%s\n", build ? build : "0000000000000000000000000000000000000000");
        fprintf(f, "calibration id=rw249-cal taken=2026-10-19 valid_days=7 load1=%s src=evidence/REWRITE-249/calibration%s%s\n",
                g_load1, blob ? " model=" : "", blob ? blob : "");
        fprintf(f, "resource id=cpu kind=compute slots=1 prov=specified src=single-thread-chain\n");
        int prevk = -1;
        for (int k = 0; k < r[i].out.n; ++k) {
            double ps = -1;
            int ok = 0;
            if (g_model) { Ch one; memset(&one, 0, sizeof one); one.n = 1; one.op[0] = r[i].out.op[k]; one.c[0] = r[i].out.c[k];
                double base; ok = cm_cost(&one, &ps) && arm_ps(ARM_BASE, &base); if (ok) ps -= base; }
            if (ok) fprintf(f, "op id=%s_s%d kind=compute engine=cpu t_ns=%.4f prov=fitted src=rx_costmodel_arms_stand_in\n", r[i].id, r[i].out.id[k], ps / 1000.0);
            else fprintf(f, "op id=%s_s%d kind=compute engine=cpu t_ns=0 prov=unknown src=no_scalar_cost_class\n", r[i].id, r[i].out.id[k]);
            if (prevk >= 0) fprintf(f, "dep %s_s%d %s_s%d\n", r[i].id, prevk, r[i].id, r[i].out.id[k]);
            prevk = r[i].out.id[k];
        }
        fprintf(f, "energy instrument=none\n");
        char rem[256]; snprintf(rem, sizeof rem, "%s", r[i].removed);
        char *save;
        for (char *t = strtok_r(rem, ",", &save); t; t = strtok_r(NULL, ",", &save))
            fprintf(f, "removed op=%s_s%s reason=eliminated proof=rwcheck:%s#%s\n", r[i].id, t, g_proof, r[i].id);
        fclose(f);
        files++;
    }
    printf("RWACCOUNT accepted=%d failed_candidates=%d unchanged=%d removed_ops=%d oev_files=%d\n", acc, rej, unchanged, removed, files);
    return 0;
}

/* Unknown cost is never ranked as free: a model with MUL unobserved must leave every candidate
 * containing MUL ineligible, and a candidate set whose input contains MUL unranked. */
static int mode_unknown_test(void) {
    RxCostModel *m = calloc(1, sizeof *m);
    rx_cm_init(m, RX_CM_ARMS);
    RxCmFeatures f = cm_feat();
    for (int o = 0; o < 16; ++o)
        for (int a = 0; a < 7; ++a) {
            if (a == ARM_MUL) continue;
            RxCmObservation ob = {(uint64_t)(300 + 10 * a + o % 3), 0};   /* synthetic, labelled */
            rx_cm_observe(m, &f, (uint32_t)a, &ob);
        }
    g_model = m;
    int bad = 0;
    Ch c; ch_parse("add 3; mul 5; add 7", &c);
    double ps;
    if (cm_cost(&c, &ps)) { printf("UNKNOWN-TEST bad: chain with unobserved MUL got cost %.1f\n", ps); bad++; }
    Budget bg = {256, 4096, 2e7};
    g_nodes = calloc(257, sizeof *g_nodes); g_tab = calloc(512, sizeof *g_tab);
    Result r; memset(&r, 0, sizeof r);
    arm_bounded(&c, 8, &bg, 1, &r);
    if (!r.unranked || !ch_eq(&r.out, &c)) { printf("UNKNOWN-TEST bad: input with unknown cost was ranked\n"); bad++; }
    Ch d; ch_parse("add 3; add 4; and 0xff", &d);
    memset(&r, 0, sizeof r);
    arm_bounded(&d, 16, &bg, 1, &r);
    for (int i = 0; i < r.out.n; ++i) if (r.out.op[i] == O_MUL) { printf("UNKNOWN-TEST bad: picked a MUL candidate\n"); bad++; }
    if (r.unranked) { printf("UNKNOWN-TEST bad: known input left unranked\n"); bad++; }
    printf("UNKNOWN-TEST synthetic_model=1 bad_rows=%d %s\n", bad, bad ? "RED" : "GREEN");
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) die("usage: rwsearch gen|calibrate|run|exec|account|verify-accepted|unknown-test ...");
    const char *mode = argv[1], *arm = NULL, *set = NULL, *dir = NULL, *model = NULL, *out = NULL, *verdicts = NULL, *oev = NULL, *build = NULL;
    uint64_t seed = 1; int n = 100, obs = 16; uint32_t calls = 100000;
    Budget bg = {512, 4096, 20e6};
    for (int i = 2; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--seed") && v) { seed = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--n") && v) { n = atoi(v); i++; }
        else if (!strcmp(a, "--obs") && v) { obs = atoi(v); i++; }
        else if (!strcmp(a, "--calls") && v) { calls = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--arm") && v) { arm = v; i++; }
        else if (!strcmp(a, "--set") && v) { set = v; i++; }
        else if (!strcmp(a, "--dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--out") && v) { out = v; i++; }
        else if (!strcmp(a, "--model") && v) { model = v; i++; }
        else if (!strcmp(a, "--verdicts") && v) { verdicts = v; i++; }
        else if (!strcmp(a, "--oev") && v) { oev = v; i++; }
        else if (!strcmp(a, "--build") && v) { build = v; i++; }
        else if (!strcmp(a, "--nodes") && v) { bg.nodes_max = atoi(v); i++; }
        else if (!strcmp(a, "--evals") && v) { bg.evals_max = atoi(v); i++; }
        else if (!strcmp(a, "--cpu-us") && v) { bg.cpu_ns_max = atof(v) * 1000.0; i++; }
        else if (!strcmp(a, "--inject-probe-rule")) g_inject = 1;
        else if (!strcmp(a, "--profile")) g_profile = 1;
        else if (!strcmp(a, "--load1") && v) { g_load1 = v; i++; }
        else if (!strcmp(a, "--proof") && v) { g_proof = v; i++; }
        else die("unknown flag");
    }
    const char *mu = getenv("RW_MUTANT");
    if (mu && !strcmp(mu, "unknown_zero")) { g_mutant_unknown_zero = 1; fprintf(stderr, "rwsearch: MUTANT unknown_zero active\n"); }
    if (model) g_model = load_model(model);
    if (!strcmp(mode, "gen")) return mode_gen(seed, n);
    if (!strcmp(mode, "calibrate")) { if (!out) die("--out"); return mode_calibrate(out, obs); }
    if (!strcmp(mode, "run")) { if (!arm || !set || !out) die("run needs --arm --set --out"); return mode_run(arm, set, out, &bg); }
    if (!strcmp(mode, "exec")) { if (!dir) die("--dir"); return mode_exec(dir, verdicts, calls); }
    if (!strcmp(mode, "account")) { if (!dir || !verdicts) die("--dir --verdicts"); return mode_account(dir, verdicts, oev, build, model); }
    if (!strcmp(mode, "verify-accepted")) { if (!dir || !verdicts) die("--dir --verdicts"); return mode_verify(dir, verdicts); }
    if (!strcmp(mode, "unknown-test")) return mode_unknown_test();
    die("unknown mode");
    return 2;
}
