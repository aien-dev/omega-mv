/*
 * rwcheck -- independent derivation checker for #249 rewrite contracts (ledger L059).
 *
 * Read: aien-dev/aien-architecture-mv#249 (Wolfram-method extension, 2026-10-19), omega
 * a8ffd2b9 src/omega_program.h (V0 body semantics, OVERFLOW_WRAP lowering). Design note:
 * docs/research/249-checked-elimination.md.
 *
 * This program shares no source with rwsearch.c. It has its own parser, term representation,
 * evaluator and matcher, and links omega's core only to cross-check V0 chains with
 * omega_program_eval.
 *
 * Modes:
 *   rwcheck axioms      --axioms A
 *   rwcheck contracts   --axioms A [--cache F] [--semantics-version V] FILE.rw...
 *   rwcheck derivations --axioms A --contracts DIR [--cache F] [--semantics-version V] FILE.rwd
 *
 * UNVERIFIED: semantic guard sample sizes (RW_SAMPLE_*), the int-domain window, the f32 sample set.
 * UNVERIFIED: integer division in the int domain truncates toward zero (C semantics).
 */
/* Messages are cut to their buffers on purpose; a truncated reason is still a refusal. */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "omega_program.h"
#include "sha256.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define RW_MAX_NODES   65536
#define RW_MAX_SYMS    64
#define RW_MAX_STEPS   32
#define RW_MAX_ASSUME  8
#define RW_MAX_AXIOMS  64
#define RW_MAX_CONTRACTS 64
#define RW_MAX_PARAMS  4
#define RW_SAMPLE_W8_PARAMS  65536u  /* param combos sampled at w=8 when exhaustive is too large */
#define RW_SAMPLE_W16_PARAMS 48u     /* param combos at w=16 (plus edge values) */
#define RW_SAMPLE_WIDE_X     4096u   /* x samples at w=32/64 for instance checks */
#define RW_SEMANTICS_DEFAULT "omega.v0.wrap.1"

/* ---------------------------------------------------------------- terms -- */
typedef enum {
    K_LIT, K_ONES, K_SYM, K_PVAR, K_OPVAR,
    K_ADD, K_SUB, K_MUL, K_AND, K_OR, K_NEG, K_NOT, K_UDIV, K_ISQRT,
    K_EQ, K_NE, K_GE, K_LE
} Kind;

typedef struct {
    uint8_t kind;
    uint8_t sym;        /* K_SYM / K_PVAR / K_OPVAR: symbol index */
    int32_t a, b;       /* children, -1 if none */
    uint64_t v;         /* K_LIT */
} Node;

static Node g_n[RW_MAX_NODES];
static int g_nn;
static uint64_t g_rng;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static char g_sym[RW_MAX_SYMS][16];
static int g_nsym;

static int die_line;
static void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rwcheck: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

static int sym_id(const char *s) {
    for (int i = 0; i < g_nsym; ++i)
        if (!strcmp(g_sym[i], s)) return i;
    if (g_nsym >= RW_MAX_SYMS || strlen(s) >= sizeof g_sym[0]) fatal("too many or too long symbols (%s)", s);
    snprintf(g_sym[g_nsym], sizeof g_sym[0], "%s", s);
    return g_nsym++;
}

static int mk(int kind, int a, int b, uint64_t v, int sym) {
    if (g_nn >= RW_MAX_NODES) fatal("node arena full");
    Node *n = &g_n[g_nn];
    n->kind = (uint8_t)kind; n->a = a; n->b = b; n->v = v; n->sym = (uint8_t)sym;
    return g_nn++;
}

static int is_binop(int k) { return k == K_ADD || k == K_SUB || k == K_MUL || k == K_AND || k == K_OR || k == K_UDIV
                                 || k == K_EQ || k == K_NE || k == K_GE || k == K_LE; }

static const struct { const char *name; int kind; } OPS[] = {
    {"add", K_ADD}, {"sub", K_SUB}, {"mul", K_MUL}, {"and", K_AND}, {"or", K_OR},
    {"neg", K_NEG}, {"not", K_NOT}, {"udiv", K_UDIV}, {"isqrt", K_ISQRT},
    {"eq", K_EQ}, {"ne", K_NE}, {"ge", K_GE}, {"le", K_LE}, {NULL, 0}
};
static const char *op_name(int k) {
    for (int i = 0; OPS[i].name; ++i) if (OPS[i].kind == k) return OPS[i].name;
    return "?";
}
static int op_kind(const char *s) {
    for (int i = 0; OPS[i].name; ++i) if (!strcmp(OPS[i].name, s)) return OPS[i].kind;
    return -1;
}

/* s-expression parser */
static const char *g_p;
static void skipws(void) { while (*g_p && isspace((unsigned char)*g_p)) g_p++; }
static int read_tok(char *out, size_t cap) {
    skipws();
    size_t n = 0;
    while (*g_p && !isspace((unsigned char)*g_p) && *g_p != '(' && *g_p != ')') {
        if (n + 1 >= cap) return -1;
        out[n++] = *g_p++;
    }
    out[n] = 0;
    return (int)n;
}
static int parse_term(void) {
    skipws();
    if (*g_p == '(') {
        g_p++;
        char t[32];
        if (read_tok(t, sizeof t) <= 0) fatal("line %d: expected operator", die_line);
        int k;
        int a = -1, b = -1;
        if (!strcmp(t, "OP")) k = K_OPVAR;
        else if ((k = op_kind(t)) < 0) fatal("line %d: unknown operator %s", die_line, t);
        if (k == K_OPVAR) {
            /* (OP a b): an operation variable applied to two children */
            a = parse_term(); b = parse_term();
            skipws(); if (*g_p != ')') fatal("line %d: expected )", die_line); g_p++;
            return mk(K_OPVAR, a, b, 0, sym_id("OP"));
        }
        a = parse_term();
        if (is_binop(k)) b = parse_term();
        skipws(); if (*g_p != ')') fatal("line %d: expected ) after %s", die_line, t); g_p++;
        return mk(k, a, b, 0, 0);
    }
    char t[32];
    if (read_tok(t, sizeof t) <= 0) fatal("line %d: expected term", die_line);
    if (isdigit((unsigned char)t[0])) {
        errno = 0;
        char *end;
        unsigned long long v = strtoull(t, &end, 0);
        if (errno || *end) fatal("line %d: bad number %s", die_line, t);
        return mk(K_LIT, -1, -1, v, 0);
    }
    if (!strcmp(t, "ones")) return mk(K_ONES, -1, -1, 0, 0);
    if (t[0] == '?') return mk(K_PVAR, -1, -1, 0, sym_id(t));
    return mk(K_SYM, -1, -1, 0, sym_id(t));
}
static int parse_str(const char *s) {
    g_p = s;
    int t = parse_term();
    skipws();
    if (*g_p) fatal("line %d: trailing text '%s'", die_line, g_p);
    return t;
}

static int teq(int x, int y) {
    if (x == y) return 1;
    if (x < 0 || y < 0) return 0;
    const Node *a = &g_n[x], *b = &g_n[y];
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case K_LIT: return a->v == b->v;
    case K_ONES: return 1;
    case K_SYM: case K_PVAR: return a->sym == b->sym;
    case K_OPVAR: return a->sym == b->sym && teq(a->a, b->a) && teq(a->b, b->b);
    default: return teq(a->a, b->a) && teq(a->b, b->b);
    }
}

