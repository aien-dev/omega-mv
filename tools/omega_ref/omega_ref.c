/*
 * omega_ref.c -- REFERENCE INTERPRETER for the om4 language (Omegese with the systems words of
 * docs/one_layer/OMEGESE.md section 9). An ORACLE ONLY: it sits outside the seed chain and never makes a
 * byte of seed/. Written from OMEGESE.md (sections 3, 4 and 9) and seed/README.md; seed/rung4/om4.s and
 * seed/omc/ were not read while writing it (tools/omega_ref/README.md).
 *
 * It runs a program by its meaning (no machine code): values, arrays, pools, texts, read/write/exit and
 * the traps with their numbers, with the same input and output streams as the ELF om4 makes.
 *
 *   omega_ref check <prog>                  0 when accepted, else the refusal number (as om4's exit code)
 *   omega_ref run <prog>                    program mode: run main; stdin/stdout/exit status as the ELF
 *                                           (trap n -> exit 100+n); a refused program exits 125
 *   omega_ref call <prog> <entry> <inputs>  function mode: one line per input, as seed/check/runner.c
 *                                           prints them: value TAB raw-x0, or "TRAP n TAB -"
 *
 * Refusal numbers: the ones section 9 names, plus om3's (seed/rung3/gate3.sh): 3 unknown name, 4 too few
 * values, 5 extra words, 6 bad number, 8 number outside its type, 10 a name already in use, 11 two bare
 * numbers in one op, 15 a function without end. Where the spec gives no number the choice is marked
 * "spec silent" here and listed in README.md (decision 19 proposals).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---------- types ---------- */
enum { T_NONE, T_BOOL, T_U8, T_U16, T_U32, T_U64, T_I8, T_I16, T_I32, T_I64, T_VOID };
static const char *TNAME[] = {"?", "bool", "uchar", "ushort", "uint", "ulong", "byte", "short", "int", "long", "void"};
static const char *TIR[] = {0, 0, "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", 0};
static int twidth(int t) { static const int w[] = {0, 1, 8, 16, 32, 64, 8, 16, 32, 64, 0}; return w[t]; }
static int tsigned(int t) { return t >= T_I8 && t <= T_I64; }
static int tnum(int t) { return t >= T_U8 && t <= T_I64; }
static int tbytes(int t) { return t == T_BOOL ? 1 : twidth(t) / 8; }

/* canonical form: signed sign-extended to 64 bits, unsigned zero-extended, bool 0/1 */
static uint64_t canon(int t, uint64_t x)
{
    int w = twidth(t);
    if (t == T_BOOL) return x & 1;
    if (w >= 64) return x;
    if (tsigned(t)) { uint64_t m = 1ULL << (w - 1); x &= (1ULL << w) - 1; return (x ^ m) - m; }
    return x & ((1ULL << w) - 1);
}
static int64_t smin(int t) { return twidth(t) == 64 ? INT64_MIN : -(INT64_C(1) << (twidth(t) - 1)); }
static int64_t smax(int t) { return twidth(t) == 64 ? INT64_MAX : (INT64_C(1) << (twidth(t) - 1)) - 1; }
static uint64_t umax(int t) { return t == T_BOOL ? 1 : twidth(t) == 64 ? UINT64_MAX : (UINT64_C(1) << twidth(t)) - 1; }

/* ---------- refusals and traps ---------- */
static int cur_line;
static int refuse_exit; /* nonzero: every refusal exits with this instead of its number */
static void refuse(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "omega_ref: line %d: error %d: ", cur_line, code);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
    _exit(refuse_exit ? refuse_exit : code);
}

static jmp_buf trap_jb;
static int trap_code;
static void trap(int code) { trap_code = code; longjmp(trap_jb, 1); }

/* ---------- capacities (section 9.6) ---------- */
#define MAX_SRC (8u * 1024 * 1024 - 1)
#define MAX_NAMES 1024
#define MAX_NEST 1024
#define MAX_FUNCS 512
#define MAX_BLOCKS 512
#define MAX_POOL_ELEMS 16777216u
#define MAX_POOL_BYTES (1ull << 30)
#define MAX_LOCAL_ARR_BYTES (4ull << 20)
#define STACK_BYTES (8ull << 20)
#define MAX_PARAMS 64
#define LOOP_MAX 1000000ull

/* ---------- arrays ---------- */
typedef struct {
    uint8_t *data;
    int ety;
    uint64_t len;
    int ro; /* text or view */
} Arr;

static uint64_t arr_get(const Arr *a, uint64_t i)
{
    uint8_t *p = a->data + i * tbytes(a->ety);
    uint64_t v = 0;
    switch (tbytes(a->ety)) {
    case 1: v = *p; break;
    case 2: { uint16_t x; memcpy(&x, p, 2); v = x; } break;
    case 4: { uint32_t x; memcpy(&x, p, 4); v = x; } break;
    default: memcpy(&v, p, 8);
    }
    return canon(a->ety, v);
}
static void arr_put(Arr *a, uint64_t i, uint64_t v)
{
    uint8_t *p = a->data + i * tbytes(a->ety);
    switch (tbytes(a->ety)) {
    case 1: *p = (uint8_t)v; break;
    case 2: { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); } break;
    case 4: { uint32_t x = (uint32_t)v; memcpy(p, &x, 4); } break;
    default: memcpy(p, &v, 8);
    }
}

/* ---------- program representation ---------- */
enum { AK_GLOBAL, AK_LOCAL }; /* where an array reference lives */
typedef struct { int kind, idx; } ARef;

enum {
    N_NUM, N_VAR, N_BIN, N_CMP, N_NEG, N_NOT, N_CONV, N_GET, N_LEN, N_READ, N_WRITE, N_CALL, N_BARE
};
enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_REM, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
       OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE };
static const char *OPW[] = {"add", "sub", "mul", "div", "rem", "and", "or", "xor", "shl", "shr",
                            "eq", "ne", "lt", "le", "gt", "ge"};

typedef struct Node {
    int kind, op, ty; /* ty T_NONE while a bare number has no type yet */
    int bare;         /* a bare number, or an op over bare numbers, still waiting for its type */
    uint64_t num;     /* N_NUM: canonical value; N_BARE: magnitude */
    int neg;          /* N_BARE: minus sign */
    int slot;         /* N_VAR */
    ARef arr;         /* N_GET N_LEN N_READ N_WRITE */
    int fn;           /* N_CALL */
    struct Node *a, *b, *c, *d;
    struct Node **args; /* N_CALL: scalar args (NULL for array args) */
    ARef *aargs;        /* N_CALL: array args */
    int line;
} Node;

enum { S_ASSIGN, S_NEW, S_IF, S_WHILE, S_FOR, S_BREAK, S_RET, S_PUT, S_FREE, S_TRAP, S_EXIT, S_CALL };
typedef struct Stmt {
    int kind, line;
    int slot;      /* S_ASSIGN S_NEW S_FOR */
    Node *v, *v2;  /* value / cond / count / index, value */
    ARef arr;      /* S_PUT */
    int ety;       /* S_NEW */
    uint64_t n;    /* S_NEW length, S_WHILE bound, S_FOR literal count, S_TRAP code */
    int count_kind;/* S_FOR: 0 literal, 1 a name (read every pass), 2 a value (computed once) */
    int ty;        /* S_FOR loop type */
    struct Stmt *body, *els, *next;
} Stmt;

