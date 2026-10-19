/*
 * rwprobe -- what omega's probe signature groups together, measured (#249 deliver item 2, L059).
 *
 * Read: omega a8ffd2b9 src/omega_synthesis.c (omega_synth_compute_signature: SHA-256 of
 * omega_program_exec outputs on probes 0 1 3 7 13 42; omega_synth_equiv_contains_or_add: prune
 * on equal hash at equal or lower cost; table capacity SYNTH_MAX_SIGNATURES).
 *
 *   rwprobe universe --ce-dir DIR [--ce N]   all u8 chains of 1 and 2 steps over a fixed constant
 *                                            set: signature groups versus exhaustive truth tables,
 *                                            omega's pruning replayed, N counterexamples written
 *   rwprobe set FILE                         same grouping over an expression set (mixed widths)
 *   rwprobe verify-ce FILE...                each counterexample: equal signature, different table
 *
 * UNVERIFIED: the constant set K (chosen to include masks around the probe values).
 */
#include "omega_program.h"
#include "omega_synthesis.h"
#include "sha256.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wformat-truncation"

static const uint64_t K[] = {0, 1, 2, 3, 5, 7, 13, 15, 42, 47, 63, 64, 127, 128, 240, 255};
#define NK (int)(sizeof K / sizeof K[0])
static const uint8_t OPS[5] = {OP_ADD, OP_SUB, OP_MUL, OP_AND, OP_OR};
static const char *ONAME[5] = {"add", "sub", "mul", "and", "or"};

typedef struct { int n; uint8_t op[8]; uint64_t c[8]; unsigned w; } Ch;

static uint64_t wmask(unsigned w) { return w >= 64 ? ~0ull : ((1ull << w) - 1); }

static int build(const Ch *c, OmegaProgram *p) {
    omega_program_init(p, "rwprobe");
    p->contract.input_type = p->contract.output_type = TYPE_UNSIGNED_INT;
    p->contract.input_width = p->contract.output_width = (uint16_t)c->w;
    snprintf(p->contract.precondition, sizeof p->contract.precondition, "x >= 0");
    omega_build_constraint_id(CONST_PRECONDITION, p->contract.precondition, &p->contract.precondition_id);
    snprintf(p->contract.postcondition, sizeof p->contract.postcondition, "rwprobe");
    omega_build_constraint_id(CONST_POSTCONDITION, p->contract.postcondition, &p->contract.postcondition_id);
    p->body.has_body = true;
    p->body.step_count = (uint16_t)c->n;
    for (int i = 0; i < c->n; ++i) { p->body.steps[i].op = c->op[i]; p->body.steps[i].imm = c->c[i]; }
    if (omega_program_compute_id(p)) return -1;
    return omega_program_realize(p);
}

/* omega's signature (native execution on the six probes) and the truth table (omega's evaluator:
 * every input at w <= 16; 4096 frozen samples at w >= 32, so equality there is not proven). */
static int analyse(const Ch *c, uint8_t sig[32], uint8_t tab[32], uint32_t *insn, uint64_t *witness_out, size_t wn, const uint64_t *wx) {
    OmegaProgram *p = calloc(1, sizeof *p);
    int rc = build(c, p);
    if (!rc) rc = omega_synth_compute_signature(p, sig);
    *insn = (uint32_t)(p->realization.code_len / 4);
    if (!rc) rc = omega_program_eval(p, wx, wn, witness_out);
    if (!rc) sha256_hash((const uint8_t *)witness_out, wn * 8, tab);
    omega_program_destroy(p);
    free(p);
    return rc;
}

static void ch_str(const Ch *c, char *o, size_t cap) {
    o[0] = 0;
    for (int i = 0; i < c->n; ++i) { size_t l = strlen(o); snprintf(o + l, cap - l, "%s%s 0x%llx", i ? "; " : "", ONAME[c->op[i] == OP_ADD ? 0 : c->op[i] == OP_SUB ? 1 : c->op[i] == OP_MUL ? 2 : c->op[i] == OP_AND ? 3 : 4], (unsigned long long)c->c[i]); }
    if (!c->n) snprintf(o, cap, "id");
}

static int parse(const char *s, Ch *c) {
    memset(c, 0, sizeof *c);
    char buf[512]; snprintf(buf, sizeof buf, "%s", s);
    char *save;
    if (!strncmp(buf, "id", 2)) return 0;
    for (char *t = strtok_r(buf, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
        char o[8], vs[40], *e;
        if (sscanf(t, " %7s %39s", o, vs) != 2 || c->n >= 8) return -1;
        int k = -1; for (int j = 0; j < 5; ++j) if (!strcmp(o, ONAME[j])) k = j;
        if (k < 0) return -1;
        errno = 0; uint64_t v = strtoull(vs, &e, 0); if (errno || *e) return -1;
        c->op[c->n] = OPS[k]; c->c[c->n] = v; c->n++;
    }
    return 0;
}

typedef struct { uint8_t sig[32], tab[32]; uint32_t insn; Ch ch; uint64_t out[256]; } Prog;

static size_t inputs_for(unsigned w, uint64_t *x, size_t cap) {
    if (w <= 16) { size_t n = (size_t)wmask(w) + 1; if (n > cap) n = cap; for (size_t i = 0; i < n; ++i) x[i] = i; return n; }
    uint64_t s = 0xC0FFEE249ull; size_t n = 4096 < cap ? 4096 : cap;
    for (size_t i = 0; i < n; ++i) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; x[i] = i < 64 ? i : s & wmask(w); }
    return n;
}

