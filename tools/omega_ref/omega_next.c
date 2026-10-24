/*
 * omega_next.c -- DECISION-22 CHECK for the om4 language. An ORACLE ONLY, outside the seed chain.
 * It reads a grammar over word classes (tools/omega_ref/s9.grammar) and follows a program left to right
 * with every rule that could apply, never choosing early: after each word it counts how many ways the
 * words so far can be read (live parses). Decision 22 (docs/PROGRAM.md): exactly one, at every prefix.
 *
 *   omega_next next <grammar> <prefix-file>      the legal next items after the prefix (6.2 style lines)
 *   omega_next accepts <grammar> <file>          exit 0 when the whole file parses (one parse), else 1
 *   omega_next fuzz <grammar> <file>...          every word prefix and every byte prefix of each file:
 *                                                one parse, the real next word legal, a cut word extendable
 *   omega_next mutants <file> <seed> <n> <dir>   n single-word mutants (drop, double, replace) into dir
 *
 * The grammar is about words and names, not types: a program it accepts can still be refused for a type
 * (error 7) or a capacity. Word classes come from the names in scope, kept by the grammar's ! actions.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *m, const char *x) { fprintf(stderr, "omega_next: %s%s\n", m, x ? x : ""); exit(2); }

/* ---------- grammar ---------- */
enum { Y_NT, Y_KW, Y_CLS, Y_ACT, Y_ARGS };
typedef struct { char name[24]; int kind; int nalt; int alt[16][24]; int altn[16]; int defined; } Sym;
static Sym syms[256];
static int nsyms;
static int S_value, S_arrarg, S_program;
enum { C_NUM, C_TYPE, C_OP2, C_OP1, C_VAR, C_ARR, C_POOL, C_FNV, C_FNVOID, C_NEWNAME, C_STR, C_REST, C_NL, C_EOF, C_FNNAME, NCLS };
static const char *CLSN[] = {"NUM", "TYPE", "OP2", "OP1", "VAR", "ARR", "POOL", "FNV", "FNVOID", "NEWNAME", "STR", "REST", "NL", "EOF", "FNNAME"};
static int cls_sym[NCLS];

static int sym(const char *name, int kind)
{
    for (int i = 0; i < nsyms; i++) if (!strcmp(syms[i].name, name)) return i;
    if (nsyms >= 256) die("too many symbols", NULL);
    snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", name);
    syms[nsyms].kind = kind;
    return nsyms++;
}

static void load_grammar(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open grammar ", path);
    for (int c = 0; c < NCLS; c++) cls_sym[c] = sym(CLSN[c], Y_CLS);
    char line[1024];
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        char *tok = strtok(p, " \t\n");
        if (strcmp(tok, "|")) {
            cur = sym(tok, Y_NT);
            if (syms[cur].kind != Y_NT) die("not a rule name: ", tok);
            if (syms[cur].defined) die("rule defined twice: ", tok);
            syms[cur].defined = 1;
            tok = strtok(NULL, " \t\n");
            if (!tok || strcmp(tok, "->")) die("expected -> after ", syms[cur].name);
        } else if (cur < 0) die("| before any rule", NULL);
        Sym *s = &syms[cur];
        int a = s->nalt++;
        if (a >= 16) die("too many alternatives in ", s->name);
        s->altn[a] = 0;
        while ((tok = strtok(NULL, " \t\n"))) {
            if (!strcmp(tok, "|")) { a = s->nalt++; if (a >= 16) die("too many alternatives in ", s->name); s->altn[a] = 0; continue; }
            int id;
            if (!strcmp(tok, "_")) continue;
            if (tok[0] == '\'') { tok[strlen(tok) - 1] = 0; id = sym(tok + 1, Y_KW); if (syms[id].kind != Y_KW) die("fixed word clashes: ", tok); }
            else if (tok[0] == '!') id = sym(tok, Y_ACT);
            else if (tok[0] == '@') id = sym(tok, Y_ARGS);
            else if (tok[0] >= 'A' && tok[0] <= 'Z') { id = sym(tok, Y_CLS); if (id >= NCLS) die("unknown word class: ", tok); }
            else id = sym(tok, Y_NT);
            if (s->altn[a] >= 24) die("alternative too long in ", s->name);
            s->alt[a][s->altn[a]++] = id;
        }
    }
    fclose(f);
    for (int i = 0; i < nsyms; i++) if (syms[i].kind == Y_NT && !syms[i].defined) die("rule never defined: ", syms[i].name);
    S_value = sym("value", Y_NT); S_arrarg = sym("arrarg", Y_NT); S_program = sym("program", Y_NT);
}