enum { PK_SCALAR, PK_VIEW, PK_EDIT };
typedef struct {
    char name[64];
    int np;
    int pkind[MAX_PARAMS], pty[MAX_PARAMS];
    uint64_t plen[MAX_PARAMS];
    int ret;
    Stmt *body;
    int nslots;
    int has_call;  /* a non-leaf function: the stack model charges it a frame */
    int done;      /* its end has been read */
} Func;

typedef struct { char name[16]; int ety; uint64_t len; int is_text; Arr arr; } Global;

static Func funcs[MAX_FUNCS];
static int nfuncs;
static Global *globals;
static int nglobals, capglobals;
static uint64_t pool_bytes;
static int program_mode;

/* ---------- lexing: lines and words ---------- */
static char *src;
static size_t srclen;
static char **W;
static char *wbuf;
static int nw, wi;

static int is_reserved(const char *w);
static int split_words(const char *line, size_t n)
{
    /* words are separated by spaces; ( and ) are words of their own even against a name */
    char *buf = wbuf;
    size_t o = 0;
    nw = 0;
    size_t i = 0;
    while (i < n) {
        char ch = line[i];
        if (ch == ' ' || ch == '\t' || ch == '\r') { i++; continue; }
        W[nw++] = buf + o;
        if (ch == '(' || ch == ')') { buf[o++] = ch; buf[o++] = 0; i++; continue; }
        while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '(' && line[i] != ')')
            buf[o++] = line[i++];
        buf[o++] = 0;
    }
    wi = 0;
    return nw;
}
static const char *peekw(void) { return wi < nw ? W[wi] : NULL; }
static const char *nextw(void) { return wi < nw ? W[wi++] : NULL; }

static int type_word(const char *w)
{
    for (int t = T_BOOL; t <= T_I64; t++) {
        if (!strcmp(w, TNAME[t])) return t;
        if (TIR[t] && !strcmp(w, TIR[t])) return t;
    }
    return T_NONE;
}
static int op_word(const char *w)
{
    for (int i = 0; i < 16; i++) if (!strcmp(w, OPW[i])) return i;
    return -1;
}
static const char *KEYWORDS[] = {"fn", "to", "void", "end", "if", "else", "while", "bound", "for", "break",
                                 "ret", "put", "free", "trap", "global", "text", "new", "view", "edit", "own",
                                 "get", "len", "read", "write", "exit", "note", "true", "false", "neg", "not",
                                 "=", "(", ")", NULL};
static int is_reserved(const char *w)
{
    for (int i = 0; KEYWORDS[i]; i++) if (!strcmp(w, KEYWORDS[i])) return 1;
    return op_word(w) >= 0 || type_word(w) != T_NONE;
}
static int is_name(const char *w)
{
    if (!((w[0] >= 'a' && w[0] <= 'z') || (w[0] >= 'A' && w[0] <= 'Z') || w[0] == '_')) return 0;
    for (const char *p = w; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    return 1;
}
static int looks_number(const char *w) { return (w[0] >= '0' && w[0] <= '9') || w[0] == '-'; }
/* decimal digits, optional minus, no leading zeros, no -0, fits 64 bits (as u64 or as i64) */
static void parse_number(const char *w, int *neg, uint64_t *mag)
{
    const char *p = w;
    *neg = 0;
    if (*p == '-') { *neg = 1; p++; }
    if (!*p) refuse(6, "bad number '%s'", w);
    if (p[0] == '0' && p[1]) refuse(6, "bad number '%s' (leading zero)", w);
    uint64_t v = 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') refuse(6, "bad number '%s'", w);
        unsigned d = (unsigned)(*p - '0');
        if (v > (UINT64_MAX - d) / 10) refuse(8, "number '%s' outside every type", w);
        v = v * 10 + d;
    }
    if (*neg && v == 0) refuse(6, "bad number '-0'");
    if (*neg && v > (uint64_t)INT64_MAX + 1) refuse(8, "number '%s' outside every type", w);
    *mag = v;
}
static int fits(int t, int neg, uint64_t mag)
{
    if (t == T_BOOL) return !neg && mag <= 1;
    if (tsigned(t)) return neg ? mag <= (uint64_t)smax(t) + 1 : mag <= (uint64_t)smax(t);
    return !neg && mag <= umax(t);
}
static uint64_t num_value(int neg, uint64_t mag) { return neg ? (uint64_t)0 - mag : mag; }

/* ---------- scopes ---------- */
enum { K_SCALAR, K_OWN, K_VIEW, K_EDIT };
typedef struct { char name[16]; int kind, ty, slot, depth, is_loop; uint64_t len; } Name;
static Name names[MAX_NAMES + 1];
static int nnames;
static int depth;          /* open blocks inside the current function (0 = its outermost block) */
static Func *curf;
static int nslots;

static int find_func(const char *w)
{
    for (int i = 0; i < nfuncs; i++) if (!strcmp(funcs[i].name, w)) return i;
    return -1;
}
static int find_global(const char *w)
{
    for (int i = 0; i < nglobals; i++) if (!strcmp(globals[i].name, w)) return i;
    return -1;
}
static Name *find_name(const char *w)
{
    for (int i = nnames - 1; i >= 0; i--) if (!strcmp(names[i].name, w)) return &names[i];
    return NULL;
}
static void check_new_name(const char *w, int is_fn)
{
    if (!is_name(w) || is_reserved(w)) refuse(10, "'%s' cannot be a name", w); /* spec silent on the number */
    size_t n = strlen(w);
    if (is_fn ? n > 63 : n > 8) refuse(13, "name '%s' too long", w);
    if (find_func(w) >= 0 || find_global(w) >= 0 || find_name(w)) refuse(10, "'%s' is already in use", w);
}
static Name *add_name(const char *w, int kind, int ty, uint64_t len, int is_loop)
{
    check_new_name(w, 0);
    if (nnames >= MAX_NAMES) refuse(14, "more than %d names in scope", MAX_NAMES);
    Name *nm = &names[nnames++];
    snprintf(nm->name, sizeof nm->name, "%s", w);
    nm->kind = kind; nm->ty = ty; nm->len = len; nm->depth = depth; nm->is_loop = is_loop;
    nm->slot = nslots++;
    return nm;
}
static void close_scope(int d)
{
    while (nnames > 0 && names[nnames - 1].depth >= d) nnames--;
}

/* ---------- values ---------- */
static Node *new_node(int kind)
{
    Node *n = calloc(1, sizeof *n);
    if (!n) { perror("omega_ref"); exit(2); }
    n->kind = kind; n->line = cur_line;
    return n;
}

/* give a bare value its type (a number, or an op over bare numbers); the number must fit */
static void fix_bare(Node *n, int t)
{
    if (!n->bare) return;
    n->bare = 0;
    n->ty = t;
    switch (n->kind) {
    case N_BARE:
        if (t == T_BOOL || !tnum(t)) refuse(7, "a number where a %s is needed", TNAME[t]);
        if (!fits(t, n->neg, n->num)) refuse(8, "number %s%" PRIu64 " outside %s", n->neg ? "-" : "", n->num, TNAME[t]);
        n->num = canon(t, num_value(n->neg, n->num));
        n->kind = N_NUM;
        break;
    case N_NEG: case N_NOT:
        fix_bare(n->a, t);
        break;
    }
}

