/*
 * omega_emit.c -- constrained structured emission of OSC binary IR (aien-spark/omega#445, stage 2 of
 * the one-layer plan). A model emits an op list (JSON, schema in tools/omega_emit/SCHEMA.md); this
 * tool checks every step against the OSC IR rules as it reads it, refuses the first invalid step with
 * a reason, and writes the canonical OSC binary IR (osc_ir_encode, src/compiler/osc_ir.h) only after
 * the whole unit passes osc_ir_validate and the bytes round-trip through the strict reader
 * (osc_ir_decode -> osc_ir_encode gives the same bytes).
 *
 *   omega_emit emit <oplist.json> <out.oscir>   exit 0 written; 1 refused (one line on stdout:
 *                                               "REFUSED <stage>: <reason>"); 2 usage / I/O
 *   omega_emit lift <in.oscir>                  canonical IR -> op list JSON on stdout, structs, contract
 *                                               text and arenas included (exit 1 if the
 *                                               unit uses something the schema does not cover)
 *
 * Stages in a refusal: json (not strict JSON), schema (shape: keys, kinds, names), step (one op breaks
 * an IR rule; the message names function, block and op), validator (osc_ir_validate on the whole unit,
 * e.g. a register used before it is defined), encoding (encode/decode round trip; never expected).
 * Nothing is repaired or guessed: the first defect wins.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osc_ir.h"

#define MAX_IN (1u << 20)
#define MAX_DEPTH 64

/* ============================================================================== refusal */

static char g_why[512];
static int fail(const char *stage, const char *fmt, ...)
{
    char m[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    snprintf(g_why, sizeof g_why, "%s: %s", stage, m);
    return -1;
}

/* ================================================================================= JSON */

enum { J_OBJ = 1, J_ARR, J_STR, J_NUM, J_TRUE, J_FALSE, J_NULL };

typedef struct J {
    int t;
    size_t off;            /* byte offset in the input (for messages) */
    char *s;               /* J_STR: decoded text (ASCII); J_NUM: raw text */
    size_t n;              /* children */
    struct J **kid;
    char **key;            /* J_OBJ keys */
} J;

typedef struct { const char *p; size_t n, i; int depth; } P;

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "omega_emit: out of memory\n"); exit(2); }
    return p;
}

static void ws(P *p) { while (p->i < p->n && (p->p[p->i] == ' ' || p->p[p->i] == '\t' || p->p[p->i] == '\n' || p->p[p->i] == '\r')) p->i++; }

static J *jnew(int t, size_t off) { J *j = xmalloc(sizeof *j); j->t = t; j->off = off; return j; }

static void jadd(J *j, J *k, char *key)
{
    if ((j->n & (j->n - 1)) == 0) { /* grow at powers of two */
        size_t cap = j->n ? j->n * 2 : 1;
        J **nk = realloc(j->kid, cap * sizeof *nk);
        char **ky = realloc(j->key, cap * sizeof *ky);
        if (!nk || !ky) { fprintf(stderr, "omega_emit: out of memory\n"); exit(2); }
        j->kid = nk; j->key = ky;
    }
    j->kid[j->n] = k; j->key[j->n] = key; j->n++;
}

static J *jvalue(P *p);

static char *jstring(P *p)
{
    /* p->p[p->i] == '"' */
    size_t start = ++p->i, cap = 16, n = 0;
    char *o = xmalloc(cap);
    (void)start;
    for (;;) {
        if (p->i >= p->n) { fail("json", "unterminated string at byte %zu", p->i); free(o); return NULL; }
        unsigned char c = (unsigned char)p->p[p->i++];
        if (c == '"') break;
        if (c < 0x20 || c > 0x7e) { fail("json", "control or non-ASCII byte in a string at byte %zu", p->i - 1); free(o); return NULL; }
        if (c == '\\') {
            if (p->i >= p->n) { fail("json", "unterminated escape at byte %zu", p->i); free(o); return NULL; }
            char e = p->p[p->i++];
            if (e == '"' || e == '\\' || e == '/') c = (unsigned char)e;
            else if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else if (e == 'u') {
                unsigned v = 0;
                for (int k = 0; k < 4; k++) {
                    char h = p->i < p->n ? p->p[p->i++] : 0;
                    int d = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (d < 0) { fail("json", "bad \\u escape at byte %zu", p->i - 1); free(o); return NULL; }
                    v = v * 16 + (unsigned)d;
                }
                if (v < 0x20 || v > 0x7e) { fail("json", "\\u escape outside printable ASCII at byte %zu", p->i - 6); free(o); return NULL; }
                c = (unsigned char)v;
            } else { fail("json", "bad escape \\%c at byte %zu", e, p->i - 1); free(o); return NULL; }
        }
        if (n + 1 >= cap) { cap *= 2; char *no = realloc(o, cap); if (!no) { free(o); return NULL; } o = no; }
        o[n++] = (char)c;
    }
    o[n] = 0;
    return o;
}

static J *jvalue(P *p)
{
    ws(p);
    if (p->i >= p->n) { fail("json", "unexpected end of input"); return NULL; }
    if (++p->depth > MAX_DEPTH) { fail("json", "nesting deeper than %d at byte %zu", MAX_DEPTH, p->i); return NULL; }
    char c = p->p[p->i];
    J *j = NULL;
    if (c == '{') {
        j = jnew(J_OBJ, p->i++);
        ws(p);
        if (p->i < p->n && p->p[p->i] == '}') { p->i++; goto done; }
        for (;;) {
            ws(p);
            if (p->i >= p->n || p->p[p->i] != '"') { fail("json", "expected a key string at byte %zu", p->i); return NULL; }
            size_t koff = p->i;
            char *k = jstring(p);
            if (!k) return NULL;
            for (size_t q = 0; q < j->n; q++)
                if (!strcmp(j->key[q], k)) { fail("json", "duplicate key \"%s\" at byte %zu", k, koff); return NULL; }
            ws(p);
            if (p->i >= p->n || p->p[p->i] != ':') { fail("json", "expected ':' at byte %zu", p->i); return NULL; }
            p->i++;
            J *v = jvalue(p);
            if (!v) return NULL;
            jadd(j, v, k);
            ws(p);
            if (p->i < p->n && p->p[p->i] == ',') { p->i++; continue; }
            if (p->i < p->n && p->p[p->i] == '}') { p->i++; break; }
            fail("json", "expected ',' or '}' at byte %zu", p->i);
            return NULL;
        }
    } else if (c == '[') {
        j = jnew(J_ARR, p->i++);
        ws(p);
        if (p->i < p->n && p->p[p->i] == ']') { p->i++; goto done; }
        for (;;) {
            J *v = jvalue(p);
            if (!v) return NULL;
            jadd(j, v, NULL);
            ws(p);
            if (p->i < p->n && p->p[p->i] == ',') { p->i++; continue; }
            if (p->i < p->n && p->p[p->i] == ']') { p->i++; break; }
            fail("json", "expected ',' or ']' at byte %zu", p->i);
            return NULL;
        }
    } else if (c == '"') {
        j = jnew(J_STR, p->i);
        if (!(j->s = jstring(p))) return NULL;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        j = jnew(J_NUM, p->i);
        size_t s = p->i;
        if (p->p[p->i] == '-') p->i++;
        if (p->i >= p->n || p->p[p->i] < '0' || p->p[p->i] > '9') { fail("json", "bad number at byte %zu", s); return NULL; }
        if (p->p[p->i] == '0' && p->i + 1 < p->n && p->p[p->i + 1] >= '0' && p->p[p->i + 1] <= '9') { fail("json", "number with a leading zero at byte %zu", s); return NULL; }
        while (p->i < p->n && ((p->p[p->i] >= '0' && p->p[p->i] <= '9') || p->p[p->i] == '.' || p->p[p->i] == 'e' ||
                               p->p[p->i] == 'E' || p->p[p->i] == '+' || p->p[p->i] == '-'))
            p->i++;
        j->s = xmalloc(p->i - s + 1);
        memcpy(j->s, p->p + s, p->i - s);
    } else if (p->n - p->i >= 4 && !memcmp(p->p + p->i, "true", 4)) { j = jnew(J_TRUE, p->i); p->i += 4; }
    else if (p->n - p->i >= 5 && !memcmp(p->p + p->i, "false", 5)) { j = jnew(J_FALSE, p->i); p->i += 5; }
    else if (p->n - p->i >= 4 && !memcmp(p->p + p->i, "null", 4)) { j = jnew(J_NULL, p->i); p->i += 4; }
    else { fail("json", "unexpected character '%c' at byte %zu", c >= 0x20 && c < 0x7f ? c : '?', p->i); return NULL; }
done:
    p->depth--;
    return j;
}