/* ---------- words of a source ---------- */
typedef struct { const char *t; int len; int kind; long off; } Word; /* kind: 0 word, 1 STR, 2 REST, 3 NL, 4 EOF, 5 the name after fn */
static Word *words;
static int nwords;

static void lex(const char *src, long n, int final)
{
    words = realloc(words, sizeof *words * (size_t)(n + 4));
    nwords = 0;
    long i = 0;
    while (i < n) {
        long ls = i, le = i;
        while (le < n && src[le] != '\n') le++;
        int first = 1, idx = 0;
        const char *fw = NULL; int fwl = 0;
        long j = ls;
        while (j < le) {
            char c = src[j];
            if (c == ' ' || c == '\t' || c == '\r') { j++; continue; }
            /* a text line: the bytes from the first quote on are one STR word; a note line: the rest is REST */
            if (idx == 2 && fw && fwl == 4 && !strncmp(fw, "text", 4) && c == '"') {
                long e = le; while (e > j && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r')) e--;
                words[nwords++] = (Word){src + j, (int)(e - j), 1, j}; j = le; break;
            }
            if (idx == 1 && fw && fwl == 4 && !strncmp(fw, "note", 4)) {
                long e = le; while (e > j && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r')) e--;
                words[nwords++] = (Word){src + j, (int)(e - j), 2, j}; j = le; break;
            }
            long s = j;
            if (c == '(' || c == ')') j++;
            else while (j < le && src[j] != ' ' && src[j] != '\t' && src[j] != '\r' && src[j] != '(' && src[j] != ')') j++;
            /* the word after fn at the start of a line is the function's name (the names pass, 9.2) */
            int kind = (idx == 1 && fw && fwl == 2 && !strncmp(fw, "fn", 2)) ? 5 : 0;
            words[nwords++] = (Word){src + s, (int)(j - s), kind, s};
            if (first) { fw = src + s; fwl = (int)(j - s); first = 0; }
            idx++;
        }
        if (le < n) words[nwords++] = (Word){src + le, 1, 3, le};
        else if (final && nwords && words[nwords - 1].kind != 3) words[nwords++] = (Word){src + le, 0, 3, le};
        i = le + 1;
    }
    if (final) words[nwords++] = (Word){src + n, 0, 4, n};
}

/* ---------- names in scope (driven by the ! actions) ---------- */
typedef struct { char name[64]; int np; int pk[64]; int retvoid; } Fn;
static Fn fns[1024];
static int nfns, curfn = -1;
typedef struct { char name[64]; int cls, level; } Nm;
static Nm nms[8192];
static int nnms, level;
static char lastnew[64];
static int late_actions;

static const char *TYPEW[] = {"bool", "uchar", "ushort", "uint", "ulong", "byte", "short", "int", "long",
                              "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", NULL};
static const char *OP2W[] = {"add", "sub", "mul", "div", "rem", "and", "or", "xor", "shl", "shr",
                             "eq", "ne", "lt", "le", "gt", "ge", NULL};
static const char *OP1W[] = {"neg", "not", NULL};
/* the fixed words of sections 3 and 9, from the spec and not from the grammar, so a grammar that loses a rule
   cannot turn its word into a name */
static const char *OTHER_RESERVED[] = {"fn", "to", "void", "end", "if", "else", "while", "bound", "for", "break", "ret",
                                       "put", "free", "trap", "global", "text", "new", "view", "edit", "own", "get",
                                       "len", "read", "write", "exit", "note", "true", "false", "=", "(", ")", NULL};
static int in(const char **l, const char *w, int n)
{
    for (int i = 0; l[i]; i++) if ((int)strlen(l[i]) == n && !strncmp(l[i], w, n)) return 1;
    return 0;
}
static int kw_sym(const char *w, int n)
{
    for (int i = 0; i < nsyms; i++)
        if (syms[i].kind == Y_KW && (int)strlen(syms[i].name) == n && !strncmp(syms[i].name, w, n)) return i;
    return -1;
}
static int reserved(const char *w, int n)
{
    return kw_sym(w, n) >= 0 || in(TYPEW, w, n) || in(OP2W, w, n) || in(OP1W, w, n) || in(OTHER_RESERVED, w, n);
}
static int ident(const char *w, int n)
{
    if (n < 1 || n > 63) return 0;
    if (!((w[0] >= 'a' && w[0] <= 'z') || (w[0] >= 'A' && w[0] <= 'Z') || w[0] == '_')) return 0;
    for (int i = 1; i < n; i++)
        if (!((w[i] >= 'a' && w[i] <= 'z') || (w[i] >= 'A' && w[i] <= 'Z') || (w[i] >= '0' && w[i] <= '9') || w[i] == '_')) return 0;
    return 1;
}
static int number(const char *w, int n) /* 6.1 spelling: optional minus, digits, no leading zero, no -0 */
{
    int i = 0;
    if (n && w[0] == '-') i = 1;
    if (i >= n) return 0;
    if (w[i] == '0' && (n - i > 1 || i == 1)) return 0;
    for (int k = i; k < n; k++) if (w[k] < '0' || w[k] > '9') return 0;
    return 1;
}
static int find_fn(const char *w, int n)
{
    for (int i = 0; i < nfns; i++) if ((int)strlen(fns[i].name) == n && !strncmp(fns[i].name, w, n)) return i;
    return -1;
}
static Nm *find_nm(const char *w, int n)
{
    for (int i = nnms - 1; i >= 0; i--) if ((int)strlen(nms[i].name) == n && !strncmp(nms[i].name, w, n)) return &nms[i];
    return NULL;
}
/* the terminal symbol a word is; *fi the function for FNV/FNVOID; -1 when it is no word of the language */
static int classify(const Word *w, int *fi)
{
    *fi = -1;
    if (w->kind == 1) return cls_sym[C_STR];
    if (w->kind == 2) return cls_sym[C_REST];
    if (w->kind == 3) return cls_sym[C_NL];
    if (w->kind == 4) return cls_sym[C_EOF];
    if (w->kind == 5) return ident(w->t, w->len) && !reserved(w->t, w->len) ? cls_sym[C_FNNAME] : -1;
    int k = kw_sym(w->t, w->len);
    if (k >= 0) return k;
    if (in(OTHER_RESERVED, w->t, w->len)) return -1; /* a fixed word no rule takes */
    if (in(TYPEW, w->t, w->len)) return cls_sym[C_TYPE];
    if (in(OP2W, w->t, w->len)) return cls_sym[C_OP2];
    if (in(OP1W, w->t, w->len)) return cls_sym[C_OP1];
    if (number(w->t, w->len)) return cls_sym[C_NUM];
    if (!ident(w->t, w->len) || reserved(w->t, w->len)) return -1;
    Nm *nm = find_nm(w->t, w->len);
    if (nm) return cls_sym[nm->cls];
    int f = find_fn(w->t, w->len);
    if (f >= 0) { *fi = f; return cls_sym[fns[f].retvoid ? C_FNVOID : C_FNV]; }
    return cls_sym[C_NEWNAME];
}
static void add_nm(int cls)
{
    if (nnms >= 8192) die("too many names", NULL);
    snprintf(nms[nnms].name, sizeof nms[nnms].name, "%s", lastnew);
    nms[nnms].cls = cls; nms[nnms].level = level;
    nnms++;
}
/* The names pass (9.2): every function of the unit is known before any body is read, so a call may name a
 * function written later. The fn lines are read here, from the lexed words, before the parse starts;
 * the grammar's !fn then finds the function instead of adding it (and its !param actions fill the
 * parameters again, to the same values). */
static void prescan_fns(void)
{
    nfns = 0;
    for (int i = 0; i < nwords; i++) {
        const Word *w = &words[i];
        if (w->kind != 5 || !ident(w->t, w->len) || reserved(w->t, w->len) || find_fn(w->t, w->len) >= 0) continue;
        if (nfns >= 1024) die("too many functions", NULL);
        Fn *f = &fns[nfns++]; memset(f, 0, sizeof *f);
        snprintf(f->name, sizeof f->name, "%.*s", w->len, w->t);
        int j = i + 1;
        while (j < nwords && words[j].kind == 0) {
            if (words[j].len == 2 && !strncmp(words[j].t, "to", 2)) {
                j++;
                if (j < nwords && words[j].kind == 0) f->retvoid = words[j].len == 4 && !strncmp(words[j].t, "void", 4);
                break;
            }
            j++; /* a parameter: NAME TYPE, or NAME view|edit TYPE NUM */
            if (j >= nwords || words[j].kind != 0) break;
            int arr = words[j].len == 4 && (!strncmp(words[j].t, "view", 4) || !strncmp(words[j].t, "edit", 4));
            if (f->np < 64) f->pk[f->np++] = arr;
            j += arr ? 3 : 1;
        }
    }
}
static void act(int a)
{
    const char *n = syms[a].name;
    if (!strcmp(n, "!fn")) {
        int f = find_fn(lastnew, (int)strlen(lastnew));
        if (f < 0) {
            if (nfns >= 1024) die("too many functions", NULL);
            f = nfns++; memset(&fns[f], 0, sizeof fns[f]);
            snprintf(fns[f].name, sizeof fns[f].name, "%s", lastnew);
        }
        fns[f].np = 0; /* the !param actions fill them again */
        curfn = f; level++;
    } else if (!strcmp(n, "!param_s") || !strcmp(n, "!param_a")) {
        Fn *f = &fns[curfn];
        if (f->np < 64) f->pk[f->np++] = n[7] == 'a';
        add_nm(n[7] == 'a' ? C_ARR : C_VAR);
    } else if (!strcmp(n, "!ret_val")) fns[curfn].retvoid = 0;
    else if (!strcmp(n, "!ret_void")) fns[curfn].retvoid = 1;
    else if (!strcmp(n, "!open")) level++;
    else if (!strcmp(n, "!close") || !strcmp(n, "!fnend")) {
        while (nnms > 0 && nms[nnms - 1].level >= level) nnms--;
        level--;
    } else if (!strcmp(n, "!pool")) add_nm(C_POOL);
    else if (!strcmp(n, "!text")) add_nm(C_ARR);
    else if (!strcmp(n, "!bind_s") || !strcmp(n, "!bind_loop")) add_nm(C_VAR);
    else if (!strcmp(n, "!bind_a")) add_nm(C_ARR);
    else die("unknown action ", n);
}

/* ---------- live parses ---------- */
typedef struct { int *s; int n; int *acts; int na; } Cfg;
static Cfg cfg_copy(const Cfg *c, int extra)
{
    Cfg d;
    d.s = malloc(sizeof(int) * (size_t)(c->n + extra + 1)); memcpy(d.s, c->s, sizeof(int) * (size_t)c->n); d.n = c->n;
    d.acts = malloc(sizeof(int) * (size_t)(c->na + 64)); memcpy(d.acts, c->acts, sizeof(int) * (size_t)c->na); d.na = c->na;
    return d;
}
static void cfg_free(Cfg *c) { free(c->s); free(c->acts); }

static Cfg live[64];
static int nlive;
static long max_live;

static void reset(void)
{
    for (int i = 0; i < nlive; i++) cfg_free(&live[i]);
    nlive = 1;
    live[0].s = malloc(sizeof(int) * 4); live[0].s[0] = S_program; live[0].n = 1;
    live[0].acts = malloc(sizeof(int) * 64); live[0].na = 0;
    nfns = 0; curfn = -1; nnms = 0; level = 0; lastnew[0] = 0; late_actions = 0;
    prescan_fns();
}

/* every live parse takes the word, trying every rule; returns how many parses read it */
static int step(const Word *w)
{
    int fi, t = classify(w, &fi);
    Cfg out[64]; int nout = 0;
    Cfg *work = malloc(sizeof(Cfg) * 4096); int nwork = 0;
    for (int i = 0; i < nlive; i++) work[nwork++] = live[i];
    nlive = 0;
    long budget = 100000;
    while (nwork > 0) {
        Cfg c = work[--nwork];
        if (--budget < 0) die("parse search too large (left recursion?)", NULL);
        if (c.n == 0 || t < 0) { cfg_free(&c); continue; }
        int top = c.s[c.n - 1];
        Sym *y = &syms[top];
        if (y->kind == Y_ACT) { c.n--; c.acts[c.na++] = top; work[nwork++] = c; continue; }
        if (y->kind == Y_NT) {
            for (int a = 0; a < y->nalt; a++) {
                Cfg d = cfg_copy(&c, y->altn[a]);
                d.n--;
                for (int k = y->altn[a] - 1; k >= 0; k--) d.s[d.n++] = y->alt[a][k];
                if (nwork >= 4096) die("too many parses", NULL);
                work[nwork++] = d;
            }
            cfg_free(&c);
            continue;
        }
        if (y->kind == Y_ARGS) die("@args with no function before it", NULL);
        if (top != t) { cfg_free(&c); continue; }
        c.n--;
        if (nout < 64) out[nout++] = c; else cfg_free(&c);
    }
    free(work);
    if (nout > max_live) max_live = nout;
    if (nout != 1) { for (int i = 0; i < nout; i++) live[nlive++] = out[i]; return nout; }
    Cfg *c = &out[0];
    if (c->na) late_actions++; /* an action reached only through an empty rule: it ran after this word was classified */
    for (int i = 0; i < c->na; i++) act(c->acts[i]);
    c->na = 0;
    if (t == cls_sym[C_NEWNAME] || t == cls_sym[C_FNNAME]) snprintf(lastnew, sizeof lastnew, "%.*s", w->len, w->t);
    if ((t == cls_sym[C_FNV] || t == cls_sym[C_FNVOID])) {
        if (c->n == 0 || syms[c->s[c->n - 1]].kind != Y_ARGS) die("a function word not followed by @args", NULL);
        c->n--;
        Fn *f = &fns[fi];
        c->s = realloc(c->s, sizeof(int) * (size_t)(c->n + f->np + 1));
        for (int k = f->np - 1; k >= 0; k--) c->s[c->n++] = f->pk[k] ? S_arrarg : S_value;
    }
    while (c->n > 0 && syms[c->s[c->n - 1]].kind == Y_ACT) act(c->s[--c->n]);
    live[nlive++] = *c;
    return 1;
}

/* ---------- the legal next items of the one live parse ---------- */
static int first[256][256], nullable[256];
static void compute_first(void)
{
    for (int ch = 1; ch;) {
        ch = 0;
        for (int i = 0; i < nsyms; i++) {
            if (syms[i].kind != Y_NT) continue;
            for (int a = 0; a < syms[i].nalt; a++) {
                int all = 1;
                for (int k = 0; k < syms[i].altn[a] && all; k++) {
                    int x = syms[i].alt[a][k];
                    if (syms[x].kind == Y_ACT) continue;
                    if (syms[x].kind == Y_NT) {
                        for (int z = 0; z < nsyms; z++) if (first[x][z] && !first[i][z]) first[i][z] = ch = 1;
                        if (!nullable[x]) all = 0;
                    } else { if (!first[i][x]) first[i][x] = ch = 1; all = 0; }
                }
                if (all && !nullable[i]) nullable[i] = ch = 1;
            }
        }
    }
}
static void legal_set(int *set)
{
    memset(set, 0, sizeof(int) * 256);
    if (nlive != 1) return;
    Cfg *c = &live[0];
    for (int i = c->n - 1; i >= 0; i--) {
        int x = c->s[i];
        if (syms[x].kind == Y_ACT) continue;
        if (syms[x].kind == Y_NT) { for (int z = 0; z < nsyms; z++) if (first[x][z]) set[z] = 1; if (!nullable[x]) return; continue; }
        set[x] = 1;
        return;
    }
}
static int has_prefix(const char *s, const char *u, int un) { return (int)strlen(s) >= un && !strncmp(s, u, un); }
/* can the cut word u (un bytes) still become a legal item? */
static int extendable(const int *set, const char *u, int un, int kind)
{
    if (kind == 1) return set[cls_sym[C_STR]] && u[0] == '"';
    if (kind == 2) return set[cls_sym[C_REST]];
    for (int i = 0; i < nsyms; i++) if (set[i] && syms[i].kind == Y_KW && has_prefix(syms[i].name, u, un)) return 1;
    const char **lists[] = {TYPEW, OP2W, OP1W};
    int lc[] = {C_TYPE, C_OP2, C_OP1};
    for (int l = 0; l < 3; l++) if (set[cls_sym[lc[l]]]) for (int i = 0; lists[l][i]; i++) if (has_prefix(lists[l][i], u, un)) return 1;
    if (set[cls_sym[C_NUM]]) {
        int ok = 1, i = 0;
        if (u[0] == '-') i = 1;
        if (i < un && u[i] == '0' && (un - i > 1 || i == 1)) ok = 0;
        for (int k = i; k < un; k++) if (u[k] < '0' || u[k] > '9') ok = 0;
        if (ok) return 1;
    }
    for (int i = 0; i < nnms; i++) if (set[cls_sym[nms[i].cls]] && has_prefix(nms[i].name, u, un)) return 1;
    for (int i = 0; i < nfns; i++) if (set[cls_sym[fns[i].retvoid ? C_FNVOID : C_FNV]] && has_prefix(fns[i].name, u, un)) return 1;
    if ((set[cls_sym[C_NEWNAME]] || set[cls_sym[C_FNNAME]]) && ident(u, un)) return 1; /* a longer name is always new */
    return 0;
}
static void print_set(const int *set, const char *u, int un)
{
#define P(s) do { if (has_prefix((s), u, un)) printf("word %s\n", (s)); } while (0)
    for (int i = 0; i < nsyms; i++) if (set[i] && syms[i].kind == Y_KW) P(syms[i].name);
    const char **lists[] = {TYPEW, OP2W, OP1W};
    int lc[] = {C_TYPE, C_OP2, C_OP1};
    for (int l = 0; l < 3; l++) if (set[cls_sym[lc[l]]]) for (int i = 0; lists[l][i]; i++) P(lists[l][i]);
    for (int i = 0; i < nnms; i++) if (set[cls_sym[nms[i].cls]] && find_nm(nms[i].name, (int)strlen(nms[i].name)) == &nms[i]) P(nms[i].name);
    for (int i = 0; i < nfns; i++) if (set[cls_sym[fns[i].retvoid ? C_FNVOID : C_FNV]]) P(fns[i].name);
    if ((set[cls_sym[C_NEWNAME]] || set[cls_sym[C_FNNAME]]) && (!un || ident(u, un))) printf("newname\n");
    int only[256]; memset(only, 0, sizeof only); only[cls_sym[C_NUM]] = 1;
    if (set[cls_sym[C_NUM]] && (!un || extendable(only, u, un, 0))) printf("number\n");
    if (un) return;
    if (set[cls_sym[C_STR]]) printf("text\n");
    if (set[cls_sym[C_REST]]) printf("rest\n");
    if (set[cls_sym[C_NL]]) printf("newline\n");
    if (set[cls_sym[C_EOF]]) printf("eof\n");
}

/* ---------- commands ---------- */
static char *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) die("cannot open ", p);
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)*n + 1);
    if ((long)fread(b, 1, (size_t)*n, f) != *n) die("short read ", p);
    b[*n] = 0; fclose(f);
    return b;
}

