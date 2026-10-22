/* Test-only adapter to unchanged OSC APIs. Never a production compiler. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osc_front.h"
#include "osc_interp.h"
#include "osc_native.h"
#include "osc_cg.h"
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    char src[4096]; size_t n = fread(src, 1, sizeof src, f);
    int bad = ferror(f) || !feof(f); fclose(f);
    if (bad) return 2;
    OscUnit *u = calloc(1, sizeof *u);
    OscRt *rt = calloc(1, sizeof *rt);
    if (!u || !rt) return 2;
    OscDiag diag;
    if (osc_compile(src, n, u, &diag, NULL)) {
        fprintf(stderr, "OSC compile: %s\n", diag.message); return 3;
    }
    OscCode code = {0}; char err[256];
    if (osc_cg_compile(u, &code, err, sizeof err)) return 4;
    OscNative native;
    if (osc_native_map(&native, code.code, code.len)) {
        fprintf(stderr, "native unavailable: requires executable AArch64 mapping\n"); return 5;
    }
    uint64_t args[2];
    while (scanf("%" SCNu64 " %" SCNu64, args, args+1) == 2) {
        for (unsigned k = 0; k < 4; ++k) {
            uint64_t ref = 0, got = 0;
            osc_rt_init(rt);
            int tr = osc_interp_run(u, (int)k, args, 2, rt, &ref);
            osc_rt_init(rt);
            int tn = osc_native_call(u, (int)k, &native, code.entry[k], rt, args, 2, &got);
            if (tr || tn || ref != got) {
                fprintf(stderr, "OSC mismatch fn=%u reftrap=%d nativetrap=%d\n", k,tr,tn); return 6;
            }
            printf("%s%" PRIu64, k ? " " : "", ref);
        }
        puts("");
    }
    int status = ferror(stdin) ? 7 : 0;
    osc_native_unmap(&native); osc_cg_free(&code); free(rt); free(u);
    return status;
}