static int nest;
static Node *parse_value(int want);

static ARef parse_array_ref(const char *w, int *ety, uint64_t *len, int *ro, int *kindout)
{
    ARef r;
    if (!w) refuse(4, "an array name is missing");
    Name *nm = find_name(w);
    if (nm && nm->kind != K_SCALAR) {
        r.kind = AK_LOCAL; r.idx = nm->slot;
        *ety = nm->ty; *len = nm->len; *ro = nm->kind == K_VIEW; *kindout = nm->kind;
        return r;
    }
    int g = find_global(w);
    if (g >= 0) {
        r.kind = AK_GLOBAL; r.idx = g;
        *ety = globals[g].ety; *len = globals[g].len; *ro = globals[g].is_text; *kindout = -1 - globals[g].is_text;
        return r;
    }
    if (nm || find_func(w) >= 0) refuse(7, "'%s' is not an array", w);
    refuse(3, "unknown name '%s'", w);
    return r;
}

/* the binary op both values agree on; a bare one takes the other's type */
static int unify(Node *a, Node *b, const char *opw)
{
    if (a->bare && b->bare) refuse(11, "%s has two bare numbers", opw);
    if (a->bare) fix_bare(a, b->ty);
    else if (b->bare) fix_bare(b, a->ty);
    if (a->ty != b->ty) refuse(7, "%s of %s and %s (no implicit conversions)", opw, TNAME[a->ty], TNAME[b->ty]);
    return a->ty;
}

static Node *parse_call(int fi, int want_void)
{
    Func *f = &funcs[fi];
    if (!want_void && f->ret == T_VOID) refuse(7, "void function '%s' used as a value", f->name);
    if (want_void && f->ret != T_VOID) refuse(7, "value function '%s' called as a line", f->name);
    if (&funcs[fi] == curf && !program_mode) refuse(9, "'%s' calls itself in function mode", f->name);
    curf->has_call = 1;
    Node *n = new_node(N_CALL);
    n->fn = fi; n->ty = f->ret;
    n->args = calloc(f->np + 1, sizeof *n->args);
    n->aargs = calloc(f->np + 1, sizeof *n->aargs);
    for (int i = 0; i < f->np; i++) {
        if (f->pkind[i] == PK_SCALAR) {
            if (!peekw()) refuse(4, "%s has %d values after it; it takes %d", f->name, i, f->np);
            n->args[i] = parse_value(f->pty[i]);
            fix_bare(n->args[i], f->pty[i]);
            if (n->args[i]->ty != f->pty[i]) refuse(7, "argument %d of %s is %s, wants %s", i + 1, f->name, TNAME[n->args[i]->ty], TNAME[f->pty[i]]);
            continue;
        }
        const char *w = nextw();
        if (!w) refuse(4, "%s has %d values after it; it takes %d", f->name, i, f->np);
        int as = -1; /* view/edit written before the argument */
        if (!strcmp(w, "view")) as = PK_VIEW;
        else if (!strcmp(w, "edit")) as = PK_EDIT;
        else if (!strcmp(w, "own")) refuse(9, "own is not in the subset");
        if (as >= 0) { w = nextw(); if (!w) refuse(4, "an array name is missing"); }
        int ety, ro, kind; uint64_t len;
        if (find_global(w) >= 0) refuse(19, "a global or text '%s' is never passed", w);
        ARef r = parse_array_ref(w, &ety, &len, &ro, &kind);
        if (ety != f->pty[i] || len != f->plen[i]) refuse(7, "argument %d of %s: %s %" PRIu64 ", wants %s %" PRIu64, i + 1, f->name, TNAME[ety], len, TNAME[f->pty[i]], f->plen[i]);
        if (as == PK_EDIT && ro) refuse(20, "'%s' is read-only, passed as edit", w);
        if (f->pkind[i] == PK_EDIT && (ro || as == PK_VIEW)) refuse(20, "'%s' passed to an edit parameter is read-only", w);
        for (int j = 0; j < i; j++)
            if (f->pkind[j] != PK_SCALAR && n->aargs[j].kind == r.kind && n->aargs[j].idx == r.idx)
                refuse(19, "'%s' passed twice in one call", w);
        n->aargs[i] = r;
    }
    return n;
}