__attribute__((unused)) static void tprint(int t, char *out, size_t cap) {
    size_t len = strlen(out);
    if (len + 1 >= cap) return;
    const Node *n = &g_n[t];
    char *o = out + len;
    size_t c = cap - len;
    switch (n->kind) {
    case K_LIT: snprintf(o, c, "%llu", (unsigned long long)n->v); return;
    case K_ONES: snprintf(o, c, "ones"); return;
    case K_SYM: case K_PVAR: snprintf(o, c, "%s", g_sym[n->sym]); return;
    default: break;
    }
    snprintf(o, c, "(%s ", n->kind == K_OPVAR ? "OP" : op_name(n->kind));
    tprint(n->a, out, cap);
    if (n->b >= 0) { strncat(out, " ", cap - strlen(out) - 1); tprint(n->b, out, cap); }
    strncat(out, ")", cap - strlen(out) - 1);
}

/* replace every K_OPVAR by concrete op k (deep copy) */
static int subst_op(int t, int k) {
    if (t < 0) return -1;
    const Node n = g_n[t];
    if (n.kind == K_OPVAR) return mk(k, subst_op(n.a, k), subst_op(n.b, k), 0, 0);
    if (n.a < 0 && n.b < 0) return t;
    return mk(n.kind, subst_op(n.a, k), subst_op(n.b, k), n.v, n.sym);
}

/* --------------------------------------------------------- evaluation -- */
typedef enum { D_WRAP, D_INT, D_F32 } Domain;

typedef struct {
    Domain d;
    unsigned w;            /* D_WRAP width */
    uint64_t val[RW_MAX_SYMS];  /* by symbol id: wrap / int (as int64 bits) / f32 (as bits) */
} Env;

static uint64_t wmask(unsigned w) { return w >= 64 ? ~0ull : ((1ull << w) - 1); }
static uint64_t isqrt64(uint64_t n) {
    uint64_t r = (uint64_t)sqrtl((long double)n);
    while (r * r > n) r--;
    while ((r + 1) * (r + 1) <= n && (r + 1) * (r + 1) > r * r) r++;
    return r;
}

/* returns 0 ok, -1 undefined (division by zero, isqrt of a negative) */
static int ev_wrap(int t, const Env *e, uint64_t *out) {
    const Node *n = &g_n[t];
    uint64_t m = wmask(e->w), a = 0, b = 0;
    switch (n->kind) {
    case K_LIT: *out = n->v & m; return (n->v & ~m) ? -1 : 0;   /* a literal must fit the width */
    case K_ONES: *out = m; return 0;
    case K_SYM: case K_PVAR: *out = e->val[n->sym] & m; return 0;
    default: break;
    }
    if (n->a >= 0 && ev_wrap(n->a, e, &a)) return -1;
    if (n->b >= 0 && ev_wrap(n->b, e, &b)) return -1;
    switch (n->kind) {
    case K_ADD: *out = (a + b) & m; return 0;
    case K_SUB: *out = (a - b) & m; return 0;
    case K_MUL: *out = (a * b) & m; return 0;
    case K_AND: *out = a & b; return 0;
    case K_OR: *out = a | b; return 0;
    case K_NEG: *out = (0 - a) & m; return 0;
    case K_NOT: *out = ~a & m; return 0;
    case K_UDIV: if (!b) return -1; *out = a / b; return 0;
    case K_ISQRT: *out = isqrt64(a); return 0;
    case K_EQ: *out = a == b; return 0;
    case K_NE: *out = a != b; return 0;
    case K_GE: *out = a >= b; return 0;
    case K_LE: *out = a <= b; return 0;
    default: return -1;
    }
}

static int ev_int(int t, const Env *e, int64_t *out) {
    const Node *n = &g_n[t];
    int64_t a = 0, b = 0;
    switch (n->kind) {
    case K_LIT: *out = (int64_t)n->v; return 0;
    case K_ONES: return -1;                     /* no all-ones word in Z */
    case K_SYM: case K_PVAR: *out = (int64_t)e->val[n->sym]; return 0;
    default: break;
    }
    if (n->a >= 0 && ev_int(n->a, e, &a)) return -1;
    if (n->b >= 0 && ev_int(n->b, e, &b)) return -1;
    switch (n->kind) {
    case K_ADD: *out = a + b; return 0;
    case K_SUB: *out = a - b; return 0;
    case K_MUL: *out = a * b; return 0;          /* window keeps |a*b| far below 2^63 */
    case K_NEG: *out = -a; return 0;
    case K_UDIV: if (!b) return -1; *out = a / b; return 0;
    case K_ISQRT: if (a < 0) return -1; *out = (int64_t)isqrt64((uint64_t)a); return 0;
    case K_EQ: *out = a == b; return 0;
    case K_NE: *out = a != b; return 0;
    case K_GE: *out = a >= b; return 0;
    case K_LE: *out = a <= b; return 0;
    default: return -1;                          /* bitwise ops are not integer operations here */
    }
}

static float bits_f(uint64_t v) { uint32_t u = (uint32_t)v; float f; memcpy(&f, &u, 4); return f; }
static uint64_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int ev_f32(int t, const Env *e, uint64_t *out) {
    const Node *n = &g_n[t];
    uint64_t a = 0, b = 0;
    switch (n->kind) {
    case K_LIT: *out = f_bits((float)n->v); return 0;
    case K_SYM: case K_PVAR: *out = e->val[n->sym]; return 0;
    default: break;
    }
    if (n->a >= 0 && ev_f32(n->a, e, &a)) return -1;
    if (n->b >= 0 && ev_f32(n->b, e, &b)) return -1;
    volatile float x = bits_f(a), y = bits_f(b), r;
    switch (n->kind) {
    case K_ADD: r = x + y; break;
    case K_SUB: r = x - y; break;
    case K_MUL: r = x * y; break;
    case K_NEG: r = -x; break;
    default: return -1;
    }
    *out = f_bits(r);
    return 0;
}

/* lhs and rhs agree on this row? 1 yes, 0 no; undefined on exactly one side is a disagreement */
static int row_agrees(Domain d, int l, int r, const Env *e) {
    if (d == D_WRAP) {
        uint64_t x, y; int ul = ev_wrap(l, e, &x), ur = ev_wrap(r, e, &y);
        return ul || ur ? (ul && ur) : x == y;
    }
    if (d == D_INT) {
        int64_t x, y; int ul = ev_int(l, e, &x), ur = ev_int(r, e, &y);
        return ul || ur ? (ul && ur) : x == y;
    }
    uint64_t x, y; int ul = ev_f32(l, e, &x), ur = ev_f32(r, e, &y);
    if (ul || ur) return ul && ur;
    float fx = bits_f(x), fy = bits_f(y);
    if (isnan(fx) && isnan(fy)) return 1;
    return x == y;
}

static int cond_holds(Domain d, int c, const Env *e) {
    if (d == D_WRAP) { uint64_t v; return ev_wrap(c, e, &v) == 0 && v; }
    if (d == D_INT) { int64_t v; return ev_int(c, e, &v) == 0 && v; }
    return 0;
}

/* ------------------------------------------------------------- axioms -- */
typedef struct {
    char name[32];
    int lhs, rhs;
    int side;           /* -1 or a condition term over pattern variables */
    unsigned domains;   /* bit per Domain */
} Axiom;
static Axiom g_ax[RW_MAX_AXIOMS];
static int g_nax;

