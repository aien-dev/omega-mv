#define main author_oracle_main
#include "tests/gpu_regalloc_lifetime_oracle.c"
#undef main
static int fill(BlackwellIRProgram *p,int width,int count,int disjoint) {
    omega_bw_ir_init(p);
    for(int i=0;i<count;i++) if(add(p,width,disjoint?i:0,disjoint?i:1)<0) return -2;
    int rc=omega_bw_regalloc_solve(p);
    return rc ? -1 : check(p)?0:-3;
}
int main(void) {
    BlackwellIRProgram *p=malloc(sizeof *p);
    if(!p)return 2;
    int widths[]={1,2,4}, caps[]={126,63,31};
    for(int i=0;i<3;i++){
        int exact=fill(p,widths[i],caps[i],0), over=fill(p,widths[i],caps[i]+1,0);
        int sequential=fill(p,widths[i],127,1);
        printf("width=%d exact_capacity=%d rc=%d excess_rc=%d disjoint127_rc=%d\n",widths[i],caps[i],exact,over,sequential);
        if(exact||over!=-1||sequential){free(p);return 1;}
    }
    omega_bw_ir_init(p);add(p,2,0,0);add(p,2,1,1);
    if(omega_bw_regalloc_solve(p)||p->regalloc.vreg_to_phys[0]!=p->regalloc.vreg_to_phys[1])return 1;
    int killed=0;
    p->regalloc.peak_gpr_usage++; killed+=!check(p);p->regalloc.peak_gpr_usage--;
    p->regalloc.vreg_to_phys[0]++;p->regalloc.intervals[0].phys_reg++;killed+=!check(p);
    p->regalloc.vreg_to_phys[0]=128;p->regalloc.intervals[0].phys_reg=128;killed+=!check(p);
    printf("explicit_reuse PASS; false_peak/misalignment/out_of_range killed=%d\n",killed);
    free(p);return killed==3?0:1;
}