/* want: the type the place needs (T_NONE when the place has none); the caller checks the type */
static Node *parse_value(int want)
{
    const char *w = nextw();
    if (!w) refuse(4, "a value is missing");
    if (++nest > MAX_NEST) refuse(14, "values nested over %d deep", MAX_NEST);
    Node *n = NULL;
    int op, t;
    if (!strcmp(w, "(")) {
        n = parse_value(want);
        const char *c = nextw();
        if (!c || strcmp(c, ")")) refuse(c ? 5 : 4, "a ( without its )"); /* spec silent on the number */
    } else if (!strcmp(w, ")")) {
        refuse(5, "a ) without its (");
    } else if (looks_number(w)) {
        n = new_node(N_BARE);
        parse_number(w, &n->neg, &n->num);
        n->bare = 1; n->ty = T_NONE;
    } else if (!strcmp(w, "true") || !strcmp(w, "false")) {
        n = new_node(N_NUM); n->ty = T_BOOL; n->num = w[0] == 't';
    } else if ((op = op_word(w)) >= 0) {
        Node *a = parse_value(T_NONE), *b;
        if (op == OP_SHL || op == OP_SHR) {
            b = parse_value(T_NONE);
            if (a->bare && b->bare) refuse(11, "%s has two bare numbers", w);
            if (a->bare) fix_bare(a, b->ty);
            if (b->bare) fix_bare(b, a->ty);
            if (!tnum(a->ty) || !tnum(b->ty)) refuse(7, "%s of %s by %s", w, TNAME[a->ty], TNAME[b->ty]);
            n = new_node(N_BIN); n->ty = a->ty;
        } else {
            b = parse_value(T_NONE);
            t = unify(a, b, w);
            if (op >= OP_EQ) {
                n = new_node(N_CMP); n->ty = T_BOOL;
                if (t == T_BOOL && op != OP_EQ && op != OP_NE) refuse(7, "%s of bool", w);
            } else {
                n = new_node(N_BIN); n->ty = t;
                if (t == T_BOOL && op != OP_AND && op != OP_OR && op != OP_XOR) refuse(7, "%s of bool", w);
            }
        }
        n->op = op; n->a = a; n->b = b;
    } else if (!strcmp(w, "neg") || !strcmp(w, "not")) {
        Node *a = parse_value(want);
        n = new_node(w[1] == 'e' ? N_NEG : N_NOT);
        n->a = a; n->ty = a->ty; n->bare = a->bare;
        if (!a->bare && (n->kind == N_NEG ? !tnum(a->ty) : !(tnum(a->ty) || a->ty == T_BOOL)))
            refuse(7, "%s of %s", w, TNAME[a->ty]);
    } else if ((t = type_word(w)) != T_NONE) {
        Node *a = parse_value(t);
        if (a->bare) { fix_bare(a, t); n = a; }       /* a typed number */
        else {
            if (!tnum(a->ty) && a->ty != T_BOOL) refuse(7, "%s of %s", w, TNAME[a->ty]);
            n = new_node(N_CONV); n->a = a; n->ty = t;  /* a conversion: trap 5 when outside */
        }
    } else if (!strcmp(w, "get")) {
        n = new_node(N_GET);
        int ro, kind; uint64_t len;
        n->arr = parse_array_ref(nextw(), &n->ty, &len, &ro, &kind);
        n->a = parse_value(T_NONE);
        if (n->a->bare) {
            if (n->a->neg || n->a->num >= len) refuse(21, "index %s%" PRIu64 " outside an array of %" PRIu64, n->a->neg ? "-" : "", n->a->num, len);
            fix_bare(n->a, T_U64);
        }
        if (!tnum(n->a->ty)) refuse(7, "an index of type %s", TNAME[n->a->ty]);
    } else if (!strcmp(w, "len")) {
        n = new_node(N_LEN);
        int ety, ro, kind; uint64_t len;
        n->arr = parse_array_ref(nextw(), &ety, &len, &ro, &kind);
        n->ty = T_U64; n->num = len;
    } else if (!strcmp(w, "read") || !strcmp(w, "write")) {
        n = new_node(w[0] == 'r' ? N_READ : N_WRITE);
        n->a = parse_value(T_I32);
        fix_bare(n->a, T_I32);
        if (n->a->ty != T_I32) refuse(7, "%s fd is %s, wants int", w, TNAME[n->a->ty]);
        int ety, ro, kind; uint64_t len;
        n->arr = parse_array_ref(nextw(), &ety, &len, &ro, &kind);
        if (n->kind == N_READ && ro) refuse(20, "read into a read-only array");
        if (ety != T_U8) refuse(7, "%s needs an array of uchar", w); /* spec: "must be an array of uchar for the counts to be bytes" */
        n->b = parse_value(T_U64); fix_bare(n->b, T_U64);
        n->c = parse_value(T_U64); fix_bare(n->c, T_U64);
        if (n->b->ty != T_U64 || n->c->ty != T_U64) refuse(7, "%s off and n are ulong", w);
        n->ty = T_I64;
    } else if (!strcmp(w, "new")) {
        refuse(9, "new only as the whole value of a line in the outermost block");
    } else if (!strcmp(w, "own")) {
        refuse(9, "own is not in the subset");
    } else if (!strcmp(w, "view") || !strcmp(w, "edit")) {
        refuse(7, "%s only before a call argument", w);
    } else {
        int fi = find_func(w);
        Name *nm = find_name(w);
        if (nm) {
            if (nm->kind != K_SCALAR) refuse(7, "array '%s' used as a value", w);
            n = new_node(N_VAR); n->slot = nm->slot; n->ty = nm->ty;
        } else if (fi >= 0) {
            n = parse_call(fi, 0);
        } else if (find_global(w) >= 0) {
            refuse(7, "array '%s' used as a value", w);
        } else if (is_name(w) && !is_reserved(w)) {
            refuse(3, "unknown name '%s'", w);
        } else {
            refuse(6, "'%s' is not a value", w); /* spec silent on the number */
        }
    }
    nest--;
    return n;
}

/* a whole value for a place of type want (T_NONE: the value decides) */
static Node *value_for(int want, const char *place)
{
    nest = 0;
    Node *n = parse_value(want);
    if (n->bare) {
        if (want == T_NONE) refuse(7, "a bare number has no type here; write it typed (%s)", place); /* spec silent */
        fix_bare(n, want);
    }
    if (want != T_NONE && n->ty != want) refuse(7, "%s is %s, wants %s", place, TNAME[n->ty], TNAME[want]);
    return n;
}
static void line_end(void)
{
    if (wi < nw) refuse(5, "extra words at the end of the line: '%s'", W[wi]);
}

/* ---------- statements ---------- */
static Stmt *new_stmt(int kind)
{
    Stmt *s = calloc(1, sizeof *s);
    if (!s) { perror("omega_ref"); exit(2); }
    s->kind = kind; s->line = cur_line;
    return s;
}

typedef struct {
    int kind;             /* 0 function body, 1 if, 2 else, 3 while, 4 for */
    Stmt *owner;
    Stmt **tail;          /* where the next statement goes */
    int ended;            /* the last line of this block ends the path */
    int then_ended;       /* if/else: the then-branch ended its path */
    int name_mark;
} Block;
static Block blocks[MAX_BLOCKS + 2];
static int nblocks;
static int local_arr_bytes_ok(uint64_t add);
static uint64_t local_arr_bytes;

static void push_stmt(Stmt *s)
{
    Block *b = &blocks[nblocks - 1];
    if (b->ended) refuse(17, "a line after ret, exit or trap in the same block");
    *b->tail = s;
    b->tail = &s->next;
}
static void open_block(int kind, Stmt *owner, Stmt **tail)
{
    if (nblocks > MAX_BLOCKS) refuse(14, "more than %d nested blocks", MAX_BLOCKS);
    Block *b = &blocks[nblocks++];
    b->kind = kind; b->owner = owner; b->tail = tail; b->ended = 0; b->then_ended = 0;
    depth++;
    b->name_mark = depth;
}