static J *jparse(const char *s, size_t n)
{
    P p = {s, n, 0, 0};
    J *j = jvalue(&p);
    if (!j) return NULL;
    ws(&p);
    if (p.i != n) { fail("json", "trailing data after the JSON value at byte %zu", p.i); return NULL; }
    return j;
}

static J *jget(const J *o, const char *k)
{
    for (size_t i = 0; i < o->n; i++) if (!strcmp(o->key[i], k)) return o->kid[i];
    return NULL;
}

static const char *jtname(int t)
{
    static const char *const n[] = {"?", "object", "array", "string", "number", "true", "false", "null"};
    return n[t >= 1 && t <= 7 ? t : 0];
}

/* Exact integer from a JSON number: [-]digits only (no fraction or exponent). 0 ok. */
static int jint(const J *j, int *neg, uint64_t *mag)
{
    if (j->t != J_NUM) return -1;
    const char *s = j->s;
    *neg = 0;
    if (*s == '-') { *neg = 1; s++; }
    if (!*s) return -1;
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        if (v > (UINT64_MAX - (uint64_t)(*s - '0')) / 10) return -1;
        v = v * 10 + (uint64_t)(*s - '0');
    }
    *mag = v;
    return 0;
}

/* ================================================================================ types */

static const char *const k_scal[] = {"void", "bool", "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"};
static const char *const k_refk[] = {"", "own", "shared", "mut"};

/* the unit whose struct table names struct reference types (emit: g_u; lift: the decoded unit) */
static const OscUnit *g_tu;

static int scalar_from(const char *s)
{
    for (int i = 1; i <= 9; i++) if (!strcmp(s, k_scal[i])) return i;
    return -1;
}

static int struct_from(const char *s)
{
    for (unsigned k = 0; g_tu && k < g_tu->nstructs; k++) if (!strcmp(g_tu->structs[k].name, s)) return (int)k;
    return -1;
}

/* "u32" | "own:u32:8" | "shared:u32:8" | "mut:u32:8" | "own:S" | "shared:S" | "mut:S" (S a declared struct).
 * 0 ok; -1 not a type; -2 a struct reference to a struct that is not declared. */
static int type_from(const char *s, OscType *t)
{
    memset(t, 0, sizeof *t);
    int sc = scalar_from(s);
    if (sc > 0) { t->s = (OscScalar)sc; return 0; }
    const char *c1 = strchr(s, ':');
    if (!c1) return -1;
    const char *c2 = strchr(c1 + 1, ':');
    int rk = 0;
    for (int k = 1; k <= 3; k++) if ((size_t)(c1 - s) == strlen(k_refk[k]) && !strncmp(s, k_refk[k], (size_t)(c1 - s))) rk = k;
    if (!rk) return -1;
    if (!c2) { /* struct reference */
        int k = struct_from(c1 + 1);
        if (k < 0) return scalar_from(c1 + 1) > 0 || !c1[1] ? -1 : -2;
        t->s = OSC_T_REF; t->ref = (OscRefKind)rk; t->elem = OSC_T_VOID;
        t->len = g_tu->structs[k].ncells; t->sid = (uint8_t)(k + 1);
        return 0;
    }
    char el[8];
    size_t el_n = (size_t)(c2 - c1 - 1);
    if (el_n == 0 || el_n >= sizeof el) return -1;
    memcpy(el, c1 + 1, el_n); el[el_n] = 0;
    int e = scalar_from(el);
    if (e < 0) return -1;
    const char *d = c2 + 1;
    if (!*d || (d[0] == '0') || strlen(d) > 2) return -1;
    unsigned len = 0;
    for (; *d; d++) { if (*d < '0' || *d > '9') return -1; len = len * 10 + (unsigned)(*d - '0'); }
    if (len < 1 || len > OSC_MAX_ARRAY_LEN) return -1;
    t->s = OSC_T_REF; t->ref = (OscRefKind)rk; t->elem = (OscScalar)e; t->len = (uint16_t)len;
    return 0;
}

static void type_name(const OscType *t, char *o, size_t n)
{
    if (t->s == OSC_T_REF && t->sid == 0 && t->ref >= 1 && t->ref <= 3 && t->elem >= 1 && t->elem <= 9)
        snprintf(o, n, "%s:%s:%u", k_refk[t->ref], k_scal[t->elem], t->len);
    else if (t->s == OSC_T_REF && t->sid && g_tu && t->sid <= g_tu->nstructs && t->ref >= 1 && t->ref <= 3 &&
             t->elem == OSC_T_VOID && t->len == g_tu->structs[t->sid - 1].ncells)
        snprintf(o, n, "%s:%s", k_refk[t->ref], g_tu->structs[t->sid - 1].name);
    else if (t->s <= OSC_T_I64 && t->ref == 0 && t->len == 0 && t->sid == 0) snprintf(o, n, "%s", k_scal[t->s]);
    else snprintf(o, n, "?");
}

static int is_int(const OscType *t) { return t->s >= OSC_T_U8 && t->s <= OSC_T_I64 && t->ref == 0; }
static int is_scalar(const OscType *t) { return t->s >= OSC_T_BOOL && t->s <= OSC_T_I64 && t->ref == 0; }
static int is_arr(const OscType *t) { return t->s == OSC_T_REF && t->sid == 0; }
static int is_sref(const OscType *t) { return t->s == OSC_T_REF && t->sid != 0; }
static int is_ref(const OscType *t) { return t->s == OSC_T_REF; }
/* may reference register `a` be bound to reference slot `p` (mov dst or call parameter)? osc_ir.c
 * ref_bindable: the same array (element type, length) or the same struct, and the kind rule */
static int ref_ok(const OscType *p, const OscType *a)
{
    return is_ref(p) && is_ref(a) && a->elem == p->elem && a->len == p->len && a->sid == p->sid &&
           (p->ref == OSC_REF_SHARED || (p->ref == OSC_REF_MUT && a->ref != OSC_REF_SHARED) ||
            (p->ref == OSC_REF_OWN && a->ref == OSC_REF_OWN));
}
static int teq(const OscType *a, const OscType *b) { return a->s == b->s && a->ref == b->ref && a->elem == b->elem && a->len == b->len && a->sid == b->sid; }

/* ============================================================================== op table */

typedef struct {
    const char *name;
    uint8_t op, sub;
    const char *fields[5];   /* required fields in addition to "op" (NULL-terminated) */
    const char *opt;         /* one optional field (or NULL) */
} OpDef;

