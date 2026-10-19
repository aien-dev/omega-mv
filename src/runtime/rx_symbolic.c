/*
 * rx_symbolic.c -- Omega symbolic rewrite layer (see rx_symbolic.h).
 *
 * Ideas from term rewriting systems (head[args] expressions, normal forms,
 * conditional rules) are used; no code from any other system is.
 */
#include "rx_symbolic.h"

#include "omega_canonical.h"
#include "omega_core.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- terms */

RxSymType rx_sym_u64(void) { RxSymType t = { RX_SYM_TY_U64, RX_SYM_AR_WRAP, 64, 0, 0 }; return t; }
RxSymType rx_sym_bool(void) { RxSymType t = { RX_SYM_TY_BOOL, RX_SYM_AR_WRAP, 1, 0, 0 }; return t; }

void rx_sym_store_init(RxSymStore *s) { s->n = 0; }

static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (24 - 8 * i)); }
static void be64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i)); }

int rx_sym_object(const RxSymStore *s, const RxSymTerm *t, OmegaObject *o) {
    if (!s || !t || !o || t->n_args > RX_SYM_MAX_ARGS || t->n_assume > RX_SYM_MAX_ASSUME) return -1;
    memset(o, 0, sizeof *o);
    o->kind = KIND_OPERATION;
    o->attr_count = 1;
    strcpy(o->attributes[0].key, "omega.sym");
    memcpy(o->attributes[0].value, "term/1", 6);
    o->attributes[0].val_len = 6;
    for (uint32_t i = 0; i < t->n_assume; i++) {
        OmegaConstraint *c = &o->constraints[o->const_count++];
        c->kind = t->assume[i].kind == RX_SYM_A_RANGE ? CONST_RANGE : CONST_PRECONDITION;
        c->payload[0] = t->assume[i].kind;
        be64(c->payload + 1, t->assume[i].lo);
        be64(c->payload + 9, t->assume[i].hi);
        c->payload_len = 17;
    }
    uint8_t *p = o->payload;
    size_t k = 0;
    p[k++] = t->head;
    be16(p + k, t->op); k += 2;
    p[k++] = t->type.scalar;
    p[k++] = t->type.arith;
    be16(p + k, t->type.width); k += 2;
    be32(p + k, t->type.rows); k += 4;
    be32(p + k, t->type.cols); k += 4;
    be64(p + k, t->imm); k += 8;
    p[k++] = t->n_args;
    for (uint32_t i = 0; i < t->n_args; i++) {
        if (t->arg[i] >= s->n) return -1;
        memcpy(p + k, s->t[t->arg[i]].id.bytes, OMEGA_ID_BYTES);
        k += OMEGA_ID_BYTES;
    }
    o->payload_len = (uint32_t)k;
    return 0;
}

uint32_t rx_sym_intern(RxSymStore *s, const RxSymTerm *proto) {
    static __thread OmegaObject o;
    if (rx_sym_object(s, proto, &o) != 0 || omega_compute_semantic_id(&o) != 0) return RX_SYM_NONE;
    for (uint32_t i = 0; i < s->n; i++)
        if (memcmp(s->t[i].id.bytes, o.id.bytes, OMEGA_ID_BYTES) == 0) return i;
    if (s->n >= RX_SYM_MAX_TERMS) return RX_SYM_NONE;
    RxSymTerm *t = &s->t[s->n];
    *t = *proto;
    t->id = o.id;
    return s->n++;
}

uint32_t rx_sym_var(RxSymStore *s, uint64_t index, RxSymType ty) {
    RxSymTerm t; memset(&t, 0, sizeof t);
    t.head = RX_SYM_H_VAR; t.type = ty; t.imm = index;
    return rx_sym_intern(s, &t);
}

uint32_t rx_sym_const(RxSymStore *s, uint64_t v, RxSymType ty) {
    RxSymTerm t; memset(&t, 0, sizeof t);
    t.head = RX_SYM_H_CONST; t.type = ty; t.imm = v;
    return rx_sym_intern(s, &t);
}

uint32_t rx_sym_op(RxSymStore *s, uint32_t op, RxSymType ty, uint32_t a, uint32_t b) {
    if (a == RX_SYM_NONE || (b == RX_SYM_NONE && op != OP_IDENTITY && op != OP_NOT)) return RX_SYM_NONE;
    RxSymTerm t; memset(&t, 0, sizeof t);
    t.head = RX_SYM_H_OP; t.op = (uint16_t)op; t.type = ty;
    t.arg[t.n_args++] = a;
    if (b != RX_SYM_NONE) t.arg[t.n_args++] = b;
    return rx_sym_intern(s, &t);
}

uint32_t rx_sym_with_assume(RxSymStore *s, uint32_t i, const RxSymAssume *a) {
    if (i == RX_SYM_NONE || !a) return i;
    RxSymTerm t = s->t[i];
    if (t.n_assume >= RX_SYM_MAX_ASSUME) return RX_SYM_NONE;
    t.assume[t.n_assume++] = *a;
    return rx_sym_intern(s, &t);
}

int rx_sym_equivalence_object(const RxSymStore *s, uint32_t lhs, uint32_t rhs, OmegaObject *out) {
    if (lhs >= s->n || rhs >= s->n || rx_sym_object(s, &s->t[lhs], out) != 0) return -1;
    out->relations[0].kind = REL_EQUIVALENT_TO;
    out->relations[0].target_id = s->t[rhs].id;
    out->rel_count = 1;
    return omega_compute_semantic_id(out);
}

static void cost_walk(const RxSymStore *s, uint32_t t, uint8_t *seen, uint32_t *c) {
    if (t >= s->n || seen[t]) return;
    seen[t] = 1;
    if (s->t[t].head == RX_SYM_H_OP || s->t[t].head == RX_SYM_H_KERNEL) (*c)++;
    for (uint32_t i = 0; i < s->t[t].n_args; i++) cost_walk(s, s->t[t].arg[i], seen, c);
}