static int group_report(Prog *P, int n, const char *label, const char *ce_dir, int ce_max) {
    /* signature groups: first program of each group is its representative */
    int groups = 0, mixed = 0, misgrouped = 0, written = 0;
    int *rep = malloc((size_t)n * sizeof *rep);
    for (int i = 0; i < n; ++i) {
        rep[i] = -1;
        for (int j = 0; j < i; ++j) if (rep[j] == j && P[j].ch.w == P[i].ch.w && !memcmp(P[j].sig, P[i].sig, 32)) { rep[i] = j; break; }
        if (rep[i] < 0) { rep[i] = i; groups++; }
    }
    int *mixed_flag = calloc((size_t)n, sizeof *mixed_flag);
    for (int i = 0; i < n; ++i) {
        int r = rep[i];
        if (r == i || !memcmp(P[r].tab, P[i].tab, 32)) continue;
        misgrouped++;
        if (!mixed_flag[r]) { mixed_flag[r] = 1; mixed++; }
        if (ce_dir && written < ce_max && P[i].ch.w <= 16) {
            /* first differing input */
            int x = -1; for (int k = 0; k < 256; ++k) if (P[r].out[k] != P[i].out[k]) { x = k; break; }
            char path[512]; snprintf(path, sizeof path, "%s/CE%02d.txt", ce_dir, written + 1);
            FILE *f = fopen(path, "w");
            char a[256], b[256]; ch_str(&P[r].ch, a, sizeof a); ch_str(&P[i].ch, b, sizeof b);
            fprintf(f, "# probe-agreeing, non-equivalent pair found by rwprobe universe (omega a8ffd2b9 probes 0 1 3 7 13 42)\n");
            fprintf(f, "width: %u\na: %s\nb: %s\nwitness_x: %d\na_at_witness: %llu\nb_at_witness: %llu\n", P[r].ch.w, a, b, x,
                    (unsigned long long)P[r].out[x], (unsigned long long)P[i].out[x]);
            fclose(f);
            written++;
        }
    }
    /* true equivalences missed: same table, different signature (same width) */
    int missed = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j)
            if (P[i].ch.w == P[j].ch.w && !memcmp(P[i].tab, P[j].tab, 32) && memcmp(P[i].sig, P[j].sig, 32)) { missed++; break; }
    /* distinct functions */
    int funcs = 0;
    for (int i = 0; i < n; ++i) {
        int seen = 0;
        for (int j = 0; j < i; ++j) if (P[i].ch.w == P[j].ch.w && !memcmp(P[i].tab, P[j].tab, 32)) { seen = 1; break; }
        funcs += !seen;
    }
    /* omega's pruning replayed in enumeration order, cost = emitted instruction count.
     * Note: the hash covers probe outputs only, not the width; the replay keys by hash alone as omega does. */
    EquivTable *tbl = calloc(1, sizeof *tbl);
    omega_synth_equiv_init(tbl);
    char *kept = calloc((size_t)n, 1);
    int pruned = 0;
    for (int i = 0; i < n; ++i) { if (omega_synth_equiv_contains_or_add(tbl, P[i].sig, P[i].insn)) pruned++; else kept[i] = 1; }
    int lost = 0;
    for (int i = 0; i < n; ++i) {
        int first = 1; for (int j = 0; j < i; ++j) if (P[i].ch.w == P[j].ch.w && !memcmp(P[i].tab, P[j].tab, 32)) { first = 0; break; }
        if (!first) continue;
        int any = 0;
        for (int j = 0; j < n; ++j) if (kept[j] && P[i].ch.w == P[j].ch.w && !memcmp(P[i].tab, P[j].tab, 32)) { any = 1; break; }
        lost += !any;
    }
    printf("RWPROBE %s programs=%d distinct_functions=%d signature_groups=%d mixed_groups=%d misgrouped_programs=%d "
           "true_equivalences_split=%d omega_pruned=%d functions_lost_to_pruning=%d table_entries=%zu table_capacity=%d ce_written=%d\n",
           label, n, funcs, groups, mixed, misgrouped, missed, pruned, lost, tbl->count, SYNTH_MAX_SIGNATURES, written);
    free(rep); free(mixed_flag); free(tbl); free(kept);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: rwprobe universe --ce-dir DIR [--ce N] | set FILE | verify-ce FILE...\n"); return 2; }
    uint64_t xs[65536];
    if (!strcmp(argv[1], "universe")) {
        const char *ce_dir = NULL; int ce = 3;
        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--ce-dir") && i + 1 < argc) ce_dir = argv[++i];
            else if (!strcmp(argv[i], "--ce") && i + 1 < argc) ce = atoi(argv[++i]);
        }
        int n = 5 * NK + 25 * NK * NK;
        Prog *P = calloc((size_t)n, sizeof *P);
        size_t nx = inputs_for(8, xs, 256);
        int k = 0;
        for (int a = 0; a < 5; ++a) for (int ca = 0; ca < NK; ++ca) {
            P[k].ch.w = 8; P[k].ch.n = 1; P[k].ch.op[0] = OPS[a]; P[k].ch.c[0] = K[ca]; k++; }
        for (int a = 0; a < 5; ++a) for (int ca = 0; ca < NK; ++ca) for (int b = 0; b < 5; ++b) for (int cb = 0; cb < NK; ++cb) {
            P[k].ch.w = 8; P[k].ch.n = 2; P[k].ch.op[0] = OPS[a]; P[k].ch.c[0] = K[ca]; P[k].ch.op[1] = OPS[b]; P[k].ch.c[1] = K[cb]; k++; }
        for (int i = 0; i < n; ++i)
            if (analyse(&P[i].ch, P[i].sig, P[i].tab, &P[i].insn, P[i].out, nx, xs)) { fprintf(stderr, "analyse refused %d\n", i); return 1; }
        return group_report(P, n, "universe=u8-len1-2-K16", ce_dir, ce);
    }
    if (!strcmp(argv[1], "set") && argc == 3) {
        FILE *f = fopen(argv[2], "r");
        if (!f) return 2;
        Prog *P = calloc(2048, sizeof *P);
        int n = 0;
        char line[1024];
        while (fgets(line, sizeof line, f) && n < 2048) {
            if (line[0] == '#') continue;
            char *nl = strchr(line, '\n'); if (nl) *nl = 0;
            char *ws = strstr(line, " w="); if (!ws) continue;
            char *rest;
            unsigned w = (unsigned)strtoul(ws + 3, &rest, 10);
            if (parse(rest, &P[n].ch)) { fprintf(stderr, "bad line\n"); return 2; }
            P[n].ch.w = w;
            uint64_t out[4096];
            size_t nx = inputs_for(w, xs, 65536);
            uint64_t *o = nx > 4096 ? malloc(nx * 8) : out;
            if (analyse(&P[n].ch, P[n].sig, P[n].tab, &P[n].insn, o, nx, xs)) return 1;
            memcpy(P[n].out, o, 256 * 8);
            if (o != out) free(o);
            n++;
        }
        fclose(f);
        printf("# set: widths <= 16 compared on every input; widths >= 32 on 4096 frozen samples (a difference there is a witness, an equality is not a proof)\n");
        return group_report(P, n, argv[2], NULL, 0);
    }
    if (!strcmp(argv[1], "verify-ce")) {
        int bad = 0;
        for (int i = 2; i < argc; ++i) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { bad++; continue; }
            char line[512], a[256] = "", b[256] = ""; unsigned w = 0;
            while (fgets(line, sizeof line, f)) {
                char *nl = strchr(line, '\n'); if (nl) *nl = 0;
                if (!strncmp(line, "width: ", 7)) w = (unsigned)atoi(line + 7);
                else if (!strncmp(line, "a: ", 3)) snprintf(a, sizeof a, "%s", line + 3);
                else if (!strncmp(line, "b: ", 3)) snprintf(b, sizeof b, "%s", line + 3);
            }
            fclose(f);
            Prog pa, pb; memset(&pa, 0, sizeof pa); memset(&pb, 0, sizeof pb);
            if (parse(a, &pa.ch) || parse(b, &pb.ch)) { bad++; continue; }
            pa.ch.w = pb.ch.w = w;
            size_t nx = inputs_for(w, xs, 256);
            if (analyse(&pa.ch, pa.sig, pa.tab, &pa.insn, pa.out, nx, xs) || analyse(&pb.ch, pb.sig, pb.tab, &pb.insn, pb.out, nx, xs)) { bad++; continue; }
            int same_sig = !memcmp(pa.sig, pb.sig, 32), diff_tab = memcmp(pa.tab, pb.tab, 32) != 0;
            printf("CE %s same_signature=%d different_function=%d %s\n", argv[i], same_sig, diff_tab, same_sig && diff_tab ? "ok" : "BAD");
            if (!(same_sig && diff_tab)) bad++;
        }
        printf("CE-SUMMARY bad_rows=%d\n", bad);
        return bad ? 1 : 0;
    }
    return 2;
}