static unsigned parse_domains(const char *s) {
    unsigned m = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        if (!strcmp(tok, "wrap")) m |= 1u << D_WRAP;
        else if (!strcmp(tok, "int")) m |= 1u << D_INT;
        else if (!strcmp(tok, "f32")) m |= 1u << D_F32;
        else fatal("line %d: unknown domain %s", die_line, tok);
    }
    return m;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void load_axioms(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) fatal("cannot open axioms %s", path);
    char line[512];
    die_line = 0;
    int header = 0;
    while (fgets(line, sizeof line, f)) {
        die_line++;
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *s = trim(line);
        if (!*s) continue;
        if (!header) { if (strcmp(s, "rw-axioms v1")) fatal("%s: header must be 'rw-axioms v1'", path); header = 1; continue; }
        /* axiom NAME domains=D [side=(...)] : LHS = RHS */
        if (strncmp(s, "axiom ", 6)) fatal("%s:%d: expected 'axiom'", path, die_line);
        if (g_nax >= RW_MAX_AXIOMS) fatal("too many axioms");
        Axiom *a = &g_ax[g_nax];
        memset(a, 0, sizeof *a);
        a->side = -1;
        char *colon = strstr(s, " : ");
        if (!colon) fatal("%s:%d: expected ' : '", path, die_line);
        *colon = 0;
        char *head = s + 6, *body = colon + 3;
        char *sp = strchr(head, ' ');
        if (!sp) fatal("%s:%d: axiom needs domains=", path, die_line);
        *sp = 0;
        snprintf(a->name, sizeof a->name, "%s", head);
        char *dom = strstr(sp + 1, "domains=");
        if (!dom) fatal("%s:%d: axiom needs domains=", path, die_line);
        char dbuf[64]; sscanf(dom + 8, "%63s", dbuf);
        a->domains = parse_domains(dbuf);
        char *side = strstr(sp + 1, "side=");
        if (side) a->side = parse_str(side + 5);
        char *eq = strstr(body, " = ");
        if (!eq) fatal("%s:%d: expected ' = '", path, die_line);
        *eq = 0;
        a->lhs = parse_str(body);
        a->rhs = parse_str(eq + 3);
        g_nax++;
    }
    fclose(f);
    if (!header) fatal("%s: empty axiom file", path);
}

static const Axiom *find_axiom(const char *name) {
    for (int i = 0; i < g_nax; ++i) if (!strcmp(g_ax[i].name, name)) return &g_ax[i];
    return NULL;
}

/* collect distinct pattern-variable / symbol ids under t */
static void collect_syms(int t, int kind, int *ids, int *n) {
    if (t < 0) return;
    const Node *x = &g_n[t];
    if (x->kind == kind) {
        for (int i = 0; i < *n; ++i) if (ids[i] == x->sym) return;
        ids[(*n)++] = x->sym;
        return;
    }
    collect_syms(x->a, kind, ids, n);
    collect_syms(x->b, kind, ids, n);
}

/* Every axiom is checked semantically before it may be cited: wrap at w=8 over every
 * assignment of up to three pattern variables, int over a window. An axiom with a
 * counterexample poisons the whole base (exit 3). Returns number of unsound axioms. */
static int check_axiom_base(int verbose) {
    int bad = 0;
    for (int i = 0; i < g_nax; ++i) {
        const Axiom *a = &g_ax[i];
        int ids[8], n = 0;
        collect_syms(a->lhs, K_PVAR, ids, &n);
        collect_syms(a->rhs, K_PVAR, ids, &n);
        if (a->side >= 0) collect_syms(a->side, K_PVAR, ids, &n);
        if (n > 3) fatal("axiom %s has more than three pattern variables", a->name);
        uint64_t fails = 0, rows = 0;
        for (unsigned aw = 4; (a->domains & (1u << D_WRAP)) && aw <= 8; aw += 4) {
            Env e; memset(&e, 0, sizeof e); e.d = D_WRAP; e.w = aw;
            int exh = aw == 4 || n <= 2;
            uint64_t total = exh ? 1ull << (aw * (unsigned)n) : 262144u;
            g_rng = 0xA810A5ull;
            for (uint64_t r = 0; r < total; ++r) {
                for (int k = 0; k < n; ++k) e.val[ids[k]] = exh ? (r >> (aw * (unsigned)k)) & wmask(aw) : rnd() & wmask(aw);
                if (a->side >= 0 && !cond_holds(D_WRAP, a->side, &e)) continue;
                rows++;
                if (!row_agrees(D_WRAP, a->lhs, a->rhs, &e)) fails++;
            }
        }
        if (a->domains & (1u << D_INT)) {
            Env e; memset(&e, 0, sizeof e); e.d = D_INT;
            uint64_t total = 1ull << (6 * n);
            for (uint64_t r = 0; r < total; ++r) {
                for (int k = 0; k < n; ++k) e.val[ids[k]] = (uint64_t)((int64_t)((r >> (6 * k)) & 63) - 32);
                if (a->side >= 0 && !cond_holds(D_INT, a->side, &e)) continue;
                rows++;
                if (!row_agrees(D_INT, a->lhs, a->rhs, &e)) fails++;
            }
        }
        if (a->domains & (1u << D_F32)) { fails++; }   /* no f32 axiom is accepted into the base */
        if (rows == 0) fails++;
        if (verbose || fails)
            printf("RWAXIOM %s %s rows=%llu bad_rows=%llu\n", a->name, fails ? "UNSOUND" : "ok",
                   (unsigned long long)rows, (unsigned long long)fails);
        if (fails) bad++;
    }
    return bad;
}

/* --------------------------------------------------------- matching -- */
typedef struct { int bound[RW_MAX_SYMS]; } Subst;
static void subst_init(Subst *s) { for (int i = 0; i < RW_MAX_SYMS; ++i) s->bound[i] = -1; }

/* match an axiom pattern against a term (pattern variables bind whole subterms) */
static int pmatch(int p, int t, Subst *s) {
    const Node *pn = &g_n[p];
    if (pn->kind == K_PVAR) {
        if (s->bound[pn->sym] < 0) { s->bound[pn->sym] = t; return 1; }
        return teq(s->bound[pn->sym], t);
    }
    const Node *tn = &g_n[t];
    if (pn->kind != tn->kind) return 0;
    switch (pn->kind) {
    case K_LIT: return pn->v == tn->v;
    case K_ONES: return 1;
    case K_SYM: return pn->sym == tn->sym;
    default: break;
    }
    if (pn->a >= 0 && !pmatch(pn->a, tn->a, s)) return 0;
    if (pn->b >= 0 && !pmatch(pn->b, tn->b, s)) return 0;
    return 1;
}

static int instantiate(int p, const Subst *s) {
    const Node n = g_n[p];
    if (n.kind == K_PVAR) return s->bound[n.sym];
    if (n.a < 0 && n.b < 0) return p;
    int a = n.a >= 0 ? instantiate(n.a, s) : -1;
    int b = n.b >= 0 ? instantiate(n.b, s) : -1;
    if (n.a >= 0 && a < 0) return -1;
    if (n.b >= 0 && b < 0) return -1;
    return mk(n.kind, a, b, n.v, n.sym);
}

typedef struct {
    Domain d;
    int assume[RW_MAX_ASSUME];
    int nassume;
} Ctx;

static int assumed(const Ctx *c, int cond) {
    for (int i = 0; i < c->nassume; ++i) if (teq(c->assume[i], cond)) return 1;
    return 0;
}

/* One rewrite at the root: prev -> next by axiom (either direction) or by an assumption
 * (eq A B) in the contract's assume list. */
static int step_at_root(int prev, int next, const char *by, int rev, const Ctx *c, char *why, size_t wl) {
    if (!strcmp(by, "assume")) {
        for (int i = 0; i < c->nassume; ++i) {
            const Node *q = &g_n[c->assume[i]];
            if (q->kind != K_EQ) continue;
            if ((teq(q->a, prev) && teq(q->b, next)) || (teq(q->b, prev) && teq(q->a, next))) return 1;
        }
        snprintf(why, wl, "no (eq ...) assumption links these terms");
        return 0;
    }
    const Axiom *a = find_axiom(by);
    if (!a) { snprintf(why, wl, "unknown axiom %s", by); return 0; }
    if (!(a->domains & (1u << c->d))) { snprintf(why, wl, "axiom %s is not valid in this domain", by); return 0; }
    Subst s; subst_init(&s);
    int from = rev ? a->rhs : a->lhs, to = rev ? a->lhs : a->rhs;
    if (!pmatch(from, prev, &s) || !pmatch(to, next, &s)) return 0;
    if (a->side >= 0) {
        int sc = instantiate(a->side, &s);
        if (sc < 0 || !assumed(c, sc)) { snprintf(why, wl, "side condition of %s is not in the assume list", by); return 0; }
    }
    return 1;
}