uint32_t rx_sym_cost(const RxSymStore *s, uint32_t t) {
    static __thread uint8_t seen[RX_SYM_MAX_TERMS];
    memset(seen, 0, s->n);
    uint32_t c = 0;
    cost_walk(s, t, seen, &c);
    return c;
}

/* ------------------------------------------------------- machine arithmetic */

static int is_wrap64(const RxSymTerm *t) {
    return t->type.scalar == RX_SYM_TY_U64 && t->type.arith == RX_SYM_AR_WRAP && t->type.width == 64;
}

static int aff_add(RxSymAffine *a, uint64_t var, uint64_t coef) {
    for (uint32_t i = 0; i < a->n; i++)
        if (a->var[i] == var) { a->coef[i] += coef; return 0; }
    if (a->n >= RX_SYM_AFF_MAX) return -1;
    a->var[a->n] = var; a->coef[a->n] = coef; a->n++;
    return 0;
}

static void aff_drop_zero(RxSymAffine *a) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < a->n; i++)
        if (a->coef[i] != 0) { a->var[k] = a->var[i]; a->coef[k] = a->coef[i]; k++; }
    a->n = k;
}

static int affine_rec(const RxSymStore *s, uint32_t i, RxSymAffine *out, uint32_t depth) {
    if (i >= s->n || depth > 64) return -1;
    const RxSymTerm *t = &s->t[i];
    memset(out, 0, sizeof *out);
    if (!is_wrap64(t)) return -1;
    if (t->head == RX_SYM_H_CONST) { out->k = t->imm; return 0; }
    if (t->head == RX_SYM_H_VAR) { out->n = 1; out->var[0] = t->imm; out->coef[0] = 1; return 0; }
    if (t->head != RX_SYM_H_OP) return -1;
    RxSymAffine a, b;
    if (t->op == OP_IDENTITY && t->n_args >= 1) return affine_rec(s, t->arg[0], out, depth + 1);
    if (t->n_args != 2) return -1;
    if (affine_rec(s, t->arg[0], &a, depth + 1) != 0 || affine_rec(s, t->arg[1], &b, depth + 1) != 0) return -1;
    switch (t->op) {
    case OP_ADD: case OP_SUB: {
        uint64_t sg = t->op == OP_ADD ? 1u : (uint64_t)-1;   /* -1 in Z/2^64 */
        *out = a;
        for (uint32_t j = 0; j < b.n; j++) if (aff_add(out, b.var[j], sg * b.coef[j]) != 0) return -1;
        out->k = a.k + sg * b.k;
        break;
    }
    case OP_MUL: {
        const RxSymAffine *v, *c;
        if (b.n == 0) { v = &a; c = &b; } else if (a.n == 0) { v = &b; c = &a; } else return -1;  /* x*y is not affine */
        *out = *v;
        for (uint32_t j = 0; j < out->n; j++) out->coef[j] *= c->k;
        out->k = v->k * c->k;
        break;
    }
    default:
        return -1;     /* DIV, compares, logic: not ring operations */
    }
    aff_drop_zero(out);
    return 0;
}

int rx_sym_affine(const RxSymStore *s, uint32_t t, RxSymAffine *out) { return affine_rec(s, t, out, 0); }

static int eval_rec(const RxSymStore *s, uint32_t i, const uint64_t *vars, uint32_t n_vars, uint64_t *out, uint32_t depth) {
    if (i >= s->n || depth > 64) return -1;
    const RxSymTerm *t = &s->t[i];
    if (t->head == RX_SYM_H_CONST) { *out = t->imm; return 0; }
    if (t->head == RX_SYM_H_VAR) { if (t->imm >= n_vars) return -1; *out = vars[t->imm]; return 0; }
    if (t->head != RX_SYM_H_OP || t->n_args < 1) return -1;
    uint64_t a = 0, b = 0;
    if (eval_rec(s, t->arg[0], vars, n_vars, &a, depth + 1) != 0) return -1;
    if (t->n_args > 1 && eval_rec(s, t->arg[1], vars, n_vars, &b, depth + 1) != 0) return -1;
    if (t->op == OP_IDENTITY) { *out = a; return 0; }
    if (t->op == OP_CONSTANT) { *out = b; return 0; }
    /* The same evaluator, policy and width the action graph's PURE nodes use. */
    return omega_eval_pure_binary_uint((OpCode)t->op, OVERFLOW_WRAP, 64, a, b, out) == 0 ? 0 : -1;
}

int rx_sym_eval_u64(const RxSymStore *s, uint32_t t, const uint64_t *vars, uint32_t n_vars, uint64_t *out) {
    return eval_rec(s, t, vars, n_vars, out, 0);
}

/* ------------------------------------------------------------ verification */

