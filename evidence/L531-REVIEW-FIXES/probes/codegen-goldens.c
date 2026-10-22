/* Invoke unchanged repository golden-word verifiers, host only. */
#include "omega_blackwell_codegen.h"
#include <stdio.h>
int main(void) {
    struct { const char *name; int (*verify)(void); } suites[] = {
        {"base", omega_blackwell_verify_codegen_fixtures},
        {"fb1cut4", omega_blackwell_verify_codegen_fixtures_fb1cut4},
        {"intops", omega_blackwell_verify_codegen_fixtures_intops},
        {"fragmm", omega_blackwell_verify_codegen_fixtures_fragmm},
        {"reconv", omega_blackwell_verify_codegen_fixtures_reconv}
    };
    int failed = 0;
    for (unsigned i=0; i<sizeof suites/sizeof *suites; ++i) {
        int rc = suites[i].verify();
        printf("golden %s exit=%d\n",suites[i].name,rc);
        failed += rc != 0;
    }
    return failed ? 1 : 0;
}
