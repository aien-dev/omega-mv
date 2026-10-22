/* M1 block 1 (omega-mv#574): time both realization schedules on the Spark.
 * Two measurements per program:
 *   path  = omega_exec_native_f3 as Omega runs it (mmap+copy+mprotect+call+munmap per call)
 *   code  = map once, call the function N times (the instruction bytes alone)
 * Verifies every schedule against omega_program_eval before timing. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include "omega_program.h"
#include "omega_realize.h"
#include "omega_realize_synth.h"
#include "omega_exec.h"

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int make_prog(OmegaProgram *p,const char *name,uint16_t w,const uint8_t *ops,const uint64_t *imms,size_t n){
    omega_program_init(p,name);
    p->contract.input_type=p->contract.output_type=TYPE_UNSIGNED_INT;
    p->contract.input_width=p->contract.output_width=w;
    snprintf(p->contract.precondition,sizeof p->contract.precondition,"true");
    snprintf(p->contract.postcondition,sizeof p->contract.postcondition,"true");
    omega_build_constraint_id(CONST_PRECONDITION,"true",&p->contract.precondition_id);
    omega_build_constraint_id(CONST_POSTCONDITION,"true",&p->contract.postcondition_id);
    p->body.has_body=true;p->body.step_count=(uint16_t)n;
    for(size_t i=0;i<n;++i){p->body.steps[i].op=ops[i];p->body.steps[i].imm=imms[i];}
    return omega_program_compute_id(p);
}
static int compose2(OmegaProgram *out,OpCode o1,uint64_t i1,OpCode o2,uint64_t i2){
    OmegaProgram a,b;char err[160];
    if(omega_program_build_unary_op(&a,"a",o1,i1)||omega_program_build_unary_op(&b,"b",o2,i2))return -1;
    return omega_program_compose(&a,&b,out,err,sizeof err);
}
typedef uint64_t(*Fn3)(uint64_t,uint64_t,uint64_t);

int main(int argc,char**argv){
    if (argc > 3) { fprintf(stderr,"usage: block1_sched [path_calls] [code_calls]\n"); return 2; }
    char *end = NULL;
    long NPATH=20000, NCODE=20000000;
    if (argc > 1) { errno=0; NPATH=strtol(argv[1],&end,10); if (errno || *end || NPATH <= 0 || NPATH > 100000000) return 2; }
    if (argc > 2) { errno=0; NCODE=strtol(argv[2],&end,10); if (errno || *end || NCODE <= 0 || NCODE > 100000000) return 2; }
    int failures=0;
    OmegaProgram P[24];const char*L[24];int n=0;
#define ADD(label,rc) do{if((rc)==0){L[n++]=label;}else{fprintf(stderr,"build fail %s\n",label);return 2;}}while(0)
    ADD("x+1",omega_program_build_unary_op(&P[n],"f",OP_ADD,1));
    ADD("x*3",omega_program_build_unary_op(&P[n],"f",OP_MUL,3));
    ADD("(x*3)-2",compose2(&P[n],OP_MUL,3,OP_SUB,2));
    ADD("(x+7)*5",compose2(&P[n],OP_ADD,7,OP_MUL,5));
    ADD("x&0xFF00FF",omega_program_build_unary_op(&P[n],"f",OP_AND,0xFF00FF));
    {uint8_t o[]={OP_ADD};uint64_t i[]={0x123456789ABCDEF0ull};ADD("x+0x123456789ABCDEF0",make_prog(&P[n],"w",64,o,i,1));}
    {uint8_t o[]={OP_MUL,OP_AND,OP_OR};uint64_t i[]={0x100000001ull,0xFFFF0000FFFF0000ull,0x0001000000000000ull};ADD("3-step wide",make_prog(&P[n],"w",64,o,i,3));}
    {uint8_t o[]={OP_MUL,OP_SUB};uint64_t i[]={3,2};ADD("u32 (x*3)-2",make_prog(&P[n],"n",32,o,i,2));}
    {uint8_t o[16];uint64_t i[16];for(int k=0;k<16;++k){o[k]=k%2?OP_MUL:OP_ADD;i[k]=0xFEDCBA9876543210ull^(uint64_t)k;}ADD("16-step wide",make_prog(&P[n],"long",64,o,i,16));}
    {uint8_t o[64];uint64_t i[64];for(int k=0;k<64;++k){o[k]=k%2?OP_MUL:OP_ADD;i[k]=0xFEDCBA9876543210ull^(uint64_t)k;}ADD("64-step wide",make_prog(&P[n],"long",64,o,i,64));}

    printf("program,schedule,code_bytes,verified,n_path,path_ns_per_call,n_code,code_ns_per_call,rule_pick\n");
    size_t pg=(size_t)sysconf(_SC_PAGESIZE);
    for(int k=0;k<n;++k){
        RealizationSynthesisResult rs; int src=omega_synthesize_for_dgx_spark(&P[k],&rs);
        if(src != 0){fprintf(stderr,"synthesis failed for %s\n",L[k]);return 1;}
        const char*pick=rs.schedule==OMEGA_SCHED_PRELOAD?"PRELOAD":"SEQUENTIAL";
        for(int s=0;s<2;++s){
            RealizationObject r;memset(&r,0,sizeof r);
            if(omega_program_emit_schedule(&P[k].body,P[k].contract.input_width,(OmegaRealizeSchedule)s,r.code_bytes,&r.code_len,sizeof r.code_bytes)){printf("%s,%s,0,emit_fail,,,,,%s\n",L[k],s?"PRELOAD":"SEQUENTIAL",pick);failures++;continue;}
            uint64_t xs[OMEGA_REALIZE_DIFF_INPUTS],ys[OMEGA_REALIZE_DIFF_INPUTS];
            size_t ni=omega_realize_differential_inputs(&P[k],xs,OMEGA_REALIZE_DIFF_INPUTS);
            if(!ni){fprintf(stderr,"no differential inputs for %s\n",L[k]);return 1;}
            int ok=omega_program_eval(&P[k],xs,ni,ys)==0;
            for(size_t i=0;ok&&i<ni;++i){uint64_t g=0;if(omega_exec_native_f3(&r,xs[i],0,0,&g)||g!=ys[i])ok=0;}
            if(!ok){printf("%s,%s,%zu,FAIL,,,,,%s\n",L[k],s?"PRELOAD":"SEQUENTIAL",r.code_len,pick);failures++;continue;}
            /* path: as Omega executes */
            volatile uint64_t sink=0;uint64_t g;
            for(int w=0;w<200;++w)if(omega_exec_native_f3(&r,xs[0],0,0,&g))return 1;
            double t0=now();for(long i=0;i<NPATH;++i){if(omega_exec_native_f3(&r,xs[i%ni],0,0,&g))return 1;sink+=g;}double t1=now();
            /* code: map once */
            size_t al=(r.code_len+pg-1)&~(pg-1);void*m=mmap(NULL,al,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
            if(m==MAP_FAILED){perror("mmap");return 2;}
            memcpy(m,r.code_bytes,r.code_len);__builtin___clear_cache((char*)m,(char*)m+r.code_len);
            if(mprotect(m,al,PROT_READ|PROT_EXEC)){perror("mprotect");munmap(m,al);return 2;}
            union{void*p;Fn3 f;}u;u.p=m;uint64_t acc=xs[0];
            for(long i=0;i<1000000;++i)acc=u.f(acc,0,0);
            double t2=now();for(long i=0;i<NCODE;++i)acc=u.f(acc,0,0);double t3=now();sink+=acc;munmap(m,al);
            printf("%s,%s,%zu,ok,%ld,%.1f,%ld,%.3f,%s\n",L[k],s?"PRELOAD":"SEQUENTIAL",r.code_len,NPATH,(t1-t0)/NPATH*1e9,NCODE,(t3-t2)/NCODE*1e9,pick);
            fflush(stdout);
        }
    }
    return failures ? 1 : 0;
}
