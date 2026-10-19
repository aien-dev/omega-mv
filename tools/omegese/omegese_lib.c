/*
 * omegese_lib.c -- omegese.c, unchanged, as a library (omegese_lib.h).
 *
 * omegese.c is written as a command: one prefix per process, global parser state that starts at zero,
 * answers on stdout, memory never freed. This file compiles it in place with three changes made from
 * outside, so omegese.c itself stays the one source of the language:
 *   - printf writes to a memory stream (the answer) instead of stdout;
 *   - malloc, calloc, realloc, strdup and free go through a list, so every block a query allocated
 *     is freed before the next query (the command left that to the process exit);
 *   - before every query each piece of parser state is put back to its start value (reset below), so
 *     the answer equals the command's in a fresh process.
 * main is renamed and never called.
 *
 * The reset list must name every mutable file-scope variable of omegese.c. test_omegese_lib.sh
 * refuses a variable missing from it, and compares the library with the command on every prefix of
 * every dev program, in one process, in order.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omegese_lib.h"

/* ---- allocation list: every block of the current query ---- */
typedef struct LibBlk { struct LibBlk *prev, *next; max_align_t pad[]; } LibBlk;
static LibBlk lib_head = {&lib_head, &lib_head};

static void *lib_link(LibBlk *b) {
    if (!b) return NULL;
    b->next = lib_head.next;
    b->prev = &lib_head;
    lib_head.next->prev = b;
    lib_head.next = b;
    return b->pad;
}
static LibBlk *lib_blk(void *p) { return (LibBlk *)((char *)p - offsetof(LibBlk, pad)); }
static void lib_unlink(LibBlk *b) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
}
static void *lib_malloc(size_t n) { return lib_link(malloc(sizeof(LibBlk) + n)); }
__attribute__((unused)) static void *lib_calloc(size_t k, size_t n) {
    if (n && k > ((size_t)-1 - sizeof(LibBlk)) / n) return NULL;
    void *p = lib_malloc(k * n);
    if (p) memset(p, 0, k * n);
    return p;
}
static void lib_free(void *p) {
    if (!p) return;
    LibBlk *b = lib_blk(p);
    lib_unlink(b);
    free(b);
}
static void *lib_realloc(void *p, size_t n) {
    if (!p) return lib_malloc(n);
    LibBlk *b = lib_blk(p);
    lib_unlink(b);
    LibBlk *nb = realloc(b, sizeof(LibBlk) + n);
    if (!nb) { lib_link(b); return NULL; }
    return lib_link(nb);
}
static char *lib_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = lib_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static void lib_free_all(void) {
    while (lib_head.next != &lib_head) {
        LibBlk *b = lib_head.next;
        lib_unlink(b);
        free(b);
    }
}

static FILE *lib_out;

#define malloc(n) lib_malloc(n)
#define calloc(k, n) lib_calloc(k, n)
#define realloc(p, n) lib_realloc(p, n)
#define strdup(s) lib_strdup(s)
#define free(p) lib_free(p)
#define printf(...) fprintf(lib_out, __VA_ARGS__)
#define main omegese_cli_main
#include "omegese.c"
#undef main
#undef printf
#undef free
#undef strdup
#undef realloc
#undef calloc
#undef malloc

/* every mutable file-scope variable of omegese.c, back to its start value (the reset list) */
#define Z(x) memset(&(x), 0, sizeof(x))
static void reset(void) {
    lib_free_all(); /* the Buf and id blocks below now point at nothing; zeroed next */
    Z(g_line); Z(g_err);
    Z(funcs); Z(nfuncs); Z(out); Z(in_fn); Z(regs); Z(nregs); Z(nparams); Z(names); Z(nnames); Z(depth);
    Z(consts); Z(nconsts); Z(blocks); Z(nblocks); Z(norder); Z(cur); Z(frames); Z(nframes);
    Z(nown); Z(own_reg); Z(ost); Z(own_pend); own_pk = -1; Z(g_loops); Z(owq); Z(nowq);
    Z(lw); Z(ln); Z(lp); Z(pdepth);
    Z(g_fill); Z(g_quiet); Z(g_ret); Z(g_rfloor); Z(g_pick); Z(g_nopt);
    Z(g_lw); Z(g_arena); Z(g_an); Z(g_gen); Z(g_taken);
    Z(S0); Z(S0q); Z(S0or); Z(S0os); Z(S0f); Z(S0r); Z(S0n); Z(S0k); Z(S0b); Z(S0fr);
    Z(W); Z(nW); Z(g_tline); U = ""; Z(g_close); Z(g_fl); Z(g_phase);
    Z(ids); Z(nids);
    memcpy(LPAR, "(", 2); memcpy(RPAR, ")", 2); /* split points words at them; never written, kept exact */
}

int omegese_lib_query(int mode, const char *src, size_t len, char **outp, size_t *out_len) {
    char *ans = NULL;
    size_t n = 0;
    *outp = NULL;
    *out_len = 0;
    lib_out = open_memstream(&ans, &n);
    if (!lib_out) return 2;
    int rc;
    if (mode != OMEGESE_NEXT_MACHINE && mode != OMEGESE_COMPLETE) {
        fprintf(lib_out, "omegese_lib: unknown mode %d\n", mode);
        rc = 2;
    } else if (len > MAX_IN) {
        /* the command's own refusal (slurp), which it prints on stderr */
        fprintf(lib_out, "omegese_lib: larger than %u bytes\n", MAX_IN);
        rc = 2;
    } else if (memchr(src, 0, len)) {
        fprintf(lib_out, "REFUSED omegese: a zero byte in the input\n");
        rc = 1;
    } else {
        reset();
        char *s = lib_malloc(len + 1);
        if (!s) { fputs("omegese: out of memory\n", stderr); exit(2); }
        memcpy(s, src, len);
        s[len] = 0;
        rc = oracle(s, mode == OMEGESE_NEXT_MACHINE ? M_MACHINE : M_COMPLETE);
        lib_free_all();
    }
    if (fclose(lib_out)) { free(ans); return 2; }
    lib_out = NULL;
    *outp = ans;
    *out_len = n;
    return rc;
}

void omegese_lib_free(char *p) { free(p); }
