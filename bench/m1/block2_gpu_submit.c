/* M1 block 2 (omega-mv#575): GPU submit/complete round trip through omega_gpu_execute,
 * the full lifecycle Omega runs per job (open, upload, submit, wait, readback, free, close).
 * Reports wall per job (p50/p95/p99/max) and thread CPU time per job (the polling cost).
 * Usage: block2 --chip [N] ; must run under flock /tmp/aien-gb10.lock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "omega_gpu_engine.h"
#include "omega_blackwell_engine.h"
#include "omega_blackwell_encoder.h"

static double now(clockid_t c){struct timespec t;clock_gettime(c,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int cmpd(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y;}

static int run_case(const char*label,uint32_t n,int no_store,long N){
    uint8_t code[OMEGA_BW_VECADD_CODE_SIZE];size_t code_bytes=0;
    if(omega_blackwell_encode_vecadd(code,sizeof code,&code_bytes)||code_bytes<0x140u){fprintf(stderr,"encode fail\n");return 1;}
    if(no_store)memcpy(code+0x120,code+0x130,OMEGA_BW_INSTRUCTION_BYTES);
    uint32_t*a=malloc(n*4),*b=malloc(n*4),*out=malloc(n*4),*poison=malloc(n*4);
    for(uint32_t i=0;i<n;i++){a[i]=i+1;b[i]=3*i;poison[i]=~(a[i]+b[i]);}
    double*wall=malloc(N*sizeof(double));double cpu_tot=0;long bad=0;
    for(long k=0;k<N;k++){
        OmegaGpuJob job;OmegaGpuResult res;memset(&job,0,sizeof job);memset(out,0,n*4);
        job.program=code;job.program_len=code_bytes;job.layout=OMEGA_GPU_LAYOUT_VECTOR_1D;job.element_count=n;
        job.input_a=a;job.input_a_len=n*4;job.input_b=b;job.input_b_len=n*4;job.output=out;job.output_len=n*4;
        job.poison=poison;job.poison_count=n;
        job.timeouts.marker_ms=5000;job.timeouts.release_semaphore_ms=5000;job.timeouts.marker2_ms=5000;job.timeouts.visibility_ms=5000;
        double w0=now(CLOCK_MONOTONIC),c0=now(CLOCK_THREAD_CPUTIME_ID);
        int rc=omega_gpu_execute(&job,&res);
        double w1=now(CLOCK_MONOTONIC),c1=now(CLOCK_THREAD_CPUTIME_ID);
        if(omega_gpu_engine_is_blocked()){fprintf(stderr,"GPU_ENGINE_BLOCKED, parking\n");fflush(stdout);for(;;)pause();}
        int ok=no_store?(rc==OMEGA_GPU_ENGINE_OUTPUT_UNCHANGED):(rc==0);
        if(ok&&!no_store)for(uint32_t i=0;i<n;i++)if(out[i]!=a[i]+b[i]){ok=0;break;}
        if(!ok){bad++;if(bad<4)fprintf(stderr,"%s job %ld rc=%d state=%d step=%d\n",label,k,rc,res.last_state,res.failed_step);}
        wall[k]=w1-w0;cpu_tot+=c1-c0;
    }
    qsort(wall,N,sizeof(double),cmpd);double sum=0;for(long k=0;k<N;k++)sum+=wall[k];
    printf("%s,%u,%ld,%ld,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f\n",label,n,N,bad,sum/N*1e6,wall[N/2]*1e6,wall[(long)(N*0.95)]*1e6,wall[(long)(N*0.99)]*1e6,wall[N-1]*1e6,cpu_tot/N*1e6,cpu_tot/sum);
    fflush(stdout);free(a);free(b);free(out);free(poison);free(wall);return bad!=0;
}
int main(int argc,char**argv){
    if(argc<2||strcmp(argv[1],"--chip")){puts("NOT_RUN: pass --chip under flock");return 2;}
    long N=argc>2?atol(argv[2]):1000;
    omega_gpu_engine_set_backend(omega_blackwell_engine_backend());
    printf("case,elements,n_jobs,failed,wall_mean_us,wall_p50_us,wall_p95_us,wall_p99_us,wall_max_us,cpu_mean_us,cpu_over_wall\n");
    int r=0;
    r|=run_case("vecadd_no_store",65,1,N);
    r|=run_case("vecadd",65,0,N);
    r|=run_case("vecadd",4096,0,N);
    r|=run_case("vecadd",65537,0,N/4);
    return r;
}
