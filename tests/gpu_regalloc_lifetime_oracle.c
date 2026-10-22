/* Omega #520: independent host oracle for the current linear-scan allocator.
 * No GPU execution or occupancy claim. Compile with codegen.c and section GC. */
#include "omega_blackwell_codegen.h"
#include <stdio.h>
#include <stdlib.h>

static int overlap(int a0, int a1, int b0, int b1) {
    return a0 <= b1 && b0 <= a1;
}
static int check(const BlackwellIRProgram *p) {
    int peak = 0;
    for (int i = 0; i < p->regalloc.num_vregs; ++i) {
        const OmegaLiveInterval *a = &p->regalloc.intervals[i];
        int pa = p->regalloc.vreg_to_phys[i];
        int wa = (int)a->bundle_size;
        if (wa != 1 && wa != 2 && wa != 4) return 0;
        if (pa < BW_PHYS_GPR_START || pa + wa > BW_PHYS_GPR_MAX || pa % wa) return 0;
        if (pa != a->phys_reg) return 0;
        if (pa + wa > peak) peak = pa + wa;
        for (int j = 0; j < i; ++j) {
            const OmegaLiveInterval *b = &p->regalloc.intervals[j];
            int pb = p->regalloc.vreg_to_phys[j], wb = (int)b->bundle_size;
            if (overlap(a->first_def,a->last_use,b->first_def,b->last_use) &&
                overlap(pa,pa+wa-1,pb,pb+wb-1)) return 0;
        }
    }
    return p->regalloc.peak_gpr_usage == (unsigned)peak;
}
static int add(BlackwellIRProgram *p, int width, int start, int end) {
    int v = width == 4 ? omega_bw_ir_alloc_vreg128(p) :
            width == 2 ? omega_bw_ir_alloc_vreg64(p) : omega_bw_ir_alloc_vreg(p);
    if (v < 0) return -1;
    p->regalloc.intervals[v].first_def = start;
    p->regalloc.intervals[v].last_use = end;
    return v;
}
int main(void) {
    BlackwellIRProgram *p = malloc(sizeof *p);
    if (!p) return 2;
    int cases = 0, killed = 0;
    /* Ten intervals [s,e] over four instruction positions, three bundle sizes. */
    for (int s0=0; s0<4; ++s0) for (int e0=s0; e0<4; ++e0)
    for (int s1=0; s1<4; ++s1) for (int e1=s1; e1<4; ++e1)
    for (int s2=0; s2<4; ++s2) for (int e2=s2; e2<4; ++e2)
    for (int w0=1; w0<=4; w0*=2) for (int w1=1; w1<=4; w1*=2)
    for (int w2=1; w2<=4; w2*=2) {
        omega_bw_ir_init(p);
        if (add(p,w0,s0,e0)<0 || add(p,w1,s1,e1)<0 || add(p,w2,s2,e2)<0 ||
            omega_bw_regalloc_solve(p) || !check(p)) {
            fprintf(stderr,"allocation mismatch at case %d\n",cases);
            free(p);return 1;
        }
        cases++;
    }
    omega_bw_ir_init(p);
    add(p,2,0,2);add(p,2,1,3);
    if (omega_bw_regalloc_solve(p) || !check(p)) { free(p);return 1; }
    p->regalloc.vreg_to_phys[1] = p->regalloc.vreg_to_phys[0];
    p->regalloc.intervals[1].phys_reg = p->regalloc.intervals[0].phys_reg;
    if (!check(p)) killed++; else { fprintf(stderr,"overlap mutant survived\n");free(p);return 1; }
    omega_bw_ir_init(p);
    for (int i=0; i<=BW_PHYS_GPR_MAX-BW_PHYS_GPR_START; ++i)
        if (add(p,1,0,1)<0) { free(p);return 1; }
    if (omega_bw_regalloc_solve(p)==0) { fprintf(stderr,"exhaustion accepted\n");free(p);return 1; }
    printf("regalloc lifetime oracle: PASS %d exhaustive cases, %d overlap mutant killed, exhaustion refused\n",cases,killed);
    free(p);return 0;
}