static const OpDef k_ops[] = {
    {"const", OSC_I_CONST, 0, {"dst", "value"}, NULL},
    {"mov", OSC_I_MOV, 0, {"dst", "a"}, NULL},
    {"add", OSC_I_BIN, OSC_B_ADD, {"dst", "a", "b"}, NULL},
    {"sub", OSC_I_BIN, OSC_B_SUB, {"dst", "a", "b"}, NULL},
    {"mul", OSC_I_BIN, OSC_B_MUL, {"dst", "a", "b"}, NULL},
    {"div", OSC_I_BIN, OSC_B_DIV, {"dst", "a", "b"}, NULL},
    {"rem", OSC_I_BIN, OSC_B_REM, {"dst", "a", "b"}, NULL},
    {"and", OSC_I_BIN, OSC_B_AND, {"dst", "a", "b"}, NULL},
    {"or", OSC_I_BIN, OSC_B_OR, {"dst", "a", "b"}, NULL},
    {"xor", OSC_I_BIN, OSC_B_XOR, {"dst", "a", "b"}, NULL},
    {"shl", OSC_I_BIN, OSC_B_SHL, {"dst", "a", "b"}, NULL},
    {"shr", OSC_I_BIN, OSC_B_SHR, {"dst", "a", "b"}, NULL},
    {"neg", OSC_I_UN, OSC_U_NEG, {"dst", "a"}, NULL},
    {"bnot", OSC_I_UN, OSC_U_BNOT, {"dst", "a"}, NULL},
    {"not", OSC_I_UN, OSC_U_LNOT, {"dst", "a"}, NULL},
    {"eq", OSC_I_CMP, OSC_C_EQ, {"dst", "a", "b"}, NULL},
    {"ne", OSC_I_CMP, OSC_C_NE, {"dst", "a", "b"}, NULL},
    {"lt", OSC_I_CMP, OSC_C_LT, {"dst", "a", "b"}, NULL},
    {"le", OSC_I_CMP, OSC_C_LE, {"dst", "a", "b"}, NULL},
    {"gt", OSC_I_CMP, OSC_C_GT, {"dst", "a", "b"}, NULL},
    {"ge", OSC_I_CMP, OSC_C_GE, {"dst", "a", "b"}, NULL},
    {"cast", OSC_I_CAST, 0, {"dst", "a"}, NULL},
    {"alloc", OSC_I_ALLOC, 0, {"dst", "init"}, NULL},
    {"release", OSC_I_RELEASE, 0, {"arr"}, NULL},
    {"load", OSC_I_LOAD, 0, {"dst", "arr", "index"}, NULL},
    {"store", OSC_I_STORE, 0, {"arr", "index", "value"}, NULL},
    {"fload", OSC_I_FLOAD, 0, {"dst", "a", "field"}, "index"},
    {"fstore", OSC_I_FSTORE, 0, {"a", "field", "value"}, "index"},
    {"aopen", OSC_I_AOPEN, 0, {"dst", "cells"}, NULL},
    {"aalloc", OSC_I_AALLOC, 0, {"dst", "init", "arena"}, NULL},
    {"adestroy", OSC_I_ADESTROY, 0, {"arena"}, NULL},
    {"call", OSC_I_CALL, 0, {"func", "args"}, "dst"},
    {"trap", OSC_I_TRAP, 0, {"code"}, NULL},
    {"br", OSC_I_BR, 0, {"to"}, NULL},
    {"cbr", OSC_I_CBR, 0, {"cond", "then", "else"}, NULL},
    {"ret", OSC_I_RET, 0, {NULL}, "a"},
};
#define NOPS (sizeof k_ops / sizeof k_ops[0])

static const OpDef *opdef_of(uint8_t op, uint8_t sub)
{
    for (size_t i = 0; i < NOPS; i++)
        if (k_ops[i].op == op && (k_ops[i].sub == sub || (op != OSC_I_BIN && op != OSC_I_UN && op != OSC_I_CMP))) return &k_ops[i];
    return NULL;
}

/* ================================================================================== emit */

static OscUnit g_u;

typedef struct {
    const char *fname;
    unsigned fi, blk, k;       /* step position, for messages */
    const char *opname;
    OscFunc *f;
    unsigned nblocks;
} Ctx;

#define STEP(...) fail("step", __VA_ARGS__)
#define AT "function %s block %u op %u (%s): "
#define ATV c->fname, c->blk, c->k, c->opname

static int reg_field(Ctx *c, const J *o, const char *k, int16_t *out)
{
    const J *j = jget(o, k);
    int neg; uint64_t v;
    if (!j || jint(j, &neg, &v) || neg) return STEP(AT "\"%s\" must be a register number (0 or more)", ATV, k);
    if (v >= c->f->nvregs) return STEP(AT "\"%s\" is register %" PRIu64 " but this function has registers 0..%u", ATV, k, v, c->f->nvregs - 1u);
    *out = (int16_t)v;
    return 0;
}

static int blk_field(Ctx *c, const J *o, const char *k, int16_t *out)
{
    const J *j = jget(o, k);
    int neg; uint64_t v;
    if (!j || jint(j, &neg, &v) || neg) return STEP(AT "\"%s\" must be a block number (0 or more)", ATV, k);
    if (v >= c->nblocks) return STEP(AT "\"%s\" is block %" PRIu64 " but this function has blocks 0..%u", ATV, k, v, c->nblocks - 1u);
    *out = (int16_t)v;
    return 0;
}

static const char *tn(const OscType *t)
{
    static char b[4][96];
    static int i;
    i = (i + 1) & 3;
    type_name(t, b[i], sizeof b[i]);
    return b[i];
}

static int find_func(const char *name, unsigned before)
{
    for (unsigned i = 0; i < before; i++) if (!strcmp(g_u.funcs[i].name, name)) return (int)i;
    return -1;
}