static void parse_statement(void)
{
    const char *w = nextw();
    Block *top = &blocks[nblocks - 1];
    if (!strcmp(w, "end")) {
        line_end();
        int ended = top->ended;
        close_scope(depth);
        depth--;
        if (top->kind == 0) {
            if (!ended && curf->ret != T_VOID) refuse(18, "function '%s' returns a value and its end can be reached", curf->name);
            curf->done = 1;
            curf->nslots = nslots;
            nblocks--;
            return;
        }
        nblocks--;
        Block *outer = &blocks[nblocks - 1];
        /* an if/else whose both branches end the path ends it too; a loop or a lone if never does */
        if (top->kind == 2 && ended && top->then_ended) outer->ended = 1;
        return;
    }
    if (!strcmp(w, "else")) {
        line_end();
        if (top->kind != 1) refuse(9, "else without if"); /* spec silent */
        int ended = top->ended;
        Stmt *ifs = top->owner;
        close_scope(depth);
        top->kind = 2; top->ended = 0; top->then_ended = ended;
        top->tail = &ifs->els;
        return;
    }
    if (!strcmp(w, "note")) return;
    if (!strcmp(w, "if") || !strcmp(w, "while")) {
        Stmt *s = new_stmt(w[0] == 'i' ? S_IF : S_WHILE);
        s->v = value_for(T_BOOL, "a condition");
        if (s->kind == S_WHILE) {
            s->n = LOOP_MAX;
            if (peekw() && !strcmp(peekw(), "bound")) {
                wi++;
                const char *b = nextw();
                if (!b) refuse(4, "bound needs a number");
                int neg; uint64_t mag;
                if (!looks_number(b)) refuse(6, "bound needs a number");
                parse_number(b, &neg, &mag);
                if (neg || mag < 1 || mag > LOOP_MAX) refuse(8, "bound %s outside 1..1000000", b);
                s->n = mag;
            }
        }
        line_end();
        push_stmt(s);
        open_block(s->kind == S_IF ? 1 : 3, s, &s->body);
        return;
    }
    if (!strcmp(w, "for")) {
        Stmt *s = new_stmt(S_FOR);
        const char *nmw = nextw();
        const char *tw = nextw();
        if (!nmw || !tw) refuse(4, "for needs a name, a type and a count");
        int t = type_word(tw);
        if (t == T_NONE || !tnum(t)) refuse(7, "for needs a whole-number type"); /* spec silent */
        s->ty = t;
        const char *cw = peekw();
        if (!cw) refuse(4, "for needs a count");
        if (looks_number(cw) && wi + 1 == nw) {
            int neg; uint64_t mag;
            wi++;
            parse_number(cw, &neg, &mag);
            if (!fits(t, neg, mag)) refuse(8, "count %s outside %s", cw, TNAME[t]);
            if (neg || mag > LOOP_MAX) refuse(8, "count %s outside 0..1000000", cw);
            s->count_kind = 0; s->n = mag;
        } else {
            Name *nm = find_name(cw);
            s->count_kind = (nm && nm->kind == K_SCALAR && wi + 1 == nw) ? 1 : 2;
            s->v = value_for(t, "a for count");
        }
        line_end();
        push_stmt(s);
        open_block(4, s, &s->body);
        Name *lv = add_name(nmw, K_SCALAR, t, 0, 1);
        s->slot = lv->slot;
        return;
    }
    if (!strcmp(w, "break")) {
        line_end();
        int inloop = 0;
        for (int i = nblocks - 1; i >= 0; i--) if (blocks[i].kind >= 3) inloop = 1;
        if (!inloop) refuse(9, "break outside a loop"); /* spec silent */
        push_stmt(new_stmt(S_BREAK));
        return;
    }
    if (!strcmp(w, "ret")) {
        Stmt *s = new_stmt(S_RET);
        if (curf->ret == T_VOID) {
            if (peekw()) refuse(7, "a void function returns no value");
        } else {
            if (!peekw()) refuse(7, "ret needs a %s", TNAME[curf->ret]);
            s->v = value_for(curf->ret, "the returned value");
        }
        line_end();
        push_stmt(s);
        blocks[nblocks - 1].ended = 1;
        return;
    }
    if (!strcmp(w, "trap")) {
        Stmt *s = new_stmt(S_TRAP);
        const char *c = nextw();
        if (!c) refuse(4, "trap needs a code");
        int neg; uint64_t mag;
        if (!looks_number(c)) refuse(6, "trap needs a number");
        parse_number(c, &neg, &mag);
        if (neg || mag < 1 || mag > 14) refuse(8, "trap code %s outside 1..14", c);
        s->n = mag;
        line_end();
        push_stmt(s);
        blocks[nblocks - 1].ended = 1;
        return;
    }
    if (!strcmp(w, "exit")) {
        Stmt *s = new_stmt(S_EXIT);
        s->v = value_for(T_I32, "exit's value");
        line_end();
        push_stmt(s);
        blocks[nblocks - 1].ended = 1;
        return;
    }
    if (!strcmp(w, "put")) {
        Stmt *s = new_stmt(S_PUT);
        int ety, ro, kind; uint64_t len;
        s->arr = parse_array_ref(nextw(), &ety, &len, &ro, &kind);
        if (ro) refuse(20, "put into a read-only array");
        nest = 0;
        s->v = parse_value(T_NONE);
        if (s->v->bare) {
            if (s->v->neg || s->v->num >= len) refuse(21, "index outside an array of %" PRIu64, len);
            fix_bare(s->v, T_U64);
        }
        if (!tnum(s->v->ty)) refuse(7, "an index of type %s", TNAME[s->v->ty]);
        s->v2 = value_for(ety, "the stored value");
        line_end();
        push_stmt(s);
        return;
    }
    if (!strcmp(w, "free")) {
        const char *a = nextw();
        if (!a) refuse(4, "free needs an array");
        Name *nm = find_name(a);
        if (!nm || nm->kind != K_OWN) {
            if (nm || find_global(a) >= 0) refuse(7, "free of '%s', not a local array", a); /* spec silent */
            refuse(3, "unknown name '%s'", a);
        }
        line_end();
        push_stmt(new_stmt(S_FREE));
        return;
    }
    if (!strcmp(w, "own")) refuse(9, "own is not in the subset");
    int fi = find_func(w);
    if (fi >= 0) {
        Stmt *s = new_stmt(S_CALL);
        nest = 0;
        s->v = parse_call(fi, 1);
        line_end();
        push_stmt(s);
        return;
    }
    /* <name> [=] <value> */
    if (find_global(w) >= 0) refuse(7, "'%s' is an array; write its elements with put", w); /* spec silent */
    if (!is_name(w) || is_reserved(w)) {
        if (looks_number(w)) refuse(6, "a line cannot start with a number"); /* spec silent */
        refuse(9, "'%s' cannot start a line", w);                       /* spec silent */
    }
    if (peekw() && !strcmp(peekw(), "=")) wi++;
    if (!peekw()) refuse(4, "'%s' needs a value", w);
    Name *nm = find_name(w);
    if (!strcmp(peekw(), "new")) {
        wi++;
        if (nm) refuse(10, "'%s' is already in use", w);
        if (depth != 1) refuse(9, "new only in a function's outermost block");
        const char *tw = nextw(), *lw = nextw();
        if (!tw || !lw) refuse(4, "new needs a type, a length and a value");
        int t = type_word(tw);
        if (t == T_NONE || !tnum(t)) refuse(7, "new needs a whole-number element type");
        int neg; uint64_t mag;
        if (!looks_number(lw)) refuse(16, "new needs its length written as a number");
        parse_number(lw, &neg, &mag);
        if (neg || mag < 1) refuse(8, "array length %s", lw);
        if (mag > MAX_LOCAL_ARR_BYTES / (uint64_t)tbytes(t) || !local_arr_bytes_ok(mag * (uint64_t)tbytes(t)))
            refuse(23, "local arrays over 4 MiB");
        Stmt *s = new_stmt(S_NEW);
        s->ety = t; s->n = mag;
        s->v = value_for(t, "the initial value");
        line_end();
        push_stmt(s);
        Name *a = add_name(w, K_OWN, t, mag, 0);
        s->slot = a->slot;
        return;
    }
    Stmt *s = new_stmt(S_ASSIGN);
    if (nm) {
        if (nm->kind != K_SCALAR) refuse(7, "'%s' is an array", w);
        s->v = value_for(nm->ty, "the value");
        s->slot = nm->slot;
        line_end();
        push_stmt(s);
    } else {
        check_new_name(w, 0);
        s->v = value_for(T_NONE, "a new name's value");
        if (s->v->ty == T_VOID) refuse(7, "a void call has no value");
        line_end();
        push_stmt(s);
        Name *a = add_name(w, K_SCALAR, s->v->ty, 0, 0);
        s->slot = a->slot;
    }
}

static int local_arr_bytes_ok(uint64_t add)
{
    local_arr_bytes += add;
    return local_arr_bytes <= MAX_LOCAL_ARR_BYTES;
}

