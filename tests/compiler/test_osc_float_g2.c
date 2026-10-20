/*
 * test_osc_float_g2.c -- gate G2 of the OSC-4 float slice (docs/osc/OSC-4-FLOATS.md).
 * Compiles tests/compiler/float_g2.osc (argv[1] overrides the path) and runs each
 * kernel on native code and on the reference interpreter for a fixed set of seeds
 * and k values; every result must equal, bit for bit, the same kernel written in C
 * (float_g2_ref.c, -O0 -ffp-contract=off). Output: one "g2 ..." line per kernel,
 * then OSC4_FLOAT_G2_PASS / _FAIL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "float_g2_ref.h"
#include "osc_cg.h"
#include "osc_front.h"
#include "osc_interp.h"
#include "osc_native.h"
#include "osc_rt.h"

static int fails;

static const struct {
    const char *name;
    uint64_t (*ref)(uint64_t, uint64_t);
    unsigned nk; /* k = 0 .. nk-1 for each seed */
} K[] = {
    {"dot_f32", g2_dot_f32, 64},   {"dot_fma_f32", g2_dot_fma_f32, 64}, {"dot_f64", g2_dot_f64, 64},
    {"dot_f16", g2_dot_f16, 64},   {"dot_bf16", g2_dot_bf16, 64},       {"dot_f8e4m3", g2_dot_f8e4m3, 64},
    {"softmax_f32", g2_softmax_f32, 64},
};

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tests/compiler/float_g2.osc";
    static char src[65536];
    FILE *fp = fopen(path, "rb");
    size_t len = fp ? fread(src, 1, sizeof src - 1, fp) : 0;
    if (fp) fclose(fp);
    if (!len) { printf("OSC4_FLOAT_G2_FAIL cannot read %s\n", path); return 1; }
    OscUnit *u = malloc(sizeof *u);
    OscRt *ri = malloc(sizeof *ri), *rn = malloc(sizeof *rn);
    if (!u || !ri || !rn) { printf("OSC4_FLOAT_G2_FAIL out of memory\n"); return 1; }
    osc_rt_init(ri);
    osc_rt_init(rn);
    OscDiag d;
    if (osc_compile(src, len, u, &d, NULL) != 0) {
        printf("OSC4_FLOAT_G2_FAIL %s refused: %s line %u: %s\n", path, osc_diag_kind_name(d.kind), d.line, d.message);
        return 1;
    }
    OscCode c;
    char err[160];
    memset(&c, 0, sizeof c);
    if (osc_cg_compile(u, &c, err, sizeof err) != 0) { printf("OSC4_FLOAT_G2_FAIL codegen: %s\n", err); return 1; }
    OscNative nm;
    if (osc_native_map(&nm, c.code, c.len) != 0) { printf("OSC4_FLOAT_G2_FAIL native map\n"); return 1; }
    static const uint64_t seeds[] = {1, 2, 3, 42, 0x5EED, 0xDEADBEEF, 0x0123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull};
    unsigned long total = 0;
    for (size_t ki = 0; ki < sizeof K / sizeof K[0]; ki++) {
        int fi = -1;
        for (int f = 0; f < u->nfuncs; f++)
            if (strcmp(u->funcs[f].name, K[ki].name) == 0) fi = f;
        if (fi < 0) { printf("FAIL no function %s in %s\n", K[ki].name, path); fails++; continue; }
        void *entry = osc_native_at(&nm, c.entry[fi]);
        unsigned long runs = 0, bad = 0;
        uint64_t first = 0;
        for (size_t si = 0; si < sizeof seeds / sizeof seeds[0]; si++)
            for (uint64_t k = 0; k < K[ki].nk; k++) {
                uint64_t args[2] = {seeds[si], k}, want = K[ki].ref(seeds[si], k), r1 = 0, r2 = 0;
                osc_rt_reset(ri);
                osc_rt_reset(rn);
                int t1 = osc_interp_run_prevalidated(u, fi, args, 2, ri, &r1);
                int t2 = osc_rt_call_native(rn, entry, args, 2, &r2);
                if (!runs) first = want;
                runs++;
                if (t1 || t2 || r1 != want || r2 != want) {
                    if (bad++ < 3)
                        printf("FAIL %s seed %#llx k %llu: interp trap %d %#llx native trap %d %#llx, C %#llx\n", K[ki].name,
                               (unsigned long long)seeds[si], (unsigned long long)k, t1, (unsigned long long)r1, t2,
                               (unsigned long long)r2, (unsigned long long)want);
                }
            }
        fails += bad != 0;
        total += runs;
        printf("g2 %-12s runs=%lu mismatches=%lu first=%#llx\n", K[ki].name, runs, bad, (unsigned long long)first);
    }
    osc_native_unmap(&nm);
    osc_cg_free(&c);
    printf("g2: kernels=%zu runs=%lu (each on native and interpreter) failures=%d\n", sizeof K / sizeof K[0], total, fails);
    printf(fails ? "OSC4_FLOAT_G2_FAIL\n" : "OSC4_FLOAT_G2_PASS\n");
    return fails ? 1 : 0;
}