/* One step: parse the op object, check it against the IR rules, fill *in. */
static int step(Ctx *c, const J *o, int last, OscInsn *in)
{
    c->opname = "?";
    if (o->t != J_OBJ) return STEP("function %s block %u op %u: an op must be an object, not %s", c->fname, c->blk, c->k, jtname(o->t));
    const J *jo = jget(o, "op");
    if (!jo || jo->t != J_STR) return STEP("function %s block %u op %u: missing \"op\" string", c->fname, c->blk, c->k);
    const OpDef *d = NULL;
    for (size_t i = 0; i < NOPS; i++) if (!strcmp(k_ops[i].name, jo->s)) d = &k_ops[i];
    if (!d) return STEP("function %s block %u op %u: unknown op \"%s\"", c->fname, c->blk, c->k, jo->s);
    c->opname = d->name;
    /* exactly the declared fields */
    for (size_t i = 0; i < o->n; i++) {
        const char *k = o->key[i];
        int ok = !strcmp(k, "op") || (d->opt && !strcmp(k, d->opt));
        for (int f = 0; f < 5 && d->fields[f]; f++) if (!strcmp(k, d->fields[f])) ok = 1;
        if (!ok) return STEP(AT "unknown field \"%s\"", ATV, k);
    }
    for (int f = 0; f < 5 && d->fields[f]; f++)
        if (!jget(o, d->fields[f])) return STEP(AT "missing field \"%s\"", ATV, d->fields[f]);
    memset(in, 0, sizeof *in);
    in->dst = in->a = in->b = in->c = in->blk_t = in->blk_f = in->callee = -1;
    in->op = d->op; in->sub = d->sub;
    const OscType *T = c->f->vtype;
    int term = d->op == OSC_I_BR || d->op == OSC_I_CBR || d->op == OSC_I_RET;
    if (term && !last) return STEP(AT "br, cbr and ret end a block: this one is followed by more ops", ATV);
    if (!term && last) return STEP(AT "the last op of a block must be br, cbr or ret", ATV);
    switch (d->op) {
    case OSC_I_CONST: {
        if (reg_field(c, o, "dst", &in->dst)) return -1;
        const OscType *t = &T[in->dst];
        if (!is_scalar(t)) return STEP(AT "dst r%d is %s; const needs a scalar register", ATV, in->dst, tn(t));
        const J *v = jget(o, "value");
        int neg; uint64_t m;
        if (t->s == OSC_T_BOOL) {
            if (v->t == J_TRUE) in->imm = 1;
            else if (v->t == J_FALSE) in->imm = 0;
            else if (!jint(v, &neg, &m) && !neg && m <= 1) in->imm = m;
            else return STEP(AT "a bool value is true, false, 0 or 1", ATV);
            break;
        }
        if (jint(v, &neg, &m)) return STEP(AT "\"value\" must be a whole decimal number", ATV);
        unsigned w = osc_scalar_width(t->s);
        if (!osc_scalar_signed(t->s)) {
            if (neg && m) return STEP(AT "value -%" PRIu64 " is negative; r%d is %s", ATV, m, in->dst, tn(t));
            if (w < 64 && (m >> w)) return STEP(AT "value %" PRIu64 " does not fit %s", ATV, m, tn(t));
            in->imm = m;
        } else {
            uint64_t lim = (uint64_t)1 << (w - 1);
            if (neg ? m > lim : m >= lim) return STEP(AT "value %s%" PRIu64 " does not fit %s", ATV, neg ? "-" : "", m, tn(t));
            in->imm = neg ? (uint64_t)0 - m : m; /* canonical: sign-extended to 64 bits */
        }
        break;
    }
    case OSC_I_MOV:
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "a", &in->a)) return -1;
        if (is_ref(&T[in->dst])) { /* EMIT:mov-ref: binding rule of osc_ir.c ref_bindable, as for call arguments */
            if (!ref_ok(&T[in->dst], &T[in->a]))
                return STEP(AT "mov to r%d (%s) from r%d (%s): a reference register takes an own: (move), own: or mut: (into mut:), or any reference (into shared:) to the same array (element type and length) or struct",
                            ATV, in->dst, tn(&T[in->dst]), in->a, tn(&T[in->a]));
        } else if (!is_scalar(&T[in->dst]) || !teq(&T[in->dst], &T[in->a]))
            return STEP(AT "mov needs two scalar registers of the same type (r%d is %s, r%d is %s)", ATV, in->dst, tn(&T[in->dst]), in->a, tn(&T[in->a]));
        break;
    case OSC_I_BIN: {
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "a", &in->a) || reg_field(c, o, "b", &in->b)) return -1;
        const OscType *D = &T[in->dst], *A = &T[in->a], *B = &T[in->b];
        if (!is_scalar(D) || !teq(D, A)) return STEP(AT "dst r%d (%s) and a r%d (%s) must have the same scalar type", ATV, in->dst, tn(D), in->a, tn(A));
        if (in->sub == OSC_B_SHL || in->sub == OSC_B_SHR) {
            if (!is_int(A) || !is_int(B)) return STEP(AT "shifts need integer registers (a is %s, b is %s)", ATV, tn(A), tn(B));
        } else if (in->sub == OSC_B_AND || in->sub == OSC_B_OR || in->sub == OSC_B_XOR) {
            if (!teq(A, B)) return STEP(AT "a r%d (%s) and b r%d (%s) must have the same type", ATV, in->a, tn(A), in->b, tn(B));
        } else if (!is_int(A) || !teq(A, B))
            return STEP(AT "arithmetic needs a and b of the same integer type (a r%d is %s, b r%d is %s)", ATV, in->a, tn(A), in->b, tn(B));
        break;
    }
    case OSC_I_UN: {
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "a", &in->a)) return -1;
        const OscType *D = &T[in->dst], *A = &T[in->a];
        if (!is_scalar(D) || !teq(D, A)) return STEP(AT "dst r%d (%s) and a r%d (%s) must have the same scalar type", ATV, in->dst, tn(D), in->a, tn(A));
        if (in->sub == OSC_U_LNOT && A->s != OSC_T_BOOL) return STEP(AT "not needs a bool register (r%d is %s)", ATV, in->a, tn(A));
        if (in->sub != OSC_U_LNOT && !is_int(A)) return STEP(AT "%s needs an integer register (r%d is %s)", ATV, d->name, in->a, tn(A));
        break;
    }
    case OSC_I_CMP: {
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "a", &in->a) || reg_field(c, o, "b", &in->b)) return -1;
        if (T[in->dst].s != OSC_T_BOOL || !is_scalar(&T[in->dst])) return STEP(AT "dst r%d is %s; a comparison writes a bool register", ATV, in->dst, tn(&T[in->dst]));
        if (!is_scalar(&T[in->a]) || !teq(&T[in->a], &T[in->b]))
            return STEP(AT "a r%d (%s) and b r%d (%s) must have the same scalar type", ATV, in->a, tn(&T[in->a]), in->b, tn(&T[in->b]));
        break;
    }
    case OSC_I_CAST:
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "a", &in->a)) return -1;
        if (!is_int(&T[in->dst]) || !is_int(&T[in->a]))
            return STEP(AT "cast converts between integer registers (dst is %s, a is %s)", ATV, tn(&T[in->dst]), tn(&T[in->a]));
        break;
    case OSC_I_ALLOC:
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "init", &in->a)) return -1;
        if (!is_ref(&T[in->dst]) || T[in->dst].ref != OSC_REF_OWN) return STEP(AT "dst r%d is %s; alloc writes an own:<type>:<len> or own:<struct> register", ATV, in->dst, tn(&T[in->dst]));
        if (is_sref(&T[in->dst])) { /* a struct starts zeroed; the init register is checked in zero_inits() */
            if (!is_scalar(&T[in->a]) || T[in->a].s != OSC_T_U64) return STEP(AT "init r%d is %s; a struct alloc takes a u64 register holding 0", ATV, in->a, tn(&T[in->a]));
        } else if (!is_scalar(&T[in->a]) || T[in->a].s != T[in->dst].elem) return STEP(AT "init r%d is %s; it must be the element type of r%d", ATV, in->a, tn(&T[in->a]), in->dst);
        break;
    case OSC_I_RELEASE:
        if (reg_field(c, o, "arr", &in->a)) return -1;
        if (!is_ref(&T[in->a]) || T[in->a].ref != OSC_REF_OWN) return STEP(AT "arr r%d is %s; release needs an own:<type>:<len> or own:<struct> register", ATV, in->a, tn(&T[in->a]));
        break;
    case OSC_I_AOPEN: {
        if (reg_field(c, o, "dst", &in->dst)) return -1;
        if (!is_scalar(&T[in->dst]) || T[in->dst].s != OSC_T_U64) return STEP(AT "dst r%d is %s; an arena handle is a u64 register", ATV, in->dst, tn(&T[in->dst]));
        int neg; uint64_t v;
        if (jint(jget(o, "cells"), &neg, &v) || neg || v < 1 || v > OSC_MAX_ARRAY_LEN) return STEP(AT "\"cells\" must be 1..%d", ATV, OSC_MAX_ARRAY_LEN);
        in->imm = v;
        break;
    }
    case OSC_I_AALLOC:
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "init", &in->a) || reg_field(c, o, "arena", &in->b)) return -1;
        if (!is_ref(&T[in->dst]) || T[in->dst].ref != OSC_REF_OWN) return STEP(AT "dst r%d is %s; aalloc writes an own:<type>:<len> or own:<struct> register", ATV, in->dst, tn(&T[in->dst]));
        if (is_sref(&T[in->dst])) {
            if (!is_scalar(&T[in->a]) || T[in->a].s != OSC_T_U64) return STEP(AT "init r%d is %s; a struct aalloc takes a u64 register holding 0", ATV, in->a, tn(&T[in->a]));
        } else if (!is_scalar(&T[in->a]) || T[in->a].s != T[in->dst].elem) return STEP(AT "init r%d is %s; it must be the element type of r%d", ATV, in->a, tn(&T[in->a]), in->dst);
        if (!is_scalar(&T[in->b]) || T[in->b].s != OSC_T_U64) return STEP(AT "arena r%d is %s; an arena handle is a u64 register", ATV, in->b, tn(&T[in->b]));
        break;
    case OSC_I_ADESTROY:
        if (reg_field(c, o, "arena", &in->a)) return -1;
        if (!is_scalar(&T[in->a]) || T[in->a].s != OSC_T_U64) return STEP(AT "arena r%d is %s; an arena handle is a u64 register", ATV, in->a, tn(&T[in->a]));
        break;
    case OSC_I_FLOAD:
    case OSC_I_FSTORE: {
        int ld = d->op == OSC_I_FLOAD;
        if ((ld && reg_field(c, o, "dst", &in->dst)) || reg_field(c, o, "a", &in->a)) return -1;
        if (!ld && reg_field(c, o, "value", &in->c)) return -1;
        const OscType *A = &T[in->a];
        if (!is_sref(A)) return STEP(AT "a r%d is %s, not a struct reference", ATV, in->a, tn(A));
        if (!ld && A->ref == OSC_REF_SHARED) return STEP(AT "a r%d is %s; fstore needs an own: or mut: struct reference", ATV, in->a, tn(A)); /* EMIT:fstore-shared */
        const OscStruct *S = &g_u.structs[A->sid - 1];
        const J *fj = jget(o, "field");
        if (fj->t != J_STR) return STEP(AT "\"field\" must be a field name of struct %s", ATV, S->name);
        int fk = -1;
        for (unsigned k = 0; k < S->nfields; k++) if (!strcmp(S->fields[k].name, fj->s)) fk = (int)k;
        if (fk < 0) return STEP(AT "struct %s has no field \"%s\"", ATV, S->name, fj->s);
        const OscField *F = &S->fields[fk];
        in->imm = (uint64_t)fk;
        if (F->alen) {
            if (!jget(o, "index")) return STEP(AT "field %s.%s is an array [%s; %u]: \"index\" is required", ATV, S->name, F->name, k_scal[F->s], F->alen); /* EMIT:field-noindex */
            if (reg_field(c, o, "index", &in->b)) return -1;
            if (!is_int(&T[in->b])) return STEP(AT "index r%d is %s, not an integer", ATV, in->b, tn(&T[in->b]));
        } else if (jget(o, "index")) return STEP(AT "field %s.%s is a scalar %s: leave out \"index\"", ATV, S->name, F->name, k_scal[F->s]); /* EMIT:field-index */
        int v = ld ? in->dst : in->c;
        if (!is_scalar(&T[v]) || T[v].s != F->s) /* EMIT:field-type */
            return STEP(AT "%s r%d is %s; field %s.%s is %s", ATV, ld ? "dst" : "value", v, tn(&T[v]), S->name, F->name, k_scal[F->s]);
        break;
    }
    case OSC_I_LOAD:
        if (reg_field(c, o, "dst", &in->dst) || reg_field(c, o, "arr", &in->a) || reg_field(c, o, "index", &in->b)) return -1;
        if (!is_arr(&T[in->a])) return STEP(AT "arr r%d is %s, not an array", ATV, in->a, tn(&T[in->a]));
        if (!is_int(&T[in->b])) return STEP(AT "index r%d is %s, not an integer", ATV, in->b, tn(&T[in->b]));
        if (!is_scalar(&T[in->dst]) || T[in->dst].s != T[in->a].elem) return STEP(AT "dst r%d is %s; it must be the element type of r%d", ATV, in->dst, tn(&T[in->dst]), in->a);
        break;
    case OSC_I_STORE:
        if (reg_field(c, o, "arr", &in->a) || reg_field(c, o, "index", &in->b) || reg_field(c, o, "value", &in->c)) return -1;
        if (!is_arr(&T[in->a]) || T[in->a].ref == OSC_REF_SHARED) return STEP(AT "arr r%d is %s; store needs an own: or mut: array", ATV, in->a, tn(&T[in->a]));
        if (!is_int(&T[in->b])) return STEP(AT "index r%d is %s, not an integer", ATV, in->b, tn(&T[in->b]));
        if (!is_scalar(&T[in->c]) || T[in->c].s != T[in->a].elem) return STEP(AT "value r%d is %s; it must be the element type of r%d", ATV, in->c, tn(&T[in->c]), in->a);
        break;
    case OSC_I_CALL: {
        const J *fn = jget(o, "func"), *args = jget(o, "args");
        if (fn->t != J_STR) return STEP(AT "\"func\" must be the name of an earlier function", ATV);
        int g = find_func(fn->s, c->fi);
        if (g < 0) return STEP(AT "no function named \"%s\" before this one (calls go to earlier functions only)", ATV, fn->s);
        in->callee = (int16_t)g;
        const OscFunc *G = &g_u.funcs[g];
        if (args->t != J_ARR) return STEP(AT "\"args\" must be an array of register numbers", ATV);
        if (args->n != G->nparams) return STEP(AT "%s takes %u arguments, got %zu", ATV, G->name, G->nparams, args->n);
        in->nargs = (uint8_t)args->n;
        for (size_t i = 0; i < args->n; i++) {
            int neg; uint64_t v;
            if (jint(args->kid[i], &neg, &v) || neg || v >= c->f->nvregs) return STEP(AT "args[%zu] is not a register of this function", ATV, i);
            in->args[i] = (int16_t)v;
            const OscType *p = &G->vtype[i], *a = &T[v];
            int ok = is_scalar(p) ? teq(p, a) : ref_ok(p, a);
            if (!ok) return STEP(AT "args[%zu] r%" PRIu64 " is %s; %s parameter %zu is %s", ATV, i, v, tn(a), G->name, i, tn(p));
        }
        const J *dj = jget(o, "dst");
        if (G->ret.s == OSC_T_VOID) {
            if (dj) return STEP(AT "%s returns nothing: leave out \"dst\"", ATV, G->name);
        } else {
            if (!dj) return STEP(AT "%s returns %s: \"dst\" is required", ATV, G->name, tn(&G->ret));
            if (reg_field(c, o, "dst", &in->dst)) return -1;
            if (!teq(&T[in->dst], &G->ret)) return STEP(AT "dst r%d is %s; %s returns %s", ATV, in->dst, tn(&T[in->dst]), G->name, tn(&G->ret));
        }
        break;
    }
    case OSC_I_TRAP: {
        int neg; uint64_t v;
        if (jint(jget(o, "code"), &neg, &v) || neg || v < 1 || v > OSC_TRAP_MAX) return STEP(AT "trap code must be 1..%d", ATV, OSC_TRAP_MAX);
        in->imm = v;
        break;
    }
    case OSC_I_BR:
        if (blk_field(c, o, "to", &in->blk_t)) return -1;
        break;
    case OSC_I_CBR:
        if (reg_field(c, o, "cond", &in->a) || blk_field(c, o, "then", &in->blk_t) || blk_field(c, o, "else", &in->blk_f)) return -1;
        if (T[in->a].s != OSC_T_BOOL || !is_scalar(&T[in->a])) return STEP(AT "cond r%d is %s, not bool", ATV, in->a, tn(&T[in->a]));
        break;
    case OSC_I_RET: {
        const J *aj = jget(o, "a");
        if (c->f->ret.s == OSC_T_VOID) {
            if (aj) return STEP(AT "this function returns nothing: leave out \"a\"", ATV);
        } else {
            if (!aj) return STEP(AT "this function returns %s: \"a\" is required", ATV, tn(&c->f->ret));
            if (reg_field(c, o, "a", &in->a)) return -1;
            if (!teq(&T[in->a], &c->f->ret)) return STEP(AT "r%d is %s; this function returns %s", ATV, in->a, tn(&T[in->a]), tn(&c->f->ret));
        }
        break;
    }
    }
    return 0;
}