/* prev -> next differ by exactly one rewrite somewhere */
static int step_ok(int prev, int next, const char *by, int rev, const Ctx *c, char *why, size_t wl) {
    if (step_at_root(prev, next, by, rev, c, why, wl)) return 1;
    const Node *p = &g_n[prev], *n = &g_n[next];
    if (p->kind != n->kind || p->a < 0) return 0;
    if (p->kind == K_OPVAR && p->sym != n->sym) return 0;
    int da = !teq(p->a, n->a), db = p->b >= 0 && !teq(p->b, n->b);
    if (da && db) return 0;
    if (da) return step_ok(p->a, n->a, by, rev, c, why, wl);
    if (db) return step_ok(p->b, n->b, by, rev, c, why, wl);
    return 0;
}

/* ----------------------------------------------------------- contracts -- */
typedef struct {
    char path[256];
    char id[16];
    char semantics[48];
    Domain d;
    unsigned widths;                /* bitmask of 8/16/32/64 -> bits 0..3 */
    char params[RW_MAX_PARAMS][16];
    int nparams;
    int ops[8], nops;               /* expansion of OP; 0 if none */
    Ctx ctx;
    char assume_text[512];
    int lhs, rhs;
    int step_term[RW_MAX_STEPS];
    char step_by[RW_MAX_STEPS][32];
    int step_rev[RW_MAX_STEPS];
    int nsteps;
    int has_derive;
    char claim_text[4096];          /* every field except semantics and assume: the claim digest input */
    char verdict[24];
    char reason[160];
} Contract;

static Contract g_c[RW_MAX_CONTRACTS];
static int g_nc;
static const char *g_semver = RW_SEMANTICS_DEFAULT;

static int width_bit(unsigned w) { return w == 8 ? 1 : w == 16 ? 2 : w == 32 ? 4 : w == 64 ? 8 : 0; }