static int cmd_next(const char *path)
{
    long n; char *src = slurp(path, &n);
    /* the word still being written (no space after it) is the cut word */
    long cut = n;
    while (cut > 0 && src[cut - 1] != ' ' && src[cut - 1] != '\t' && src[cut - 1] != '\n' && src[cut - 1] != '(' && src[cut - 1] != ')') cut--;
    lex(src, cut, 0);
    reset();
    for (int i = 0; i < nwords; i++)
        if (step(&words[i]) != 1) { printf("REFUSED omega_next: '%.*s' at byte %ld: %s\n", words[i].len, words[i].t, words[i].off, nlive ? "more than one parse" : "no rule takes this word"); return 1; }
    int set[256]; legal_set(set);
    if (cut < n) {
        int kind = 0; /* the cut word may be a text's bytes or a note's rest */
        if (set[cls_sym[C_STR]] && src[cut] == '"') kind = 1; else if (set[cls_sym[C_REST]]) kind = 2;
        if (!extendable(set, src + cut, (int)(n - cut), kind)) { printf("REFUSED omega_next: no legal word starts with '%.*s'\n", (int)(n - cut), src + cut); return 1; }
        printf("cut %.*s\n", (int)(n - cut), src + cut);
    }
    print_set(set, src + cut, (int)(n - cut));
    return 0;
}