static void parse_fn_header(void)
{
    if (nfuncs >= MAX_FUNCS) refuse(14, "more than %d functions", MAX_FUNCS);
    const char *nmw = nextw();
    if (!nmw) refuse(4, "fn needs a name");
    check_new_name(nmw, 1);
    Func *f = &funcs[nfuncs];
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", nmw);
    curf = f;
    nnames = 0; nslots = 0; depth = 0; local_arr_bytes = 0;
    int is_main = !strcmp(nmw, "main");
    for (;;) {
        const char *pw = nextw();
        if (!pw) refuse(4, "fn %s needs 'to' and a return type", nmw);
        if (!strcmp(pw, "to")) break;
        if (is_main) refuse(9, "main has no parameters");
        if (f->np >= MAX_PARAMS) refuse(14, "too many parameters");
        check_new_name(pw, 0);
        for (int j = 0; j < nnames; j++) if (!strcmp(names[j].name, pw)) refuse(10, "two parameters named '%s'", pw);
        const char *tw = nextw();
        if (!tw) refuse(4, "parameter '%s' needs a type", pw);
        int i = f->np++;
        if (!strcmp(tw, "view") || !strcmp(tw, "edit")) {
            f->pkind[i] = tw[0] == 'v' ? PK_VIEW : PK_EDIT;
            const char *et = nextw(), *lw = peekw();
            int t = et ? type_word(et) : T_NONE;
            if (t == T_NONE || !tnum(t)) refuse(7, "an array parameter needs a whole-number element type");
            if (!lw || !looks_number(lw)) refuse(16, "array parameter '%s' without its length", pw);
            wi++;
            int neg; uint64_t mag;
            parse_number(lw, &neg, &mag);
            if (neg || mag < 1 || mag > MAX_POOL_ELEMS) refuse(8, "array length %s", lw); /* spec silent on the upper limit */
            f->pty[i] = t; f->plen[i] = mag;
            add_name(pw, f->pkind[i] == PK_VIEW ? K_VIEW : K_EDIT, t, mag, 0);
        } else if (!strcmp(tw, "own")) {
            refuse(9, "own is not in the subset");
        } else {
            int t = type_word(tw);
            if (t == T_NONE) refuse(7, "'%s' is not a type", tw); /* spec silent */
            f->pkind[i] = PK_SCALAR; f->pty[i] = t;
            add_name(pw, K_SCALAR, t, 0, 0);
        }
    }
    const char *rw = nextw();
    if (!rw) refuse(4, "fn %s needs a return type", nmw);
    if (!strcmp(rw, "void")) f->ret = T_VOID;
    else if ((f->ret = type_word(rw)) == T_NONE) refuse(7, "'%s' is not a type", rw);
    if (is_main && f->ret != T_VOID && !tnum(f->ret)) refuse(9, "main returns a whole number or void");
    line_end();
    nfuncs++; /* visible from here on (recursion in program mode) */
    nblocks = 0;
    open_block(0, NULL, &f->body);
}

static void parse_global(int is_text, const char *line, size_t n)
{
    if (!program_mode) refuse(9, "%s only in program mode", is_text ? "text" : "global");
    const char *nmw = nextw();
    if (!nmw) refuse(4, "%s needs a name", is_text ? "text" : "global");
    check_new_name(nmw, 0);
    if (nglobals >= capglobals) {
        capglobals = capglobals ? 2 * capglobals : 16;
        globals = realloc(globals, capglobals * sizeof *globals);
    }
    Global *g = &globals[nglobals];
    memset(g, 0, sizeof *g);
    snprintf(g->name, sizeof g->name, "%s", nmw);
    if (!is_text) {
        const char *tw = nextw(), *lw = nextw();
        int t = tw ? type_word(tw) : T_NONE;
        if (!tw) refuse(4, "global needs a type and a length");
        if (t == T_NONE || !tnum(t)) refuse(7, "a pool of whole numbers"); /* spec silent */
        if (!lw || !looks_number(lw)) refuse(16, "pool '%s' without its length", nmw);
        int neg; uint64_t mag;
        parse_number(lw, &neg, &mag);
        if (neg || mag < 1) refuse(8, "pool length %s", lw); /* spec silent */
        if (mag > MAX_POOL_ELEMS) refuse(23, "pool over 16777216 elements");
        line_end();
        uint64_t bytes = mag * (uint64_t)tbytes(t);
        if (pool_bytes + bytes > MAX_POOL_BYTES) refuse(23, "pools over 1 GiB");
        pool_bytes += bytes;
        g->ety = t; g->len = mag;
        g->arr.data = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (g->arr.data == MAP_FAILED) { perror("omega_ref: pool"); exit(2); }
        g->arr.ety = t; g->arr.len = mag; g->arr.ro = 0;
    } else {
        /* the bytes between the quotes, from the raw line */
        const char *q = memchr(line, '"', n);
        if (!q) refuse(22, "text '%s' needs its bytes in quotes", nmw);
        const char *p = q + 1, *e = line + n;
        uint8_t *buf = malloc(n + 1);
        size_t len = 0;
        int closed = 0;
        while (p < e) {
            char c = *p++;
            if (c == '"') { closed = 1; break; }
            if (c == '\\') {
                if (p >= e) break;
                char x = *p++;
                if (x == 'n') c = 10; else if (x == 't') c = 9; else if (x == '\\') c = 92; else if (x == '"') c = 34;
                else refuse(22, "escape '\\%c' has no meaning", x);
            }
            buf[len++] = (uint8_t)c;
        }
        if (!closed) refuse(22, "text '%s' has no end quote", nmw);
        /* the name must be the only word between text and the quote; nothing after the end quote */
        for (const char *r = p; r < e; r++) if (*r != ' ' && *r != '\t' && *r != '\r') refuse(5, "extra words after a text");
        if (len == 0) refuse(16, "an empty text has no elements"); /* spec silent: decision 19 proposal */
        g->is_text = 1; g->ety = T_U8; g->len = len;
        g->arr.data = buf; g->arr.ety = T_U8; g->arr.len = len; g->arr.ro = 1;
    }
    nglobals++;
}

static void parse_program(void)
{
    /* program mode: a line `fn main` anywhere (9.1) */
    size_t pos = 0;
    while (pos < srclen) {
        const char *ls = src + pos, *nl = memchr(ls, '\n', srclen - pos);
        size_t n = nl ? (size_t)(nl - ls) : srclen - pos;
        split_words(ls, n);
        if (nw >= 2 && !strcmp(W[0], "fn") && !strcmp(W[1], "main")) program_mode = 1;
        pos += n + 1;
    }
    pos = 0;
    cur_line = 0;
    int infn = 0;
    while (pos < srclen) {
        const char *ls = src + pos, *nl = memchr(ls, '\n', srclen - pos);
        size_t n = nl ? (size_t)(nl - ls) : srclen - pos;
        pos += n + 1;
        cur_line++;
        split_words(ls, n);
        if (nw == 0) continue;
        const char *w = W[0];
        if (!infn) {
            wi = 1;
            if (!strcmp(w, "fn")) { parse_fn_header(); infn = 1; }
            else if (!strcmp(w, "global")) parse_global(0, ls, n);
            else if (!strcmp(w, "text")) parse_global(1, ls, n);
            else if (!strcmp(w, "note")) continue;
            else refuse(16, "'%s' outside a function", w);
            continue;
        }
        if (!strcmp(w, "fn") || !strcmp(w, "global") || !strcmp(w, "text"))
            refuse(15, "function '%s' has no end", curf->name);
        parse_statement();
        if (curf->done) infn = 0;
    }
    if (infn) refuse(15, "function '%s' has no end", curf->name);
    if (nfuncs == 0) refuse(16, "no function"); /* spec silent */
}

