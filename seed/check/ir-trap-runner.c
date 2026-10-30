/* L524 TEST ORACLE ONLY. Derived from seed/check/runner.c at 57f5f425.
 * Adds an independent IR interpreter/native differential, never builds omc. */
/*
 * runner.c -- REFERENCE CHECK ONLY for rung 3 of the seed chain (seed/README.md). It never makes a byte
 * of seed/. It runs one task function, made by either chain, on the task's inputs, under the same
 * runtime and calling rules (docs/osc/OSC-1-DESIGN.md section 7: params x0..x5, result x0 in canonical
 * form, OscRt * in x7, traps through OscRt.trap at offset 16).
 *
 *   runner <task.oscir> <entry> <inputs> [<seed code.bin>]
 *
 * Without a code file the function is compiled by the repo's C back end (osc_cg, the C path); with one,
 * the seed chain's words are mapped and called instead. The parameter and result types always come
 * from the .oscir (the C path's own IR), so both chains are fed and read the same way.
 * stdout: "words <n>" (all words of the code), then one line per input: the result as a number
 * (signed or unsigned by the result type) or "TRAP <code>", a tab, and the raw 64-bit x0 in hex.
 * A result that is not in canonical form for its type is marked " NONCANONICAL".
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osc_cg.h"
#include "osc_ir.h"
#include "osc_interp.h"
#include "osc_native.h"
#include "osc_rt.h"

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 20;
    uint8_t *b = malloc(cap);
    *n = b ? fread(b, 1, cap, f) : 0;
    fclose(f);
    return b;
}

static uint64_t canon(OscScalar t, uint64_t x)
{
    unsigned w = osc_scalar_width(t);
    if (t == OSC_T_BOOL) return x & 1;
    if (w >= 64) return x;
    if (osc_scalar_signed(t)) { uint64_t m = 1ULL << (w - 1); x &= (1ULL << w) - 1; return (x ^ m) - m; }
    return x & ((1ULL << w) - 1);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) { fprintf(stderr, "usage: runner <task.oscir> <entry> <inputs> [<code.bin>]\n"); return 2; }
    size_t irn = 0, cn = 0;
    uint8_t *ir = slurp(argv[1], &irn);
    static OscUnit u;
    if (!ir || osc_ir_decode(ir, irn, &u)) { fprintf(stderr, "runner: cannot decode %s\n", argv[1]); return 2; }
    int fi = -1;
    for (unsigned i = 0; i < u.nfuncs; i++) if (!strcmp(u.funcs[i].name, argv[2])) fi = (int)i;
    if (fi < 0) { fprintf(stderr, "runner: no function %s\n", argv[2]); return 2; }
    const OscFunc *fn = &u.funcs[fi];
    OscCode code;
    memset(&code, 0, sizeof code);
    uint8_t *bytes;
    uint32_t entry = 0;
    char err[160];
    if (argc == 4) {
        if (osc_cg_compile(&u, &code, err, sizeof err)) { fprintf(stderr, "runner: C back end: %s\n", err); return 2; }
        bytes = code.code; cn = code.len; entry = code.entry[fi];
    } else {
        bytes = slurp(argv[4], &cn);
        if (!bytes || cn == 0 || cn % 4) { fprintf(stderr, "runner: bad code file %s\n", argv[4]); return 2; }
    }
    printf("words %zu\n", cn / 4);
    OscNative nm;
    if (osc_native_map(&nm, bytes, cn)) { fprintf(stderr, "runner: cannot map code\n"); return 2; }
    static OscRt rt, ri;
    osc_rt_init(&rt);
    FILE *in = fopen(argv[3], "r");
    if (!in) { fprintf(stderr, "runner: missing inputs\n"); return 2; }
    char line[256];
    while (in && fgets(line, sizeof line, in)) {
        uint64_t a[OSC_MAX_PARAMS] = {0}, r = 0;
        char *p = line;
        for (unsigned i = 0; i < fn->nparams; i++) {
            OscScalar t = fn->vtype[i].s;
            a[i] = osc_scalar_signed(t) ? (uint64_t)strtoll(p, &p, 10) : strtoull(p, &p, 10);
            if (a[i] != canon(t, a[i])) { fprintf(stderr, "runner: input out of range for its type: %s", line); return 2; }
        }
        osc_rt_reset(&rt);
        int t = osc_rt_call_native(&rt, osc_native_at(&nm, entry), a, fn->nparams, &r);
        uint64_t ir = 0;
        osc_rt_reset(&ri);
        int ti = osc_interp_run(&u, fi, a, fn->nparams, &ri, &ir);
        if (ti != t || (t == 0 && ir != r) || !osc_rt_same_outcome(&ri, &rt)) {
            fprintf(stderr, "interpreter/native mismatch ti=%d tn=%d\n", ti, t);
            return 1;
        }
        if (t < 0) { fprintf(stderr, "runner: call refused\n"); return 2; }
        if (t > 0) { printf("TRAP %d\t-\n", t); continue; }
        if (osc_scalar_signed(fn->ret.s)) printf("%" PRId64, (int64_t)r); else printf("%" PRIu64, r);
        printf("\t0x%016" PRIx64 "%s\n", r, r == canon(fn->ret.s, r) ? "" : " NONCANONICAL");
    }
    return 0;
}