static int cmd_accepts(const char *path, int quiet)
{
    long n; char *src = slurp(path, &n);
    lex(src, n, 1);
    reset();
    for (int i = 0; i < nwords; i++) {
        int k = step(&words[i]);
        if (k != 1) {
            if (!quiet) fprintf(stderr, "omega_next: %s: byte %ld '%.*s': %d parses\n", path, words[i].off, words[i].len, words[i].t, k);
            return k == 0 ? 1 : 3;
        }
    }
    free(src);
    return 0;
}

/* every word prefix: exactly one live parse, the next word legal; every byte prefix: the cut word extendable */
static int cmd_fuzz(int argc, char **argv)
{
    int bad = 0;
    long tw = 0, tb = 0;
    for (int f = 0; f < argc; f++) {
        long n; char *src = slurp(argv[f], &n);
        lex(src, n, 1);
        reset();
        long bytes = 0; int stop = -1, why = 0;
        int set[256];
        for (int i = 0; i < nwords; i++) {
            legal_set(set);
            int any = 0; for (int z = 0; z < nsyms; z++) any |= set[z];
            if (!any) { stop = i; why = 4; break; }
            int fi; int t = classify(&words[i], &fi);
            if (t < 0 || !set[t]) { stop = i; why = 2; break; }
            for (int k = 1; k < words[i].len; k++, bytes++)
                if (!extendable(set, words[i].t, k, words[i].kind)) { stop = i; why = 3; break; }
            if (stop >= 0) break;
            int live_n = step(&words[i]);
            if (live_n != 1) { stop = i; why = live_n ? 1 : 2; break; }
        }
        tw += stop < 0 ? nwords : stop; tb += bytes;
        if (stop < 0) printf("PASS %s: %d word prefixes and %ld cut words, one parse each (most live parses seen %ld)\n", argv[f], nwords, bytes, max_live);
        else {
            static const char *W[] = {"", "MORE THAN ONE PARSE", "the next word is not legal", "a cut word cannot be finished", "no legal next item"};
            printf("FAIL %s: at word %d '%.*s' (byte %ld): %s\n", argv[f], stop, words[stop].len, words[stop].t, words[stop].off, W[why]);
            bad = 1;
        }
        if (late_actions) { printf("FAIL %s: %d actions ran late (grammar defect)\n", argv[f], late_actions); bad = 1; }
        free(src);
    }
    printf("fuzz: %d files, %ld word prefixes, %ld cut words\n", argc, tw, tb);
    return bad;
}

