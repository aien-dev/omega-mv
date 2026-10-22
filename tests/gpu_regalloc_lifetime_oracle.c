/* Omega #520: independent host oracle for the current linear-scan allocator.
 * No GPU execution or occupancy claim. Compile with codegen.c and section GC. */
#include "omega_blackwell_codegen.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int count;
    OmegaLiveInterval intervals[BW_MAX_VREGS];
} Inputs;

/* Save inputs before solve. Never derive expected liveness/width from solver output. */
static void snapshot(const BlackwellIRProgram *p, Inputs *in) {
    in->count = p->regalloc.num_vregs;
    for (int i = 0; i < in->count; ++i) in->intervals[i] = p->regalloc.intervals[i];
}
static int overlap(int a0, int a1, int b0, int b1) {
    return a0 <= b1 && b0 <= a1;
}
static int unchanged(const BlackwellIRProgram *p, const Inputs *in) {
    if (p->regalloc.num_vregs != in->count) return 0;
    for (int i = 0; i < in->count; ++i) {
        const OmegaLiveInterval *a = &in->intervals[i], *b = &p->regalloc.intervals[i];
        if (a->vreg != b->vreg || a->first_def != b->first_def ||
            a->last_use != b->last_use || a->bundle_size != b->bundle_size ||
            a->is_pair != b->is_pair || a->is_quad != b->is_quad) return 0;
    }
    return 1;
}
static int check(const BlackwellIRProgram *p, const Inputs *in) {
    int peak = 0;
    if (!unchanged(p, in)) return 0;
    for (int i = 0; i < in->count; ++i) {
        const OmegaLiveInterval *a = &in->intervals[i];
        int pa = p->regalloc.vreg_to_phys[i];
        int wa = (int)a->bundle_size;
        if (wa != 1 && wa != 2 && wa != 4) return 0;
        if (pa < BW_PHYS_GPR_START || pa > BW_PHYS_GPR_MAX-wa || pa % wa) return 0;
        if (pa != p->regalloc.intervals[i].phys_reg) return 0;
        if (pa + wa > peak) peak = pa + wa;
        for (int j = 0; j < i; ++j) {
            const OmegaLiveInterval *b = &in->intervals[j];
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
static int solve_checked(BlackwellIRProgram *p, Inputs *in) {
    snapshot(p, in);
    return omega_bw_regalloc_solve(p) == 0 && check(p, in);
}
/* Count all legal nonoverlapping aligned spans from source software bounds,
 * independently of the production allocator's free list or search loops. */
static int capacity(int width) {
    int n = 0;
    for (int r = BW_PHYS_GPR_START; r <= BW_PHYS_GPR_MAX-width; ++r)
        if (r % width == 0) ++n;
    return n;
}
static int fill(BlackwellIRProgram *p, int width, int n, int disjoint) {
    omega_bw_ir_init(p);
    for (int i = 0; i < n; ++i)
        if (add(p,width,disjoint ? i : 0,disjoint ? i : 1)<0) return 0;
    return 1;
}
static int boundaries(BlackwellIRProgram *p, Inputs *in) {
    for (int w = 1; w <= 4; w *= 2) {
        int cap = capacity(w), reused = 0;
        if (cap < 1 || cap+1 > BW_MAX_VREGS) return 0;
        if (!fill(p,w,cap+1,1) || !solve_checked(p,in)) {
            fprintf(stderr,"disjoint reuse failed width=%d count=%d\n",w,cap+1); return 0;
        }
        for (int i = 0; i < in->count; ++i) for (int j = 0; j < i; ++j)
            if (p->regalloc.vreg_to_phys[i] == p->regalloc.vreg_to_phys[j]) reused = 1;
        if (!reused) { fprintf(stderr,"no physical span reused width=%d\n",w); return 0; }
        if (!fill(p,w,cap,0) || !solve_checked(p,in)) {
            fprintf(stderr,"exact capacity refused width=%d capacity=%d\n",w,cap); return 0;
        }
        if (!fill(p,w,cap+1,0)) return 0;
        snapshot(p,in);
        if (omega_bw_regalloc_solve(p) == 0 || !unchanged(p,in)) {
            fprintf(stderr,"one-more refusal failed width=%d\n",w); return 0;
        }
        printf("width=%d disjoint%d reused, exactly%d allocated, one-more refused\n",w,cap+1,cap);
    }
    return 1;
}
static int fragmentation(BlackwellIRProgram *p, Inputs *in) {
    omega_bw_ir_init(p);
    /* Current software bounds include R2..R127. First fill with scalars; only
     * aligned holes R4..7 and R12..15 expire. A pair uses part of the first hole,
     * forcing a simultaneous quad into the second. After the pair expires,
     * a quad fits the recombined first hole. This is a feasibility witness,
     * not a requirement to use a particular lowest-slot placement policy. */
    for (int r=BW_PHYS_GPR_START; r<BW_PHYS_GPR_MAX; ++r) {
        int hole = (r>=4 && r<8) || (r>=12 && r<16);
        if (add(p,1,0,hole ? 0 : 5)<0) return 0;
    }
    if (add(p,2,1,1)<0 || add(p,4,1,4)<0 || add(p,4,2,3)<0 || !solve_checked(p,in)) {
        fprintf(stderr,"mixed-width fragmented reuse failed\n"); return 0;
    }
    omega_bw_ir_init(p);
    /* Alternate scalar words expire: many free words, no aligned pair. */
    for (int r=BW_PHYS_GPR_START; r<BW_PHYS_GPR_MAX; ++r)
        if (add(p,1,0,r%2 ? 5 : 0)<0) return 0;
    if (add(p,2,1,2)<0) return 0;
    snapshot(p,in);
    if (omega_bw_regalloc_solve(p) == 0 || !unchanged(p,in)) {
        fprintf(stderr,"fragmented pair exhaustion accepted\n"); return 0;
    }
    return 1;
}
static int controls(BlackwellIRProgram *p, Inputs *in) {
    omega_bw_ir_init(p);
    if (add(p,2,0,2)<0 || add(p,2,1,3)<0 || !solve_checked(p,in)) return 0;
    OmegaRegAlloc saved = p->regalloc;
    int killed = 0;
#define KILL(label) do { if (check(p,in)) { fprintf(stderr,label " mutant survived\n"); return 0; } \
    ++killed; p->regalloc = saved; } while (0)
    p->regalloc.vreg_to_phys[1] = p->regalloc.vreg_to_phys[0];
    p->regalloc.intervals[1].phys_reg = p->regalloc.intervals[0].phys_reg;
    KILL("overlap");
    p->regalloc.peak_gpr_usage++; KILL("false peak");
    p->regalloc.vreg_to_phys[0]++; p->regalloc.intervals[0].phys_reg++; KILL("misalignment");
    p->regalloc.vreg_to_phys[0] = BW_PHYS_GPR_MAX;
    p->regalloc.intervals[0].phys_reg = BW_PHYS_GPR_MAX; KILL("out-of-range");
    p->regalloc.intervals[0].first_def++; KILL("first_def corruption");
    p->regalloc.intervals[0].last_use--; KILL("last_use corruption");
    p->regalloc.intervals[0].bundle_size = 1; KILL("width corruption");
    p->regalloc.intervals[0].is_pair = false; KILL("pair metadata corruption");
    p->regalloc.intervals[0].is_quad = true; KILL("quad metadata corruption");
    p->regalloc.intervals[0].vreg++; KILL("vreg corruption");
    p->regalloc.num_vregs--; KILL("count corruption");
#undef KILL
    printf("oracle output/input controls killed=%d\n",killed);
    return killed == 11;
}
int main(void) {
    BlackwellIRProgram *p = malloc(sizeof *p);
    Inputs in;
    if (!p) return 2;
    int cases = 0;
    /* Original ten closed intervals over four positions, three bundle sizes. */
    for (int s0=0; s0<4; ++s0) for (int e0=s0; e0<4; ++e0)
    for (int s1=0; s1<4; ++s1) for (int e1=s1; e1<4; ++e1)
    for (int s2=0; s2<4; ++s2) for (int e2=s2; e2<4; ++e2)
    for (int w0=1; w0<=4; w0*=2) for (int w1=1; w1<=4; w1*=2)
    for (int w2=1; w2<=4; w2*=2) {
        omega_bw_ir_init(p);
        if (add(p,w0,s0,e0)<0 || add(p,w1,s1,e1)<0 || add(p,w2,s2,e2)<0 ||
            !solve_checked(p,&in)) {
            fprintf(stderr,"allocation mismatch at case %d\n",cases);
            free(p);return 1;
        }
        cases++;
    }
    if (!boundaries(p,&in) || !fragmentation(p,&in) || !controls(p,&in)) {
        free(p); return 1;
    }
    printf("regalloc lifetime oracle: PASS %d exhaustive cases, scalar/pair/quad reuse and capacity, mixed fragmentation, 11 controls killed\n",cases);
    free(p);return 0;
}