static uint64_t splitmix(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void vars_of(const RxSymStore *s, uint32_t i, uint64_t *mask, uint32_t depth) {
    if (i >= s->n || depth > 64) return;
    if (s->t[i].head == RX_SYM_H_VAR && s->t[i].imm < 64) *mask |= 1ull << s->t[i].imm;
    for (uint32_t j = 0; j < s->t[i].n_args; j++) vars_of(s, s->t[i].arg[j], mask, depth + 1);
}

/* Both sides must evaluate the same: equal values, or both fail. */
static int same_at(const RxSymStore *s, uint32_t lhs, uint32_t rhs, const uint64_t *v, uint64_t *gl, uint64_t *gr) {
    uint64_t l = 0, r = 0;
    int el = rx_sym_eval_u64(s, lhs, v, 64, &l), er = rx_sym_eval_u64(s, rhs, v, 64, &r);
    *gl = l; *gr = r;
    if (el != er) return 0;
    return el != 0 || l == r;
}

int rx_sym_verify_u64(const RxSymStore *s, uint32_t lhs, uint32_t rhs, uint32_t n_vars,
                      const RxSymAssume *ranges, RxSymVerdict *out) {
    (void)n_vars;
    memset(out, 0, sizeof *out);
    if (lhs >= s->n || rhs >= s->n) return out->verdict = RX_SYM_V_REFUTED;
    uint64_t mask = 0;
    vars_of(s, lhs, &mask, 0);
    vars_of(s, rhs, &mask, 0);
    uint64_t v[64], seed = 0x5EEDu ^ s->t[lhs].id.bytes[0] ^ ((uint64_t)s->t[rhs].id.bytes[1] << 8);
    static const uint64_t edge[] = { 0, 1, 2, 3, 7, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull,
                                     0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFull, 0x100000000ull };
    const uint32_t n_edge = sizeof edge / sizeof edge[0];
    uint32_t vectors = 0;
    for (uint32_t r = 0; r < RX_SYM_VERIFY_VECTORS + n_edge; r++) {
        for (uint32_t j = 0; j < 64; j++) {
            if (!((mask >> j) & 1u)) { v[j] = 0; continue; }
            uint64_t x = r < n_edge ? edge[(r + j) % n_edge] : splitmix(&seed);
            if (ranges && ranges[j].kind == RX_SYM_A_RANGE) {
                uint64_t lo = ranges[j].lo, hi = ranges[j].hi;
                if (x < lo || x > hi) x = (hi - lo == UINT64_MAX) ? x : lo + x % (hi - lo + 1);
            }
            v[j] = x;
        }
        uint64_t gl, gr;
        vectors++;
        if (!same_at(s, lhs, rhs, v, &gl, &gr)) {
            out->verdict = RX_SYM_V_REFUTED;
            out->got_lhs = gl; out->got_rhs = gr;
            uint32_t q = 0;
            for (uint32_t j = 0; j < 64 && q < RX_SYM_AFF_MAX; j++) if ((mask >> j) & 1u) out->counter[q++] = v[j];
            out->vectors = vectors;
            return out->verdict;
        }
    }
    out->vectors = vectors;
    /* Proof for the affine class: the checker re-derives affinity of both
     * sides from the terms. Equal at 0 and at every unit vector => equal on
     * all of (Z/2^64)^n. With range assumptions the proof still holds (it
     * holds on the whole space). */
    RxSymAffine a, b;
    if (rx_sym_affine(s, lhs, &a) == 0 && rx_sym_affine(s, rhs, &b) == 0) {
        memset(v, 0, sizeof v);
        uint64_t gl, gr;
        int ok = same_at(s, lhs, rhs, v, &gl, &gr);
        for (uint32_t j = 0; j < 64 && ok; j++) {
            if (!((mask >> j) & 1u)) continue;
            v[j] = 1;
            ok = same_at(s, lhs, rhs, v, &gl, &gr);
            v[j] = 0;
        }
        out->verdict = ok ? RX_SYM_V_PROVED : RX_SYM_V_REFUTED;
        return out->verdict;
    }
    return out->verdict = RX_SYM_V_TESTED;
}

/* ------------------------------------------------------------------ budget */

double rx_sym_lower_bound_ns(const RxSymBudget *b, const RxSymWork *w) {
    double t_mem = b->dram_bytes_per_s > 0 ? w->bytes / b->dram_bytes_per_s * 1e9 : 0;
    double t_fl = b->peak_flops > 0 ? w->flops / b->peak_flops * 1e9 : 0;
    return (t_mem > t_fl ? t_mem : t_fl) + w->launches * b->launch_floor_ns;
}

int rx_sym_budget_admit(const RxSymBudget *b, const RxSymWork *c, double incumbent_ns, double *lb_ns) {
    double lb = rx_sym_lower_bound_ns(b, c);
    if (lb_ns) *lb_ns = lb;
    if ((b->mem_bytes && c->mem_bytes > b->mem_bytes) || (b->smem_bytes_per_cta && c->smem_bytes > b->smem_bytes_per_cta))
        return RX_SYM_B_OVER_LIMIT;
    if (lb >= incumbent_ns) return RX_SYM_B_CANNOT_WIN;
    if (incumbent_ns - lb < b->noise_frac * incumbent_ns) return RX_SYM_B_BELOW_NOISE;
    return RX_SYM_B_ADMIT;
}

const char *rx_sym_budget_name(int v) {
    switch (v) {
    case RX_SYM_B_ADMIT: return "admit";
    case RX_SYM_B_CANNOT_WIN: return "cannot-win";
    case RX_SYM_B_BELOW_NOISE: return "below-noise";
    case RX_SYM_B_OVER_LIMIT: return "over-limit";
    default: return "none";
    }
}

/* ------------------------------------------------------------------- trace */

void rx_sym_trace_add(RxSymTrace *tr, const RxSymTraceRec *r) {
    if (!tr || !r) return;
    if (tr->n >= RX_SYM_MAX_TRACE) { tr->dropped++; return; }
    tr->r[tr->n++] = *r;
}

static const char *verdict_name(int v) {
    switch (v) {
    case RX_SYM_V_PROVED: return "proved";
    case RX_SYM_V_TESTED: return "tested";
    case RX_SYM_V_REFUTED: return "refuted";
    case RX_SYM_V_CHIP_EQUAL: return "chip-equal";
    case RX_SYM_V_CHIP_DIFFERENT: return "chip-different";
    case RX_SYM_V_SIDE_CONDITION_FAILED: return "side-condition-failed";
    default: return "not-verified";
    }
}

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void hex32(const SemanticId *id, char out[65]) { omega_hex_semantic_id(id, out); }

int rx_sym_trace_json(const RxSymTrace *tr, const char *producer, const char *workload,
                      const char *conclusion, const char *extra, FILE *f) {
    if (!tr || !f) return -1;
    fprintf(f, "{\n  \"format\": \"aien.explain.evidence-graph\",\n  \"version\": 1,\n  \"producer\": ");
    json_str(f, producer ? producer : "omega/rx_symbolic");
    fprintf(f, ",\n  \"nodes\": [\n");
    char l[65], r[65], id[160];
    for (uint32_t i = 0; i < tr->n; i++) {
        const RxSymTraceRec *x = &tr->r[i];
        hex32(&x->lhs, l); hex32(&x->rhs, r);
        snprintf(id, sizeof id, "omega:symbolic:%s:%u:%.16s", workload ? workload : "w", i, l);
        int receipt = x->verdict.verdict != 0;
        /* status: 2 VerifiedFact for a proof or an equal chip run, 1 DirectObservation for tested /
         * refuted / measured, 3 Inference for a budget refusal (arithmetic, not a run). */
        int status = (x->verdict.verdict == RX_SYM_V_PROVED || x->verdict.verdict == RX_SYM_V_CHIP_EQUAL) ? 2
                   : receipt ? 1 : 3;
        fprintf(f, "    {\"id\": "); json_str(f, id);
        fprintf(f, ", \"kind\": %d, \"status\": %d, \"statement\": ", receipt ? 8 : 4, status);
        char st[400];
        snprintf(st, sizeof st, "rewrite %s at %s: %s; budget %s; verification %s over %u vectors%s%s",
                 x->rule, x->where, x->applied ? "applied" : "not applied", rx_sym_budget_name(x->budget),
                 verdict_name(x->verdict.verdict), x->verdict.vectors,
                 x->verdict.cache_hit ? " (verdict served by the rx_semantic cache)" : "", "");
        json_str(f, st);
        fprintf(f, ", \"source\": {\"system\": \"omega\", \"artifact\": \"rx_symbolic\", \"reference\": ");
        json_str(f, x->rule);
        fprintf(f, ", \"digest\": \"%s\"}, \"parents\": [], \"measurements\": [", r);
        fprintf(f, "{\"name\": \"cost_before\", \"value\": \"%u\", \"unit\": \"nodes\"}, ", x->cost_before);
        fprintf(f, "{\"name\": \"cost_after\", \"value\": \"%u\", \"unit\": \"nodes\"}, ", x->cost_after);
        fprintf(f, "{\"name\": \"lower_bound_ns\", \"value\": \"%.3f\", \"unit\": \"ns\"}, ", x->lb_ns);
        fprintf(f, "{\"name\": \"incumbent_ns\", \"value\": \"%.3f\", \"unit\": \"ns\"}, ", x->incumbent_ns);
        fprintf(f, "{\"name\": \"lhs_semantic_id\", \"value\": \"%s\", \"unit\": \"\"}, ", l);
        fprintf(f, "{\"name\": \"rhs_semantic_id\", \"value\": \"%s\", \"unit\": \"\"}", r);
        if (x->verdict.verdict == RX_SYM_V_REFUTED)
            fprintf(f, ", {\"name\": \"counterexample_x0\", \"value\": \"%llu\", \"unit\": \"\"}, "
                       "{\"name\": \"lhs_value\", \"value\": \"%llu\", \"unit\": \"\"}, {\"name\": \"rhs_value\", \"value\": \"%llu\", \"unit\": \"\"}",
                    (unsigned long long)x->verdict.counter[0], (unsigned long long)x->verdict.got_lhs,
                    (unsigned long long)x->verdict.got_rhs);
        fprintf(f, "], \"missing\": [], \"notes\": [");
        if (x->note[0]) json_str(f, x->note);
        fprintf(f, "]},\n");
    }
    snprintf(id, sizeof id, "omega:symbolic:%s:conclusion", workload ? workload : "w");
    fprintf(f, "    {\"id\": "); json_str(f, id);
    fprintf(f, ", \"kind\": 12, \"status\": 3, \"statement\": ");
    json_str(f, conclusion ? conclusion : "");
    fprintf(f, ", \"source\": {\"system\": \"omega\", \"artifact\": \"rx_symbolic\", \"reference\": ");
    json_str(f, workload ? workload : "");
    fprintf(f, "}, \"parents\": [");
    for (uint32_t i = 0; i < tr->n; i++) {
        char l2[65]; hex32(&tr->r[i].lhs, l2);
        snprintf(id, sizeof id, "omega:symbolic:%s:%u:%.16s", workload ? workload : "w", i, l2);
        fprintf(f, "%s{\"to\": ", i ? ", " : ""); json_str(f, id); fprintf(f, ", \"relation\": 1}");
    }
    fprintf(f, "], \"measurements\": [");
    int first = 1;
    if (extra) {
        char buf[2048];
        snprintf(buf, sizeof buf, "%s", extra);
        for (char *save = NULL, *tok = strtok_r(buf, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
            char *a = strchr(tok, '='); if (!a) continue; *a++ = 0;
            char *u = strchr(a, '='); if (u) *u++ = 0;
            fprintf(f, "%s{\"name\": ", first ? "" : ", "); json_str(f, tok);
            fprintf(f, ", \"value\": "); json_str(f, a);
            fprintf(f, ", \"unit\": "); json_str(f, u ? u : ""); fprintf(f, "}");
            first = 0;
        }
    }
    fprintf(f, "], \"missing\": [], \"notes\": [");
    if (tr->dropped) fprintf(f, "\"%u trace records dropped at the bound\"", tr->dropped);
    fprintf(f, "]}\n  ]\n}\n");
    return 0;
}

/* -------------------------------------------------- the pass in rx_graph */

static int has_ctl_in(const AgGraph *g, uint32_t n) {
    for (uint32_t d = 0; d < g->n_deps; d++) if (g->deps[d].to == n) return 1;
    return 0;
}

static uint32_t edges_in(const AgGraph *g, uint32_t n, uint32_t e[AG_MAX_IN]) {
    uint32_t k = 0;
    for (uint32_t p = 0; p < AG_MAX_IN; p++)
        for (uint32_t i = 0; i < g->n_data; i++)
            if (g->data[i].to == n && g->data[i].port == p && k < AG_MAX_IN) e[k++] = i;
    return k;
}

static int is_pinned(const AgGraph *g, uint32_t n) {
    AgKind k = g->nodes[n].kind;
    if (k == AG_WORLD_PUBLISH || k == AG_EFFECT_PERFORM) return 1;
    for (uint32_t i = 0; i < g->n_evidence; i++) if (g->evidence[i] == n) return 1;
    for (uint32_t i = 0; i < g->n_success; i++) if (g->success[i].node == n) return 1;
    for (uint32_t i = 0; i < g->n_failure; i++) if (g->failure[i].node == n) return 1;
    return 0;
}

typedef struct {
    RxSymPass *p;
    const AgGraph *g;
    uint32_t term[AG_MAX_NODES];
    uint64_t region[AG_MAX_NODES];   /* graph nodes inlined into node n's term (n included) */
    uint8_t inlinable[AG_MAX_NODES]; /* PURE or CONST a consumer may look through */
    uint8_t may_fail[AG_MAX_NODES];  /* the region holds an operation that can fail */
    uint8_t depth[AG_MAX_NODES];
} Build;

static const RxSymAssume *fact_of(const RxSymPass *p, uint32_t origin) {
    for (uint32_t i = 0; i < p->n_facts; i++) if (p->fact[i].origin == origin) return &p->fact[i].a;
    return NULL;
}

static uint32_t var_for(Build *b, uint32_t m) {
    RxSymStore *s = b->p->store;
    uint32_t v = rx_sym_var(s, m, b->g->nodes[m].out_type == AG_T_BOOL ? rx_sym_bool() : rx_sym_u64());
    const RxSymAssume *a = fact_of(b->p, b->g->nodes[m].origin);
    return a ? rx_sym_with_assume(s, v, a) : v;
}

/* Term of node n as its consumers see it. */
static void build_node(Build *b, uint32_t n) {
    const AgGraph *g = b->g;
    const AgNode *x = &g->nodes[n];
    RxSymStore *s = b->p->store;
    b->region[n] = 1ull << n;
    b->inlinable[n] = 0;
    b->may_fail[n] = 0;
    b->depth[n] = 0;
    b->term[n] = RX_SYM_NONE;
    if (x->kind == AG_CONST && !has_ctl_in(g, n)) {
        b->term[n] = rx_sym_const(s, x->imm, x->out_type == AG_T_BOOL ? rx_sym_bool() : rx_sym_u64());
        b->inlinable[n] = 1;
        return;
    }
    uint32_t e[AG_MAX_IN];
    uint32_t k = x->kind == AG_PURE ? edges_in(g, n, e) : 0;
    int ok = x->kind == AG_PURE && !has_ctl_in(g, n) && k >= 1 && k <= 2;
    for (uint32_t i = 0; ok && i < k; i++) ok = g->data[e[i]].mode == AG_EDGE_DATA;
    if (!ok) { b->term[n] = var_for(b, n); return; }
    uint32_t in[2];
    uint8_t dep = 0;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t m = g->data[e[i]].from;
        if (b->inlinable[m] && b->depth[m] < RX_SYM_MAX_DEPTH) {
            in[i] = b->term[m];
            b->region[n] |= b->region[m];
            b->may_fail[n] |= b->may_fail[m];
            if (b->depth[m] + 1 > dep) dep = (uint8_t)(b->depth[m] + 1);
        } else {
            in[i] = var_for(b, m);
        }
    }
    RxSymType ty = rx_sym_u64();
    uint32_t a = in[0];
    uint32_t bb = k > 1 ? in[1] : rx_sym_const(s, x->imm, ty);
    uint32_t t;
    RxSymType oty = (x->op == OP_LESS_THAN || x->op == OP_EQUAL) ? rx_sym_bool() : ty;
    if (x->op == OP_IDENTITY) t = a;
    else if (x->op == OP_CONSTANT) t = bb;
    else t = rx_sym_op(s, x->op, oty, a, bb);
    if (x->op == OP_DIV) {
        const RxSymTerm *d = bb < s->n ? &s->t[bb] : NULL;
        if (!d || d->head != RX_SYM_H_CONST || d->imm == 0) b->may_fail[n] = 1;
    }
    for (uint32_t i = 0; i < x->n_fused && t != RX_SYM_NONE; i++) {
        uint32_t c = rx_sym_const(s, x->fused[i].imm, ty);
        uint32_t fop = x->fused[i].op;
        if (fop == OP_IDENTITY) continue;
        if (fop == OP_CONSTANT) { t = c; continue; }
        if (fop == OP_DIV && x->fused[i].imm == 0) b->may_fail[n] = 1;
        t = rx_sym_op(s, fop, (fop == OP_LESS_THAN || fop == OP_EQUAL) ? rx_sym_bool() : ty, t, c);
    }
    b->term[n] = t;
    b->depth[n] = dep;
    b->inlinable[n] = t != RX_SYM_NONE && x->out_type == AG_T_U64;
    if (t == RX_SYM_NONE) b->term[n] = var_for(b, n);
}

/* Nodes of `region` (other than n) that die once n no longer reads them:
 * every consumer is inside the region or n itself, and none is pinned. */
static uint64_t dying(const AgGraph *g, uint32_t n, uint64_t region) {
    uint64_t dead = region & ~(1ull << n);
    for (int changed = 1; changed;) {
        changed = 0;
        for (uint32_t m = 0; m < g->n_nodes; m++) {
            if (!((dead >> m) & 1u)) continue;
            int keep = is_pinned(g, m);
            for (uint32_t e = 0; e < g->n_data && !keep; e++)
                if (g->data[e].from == m) {
                    uint32_t c = g->data[e].to;
                    if (c != n && !((dead >> c) & 1u)) keep = 1;
                }
            for (uint32_t d = 0; d < g->n_deps && !keep; d++)
                if (g->deps[d].from == m) keep = 1;
            if (keep) { dead &= ~(1ull << m); changed = 1; }
        }
    }
    return dead;
}

static int popc(uint64_t x) { int c = 0; while (x) { x &= x - 1; c++; } return c; }

/* Replace what node n computes: op on input nodes in[0..k) then fused steps. */
static void rewrite_node(AgGraph *g, uint32_t n, AgKind kind, uint32_t op, uint64_t imm, const uint32_t *in, uint32_t k,
                         const AgFusedStep *fused, uint32_t n_fused) {
    uint32_t j = 0;
    for (uint32_t e = 0; e < g->n_data; e++) if (g->data[e].to != n) g->data[j++] = g->data[e];
    g->n_data = j;
    AgNode *x = &g->nodes[n];
    x->kind = kind;
    x->op = op;
    x->imm = imm;
    x->n_fused = n_fused;
    for (uint32_t i = 0; i < n_fused; i++) x->fused[i] = fused[i];
    x->out_type = AG_T_U64;
    for (uint32_t i = 0; i < k; i++) rx_graph_data(g, in[i], n, i, AG_EDGE_DATA);
}

typedef struct { RxSymStore *s; uint32_t lhs, rhs; RxSymAssume ranges[64]; } VCtx;
static int vfn(void *v, RxSymVerdict *out) {
    VCtx *c = v;
    return rx_sym_verify_u64(c->s, c->lhs, c->rhs, 64, c->ranges, out);
}

/* A candidate refused once (same left side, same rule) is not tried again in
 * later rounds of the same optimize call. */
static int seen_before(RxSymPass *p, const SemanticId *lhs) {
    for (uint32_t i = 0; i < p->n_seen; i++)
        if (memcmp(p->seen[i].bytes, lhs->bytes, OMEGA_ID_BYTES) == 0) return 1;
    if (p->n_seen < 64) p->seen[p->n_seen++] = *lhs;
    return 0;
}

static int sym_pass(AgRewriteHook *h, AgGraph *g) {
    RxSymPass *p = (RxSymPass *)h;
    if (!p->store || p->tried >= p->fuel) return 0;
    rx_sym_store_init(p->store);
    static __thread Build b;
    memset(&b, 0, sizeof b);
    b.p = p; b.g = g;
    uint16_t ord[AG_MAX_NODES];
    int cnt = rx_graph_topo(g, ord);
    if (cnt < 0) return cnt;
    for (int t = 0; t < cnt; t++) build_node(&b, ord[t]);
    RxSymStore *s = p->store;
    int applied = 0;
    for (int t = 0; t < cnt && p->tried < p->fuel; t++) {
        uint32_t n = ord[t];
        const AgNode *x = &g->nodes[n];
        if (x->kind != AG_PURE || has_ctl_in(g, n) || b.term[n] == RX_SYM_NONE) continue;
        uint32_t lhs = b.term[n];
        if (s->t[lhs].head != RX_SYM_H_OP) continue;     /* already a variable or constant */
        RxSymTraceRec rec; memset(&rec, 0, sizeof rec);
        snprintf(rec.where, sizeof rec.where, "node %u (origin %u)", n, x->origin);
        rec.lhs = s->t[lhs].id;
        uint32_t old_steps = 1 + x->n_fused;
        uint32_t rhs = RX_SYM_NONE;
        AgKind nk = AG_PURE; uint32_t nop = 0, nin[2] = { 0, 0 }, nk_in = 0, nf = 0; uint64_t nimm = 0;
        AgFusedStep fs[2];
        RxSymAffine af;
        const RxSymTerm *L = &s->t[lhs];
        if (b.may_fail[n]) continue;    /* never remove a computation that can fail */
        if (rx_sym_affine(s, lhs, &af) == 0) {
            snprintf(rec.rule, sizeof rec.rule, "affine-normal-form/Z2^64");
            RxSymType ty = rx_sym_u64();
            if (af.n == 0) {
                rhs = rx_sym_const(s, af.k, ty);
                nk = AG_CONST; nimm = af.k;
            } else if (af.n == 1) {
                uint32_t v = var_for(&b, (uint32_t)af.var[0]);
                uint32_t m = af.coef[0] == 1 ? v : rx_sym_op(s, OP_MUL, ty, v, rx_sym_const(s, af.coef[0], ty));
                rhs = af.k == 0 ? m : rx_sym_op(s, OP_ADD, ty, m, rx_sym_const(s, af.k, ty));
                nin[0] = (uint32_t)af.var[0]; nk_in = 1;
                if (af.coef[0] == 1) { nop = af.k ? OP_ADD : OP_IDENTITY; nimm = af.k; }
                else { nop = OP_MUL; nimm = af.coef[0]; if (af.k) fs[nf++] = (AgFusedStep){ OP_ADD, af.k }; }
            } else if (af.n == 2 && af.coef[0] == 1 && af.coef[1] == 1) {
                uint32_t v0 = var_for(&b, (uint32_t)af.var[0]), v1 = var_for(&b, (uint32_t)af.var[1]);
                uint32_t m = rx_sym_op(s, OP_ADD, ty, v0, v1);
                rhs = af.k == 0 ? m : rx_sym_op(s, OP_ADD, ty, m, rx_sym_const(s, af.k, ty));
                nin[0] = (uint32_t)af.var[0]; nin[1] = (uint32_t)af.var[1]; nk_in = 2;
                nop = OP_ADD; if (af.k) fs[nf++] = (AgFusedStep){ OP_ADD, af.k };
            } else {
                continue;   /* would need more than one node: not cheaper here */
            }
        } else if (L->op == OP_DIV && L->n_args == 2 && s->t[L->arg[1]].head == RX_SYM_H_CONST &&
                   s->t[L->arg[0]].head == RX_SYM_H_OP && s->t[L->arg[0]].op == OP_MUL) {
            /* (y * c) / c -> y: true in Z only. Applied when a RANGE fact on y
             * proves y * c never wraps. */
            const RxSymTerm *M = &s->t[L->arg[0]];
            uint64_t c = s->t[L->arg[1]].imm;
            if (M->n_args != 2 || s->t[M->arg[1]].head != RX_SYM_H_CONST || s->t[M->arg[1]].imm != c || c == 0) continue;
            const RxSymTerm *Y = &s->t[M->arg[0]];
            if (Y->head != RX_SYM_H_VAR) continue;
            snprintf(rec.rule, sizeof rec.rule, "div-mul-cancel/range");
            const RxSymAssume *fa = NULL;
            for (uint32_t i = 0; i < Y->n_assume; i++) if (Y->assume[i].kind == RX_SYM_A_RANGE) fa = &Y->assume[i];
            if (!fa || fa->hi > UINT64_MAX / c) {
                if (seen_before(p, &rec.lhs)) continue;
                snprintf(rec.note, sizeof rec.note, "refused: no RANGE fact proves y*%llu stays below 2^64",
                         (unsigned long long)c);
                rec.budget = 0;
                p->tried++; p->refused++;
                rx_sym_trace_add(p->trace, &rec);
                continue;
            }
            rhs = M->arg[0];
            nin[0] = (uint32_t)Y->imm; nk_in = 1; nop = OP_IDENTITY;
        } else if (p->test_unsound && L->op == OP_MUL && L->n_args == 2 && s->t[L->arg[0]].head == RX_SYM_H_OP &&
                   s->t[L->arg[0]].op == OP_DIV && s->t[s->t[L->arg[0]].arg[0]].head == RX_SYM_H_VAR) {
            /* TEST ONLY: (y / c) * c -> y, false for any y not a multiple of c. */
            snprintf(rec.rule, sizeof rec.rule, "TEST-unsound-mul-div");
            const RxSymTerm *D = &s->t[L->arg[0]];
            rhs = D->arg[0];
            nin[0] = (uint32_t)s->t[D->arg[0]].imm; nk_in = 1; nop = OP_IDENTITY;
        } else {
            continue;
        }
        if (rhs == RX_SYM_NONE || rhs == lhs) continue;
        if (seen_before(p, &rec.lhs)) continue;
        p->tried++;
        rec.rhs = s->t[rhs].id;
        uint64_t dead = dying(g, n, b.region[n]);
        /* After: this node (0 if it becomes a constant the fold pass absorbs is
         * still one node) plus survivors; before: the region. Cost in reactions. */
        rec.cost_before = (uint32_t)popc(b.region[n]);
        rec.cost_after = rec.cost_before - (uint32_t)popc(dead);
        uint32_t new_steps = nk == AG_CONST ? 0 : 1 + nf;
        if (popc(dead) == 0 && new_steps >= old_steps) {
            rec.budget = RX_SYM_B_CANNOT_WIN;
            snprintf(rec.note, sizeof rec.note, "no node dies and the step count does not drop (%u -> %u)", old_steps, new_steps);
            p->refused++;
            rx_sym_trace_add(p->trace, &rec);
            continue;
        }
        if (p->budget) {
            RxSymWork w; memset(&w, 0, sizeof w);
            w.launches = rec.cost_after;
            rec.incumbent_ns = rec.cost_before * p->reaction_ns;
            rec.budget = rx_sym_budget_admit(p->budget, &w, rec.incumbent_ns, &rec.lb_ns);
            if (rec.budget != RX_SYM_B_ADMIT && popc(dead) == 0) {
                p->refused++;
                rx_sym_trace_add(p->trace, &rec);
                continue;
            }
        } else {
            rec.budget = RX_SYM_B_ADMIT;
        }
        static __thread VCtx vc;
        memset(&vc, 0, sizeof vc);
        vc.s = s; vc.lhs = lhs; vc.rhs = rhs;
        for (uint32_t m = 0; m < g->n_nodes && m < 64; m++) {
            const RxSymAssume *fa = fact_of(p, g->nodes[m].origin);
            if (fa) vc.ranges[m] = *fa;
        }
        if (p->cache_fn) p->cache_fn(p->cache, rec.rule, &rec.lhs, &rec.rhs, vfn, &vc, &rec.verdict);
        else vfn(&vc, &rec.verdict);
        if (rec.verdict.verdict != RX_SYM_V_PROVED && rec.verdict.verdict != RX_SYM_V_TESTED) {
            p->refuted++;
            snprintf(rec.note, sizeof rec.note, "refuted bit for bit: candidate dropped, graph unchanged");
            rx_sym_trace_add(p->trace, &rec);
            continue;
        }
        if (nk == AG_CONST) rewrite_node(g, n, AG_CONST, 0, nimm, NULL, 0, NULL, 0);
        else rewrite_node(g, n, AG_PURE, nop, nimm, nin, nk_in, fs, nf);
        rec.applied = 1;
        p->applied++;
        applied++;
        rx_sym_trace_add(p->trace, &rec);
        /* Terms of later nodes were built from the old graph: stop this round;
         * the optimizer loop calls again after its own passes. */
        break;
    }
    return applied;
}

void rx_sym_pass_init(RxSymPass *p, RxSymStore *store, uint32_t fuel) {
    memset(p, 0, sizeof *p);
    p->hook.pass = sym_pass;
    p->store = store;
    p->fuel = fuel;
}

int rx_sym_pass_fact(RxSymPass *p, uint32_t origin, const RxSymAssume *a) {
    if (!p || !a || p->n_facts >= 16) return -1;
    p->fact[p->n_facts].origin = origin;
    p->fact[p->n_facts].a = *a;
    p->n_facts++;
    return 0;
}

/* ------------------------------------------------- decode-graph (kernel) */

int rx_sym_barriers(const RxSymLaunchList *ll, RxSymPass *pass, RxSymBarrierReport *out, AgReport *rep) {
    if (!ll || !out || ll->n == 0 || ll->n + 1 > AG_MAX_NODES) return -1;
    memset(out, 0, sizeof *out);
    static __thread AgGraph g;
    rx_graph_init(&g, 0);
    int src = rx_graph_node(&g, AG_CONST, AG_T_U64);     /* the token input */
    int node[64];
    for (uint32_t i = 0; i < ll->n; i++) {
        node[i] = rx_graph_node(&g, AG_PHYSICAL, AG_T_U64);
        g.nodes[node[i]].op = ll->l[i].kind;
        g.nodes[node[i]].imm = i;                        /* one launch, one identity */
        g.nodes[node[i]].cost_us = (uint32_t)(ll->l[i].us + 0.5);
        out->span_us += ll->l[i].us;
        if (ll->l[i].weight_bytes) out->weight_us += ll->l[i].us; else out->glue_us += ll->l[i].us;
    }
    /* Hazards: for each launch, its nearest earlier launch on every buffer it
     * touches where one of the two writes (RAW, WAR, WAW). Ports hold up to
     * AG_MAX_IN distinct producers; more than that is refused. */
    for (uint32_t i = 0; i < ll->n; i++) {
        uint32_t ports = 0, from[64];
        uint32_t touch = ll->l[i].reads | ll->l[i].writes;
        for (uint32_t bit = 0; bit < 32; bit++) {
            if (!((touch >> bit) & 1u)) continue;
            const int iw = (ll->l[i].writes >> bit) & 1u;
            for (int j = (int)i - 1; j >= 0; j--) {
                uint32_t jt = ll->l[j].reads | ll->l[j].writes;
                if (!((jt >> bit) & 1u)) continue;
                const int jw = (ll->l[j].writes >> bit) & 1u;
                if (jw || iw) {      /* RAW or WAW with the last writer, WAR with every reader since it */
                    int dup = 0;
                    for (uint32_t q = 0; q < ports; q++) dup |= from[q] == (uint32_t)j;
                    if (!dup) from[ports++] = (uint32_t)j;
                }
                if (jw) break;       /* earlier launches are ordered behind the last writer */
            }
        }
        if (ports == 0) { rx_graph_data(&g, (uint32_t)src, (uint32_t)node[i], 0, AG_EDGE_DATA); continue; }
        if (ports > AG_MAX_IN) return -2;
        for (uint32_t q = 0; q < ports; q++) rx_graph_data(&g, (uint32_t)node[from[q]], (uint32_t)node[i], q, AG_EDGE_DATA);
    }
    /* Recorded barriers: launch i waits for launch i-1 unless marked independent. */
    for (uint32_t i = 1; i < ll->n; i++) {
        out->barriers_total++;
        if (ll->l[i].marked_independent) { out->already_dropped++; continue; }
        rx_graph_order(&g, (uint32_t)node[i - 1], (uint32_t)node[i]);
    }
    rx_graph_success(&g, (uint32_t)node[ll->n - 1], AG_OK);
    AgReport r; memset(&r, 0, sizeof r);
    if (rx_graph_validate(&g, NULL) != 0) return -3;
    int rc = rx_graph_optimize_ex(&g, &r, pass ? &pass->hook : NULL);
    if (rep) *rep = r;
    if (rc != 0) return rc;
    /* A recorded barrier i-1 -> i is needed iff launch i-1 is a direct hazard
     * source of launch i (the optimizer keeps exactly the data edges). */
    for (uint32_t i = 1; i < ll->n; i++) {
        if (ll->l[i].marked_independent) continue;
        /* needed: launch i reads or writes what i-1 touches; or i-1 itself runs beside i-2 (marked
         * independent) and i has a hazard with i-2, which the barrier before i-1 no longer covers */
        int needed = 0;
        for (uint32_t e = 0; e < g.n_data; e++) {
            /* nodes were compacted; origins survive */
            const AgNode *f = &g.nodes[g.data[e].from], *t = &g.nodes[g.data[e].to];
            if (t->origin != (uint32_t)node[i]) continue;
            if (f->origin == (uint32_t)node[i - 1]) needed = 1;
            if (i >= 2 && ll->l[i - 1].marked_independent && f->origin == (uint32_t)node[i - 2]) needed = 1;
        }
        if (needed) out->barriers_needed++;
        else { out->barriers_droppable++; out->droppable_at[out->n_droppable_at++] = i; }
    }
    return 0;
}

int rx_sym_gather_fold_check(const uint32_t *perm, uint32_t n, const uint16_t *scale_inv, uint32_t rows,
                             uint32_t cols, uint32_t *blocks_bad, char *why, size_t why_cap) {
    if (blocks_bad) *blocks_bad = 0;
    if (!perm || n == 0 || n > rows) { if (why) snprintf(why, why_cap, "bad arguments"); return 0; }
    /* 1. a permutation of n distinct source rows */
    static __thread uint8_t seen[1u << 16];
    if (rows > sizeof seen) { if (why) snprintf(why, why_cap, "too many rows"); return 0; }
    memset(seen, 0, rows);
    for (uint32_t i = 0; i < n; i++) {
        if (perm[i] >= rows || seen[perm[i]]) { if (why) snprintf(why, why_cap, "gather table is not a permutation (row %u)", i); return 0; }
        seen[perm[i]] = 1;
    }
    if (!scale_inv) { if (why) snprintf(why, why_cap, "eligible: no block scales"); return 1; }
    /* 2. block scales: new row i reads scale block perm[i]/128 of the old
     * tensor; the new tensor has one scale per 128 new rows. Every new block
     * needs one scale row: all its source blocks must hold identical scales
     * in every column block. */
    const uint32_t cb = (cols + 127) / 128;
    uint32_t bad = 0;
    for (uint32_t nb = 0; nb * 128 < n; nb++) {
        uint32_t first = perm[nb * 128] / 128;
        int ok = 1;
        for (uint32_t i = nb * 128; i < n && i < nb * 128 + 128 && ok; i++) {
            uint32_t sb = perm[i] / 128;
            if (sb == first) continue;
            for (uint32_t c = 0; c < cb; c++)
                if (scale_inv[(size_t)sb * cb + c] != scale_inv[(size_t)first * cb + c]) { ok = 0; break; }
        }
        bad += !ok;
    }
    if (blocks_bad) *blocks_bad = bad;
    if (bad) {
        if (why) snprintf(why, why_cap, "%u of %u new 128-row blocks would need two different scales", bad, (n + 127) / 128);
        return 0;
    }
    if (why) snprintf(why, why_cap, "eligible: every new block maps onto one scale row");
    return 1;
}