static void load_contract(const char *path, Contract *c) {
    FILE *f = fopen(path, "r");
    if (!f) fatal("cannot open %s", path);
    memset(c, 0, sizeof *c);
    snprintf(c->path, sizeof c->path, "%s", path);
    c->lhs = c->rhs = -1;
    char line[512];
    die_line = 0;
    int header = 0, in_derive = 0, ended = 0;
    while (fgets(line, sizeof line, f)) {
        die_line++;
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *s = trim(line);
        if (!*s) continue;
        if (!header) { if (strcmp(s, "rw-contract v1")) fatal("%s: header must be 'rw-contract v1'", path); header = 1; continue; }
        if (ended) fatal("%s:%d: text after end", path, die_line);
        if (strncmp(s, "semantics:", 10) && strncmp(s, "assume:", 7)) {
            strncat(c->claim_text, s, sizeof c->claim_text - strlen(c->claim_text) - 2);
            strncat(c->claim_text, "\n", sizeof c->claim_text - strlen(c->claim_text) - 1);
        }
        if (in_derive) {
            if (!strcmp(s, "end")) { ended = 1; continue; }
            /* TERM by NAME [rev] */
            char *by = strstr(s, " by ");
            if (!by) fatal("%s:%d: derivation step needs ' by '", path, die_line);
            *by = 0;
            if (c->nsteps >= RW_MAX_STEPS) fatal("%s: too many steps", path);
            c->step_term[c->nsteps] = parse_str(s);
            char nm[32] = "", rv[8] = "";
            sscanf(by + 4, "%31s %7s", nm, rv);
            snprintf(c->step_by[c->nsteps], sizeof c->step_by[0], "%s", nm);
            c->step_rev[c->nsteps] = !strcmp(rv, "rev");
            c->nsteps++;
            continue;
        }
        char *colon = strchr(s, ':');
        if (!colon) fatal("%s:%d: expected 'key: value'", path, die_line);
        *colon = 0;
        char *k = trim(s), *v = trim(colon + 1);
        if (!strcmp(k, "id")) snprintf(c->id, sizeof c->id, "%s", v);
        else if (!strcmp(k, "semantics")) snprintf(c->semantics, sizeof c->semantics, "%s", v);
        else if (!strcmp(k, "domain")) {
            if (!strcmp(v, "wrap")) c->d = D_WRAP; else if (!strcmp(v, "int")) c->d = D_INT;
            else if (!strcmp(v, "f32")) c->d = D_F32; else fatal("%s:%d: unknown domain", path, die_line);
        } else if (!strcmp(k, "widths")) {
            char *save; for (char *t = strtok_r(v, " ", &save); t; t = strtok_r(NULL, " ", &save)) c->widths |= (unsigned)width_bit((unsigned)atoi(t));
        } else if (!strcmp(k, "params")) {
            if (strcmp(v, "-")) { char *save; for (char *t = strtok_r(v, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
                if (c->nparams >= RW_MAX_PARAMS) fatal("%s: too many params", path);
                snprintf(c->params[c->nparams++], 16, "%s", t); sym_id(t); } }
        } else if (!strcmp(k, "ops")) {
            if (strcmp(v, "-")) { char *save; for (char *t = strtok_r(v, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
                int ok = op_kind(t); if (ok < 0 || c->nops >= 8) fatal("%s:%d: bad op %s", path, die_line, t); c->ops[c->nops++] = ok; } }
        } else if (!strcmp(k, "assume")) {
            if (strcmp(v, "-")) {
                if (c->ctx.nassume >= RW_MAX_ASSUME) fatal("%s: too many assumptions", path);
                c->ctx.assume[c->ctx.nassume++] = parse_str(v);
                strncat(c->assume_text, v, sizeof c->assume_text - strlen(c->assume_text) - 2);
                strncat(c->assume_text, ";", sizeof c->assume_text - strlen(c->assume_text) - 1);
            }
        } else if (!strcmp(k, "lhs")) c->lhs = parse_str(v);
        else if (!strcmp(k, "rhs")) c->rhs = parse_str(v);
        else if (!strcmp(k, "derive")) { in_derive = 1; c->has_derive = 1; }
        else if (!strcmp(k, "name") || !strcmp(k, "overflow") || !strcmp(k, "rounding") || !strcmp(k, "shape")
                 || !strcmp(k, "aliasing") || !strcmp(k, "effects") || !strcmp(k, "label")) { /* recorded in the claim text */ }
        else fatal("%s:%d: unknown key %s", path, die_line, k);
    }
    fclose(f);
    if (!header || !c->id[0] || c->lhs < 0 || c->rhs < 0 || !c->semantics[0] || !c->widths)
        fatal("%s: missing header, id, semantics, widths, lhs or rhs", path);
    if (c->has_derive && !ended) fatal("%s: derivation without end", path);
    c->ctx.d = c->d;
}

static int g_mutant_skip_step;     /* M7: the checker skips derivation step 2 */
/* ---- the derivation (syntactic) check ---- */
static int check_derivation_expanded(const Contract *c, int lhs, int rhs, const int *terms, char *why, size_t wl) {
    int prev = lhs;
    for (int i = 0; i < c->nsteps; ++i) {
        why[0] = 0;
        if (g_mutant_skip_step && i == 1) { prev = terms[i]; continue; }   /* M7: step 2 taken on trust */
        if (!step_ok(prev, terms[i], c->step_by[i], c->step_rev[i], &c->ctx, why, wl)) {
            char tmp[160];
            snprintf(tmp, sizeof tmp, "step %d (by %s%s) is not one valid rewrite%s%s", i + 1, c->step_by[i],
                     c->step_rev[i] ? " rev" : "", why[0] ? ": " : "", why);
            snprintf(why, wl, "%s", tmp);
            return 0;
        }
        prev = terms[i];
    }
    if (!teq(prev, rhs)) { snprintf(why, wl, "derivation does not end at rhs"); return 0; }
    return 1;
}

/* ---- the semantic guard ---- */

typedef struct { uint64_t rows, skipped, bad; } SemStat;

static void sem_wrap(const Contract *c, int lhs, int rhs, unsigned w, SemStat *st) {
    Env e; memset(&e, 0, sizeof e); e.d = D_WRAP; e.w = w;
    int xs = sym_id("x");
    int pid[RW_MAX_PARAMS];
    for (int k = 0; k < c->nparams; ++k) pid[k] = sym_id(c->params[k]);
    uint64_t m = wmask(w);
    uint64_t combos;
    int exhaustive = 0;
    if (w == 8 && c->nparams <= 2) { combos = 1ull << (8 * c->nparams); exhaustive = 1; }
    else combos = w == 8 ? RW_SAMPLE_W8_PARAMS : RW_SAMPLE_W16_PARAMS + 4;
    g_rng = 0x249249249ull ^ w;
    for (uint64_t pc = 0; pc < combos; ++pc) {
        for (int k = 0; k < c->nparams; ++k) {
            uint64_t v;
            if (exhaustive) v = (pc >> (8 * k)) & 0xFF;
            else if (pc < 4) { static const uint64_t edge[4] = {0, 1, ~0ull, 0x8000}; v = edge[pc] & m; if (pc == 3 && w == 8) v = 0x80; }
            else v = rnd() & m;
            e.val[pid[k]] = v;
        }
        for (uint64_t x = 0; x <= m && x < (1ull << 16); ++x) {
            e.val[xs] = x;
            int ok = 1;
            for (int a = 0; a < c->ctx.nassume; ++a) if (!cond_holds(D_WRAP, c->ctx.assume[a], &e)) { ok = 0; break; }
            if (!ok) { st->skipped++; continue; }
            st->rows++;
            if (!row_agrees(D_WRAP, lhs, rhs, &e)) st->bad++;
        }
    }
}

static void sem_int(const Contract *c, int lhs, int rhs, SemStat *st) {
    Env e; memset(&e, 0, sizeof e); e.d = D_INT;
    int xs = sym_id("x");
    int pid[RW_MAX_PARAMS];
    for (int k = 0; k < c->nparams; ++k) pid[k] = sym_id(c->params[k]);
    uint64_t combos = 1ull << (5 * c->nparams);   /* params in [-16, 15] */
    for (uint64_t pc = 0; pc < combos; ++pc) {
        for (int k = 0; k < c->nparams; ++k) e.val[pid[k]] = (uint64_t)((int64_t)((pc >> (5 * k)) & 31) - 16);
        for (int64_t x = -128; x <= 127; ++x) {
            e.val[xs] = (uint64_t)x;
            int ok = 1;
            for (int a = 0; a < c->ctx.nassume; ++a) if (!cond_holds(D_INT, c->ctx.assume[a], &e)) { ok = 0; break; }
            if (!ok) { st->skipped++; continue; }
            st->rows++;
            if (!row_agrees(D_INT, lhs, rhs, &e)) st->bad++;
        }
    }
}

static void sem_f32(const Contract *c, int lhs, int rhs, SemStat *st) {
    static const float special[] = {0.0f, -0.0f, 1.0f, -1.0f, 3.0f, 0.1f, 1e8f, -1e8f, 1e-38f, 16777216.0f, 0.5f, 2.0f};
    const int ns = (int)(sizeof special / sizeof special[0]);
    Env e; memset(&e, 0, sizeof e); e.d = D_F32;
    int ids[RW_MAX_PARAMS + 1], n = 0;
    ids[n++] = sym_id("x");
    for (int k = 0; k < c->nparams; ++k) ids[n++] = sym_id(c->params[k]);
    g_rng = 0xF32F32ull;
    uint64_t total = 1;
    for (int k = 0; k < n; ++k) total *= (uint64_t)ns;
    for (uint64_t r = 0; r < total + 65536; ++r) {
        uint64_t q = r;
        for (int k = 0; k < n; ++k) {
            float v;
            if (r < total) { v = special[q % (uint64_t)ns]; q /= (uint64_t)ns; }
            else { do { v = bits_f(rnd() & 0xFFFFFFFFu); } while (!isfinite(v)); }
            e.val[ids[k]] = f_bits(v);
        }
        st->rows++;
        if (!row_agrees(D_F32, lhs, rhs, &e)) st->bad++;
    }
}

/* ---- the derivation cache ---- */
static const char *g_cache_path;
static int g_mutant_cache_nosem;   /* M3: the key forgets the semantics version */

static void cache_key(const Contract *c, char hex[65]) {
    uint8_t claim[32], key[32];
    sha256_hash((const uint8_t *)c->claim_text, strlen(c->claim_text), claim);
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "rw.cache.v1|%s|%s|%s|", c->id, c->assume_text,
                     g_mutant_cache_nosem ? "" : g_semver);
    if (n < 0 || (size_t)n + 32 > sizeof buf) fatal("cache key overflow");
    memcpy(buf + n, claim, 32);
    sha256_hash((const uint8_t *)buf, (size_t)n + 32, key);
    for (int i = 0; i < 32; ++i) sprintf(hex + 2 * i, "%02x", key[i]);
}

static int cache_lookup(const char *key, char *verdict, size_t vl) {
    if (!g_cache_path) return 0;
    FILE *f = fopen(g_cache_path, "r");
    if (!f) return 0;
    char k[80], v[32];
    int hit = 0;
    while (fscanf(f, "%79s %31s", k, v) == 2)
        if (!strcmp(k, key)) { snprintf(verdict, vl, "%s", v); hit = 1; }
    fclose(f);
    return hit;
}
static void cache_store(const char *key, const char *verdict) {
    if (!g_cache_path) return;
    FILE *f = fopen(g_cache_path, "a");
    if (!f) fatal("cannot write cache %s", g_cache_path);
    fprintf(f, "%s %s\n", key, verdict);
    fclose(f);
}

static int g_mutant_no_semantic;   /* M6: the semantic guard is skipped */

/* Judge one contract. Fills c->verdict / c->reason. */
static void judge_contract(Contract *c, int verbose) {
    char key[65];
    cache_key(c, key);
    char cached[24];
    if (strcmp(c->semantics, "omega.v0.wrap") && strcmp(c->semantics, "checker.int") && strcmp(c->semantics, "checker.f32")) {
        snprintf(c->verdict, sizeof c->verdict, "rejected");
        snprintf(c->reason, sizeof c->reason, "unknown semantics family %s", c->semantics);
        return;
    }
    if (cache_lookup(key, cached, sizeof cached)) {
        snprintf(c->verdict, sizeof c->verdict, "%s", cached);
        snprintf(c->reason, sizeof c->reason, "cache=hit");
        return;
    }
    int nexp = c->nops ? c->nops : 1;
    SemStat st = {0, 0, 0};
    int deriv_ok = c->has_derive;
    char why[160] = "";
    for (int x = 0; x < nexp; ++x) {
        int opk = c->nops ? c->ops[x] : 0;
        int lhs = c->nops ? subst_op(c->lhs, opk) : c->lhs;
        int rhs = c->nops ? subst_op(c->rhs, opk) : c->rhs;
        if (c->has_derive && deriv_ok) {
            int terms[RW_MAX_STEPS];
            for (int i = 0; i < c->nsteps; ++i) terms[i] = c->nops ? subst_op(c->step_term[i], opk) : c->step_term[i];
            if (!check_derivation_expanded(c, lhs, rhs, terms, why, sizeof why)) deriv_ok = 0;
        }
        if (!g_mutant_no_semantic) {
            if (c->d == D_WRAP) {
                if (c->widths & 1) sem_wrap(c, lhs, rhs, 8, &st);
                sem_wrap(c, lhs, rhs, 16, &st);
            } else if (c->d == D_INT) sem_int(c, lhs, rhs, &st);
            else sem_f32(c, lhs, rhs, &st);
        }
    }
    if (!g_mutant_no_semantic && st.bad) {
        snprintf(c->verdict, sizeof c->verdict, "rejected");
        snprintf(c->reason, sizeof c->reason, "counterexample: bad_rows=%llu of %llu; derivation %s%s",
                 (unsigned long long)st.bad, (unsigned long long)st.rows,
                 !c->has_derive ? "absent" : deriv_ok ? "steps-valid" : "also-invalid: ", deriv_ok ? "" : why);
    } else if (!g_mutant_no_semantic && st.rows == 0) {
        snprintf(c->verdict, sizeof c->verdict, "rejected");
        snprintf(c->reason, sizeof c->reason, "vacuous: the assumptions exclude every row");
    } else if (c->has_derive && !deriv_ok) {
        snprintf(c->verdict, sizeof c->verdict, "rejected");
        snprintf(c->reason, sizeof c->reason, "%s", why);
    } else if (!c->has_derive) {
        snprintf(c->verdict, sizeof c->verdict, "probe_agreement");
        snprintf(c->reason, sizeof c->reason, "no derivation; no counterexample in %llu rows", (unsigned long long)st.rows);
    } else {
        snprintf(c->verdict, sizeof c->verdict, "proven_in_scope");
        snprintf(c->reason, sizeof c->reason, "derivation %d steps; guard rows=%llu skipped=%llu", c->nsteps,
                 (unsigned long long)st.rows, (unsigned long long)st.skipped);
    }
    (void)verbose;
    cache_store(key, c->verdict);
    char *p = c->reason + strlen(c->reason);
    snprintf(p, sizeof c->reason - strlen(c->reason), " cache=miss");
}

/* ================================================ instance derivations == */
/* Chains: "add 3; mul 5" innermost first, "id" for the empty chain. The input atom is "in". */
typedef struct { int n; int op[16]; uint64_t c[16]; } Chain;

static int parse_chain(const char *s, Chain *ch) {
    memset(ch, 0, sizeof *ch);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", s);
    char *t = trim(buf);
    if (!strcmp(t, "id")) return 0;
    char *save;
    for (char *part = strtok_r(t, ";", &save); part; part = strtok_r(NULL, ";", &save)) {
        char o[16]; unsigned long long v;
        { char vs[40]; char *e; if (sscanf(part, " %15s %39s", o, vs) != 2) return -1; errno = 0; v = strtoull(vs, &e, 0); if (errno || *e) return -1; }
        int k = op_kind(o);
        if (k != K_ADD && k != K_SUB && k != K_MUL && k != K_AND && k != K_OR) return -1;
        if (ch->n >= 16) return -1;
        ch->op[ch->n] = k; ch->c[ch->n] = v; ch->n++;
    }
    return 0;
}

static int chain_term(const Chain *ch) {
    int t = mk(K_SYM, -1, -1, 0, sym_id("in"));
    for (int i = 0; i < ch->n; ++i) t = mk(ch->op[i], t, mk(K_LIT, -1, -1, ch->c[i], 0), 0, 0);
    return t;
}

/* Is t a V0 chain over "in" with literal constants? Fills ch. */
__attribute__((unused)) static int term_chain(int t, Chain *ch) {
    int stack[32], n = 0;
    while (g_n[t].kind != K_SYM) {
        const Node *x = &g_n[t];
        if (!(x->kind == K_ADD || x->kind == K_SUB || x->kind == K_MUL || x->kind == K_AND || x->kind == K_OR)) return -1;
        if (g_n[x->b].kind != K_LIT || n >= 16) return -1;
        stack[n++] = t;
        t = x->a;
    }
    if (strcmp(g_sym[g_n[t].sym], "in")) return -1;
    ch->n = n;
    for (int i = 0; i < n; ++i) { const Node *x = &g_n[stack[n - 1 - i]]; ch->op[i] = x->kind; ch->c[i] = g_n[x->b].v; }
    return 0;
}

/* Contract pattern -> instance term. x binds a subterm, params bind literals, OP binds an op. */
typedef struct { int x; int have_x; uint64_t pv[RW_MAX_SYMS]; int pset[RW_MAX_SYMS]; int op; } Bind;

static int is_param(const Contract *c, int sym) {
    for (int k = 0; k < c->nparams; ++k) if (!strcmp(g_sym[sym], c->params[k])) return 1;
    return 0;
}

static int imatch(const Contract *c, int p, int t, unsigned w, Bind *b) {
    const Node *pn = &g_n[p], *tn = &g_n[t];
    if (pn->kind == K_SYM) {
        if (!strcmp(g_sym[pn->sym], "x")) {
            if (!b->have_x) { b->have_x = 1; b->x = t; return 1; }
            return teq(b->x, t);
        }
        if (is_param(c, pn->sym)) {
            if (tn->kind != K_LIT) return 0;
            if (!b->pset[pn->sym]) { b->pset[pn->sym] = 1; b->pv[pn->sym] = tn->v; return 1; }
            return b->pv[pn->sym] == tn->v;
        }
        return 0;
    }
    if (pn->kind == K_LIT) return tn->kind == K_LIT && tn->v == pn->v;
    if (pn->kind == K_ONES) return tn->kind == K_LIT && tn->v == wmask(w);
    int pk = pn->kind;
    if (pk == K_OPVAR) {
        int ok = 0;
        for (int i = 0; i < c->nops; ++i) if (c->ops[i] == tn->kind) ok = 1;
        if (!ok) return 0;
        if (b->op && b->op != tn->kind) return 0;
        b->op = tn->kind;
    } else if (pk != tn->kind) return 0;
    if (pn->a >= 0 && !imatch(c, pn->a, tn->a, w, b)) return 0;
    if (pn->b >= 0 && !imatch(c, pn->b, tn->b, w, b)) return 0;
    return 1;
}

static int has_x(int t) {
    if (t < 0) return 0;
    const Node *n = &g_n[t];
    if (n->kind == K_SYM && !strcmp(g_sym[n->sym], "x")) return 1;
    return has_x(n->a) || has_x(n->b);
}

/* Build the instance of a contract pattern: ground constant subterms are evaluated at width w. */
static int iinst(const Contract *c, int p, unsigned w, const Bind *b, int *err) {
    const Node n = g_n[p];
    if (!has_x(p)) {
        Env e; memset(&e, 0, sizeof e); e.d = D_WRAP; e.w = w;
        for (int s = 0; s < g_nsym; ++s) if (b->pset[s]) e.val[s] = b->pv[s];
        int q = p;
        if (n.kind == K_OPVAR) q = subst_op(p, b->op);
        else if (n.a >= 0 || n.b >= 0) q = subst_op(p, b->op ? b->op : K_ADD);
        uint64_t v;
        if (ev_wrap(q, &e, &v)) { *err = 1; return -1; }
        return mk(K_LIT, -1, -1, v, 0);
    }
    if (n.kind == K_SYM) return b->x;
    int k = n.kind == K_OPVAR ? b->op : n.kind;
    int a = n.a >= 0 ? iinst(c, n.a, w, b, err) : -1;
    int bb = n.b >= 0 ? iinst(c, n.b, w, b, err) : -1;
    return mk(k, a, bb, 0, 0);
}

/* Apply contract c to `t` at the first position where it matches with the stated result.
 * Returns 1 if some position yields exactly `next`. */
static int inst_step(const Contract *c, int t, int next, unsigned w, char *why, size_t wl) {
    Bind b; memset(&b, 0, sizeof b);
    if (imatch(c, c->lhs, t, w, &b)) {
        int ok = 1;
        Env e; memset(&e, 0, sizeof e); e.d = D_WRAP; e.w = w;
        for (int s = 0; s < g_nsym; ++s) if (b.pset[s]) e.val[s] = b.pv[s];
        for (int a = 0; a < c->ctx.nassume; ++a) {
            if (has_x(c->ctx.assume[a])) { ok = 0; snprintf(why, wl, "assumption on x cannot be discharged by constants"); break; }
            int q = subst_op(c->ctx.assume[a], b.op ? b.op : K_ADD);
            if (!cond_holds(D_WRAP, q, &e)) { ok = 0; snprintf(why, wl, "assumption of %s does not hold here", c->id); break; }
        }
        if (ok) {
            int err = 0;
            int r = iinst(c, c->rhs, w, &b, &err);
            if (!err && teq(r, next)) return 1;
        }
    }
    const Node *tn = &g_n[t], *nn = &g_n[next];
    if (tn->kind != nn->kind || tn->a < 0 || tn->kind == K_SYM || tn->kind == K_LIT) return 0;
    if (!teq(tn->b, nn->b)) return 0;
    return inst_step(c, tn->a, nn->a, w, why, wl);
}

/* omega's own evaluator on a chain (cross-check of this file's evaluator) */
static int omega_eval_chain(const Chain *ch, unsigned w, const uint64_t *xs, size_t n, uint64_t *ys) {
    OmegaProgram *p = calloc(1, sizeof *p);
    if (!p) return -1;
    omega_program_init(p, "rw249");
    p->contract.input_type = TYPE_UNSIGNED_INT;
    p->contract.output_type = TYPE_UNSIGNED_INT;
    p->contract.input_width = (uint16_t)w;
    p->contract.output_width = (uint16_t)w;
    snprintf(p->contract.precondition, sizeof p->contract.precondition, "x >= 0");
    omega_build_constraint_id(CONST_PRECONDITION, p->contract.precondition, &p->contract.precondition_id);
    snprintf(p->contract.postcondition, sizeof p->contract.postcondition, "rw249 chain");
    omega_build_constraint_id(CONST_POSTCONDITION, p->contract.postcondition, &p->contract.postcondition_id);
    p->body.has_body = true;
    p->body.step_count = (uint16_t)ch->n;
    static const uint8_t opc[] = {[K_ADD] = OP_ADD, [K_SUB] = OP_SUB, [K_MUL] = OP_MUL, [K_AND] = OP_AND, [K_OR] = OP_OR};
    for (int i = 0; i < ch->n; ++i) { p->body.steps[i].op = opc[ch->op[i]]; p->body.steps[i].imm = ch->c[i]; }
    int rc = omega_program_compute_id(p);
    if (rc == 0) rc = omega_program_eval(p, xs, n, ys);
    omega_program_destroy(p);
    free(p);
    return rc;
}

static uint64_t chain_eval(const Chain *ch, unsigned w, uint64_t x) {
    uint64_t m = wmask(w);
    x &= m;
    for (int i = 0; i < ch->n; ++i) {
        uint64_t c = ch->c[i];
        switch (ch->op[i]) {
        case K_ADD: x = (x + c) & m; break;
        case K_SUB: x = (x - c) & m; break;
        case K_MUL: x = (x * c) & m; break;
        case K_AND: x &= c; break;
        case K_OR: x |= c; break;
        }
    }
    return x;
}

static int g_mutant_trust_unknown_contract;   /* M8 helper: accept steps citing non-proven contracts */

static Contract *contract_by_id(const char *id) {
    for (int i = 0; i < g_nc; ++i) if (!strcmp(g_c[i].id, id)) return &g_c[i];
    return NULL;
}

/* Check one derivation record. Returns 1 accepted. */
static int check_instance(const char *eid, unsigned w, const Chain *start, Chain *steps, char ids[][16], int nsteps,
                          char *why, size_t wl, uint64_t *bad_rows) {
    *bad_rows = 0;
    if (!width_bit(w)) { snprintf(why, wl, "width %u not in V0", w); return 0; }
    for (int i = 0; i < start->n; ++i) if (start->c[i] & ~wmask(w)) { snprintf(why, wl, "start constant does not fit"); return 0; }
    int prev = chain_term(start);
    for (int i = 0; i < nsteps; ++i) {
        Contract *c = contract_by_id(ids[i]);
        if (!c) { snprintf(why, wl, "step %d cites unknown contract %s", i + 1, ids[i]); return 0; }
        if (strcmp(c->verdict, "proven_in_scope") && !g_mutant_trust_unknown_contract) {
            snprintf(why, wl, "step %d cites %s which is %s", i + 1, c->id, c->verdict); return 0; }
        if (c->d != D_WRAP || !(c->widths & (unsigned)width_bit(w))) {
            snprintf(why, wl, "step %d: %s does not cover wrap u%u", i + 1, c->id, w); return 0; }
        for (int k = 0; k < steps[i].n; ++k) if (steps[i].c[k] & ~wmask(w)) { snprintf(why, wl, "step %d constant does not fit", i + 1); return 0; }
        int next = chain_term(&steps[i]);
        char w2[160] = "";
        if (!inst_step(c, prev, next, w, w2, sizeof w2)) {
            snprintf(why, wl, "step %d is not an instance of %s%s%s", i + 1, c->id, w2[0] ? ": " : "", w2); return 0; }
        prev = next;
    }
    /* direct comparison of start and end: this file's evaluator and omega's */
    const Chain *end = nsteps ? &steps[nsteps - 1] : start;
    uint64_t m = wmask(w);
    size_t n = w <= 16 ? (size_t)m + 1 : RW_SAMPLE_WIDE_X + 6;
    uint64_t *xs = malloc(n * sizeof *xs), *ya = malloc(n * sizeof *ya), *yb = malloc(n * sizeof *yb);
    if (!xs || !ya || !yb) fatal("out of memory");
    g_rng = 0x5EED0249ull;
    static const uint64_t probes[6] = {0, 1, 3, 7, 13, 42};
    for (size_t i = 0; i < n; ++i) {
        if (w <= 16) xs[i] = i;
        else if (i < 6) xs[i] = probes[i];
        else if (i < 12) { static const uint64_t edge[6] = {~0ull, 0x80000000ull, 0x7FFFFFFFull, 0x8000000000000000ull, 2, 0xFFFF}; xs[i] = edge[i - 6] & m; }
        else xs[i] = rnd() & m;
    }
    int ok = 1;
    if (omega_eval_chain(start, w, xs, n, ya) || omega_eval_chain(end, w, xs, n, yb)) { snprintf(why, wl, "omega_program_eval refused"); ok = 0; }
    for (size_t i = 0; ok && i < n; ++i) {
        uint64_t a = chain_eval(start, w, xs[i]), b = chain_eval(end, w, xs[i]);
        if (a != ya[i] || b != yb[i]) { snprintf(why, wl, "checker evaluator disagrees with omega_program_eval"); ok = 0; break; }
        if (a != b) (*bad_rows)++;
    }
    if (ok && *bad_rows) { snprintf(why, wl, "start and end differ on %llu inputs", (unsigned long long)*bad_rows); ok = 0; }
    free(xs); free(ya); free(yb);
    (void)eid;
    return ok;
}

static void load_contract_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) fatal("cannot open contracts dir %s", dir);
    struct dirent *ent;
    char names[RW_MAX_CONTRACTS][128];
    int n = 0;
    while ((ent = readdir(d))) {
        size_t l = strlen(ent->d_name);
        if (l > 3 && !strcmp(ent->d_name + l - 3, ".rw") && ent->d_name[0] == 'R') {
            if (n >= RW_MAX_CONTRACTS) fatal("too many contracts");
            snprintf(names[n++], 128, "%s", ent->d_name);
        }
    }
    closedir(d);
    qsort(names, (size_t)n, 128, (int (*)(const void *, const void *))strcmp);
    for (int i = 0; i < n; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        load_contract(path, &g_c[g_nc++]);
    }
}