static uint64_t rng;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static int cmd_mutants(const char *path, long seed, int count, const char *dir)
{
    long n; char *src = slurp(path, &n);
    lex(src, n, 0);
    int idx[200000], ni = 0;
    for (int i = 0; i < nwords && ni < 200000; i++) if (words[i].kind == 0 || words[i].kind == 5) idx[ni++] = i; /* plain words and fn names */
    if (!ni) return 0;
    static const char *POOL[] = {"fn", "to", "end", "if", "else", "while", "for", "ret", "put", "get", "len", "new",
                                 "view", "edit", "global", "text", "read", "write", "exit", "add", "neg", "uint",
                                 "ulong", "int", "void", "true", "=", "(", ")", "7", "-1", "0", "zz", "bound", NULL};
    int npool = 0; while (POOL[npool]) npool++;
    rng = (uint64_t)seed * 2654435761u + 1;
    const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    for (int m = 0; m < count; m++) {
        int w = idx[rnd() % (uint64_t)ni];
        int op = (int)(rnd() % 3);
        char out[4096]; snprintf(out, sizeof out, "%s/%s.m%d", dir, base, m);
        FILE *o = fopen(out, "wb");
        if (!o) die("cannot write ", out);
        long a = words[w].off, b = a + words[w].len;
        fwrite(src, 1, (size_t)a, o);
        if (op == 1) { fwrite(src + a, 1, (size_t)(b - a), o); fputc(' ', o); fwrite(src + a, 1, (size_t)(b - a), o); }
        else if (op == 2) {
            int r = (int)(rnd() % (uint64_t)(npool + 1));
            if (r < npool) fputs(POOL[r], o);
            else { int w2 = idx[rnd() % (uint64_t)ni]; fwrite(words[w2].t, 1, (size_t)words[w2].len, o); }
        }
        fwrite(src + b, 1, (size_t)(n - b), o);
        fclose(o);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 6 && !strcmp(argv[1], "mutants")) return cmd_mutants(argv[2], atol(argv[3]), atoi(argv[4]), argv[5]);
    if (argc < 4) { fprintf(stderr, "usage: omega_next next|accepts <grammar> <file> | fuzz <grammar> <file>... | mutants <file> <seed> <n> <dir>\n"); return 2; }
    load_grammar(argv[2]);
    compute_first();
    if (!strcmp(argv[1], "next")) return cmd_next(argv[3]);
    if (!strcmp(argv[1], "accepts")) return cmd_accepts(argv[3], 0);
    if (!strcmp(argv[1], "fuzz")) return cmd_fuzz(argc - 3, argv + 3);
    die("unknown command ", argv[1]);
    return 2;
}