static int name_ok(const char *s)
{
    size_t n = strlen(s);
    if (!n || n > OSC_NAME_MAX - 1 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return 0;
    for (size_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return 0;
    return 1;
}

static int only_keys(const J *o, const char *where, const char *const *ks)
{
    for (size_t i = 0; i < o->n; i++) {
        int ok = 0;
        for (int k = 0; ks[k]; k++) if (!strcmp(o->key[i], ks[k])) ok = 1;
        if (!ok) return fail("schema", "%s: unknown key \"%s\"", where, o->key[i]);
    }
    for (int k = 0; ks[k]; k++) if (!jget(o, ks[k])) return fail("schema", "%s: missing key \"%s\"", where, ks[k]);
    return 0;
}

/* "structs": the unit's struct table, OSC-2 2.3 layout (declaration order, whole cells, offsets are
 * prefix sums, 1..64 cells). Struct k is OscUnit.structs[k]; a reference to it has sid k + 1. */
static int read_structs(const J *ss)
{
    static const char *const sk[] = {"name", "fields", NULL};
    static const char *const fk[] = {"name", "type", NULL}, *const fk_len[] = {"name", "type", "len", NULL};
    if (ss->t != J_ARR || ss->n < 1 || ss->n > OSC_MAX_STRUCTS)
        return fail("schema", "\"structs\" must be an array of 1..%d structs (leave the key out when there are none)", OSC_MAX_STRUCTS);
    for (size_t k = 0; k < ss->n; k++) {
        const J *S = ss->kid[k];
        char where[96], fw[192];
        snprintf(where, sizeof where, "structs[%zu]", k);
        if (S->t != J_OBJ) return fail("schema", "%s must be an object", where);
        const J *nm = jget(S, "name");
        if (nm && nm->t == J_STR && name_ok(nm->s)) snprintf(where, sizeof where, "struct %s", nm->s);
        if (only_keys(S, where, sk)) return -1;
        if (nm->t != J_STR || !name_ok(nm->s)) return fail("schema", "%s: \"name\" must match [A-Za-z_][A-Za-z0-9_]{0,62}", where);
        if (scalar_from(nm->s) > 0 || !strcmp(nm->s, "void")) return fail("schema", "%s: a struct may not be named like a scalar type", where);
        if (struct_from(nm->s) >= 0) return fail("schema", "%s: a struct with this name already exists", where);
        OscStruct *st = &g_u.structs[k];
        snprintf(st->name, sizeof st->name, "%s", nm->s);
        const J *fs = jget(S, "fields");
        if (fs->t != J_ARR || fs->n < 1 || fs->n > OSC_MAX_FIELDS) return fail("schema", "%s: \"fields\" must be an array of 1..%d fields", where, OSC_MAX_FIELDS);
        unsigned off = 0;
        for (size_t i = 0; i < fs->n; i++) {
            const J *F = fs->kid[i];
            snprintf(fw, sizeof fw, "%s fields[%zu]", where, i);
            if (F->t != J_OBJ) return fail("schema", "%s must be an object", fw);
            const J *fn = jget(F, "name"), *ft = jget(F, "type"), *fl = jget(F, "len");
            if (fn && fn->t == J_STR && name_ok(fn->s)) snprintf(fw, sizeof fw, "%s field %s", where, fn->s);
            if (only_keys(F, fw, fl ? fk_len : fk)) return -1;
            if (fn->t != J_STR || !name_ok(fn->s)) return fail("schema", "%s: \"name\" must match [A-Za-z_][A-Za-z0-9_]{0,62}", fw);
            for (size_t g = 0; g < i; g++) if (!strcmp(st->fields[g].name, fn->s)) return fail("schema", "%s: a field with this name already exists", fw);
            int sc = ft->t == J_STR ? scalar_from(ft->s) : -1;
            if (sc < 0) return fail("schema", "%s: \"type\" must be bool, u8..u64 or i8..i64", fw);
            unsigned len = 0;
            if (fl) {
                int neg; uint64_t v;
                if (jint(fl, &neg, &v) || neg || v < 1 || v > OSC_MAX_ARRAY_LEN) return fail("schema", "%s: \"len\" must be 1..%d (leave it out for a scalar field)", fw, OSC_MAX_ARRAY_LEN);
                len = (unsigned)v;
            }
            OscField *fd = &st->fields[i];
            snprintf(fd->name, sizeof fd->name, "%s", fn->s);
            fd->s = (OscScalar)sc; fd->alen = (uint16_t)len; fd->off = (uint16_t)off;
            off += len ? len : 1;
            if (off > OSC_MAX_ARRAY_LEN) return fail("schema", "%s: the struct takes more than %d cells (a scalar field takes 1, an array field its len)", fw, OSC_MAX_ARRAY_LEN);
        }
        st->nfields = (uint8_t)fs->n;
        st->ncells = (uint16_t)off;
        g_u.nstructs = (uint8_t)(k + 1);
    }
    return 0;
}

/* A struct starts zeroed (OSC-2 2.3; oscc lowers a literal as const 0 into a fresh u64, alloc or aalloc, then
 * fstores): the init register of a struct alloc is a local, not a parameter, and every write to it in
 * the function is const 0. Returns the instruction index of the first struct alloc that breaks this,
 * or -1. Used by emit (refuse) and lift (refuse to print). */
static int zero_inits(const OscFunc *f)
{
    for (unsigned i = 0; i < f->ninsns; i++) {
        const OscInsn *x = &f->insns[i];
        if ((x->op != OSC_I_ALLOC && x->op != OSC_I_AALLOC) || x->dst < 0 || x->dst >= f->nvregs || !f->vtype[x->dst].sid) continue;
        int ok = x->a >= f->nparams; /* EMIT:zero-param */
        for (unsigned j = 0; ok && j < f->ninsns; j++)
            if (f->insns[j].dst == x->a && !(f->insns[j].op == OSC_I_CONST && f->insns[j].imm == 0)) ok = 0; /* EMIT:zero-write */
        if (!ok) return (int)i;
    }
    return -1;
}

static void where_insn(const char *err, char *o, size_t n);

static int build(const J *root)
{
    memset(&g_u, 0, sizeof g_u);
    static const char *const top[] = {"functions", NULL}, *const top_s[] = {"structs", "functions", NULL};
    static const char *const fk[] = {"name", "params", "ret", "locals", "blocks", NULL};
    if (root->t != J_OBJ) return fail("schema", "the top level must be an object {\"functions\": [...]}");
    g_tu = &g_u;
    const J *ss = jget(root, "structs");
    if (only_keys(root, "top level", ss ? top_s : top)) return -1;
    if (ss && read_structs(ss)) return -1;
    const J *fs = jget(root, "functions");
    if (fs->t != J_ARR || fs->n < 1 || fs->n > OSC_MAX_FUNCS) return fail("schema", "\"functions\" must be an array of 1..%d functions", OSC_MAX_FUNCS);
    for (size_t fi = 0; fi < fs->n; fi++) {
        const J *F = fs->kid[fi];
        char where[96];
        snprintf(where, sizeof where, "functions[%zu]", fi);
        if (F->t != J_OBJ) return fail("schema", "%s must be an object", where);
        const J *nm = jget(F, "name");
        if (nm && nm->t == J_STR && name_ok(nm->s)) snprintf(where, sizeof where, "function %s", nm->s);
        const char *fkk[8]; int nk = 0;
        for (int q = 0; fk[q]; q++) fkk[nk++] = fk[q];
        if (jget(F, "requires")) fkk[nk++] = "requires";
        if (jget(F, "ensures")) fkk[nk++] = "ensures";
        fkk[nk] = NULL;
        if (only_keys(F, where, fkk)) return -1;
        if (nm->t != J_STR || !name_ok(nm->s)) return fail("schema", "%s: \"name\" must match [A-Za-z_][A-Za-z0-9_]{0,62}", where);
        if (find_func(nm->s, (unsigned)fi) >= 0) return fail("schema", "%s: a function with this name already exists", where);
        OscFunc *f = &g_u.funcs[fi];
        snprintf(f->name, sizeof f->name, "%s", nm->s);
        for (int q = 0; q < 2; q++) { /* optional contract source text (OSC-2 item 1; the checks are already in the blocks) */
            const J *tx = jget(F, q ? "ensures" : "requires");
            char *dst = q ? f->ensures_text : f->requires_text;
            if (!tx) continue;
            if (tx->t != J_STR || !tx->s[0] || strlen(tx->s) >= OSC_CLAUSE_MAX) return fail("schema", "%s: \"%s\" must be 1..%d characters of contract text (leave it out when there is none)", where, q ? "ensures" : "requires", OSC_CLAUSE_MAX - 1);
            for (const char *p = tx->s; *p; p++) if (*p < 0x20 || *p > 0x7e) return fail("schema", "%s: \"%s\" holds a control character", where, q ? "ensures" : "requires");
            memcpy(dst, tx->s, strlen(tx->s) + 1);
        }
        const J *ps = jget(F, "params"), *rt = jget(F, "ret"), *ls = jget(F, "locals"), *bs = jget(F, "blocks");
        if (ps->t != J_ARR || ps->n > OSC_MAX_PARAMS) return fail("schema", "%s: \"params\" must be an array of 0..%d types", where, OSC_MAX_PARAMS);
        if (ls->t != J_ARR) return fail("schema", "%s: \"locals\" must be an array of types", where);
        if (ps->n + ls->n > OSC_MAX_VREGS) return fail("schema", "%s: %zu registers (params + locals) > %d", where, ps->n + ls->n, OSC_MAX_VREGS);
        f->nparams = (uint8_t)ps->n;
        f->nvregs = (uint16_t)(ps->n + ls->n);
        for (size_t r = 0; r < ps->n + ls->n; r++) {
            const J *t = r < ps->n ? ps->kid[r] : ls->kid[r - ps->n];
            int tr = t->t == J_STR ? type_from(t->s, &f->vtype[r]) : -1;
            if (tr == -2) return fail("schema", "%s: register r%zu: type \"%s\" names no struct in \"structs\"", where, r, t->s);
            if (tr) return fail("schema", "%s: register r%zu: type must be bool, u8..u64, i8..i64, own|shared|mut:<type>:<1..64> or own|shared|mut:<struct>", where, r);
        }
        if (rt->t != J_STR) return fail("schema", "%s: \"ret\" must be a type name or \"void\"", where);
        if (!strcmp(rt->s, "void")) f->ret.s = OSC_T_VOID;
        else if (scalar_from(rt->s) > 0) f->ret.s = (OscScalar)scalar_from(rt->s);
        else return fail("schema", "%s: \"ret\" must be bool, u8..u64, i8..i64 or void", where);
        if (bs->t != J_ARR || bs->n < 1 || bs->n > OSC_MAX_BLOCKS) return fail("schema", "%s: \"blocks\" must be an array of 1..%d blocks", where, OSC_MAX_BLOCKS);
        g_u.nfuncs = (uint16_t)(fi + 1);
        Ctx c = {f->name, (unsigned)fi, 0, 0, "?", f, (unsigned)bs->n};
        for (size_t b = 0; b < bs->n; b++) {
            const J *B = bs->kid[b];
            if (B->t != J_ARR || B->n < 1) return fail("schema", "%s: block %zu must be a non-empty array of ops", where, b);
            f->blocks[b].first = f->ninsns;
            f->blocks[b].count = (uint32_t)B->n;
            for (size_t k = 0; k < B->n; k++) {
                if (f->ninsns >= OSC_MAX_INSNS) return fail("schema", "%s: more than %d ops", where, OSC_MAX_INSNS);
                c.blk = (unsigned)b; c.k = (unsigned)k;
                if (step(&c, B->kid[k], k + 1 == B->n, &f->insns[f->ninsns])) return -1;
                f->ninsns++;
            }
        }
        f->nblocks = (uint16_t)bs->n;
        int za = zero_inits(f);
        if (za >= 0) {
            char e[96], w[400];
            snprintf(e, sizeof e, "func %u insn %d: ", (unsigned)fi, za);
            where_insn(e, w, sizeof w);
            return fail("step", "%sinit r%d of a struct alloc must be a u64 local written only by const 0 (a struct starts zeroed)", w, f->insns[za].a);
        }
    }
    return 0;
}

/* "func F insn I: msg" -> "function <name> block B op K: msg" */
static void where_insn(const char *err, char *o, size_t n)
{
    int fi; unsigned ii; int used = 0;
    if (sscanf(err, "func %d insn %u: %n", &fi, &ii, &used) == 2 && used && fi >= 0 && fi < g_u.nfuncs) {
        const OscFunc *f = &g_u.funcs[fi];
        for (unsigned b = 0; b < f->nblocks; b++)
            if (ii >= f->blocks[b].first && ii < f->blocks[b].first + f->blocks[b].count) {
                const OpDef *d = opdef_of(f->insns[ii].op, f->insns[ii].sub);
                snprintf(o, n, "function %s block %u op %u (%s): %s", f->name, b, ii - f->blocks[b].first, d ? d->name : "?", err + used);
                return;
            }
    }
    snprintf(o, n, "%s", err);
}

static uint8_t *slurp(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = xmalloc(max + 1);
    size_t n = fread(b, 1, max + 1, f);
    fclose(f);
    if (n > max) { free(b); errno = EFBIG; return NULL; }
    b[n] = 0;
    *len = n;
    return b;
}

static int cmd_emit(const char *in, const char *out)
{
    size_t n;
    uint8_t *src = slurp(in, MAX_IN, &n);
    if (!src) { printf("REFUSED input: cannot read %s or it is over %u bytes\n", in, MAX_IN); return 1; }
    J *root = jparse((const char *)src, n);
    if (!root || build(root)) { printf("REFUSED %s\n", g_why); return 1; }
    char err[256] = "";
    if (osc_ir_validate(&g_u, err, sizeof err)) {
        char w[400];
        where_insn(err, w, sizeof w);
        printf("REFUSED validator: %s\n", w);
        return 1;
    }
    static uint8_t buf[OSC_IR_ENCODED_MAX], buf2[OSC_IR_ENCODED_MAX];
    static OscUnit back;
    size_t len = 0, len2 = 0;
    if (osc_ir_encode(&g_u, buf, sizeof buf, &len)) { printf("REFUSED encoding: osc_ir_encode refused the unit\n"); return 1; }
    int rc = osc_ir_decode(buf, len, &back);
    if (rc) { printf("REFUSED encoding: osc_ir_decode refused the canonical bytes (%s)\n", osc_ir_refusal_name(rc)); return 1; }
    if (osc_ir_encode(&back, buf2, sizeof buf2, &len2) || len2 != len || memcmp(buf, buf2, len)) { /* EMIT:roundtrip */
        printf("REFUSED encoding: decode -> encode is not byte-identical\n");
        return 1;
    }
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    FILE *f = fopen(tmp, "wb");
    if (!f || fwrite(buf, 1, len, f) != len || fclose(f)) { fprintf(stderr, "omega_emit: cannot write %s\n", tmp); return 2; }
    if (rename(tmp, out)) { fprintf(stderr, "omega_emit: cannot rename to %s\n", out); return 2; }
    printf("OK bytes=%zu functions=%u\n", len, g_u.nfuncs);
    return 0;
}

/* ================================================================================== lift */

/* a printable-ASCII string as a JSON string (only " and \ need escapes) */
static void pjs(const char *s)
{
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

static int cmd_lift(const char *in)
{
    size_t n;
    uint8_t *b = slurp(in, OSC_IR_ENCODED_MAX, &n);
    if (!b) { fprintf(stderr, "omega_emit: cannot read %s\n", in); return 2; }
    static OscUnit u;
    int rc = osc_ir_decode(b, n, &u);
    if (rc) { printf("REFUSED decoder: %s\n", osc_ir_refusal_name(rc)); return 1; }
    g_tu = &u;
    for (unsigned k = 0; k < u.nstructs; k++) { /* struct and field names must be schema NAMEs */
        const OscStruct *s = &u.structs[k];
        int ok = name_ok(s->name) && scalar_from(s->name) < 0 && strcmp(s->name, "void");
        for (unsigned i = 0; ok && i < s->nfields; i++) ok = name_ok(s->fields[i].name) && s->fields[i].s >= OSC_T_BOOL && s->fields[i].s <= OSC_T_I64;
        if (!ok) { printf("REFUSED lift: struct %u has a name or field outside the schema\n", k); return 1; }
    }
    /* check everything first: lift prints nothing for a unit it cannot express */
    for (unsigned fi = 0; fi < u.nfuncs; fi++) {
        const OscFunc *f = &u.funcs[fi];
        for (const char *p = f->requires_text; *p; p++) if (*p < 0x20 || *p > 0x7e) { printf("REFUSED lift: function %s: requires text holds a control character\n", f->name); return 1; }
        for (const char *p = f->ensures_text; *p; p++) if (*p < 0x20 || *p > 0x7e) { printf("REFUSED lift: function %s: ensures text holds a control character\n", f->name); return 1; }
        for (unsigned r = 0; r < f->nvregs; r++) {
            char t[96];
            type_name(&f->vtype[r], t, sizeof t);
            if (!strcmp(t, "?")) { printf("REFUSED lift: function %s register r%u has a type outside the schema\n", f->name, r); return 1; }
        }
        for (unsigned i = 0; i < f->ninsns; i++) {
            const OscInsn *x = &f->insns[i];
            if (!opdef_of(x->op, x->sub)) { printf("REFUSED lift: function %s uses op %u, outside the schema\n", f->name, x->op); return 1; }
            if ((x->op == OSC_I_FLOAD || x->op == OSC_I_FSTORE) &&
                (x->a < 0 || x->a >= f->nvregs || !f->vtype[x->a].sid || f->vtype[x->a].sid > u.nstructs ||
                 x->imm >= u.structs[f->vtype[x->a].sid - 1].nfields)) {
                printf("REFUSED lift: function %s op %u names no field of a declared struct\n", f->name, i); return 1;
            }
        }
        if (zero_inits(f) >= 0) { printf("REFUSED lift: function %s: a struct alloc init is not a u64 local written only by const 0\n", f->name); return 1; }
    }
    if (u.nstructs) { /* EMIT:lift-structs */
        printf("{\"structs\": [\n");
        for (unsigned k = 0; k < u.nstructs; k++) {
            const OscStruct *s = &u.structs[k];
            printf("  {\"name\": \"%s\", \"fields\": [", s->name);
            for (unsigned i = 0; i < s->nfields; i++) {
                const OscField *fd = &s->fields[i];
                printf("%s{\"name\": \"%s\", \"type\": \"%s\"", i ? ", " : "", fd->name, k_scal[fd->s]);
                if (fd->alen) printf(", \"len\": %u", fd->alen);
                printf("}");
            }
            printf("]}%s\n", k + 1 < u.nstructs ? "," : "");
        }
        printf(" ],\n \"functions\": [\n");
    } else printf("{\"functions\": [\n");
    for (unsigned fi = 0; fi < u.nfuncs; fi++) {
        const OscFunc *f = &u.funcs[fi];
        char t[96];
        printf("  {\"name\": \"%s\", \"params\": [", f->name);
        for (unsigned r = 0; r < f->nparams; r++) { type_name(&f->vtype[r], t, sizeof t); printf("%s\"%s\"", r ? ", " : "", t); }
        printf("], \"ret\": \"%s\", ", f->ret.s == OSC_T_VOID ? "void" : k_scal[f->ret.s]);
        if (f->requires_text[0]) { printf("\"requires\": "); pjs(f->requires_text); printf(", "); }
        if (f->ensures_text[0]) { printf("\"ensures\": "); pjs(f->ensures_text); printf(", "); }
        printf("\"locals\": [");
        for (unsigned r = f->nparams; r < f->nvregs; r++) { type_name(&f->vtype[r], t, sizeof t); printf("%s\"%s\"", r > f->nparams ? ", " : "", t); }
        printf("],\n   \"blocks\": [\n");
        for (unsigned bi = 0; bi < f->nblocks; bi++) {
            printf("    [\n");
            for (uint32_t i = f->blocks[bi].first; i < f->blocks[bi].first + f->blocks[bi].count; i++) {
                const OscInsn *x = &f->insns[i];
                const OpDef *d = opdef_of(x->op, x->sub);
                printf("     {\"op\": \"%s\"", d->name);
                switch (x->op) {
                case OSC_I_CONST: {
                    OscScalar s = f->vtype[x->dst].s;
                    if (s == OSC_T_BOOL) printf(", \"dst\": %d, \"value\": %s", x->dst, x->imm ? "true" : "false");
                    else if (osc_scalar_signed(s)) printf(", \"dst\": %d, \"value\": %" PRId64, x->dst, (int64_t)x->imm);
                    else printf(", \"dst\": %d, \"value\": %" PRIu64, x->dst, x->imm); /* EMIT:lift-const */
                    break;
                }
                case OSC_I_MOV: case OSC_I_UN: case OSC_I_CAST: printf(", \"dst\": %d, \"a\": %d", x->dst, x->a); break;
                case OSC_I_BIN: case OSC_I_CMP: printf(", \"dst\": %d, \"a\": %d, \"b\": %d", x->dst, x->a, x->b); break;
                case OSC_I_ALLOC: printf(", \"dst\": %d, \"init\": %d", x->dst, x->a); break;
                case OSC_I_RELEASE: printf(", \"arr\": %d", x->a); break;
                case OSC_I_AOPEN: printf(", \"dst\": %d, \"cells\": %" PRIu64, x->dst, x->imm); break;
                case OSC_I_AALLOC: printf(", \"dst\": %d, \"init\": %d, \"arena\": %d", x->dst, x->a, x->b); break;
                case OSC_I_ADESTROY: printf(", \"arena\": %d", x->a); break;
                case OSC_I_LOAD: printf(", \"dst\": %d, \"arr\": %d, \"index\": %d", x->dst, x->a, x->b); break;
                case OSC_I_STORE: printf(", \"arr\": %d, \"index\": %d, \"value\": %d", x->a, x->b, x->c); break;
                case OSC_I_FLOAD: case OSC_I_FSTORE: {
                    const char *fn = u.structs[f->vtype[x->a].sid - 1].fields[x->imm].name;
                    if (x->op == OSC_I_FLOAD) printf(", \"dst\": %d, \"a\": %d, \"field\": \"%s\"", x->dst, x->a, fn);
                    else printf(", \"a\": %d, \"field\": \"%s\", \"value\": %d", x->a, fn, x->c);
                    if (x->b >= 0) printf(", \"index\": %d", x->b);
                    break;
                }
                case OSC_I_CALL:
                    if (x->dst >= 0) printf(", \"dst\": %d", x->dst);
                    printf(", \"func\": \"%s\", \"args\": [", u.funcs[x->callee].name);
                    for (unsigned k = 0; k < x->nargs; k++) printf("%s%d", k ? ", " : "", x->args[k]);
                    printf("]");
                    break;
                case OSC_I_TRAP: printf(", \"code\": %" PRIu64, x->imm); break;
                case OSC_I_BR: printf(", \"to\": %d", x->blk_t); break;
                case OSC_I_CBR: printf(", \"cond\": %d, \"then\": %d, \"else\": %d", x->a, x->blk_t, x->blk_f); break;
                case OSC_I_RET: if (x->a >= 0) printf(", \"a\": %d", x->a); break;
                }
                printf("}%s\n", i + 1 < f->blocks[bi].first + f->blocks[bi].count ? "," : "");
            }
            printf("    ]%s\n", bi + 1 < f->nblocks ? "," : "");
        }
        printf("   ]}%s\n", fi + 1 < u.nfuncs ? "," : "");
    }
    printf("]}\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "emit")) return cmd_emit(argv[2], argv[3]);
    if (argc == 3 && !strcmp(argv[1], "lift")) return cmd_lift(argv[2]);
    fprintf(stderr, "usage: omega_emit emit <oplist.json> <out.oscir> | omega_emit lift <in.oscir>\n");
    return 2;
}