static double now_ns(clockid_t c) { struct timespec t; clock_gettime(c, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }

static int run_derivations(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) fatal("cannot open %s", path);
    char line[1024];
    int header = 0, accepted = 0, rejected = 0;
    char eid[64] = "";
    unsigned w = 0;
    Chain start, steps[RW_MAX_STEPS];
    char ids[RW_MAX_STEPS][16];
    int nsteps = 0, have_start = 0;
    uint64_t bad_total = 0;
    die_line = 0;
    while (fgets(line, sizeof line, f)) {
        die_line++;
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *s = trim(line);
        if (!*s) continue;
        if (!header) { if (strcmp(s, "rw-derivation v1")) fatal("%s: header must be 'rw-derivation v1'", path); header = 1; continue; }
        if (!strncmp(s, "semantics:", 10)) {
            if (strcmp(trim(s + 10), g_semver)) fatal("%s: derivations written for %s, checker is %s", path, trim(s + 10), g_semver);
            continue;
        }
        if (!strncmp(s, "expr:", 5)) {
            /* expr: ID width=W start=CHAIN */
            char *wp = strstr(s, "width="), *sp = strstr(s, "start=");
            if (!wp || !sp) fatal("%s:%d: expr needs width= and start=", path, die_line);
            sscanf(s + 5, " %63s", eid);
            w = (unsigned)atoi(wp + 6);
            if (parse_chain(sp + 6, &start)) fatal("%s:%d: bad start chain", path, die_line);
            nsteps = 0; have_start = 1;
            continue;
        }
        if (!strncmp(s, "step:", 5)) {
            /* step: CONTRACT => CHAIN */
            char *arrow = strstr(s, "=>");
            if (!arrow || !have_start || nsteps >= RW_MAX_STEPS) fatal("%s:%d: bad step", path, die_line);
            sscanf(s + 5, " %15s", ids[nsteps]);
            if (parse_chain(arrow + 2, &steps[nsteps])) fatal("%s:%d: bad step chain", path, die_line);
            nsteps++;
            continue;
        }
        if (!strcmp(s, "end")) {
            if (!have_start) fatal("%s:%d: end without expr", path, die_line);
            int mark = g_nn;
            char why[200] = "";
            uint64_t bad = 0;
            int ok = check_instance(eid, w, &start, steps, ids, nsteps, why, sizeof why, &bad);
            g_nn = mark;
            bad_total += bad;
            printf("RWDERIV %s %s steps=%d bad_rows=%llu%s%s\n", eid, ok ? "accepted" : "rejected", nsteps,
                   (unsigned long long)bad, ok ? "" : " reason=", ok ? "" : why);
            if (ok) accepted++; else rejected++;
            have_start = 0;
            continue;
        }
        fatal("%s:%d: unknown record", path, die_line);
    }
    fclose(f);
    printf("RWDERIV-SUMMARY accepted=%d rejected=%d bad_rows=%llu\n", accepted, rejected, (unsigned long long)bad_total);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: rwcheck axioms|contracts|derivations --axioms A [--contracts DIR] [--cache F] [--semantics-version V] [files]\n");
        return 2;
    }
    const char *mode = argv[1], *axioms = NULL, *cdir = NULL;
    const char *files[RW_MAX_CONTRACTS];
    int nfiles = 0, verbose = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--axioms") && i + 1 < argc) axioms = argv[++i];
        else if (!strcmp(argv[i], "--contracts") && i + 1 < argc) cdir = argv[++i];
        else if (!strcmp(argv[i], "--cache") && i + 1 < argc) g_cache_path = argv[++i];
        else if (!strcmp(argv[i], "--semantics-version") && i + 1 < argc) g_semver = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (argv[i][0] == '-') fatal("unknown flag %s", argv[i]);
        else if (nfiles < RW_MAX_CONTRACTS) files[nfiles++] = argv[i];
    }
    const char *m = getenv("RW_MUTANT");
    if (m) {
        if (!strcmp(m, "cache_nosem")) g_mutant_cache_nosem = 1;
        else if (!strcmp(m, "no_semantic")) g_mutant_no_semantic = 1;
        else if (!strcmp(m, "skip_step")) g_mutant_skip_step = 1;
        else if (!strcmp(m, "trust_contract")) g_mutant_trust_unknown_contract = 1;
        else fatal("unknown RW_MUTANT %s", m);
        fprintf(stderr, "rwcheck: MUTANT %s active\n", m);
    }
    if (!axioms) fatal("--axioms is required");
    double c0 = now_ns(CLOCK_PROCESS_CPUTIME_ID);
    load_axioms(axioms);
    int bad = check_axiom_base(!strcmp(mode, "axioms") || verbose);
    if (bad) { printf("RWAXIOM-BASE REFUSED unsound=%d\n", bad); return 3; }
    if (!strcmp(mode, "axioms")) { printf("RWAXIOM-BASE ok axioms=%d\n", g_nax); return 0; }
    if (!strcmp(mode, "contracts")) {
        int counts[3] = {0, 0, 0};
        for (int i = 0; i < nfiles; ++i) {
            Contract *c = &g_c[g_nc++];
            load_contract(files[i], c);
            judge_contract(c, verbose);
            printf("RWCHECK %s %s %s\n", c->id, c->verdict, c->reason);
            counts[!strcmp(c->verdict, "proven_in_scope") ? 0 : !strcmp(c->verdict, "probe_agreement") ? 1 : 2]++;
        }
        printf("RWCHECK-SUMMARY proven_in_scope=%d probe_agreement=%d rejected=%d semantics=%s cpu_ns=%.0f\n",
               counts[0], counts[1], counts[2], g_semver, now_ns(CLOCK_PROCESS_CPUTIME_ID) - c0);
        return 0;
    }
    if (!strcmp(mode, "derivations")) {
        if (!cdir || nfiles != 1) fatal("derivations needs --contracts DIR and one .rwd file");
        load_contract_dir(cdir);
        for (int i = 0; i < g_nc; ++i) {
            judge_contract(&g_c[i], 0);
            if (strcmp(g_c[i].verdict, "proven_in_scope")) printf("RWCHECK %s %s %s\n", g_c[i].id, g_c[i].verdict, g_c[i].reason);
        }
        run_derivations(files[0]);
        { struct rusage u; getrusage(RUSAGE_SELF, &u);
          printf("RWCHECK-CPU cpu_ns=%.0f peak_rss_bytes=%ld\n", now_ns(CLOCK_PROCESS_CPUTIME_ID) - c0, u.ru_maxrss * 1024L); }
        return 0;
    }
    fatal("unknown mode %s", mode);
    return 2;
}