/* ---------- running ---------- */
typedef struct Frame {
    uint64_t *v;
    Arr **a;
} Frame;

static uint64_t stack_used;
static uint64_t eval(Node *n, Frame *fr);
static Arr *aref(ARef r, Frame *fr) { return r.kind == AK_GLOBAL ? &globals[r.idx].arr : fr->a[r.idx]; }

static uint64_t check_index(int ity, uint64_t i, uint64_t len)
{
    if (tsigned(ity) && (int64_t)i < 0) trap(3);
    if (i >= len) trap(3);
    return i;
}

static uint64_t arith(int op, int t, uint64_t x, uint64_t y)
{
    int w = twidth(t);
    if (t == T_BOOL) {
        switch (op) { case OP_AND: return x & y; case OP_OR: return x | y; default: return x ^ y; }
    }
    if (!tsigned(t)) {
        switch (op) {
        case OP_ADD: return canon(t, x + y);
        case OP_SUB: return canon(t, x - y);
        case OP_MUL: return canon(t, x * y);
        case OP_DIV: if (!y) trap(2); return x / y;
        case OP_REM: if (!y) trap(2); return x % y;
        case OP_AND: return x & y;
        case OP_OR: return x | y;
        case OP_XOR: return x ^ y;
        }
    }
    int64_t a = (int64_t)x, b = (int64_t)y, r;
    __int128 e;
    switch (op) {
    case OP_ADD: e = (__int128)a + b; break;
    case OP_SUB: e = (__int128)a - b; break;
    case OP_MUL: e = (__int128)a * b; break;
    case OP_DIV:
        if (!b) trap(2);
        if (a == smin(t) && b == -1) trap(1);
        return canon(t, (uint64_t)(a / b));
    case OP_REM:
        if (!b) trap(2);
        if (b == -1) return 0;
        return canon(t, (uint64_t)(a % b));
    case OP_AND: return x & y;
    case OP_OR: return x | y;
    case OP_XOR: return x ^ y;
    default: e = 0;
    }
    (void)w;
    if (e < smin(t) || e > smax(t)) trap(1);
    r = (int64_t)e;
    return canon(t, (uint64_t)r);
}

static uint64_t shift(int op, int t, uint64_t x, int st, uint64_t s)
{
    int w = twidth(t);
    if ((tsigned(st) && (int64_t)s < 0) || s >= (uint64_t)w) trap(7);
    if (op == OP_SHR) {
        if (tsigned(t)) return canon(t, (uint64_t)((int64_t)x >> s));
        return x >> s;
    }
    if (!tsigned(t)) return canon(t, x << s);
    int64_t a = (int64_t)x;
    __int128 e = (__int128)a * ((__int128)1 << s);
    if (e < smin(t) || e > smax(t)) trap(1);
    return canon(t, (uint64_t)(int64_t)e);
}

static uint64_t compare(int op, int t, uint64_t x, uint64_t y)
{
    int lt, eq = x == y;
    if (tsigned(t)) lt = (int64_t)x < (int64_t)y; else lt = x < y;
    switch (op) {
    case OP_EQ: return eq;
    case OP_NE: return !eq;
    case OP_LT: return lt;
    case OP_LE: return lt || eq;
    case OP_GT: return !lt && !eq;
    default: return !lt;
    }
}

static uint64_t convert(int to, int from, uint64_t x)
{
    if (tsigned(from) && (int64_t)x < 0) {
        if (!tsigned(to) || (int64_t)x < smin(to)) trap(5);
        return x;
    }
    if (x > (tsigned(to) ? (uint64_t)smax(to) : umax(to))) trap(5);
    return x;
}

static uint64_t call_fn(int fi, uint64_t *sargs, Arr **aargs);

static uint64_t eval(Node *n, Frame *fr)
{
    uint64_t x, y, z;
    switch (n->kind) {
    case N_NUM: return n->num;
    case N_VAR: return fr->v[n->slot];
    case N_BIN:
        x = eval(n->a, fr);
        y = eval(n->b, fr);
        if (n->op == OP_SHL || n->op == OP_SHR) return shift(n->op, n->ty, x, n->b->ty, y);
        return arith(n->op, n->ty, x, y);
    case N_CMP:
        x = eval(n->a, fr);
        y = eval(n->b, fr);
        return compare(n->op, n->a->ty, x, y);
    case N_NEG:
        x = eval(n->a, fr);
        if (tsigned(n->ty)) { if ((int64_t)x == smin(n->ty)) trap(1); return canon(n->ty, (uint64_t)-(int64_t)x); }
        return canon(n->ty, (uint64_t)0 - x);
    case N_NOT:
        x = eval(n->a, fr);
        return n->ty == T_BOOL ? !x : canon(n->ty, ~x);
    case N_CONV:
        x = eval(n->a, fr);
        return convert(n->ty, n->a->ty, x);
    case N_GET: {
        Arr *a = aref(n->arr, fr);
        x = eval(n->a, fr);
        return arr_get(a, check_index(n->a->ty, x, a->len));
    }
    case N_LEN: return n->num;
    case N_READ: case N_WRITE: {
        Arr *a = aref(n->arr, fr);
        x = eval(n->a, fr);
        y = eval(n->b, fr);
        z = eval(n->c, fr);
        if (!(z <= a->len && y <= a->len - z)) trap(3);
        ssize_t r = n->kind == N_READ ? read((int)(int32_t)x, a->data + y, z) : write((int)(int32_t)x, a->data + y, z);
        return r < 0 ? (uint64_t)(int64_t)-errno : (uint64_t)r;
    }
    case N_CALL: {
        Func *f = &funcs[n->fn];
        uint64_t sargs[MAX_PARAMS];
        Arr *aargs[MAX_PARAMS];
        for (int i = 0; i < f->np; i++) {
            if (f->pkind[i] == PK_SCALAR) { sargs[i] = eval(n->args[i], fr); aargs[i] = NULL; }
            else aargs[i] = aref(n->aargs[i], fr);
        }
        return call_fn(n->fn, sargs, aargs);
    }
    }
    fprintf(stderr, "omega_ref: internal: node kind %d\n", n->kind);
    exit(2);
}

enum { C_NORMAL, C_BREAK, C_RET };

static int exec(Stmt *s, Frame *fr, uint64_t *ret, uint64_t *frame_extra)
{
    for (; s; s = s->next) {
        switch (s->kind) {
        case S_ASSIGN: fr->v[s->slot] = eval(s->v, fr); break;
        case S_NEW: {
            uint64_t bytes = s->n * (uint64_t)tbytes(s->ety);
            uint64_t charge = (bytes + 15) & ~(uint64_t)15;
            if (stack_used + charge > STACK_BYTES) trap(6);
            uint64_t init = eval(s->v, fr);
            stack_used += charge; *frame_extra += charge;
            Arr *a = malloc(sizeof *a);
            a->data = malloc(bytes ? bytes : 1);
            a->ety = s->ety; a->len = s->n; a->ro = 0;
            for (uint64_t i = 0; i < s->n; i++) arr_put(a, i, init);
            fr->a[s->slot] = a;
            break;
        }
        case S_IF: {
            int c = exec(eval(s->v, fr) ? s->body : s->els, fr, ret, frame_extra);
            if (c != C_NORMAL) return c;
            break;
        }
        case S_WHILE: {
            uint64_t passes = 0;
            while (eval(s->v, fr)) {
                if (passes >= s->n) trap(4);
                passes++;
                int c = exec(s->body, fr, ret, frame_extra);
                if (c == C_BREAK) break;
                if (c == C_RET) return c;
            }
            break;
        }
        case S_FOR: {
            uint64_t count = 0, passes = 0;
            if (s->count_kind == 0) count = s->n;
            else if (s->count_kind == 2) count = eval(s->v, fr);
            fr->v[s->slot] = 0;
            for (;;) {
                if (s->count_kind == 1) count = eval(s->v, fr);
                if (!compare(OP_LT, s->ty, fr->v[s->slot], count)) break;
                if (s->count_kind != 0) { if (passes >= LOOP_MAX) trap(4); passes++; }
                int c = exec(s->body, fr, ret, frame_extra);
                if (c == C_BREAK) break;
                if (c == C_RET) return c;
                fr->v[s->slot] = arith(OP_ADD, s->ty, fr->v[s->slot], 1);
            }
            break;
        }
        case S_BREAK: return C_BREAK;
        case S_RET: if (s->v) *ret = eval(s->v, fr); return C_RET;
        case S_PUT: {
            Arr *a = aref(s->arr, fr);
            uint64_t i = eval(s->v, fr);
            uint64_t v = eval(s->v2, fr);
            arr_put(a, check_index(s->v->ty, i, a->len), v);
            break;
        }
        case S_FREE: break;
        case S_TRAP: trap((int)s->n); break;
        case S_EXIT: {
            uint64_t v = eval(s->v, fr);
            _exit((int)(v & 255));
        }
        case S_CALL: eval(s->v, fr); break;
        }
    }
    return C_NORMAL;
}

static uint64_t call_fn(int fi, uint64_t *sargs, Arr **aargs)
{
    Func *f = &funcs[fi];
    uint64_t charge = f->has_call ? 16 : 0; /* the smallest frame a non-leaf call can have (spec silent) */
    if (program_mode && stack_used + charge > STACK_BYTES) trap(6);
    stack_used += charge;
    uint64_t vals[f->nslots + 1];
    Arr *arrs[f->nslots + 1];
    Arr views[MAX_PARAMS];
    memset(vals, 0, sizeof vals);
    memset(arrs, 0, sizeof arrs);
    Frame fr = {vals, arrs};
    for (int i = 0; i < f->np; i++) {
        if (f->pkind[i] == PK_SCALAR) vals[i] = sargs[i];
        else {
            views[i] = *aargs[i];
            if (f->pkind[i] == PK_VIEW) views[i].ro = 1;
            arrs[i] = &views[i];
        }
    }
    uint64_t ret = 0, extra = 0;
    exec(f->body, &fr, &ret, &extra);
    for (int i = f->np; i < f->nslots; i++) if (arrs[i]) { free(arrs[i]->data); free(arrs[i]); }
    stack_used -= charge + extra;
    return ret;
}

/* ---------- driver ---------- */
static int argc_g;
static char **argv_g;
static int result;

static void load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    src = malloc(MAX_SRC + 2);
    srclen = fread(src, 1, MAX_SRC + 1, f);
    fclose(f);
    if (srclen > MAX_SRC) { cur_line = 0; refuse(14, "input over 8 MiB - 1 bytes"); }
    W = malloc((srclen + 2) * sizeof *W);
    wbuf = malloc(2 * srclen + 4);
    if (!W || !wbuf) { perror("omega_ref"); exit(2); }
}

static void *run_main(void *unused)
{
    (void)unused;
    const char *cmd = argv_g[1];
    if (!strcmp(cmd, "check")) {
        load(argv_g[2]);
        parse_program();
        result = 0;
        return NULL;
    }
    if (!strcmp(cmd, "run")) {
        load(argv_g[2]);
        refuse_exit = 125; /* a refused program exits 125, not its refusal number */
        parse_program();
        static int mi; mi = find_func("main");
        if (!program_mode || mi < 0) { fprintf(stderr, "omega_ref: run needs fn main\n"); exit(125); }
        if (setjmp(trap_jb)) { result = 100 + trap_code; return NULL; }
        uint64_t v = call_fn(mi, NULL, NULL);
        result = funcs[mi].ret == T_VOID ? 0 : (int)(v & 255);
        return NULL;
    }
    if (!strcmp(cmd, "call")) {
        load(argv_g[2]);
        parse_program();
        static int fi; fi = find_func(argv_g[3]);
        if (fi < 0) { fprintf(stderr, "omega_ref: no function %s\n", argv_g[3]); exit(2); }
        Func *f = &funcs[fi];
        FILE *in = fopen(argv_g[4], "r");
        if (!in) { perror(argv_g[4]); exit(2); }
        char line[512];
        while (fgets(line, sizeof line, in)) {
            uint64_t a[MAX_PARAMS] = {0};
            char *p = line;
            for (int i = 0; i < f->np; i++) {
                if (f->pkind[i] != PK_SCALAR) { fprintf(stderr, "omega_ref: call with an array parameter\n"); exit(2); }
                a[i] = tsigned(f->pty[i]) ? (uint64_t)strtoll(p, &p, 10) : strtoull(p, &p, 10);
                if (a[i] != canon(f->pty[i], a[i])) { fprintf(stderr, "omega_ref: input out of range: %s", line); exit(2); }
            }
            stack_used = 0;
            if (setjmp(trap_jb)) { printf("TRAP %d\t-\n", trap_code); continue; }
            uint64_t r = call_fn(fi, a, NULL);
            if (tsigned(f->ret)) printf("%" PRId64, (int64_t)r); else printf("%" PRIu64, r);
            printf("\t0x%016" PRIx64 "\n", r);
        }
        fflush(stdout);
        result = 0;
        return NULL;
    }
    fprintf(stderr, "omega_ref: unknown command %s\n", cmd);
    exit(2);
}

int main(int argc, char **argv)
{
    argc_g = argc; argv_g = argv;
    if (argc < 3 || (!strcmp(argv[1], "call") && argc != 5)) {
        fprintf(stderr, "usage: omega_ref check|run <prog> | omega_ref call <prog> <entry> <inputs>\n");
        return 2;
    }
    /* a big thread stack: deep Omega recursion becomes deep C recursion here */
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, (size_t)4 << 30);
    pthread_t th;
    if (pthread_create(&th, &at, run_main, NULL)) { perror("omega_ref: thread"); return 2; }
    pthread_join(th, NULL);
    fflush(stdout);
    _exit(result);
}
