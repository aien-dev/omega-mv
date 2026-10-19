/* M1 block 2 profile (omega-mv#575): time each backend callback of the Blackwell engine
 * by wrapping the real backend table. Run under flock /tmp/aien-gb10.lock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "omega_gpu_engine.h"
#include "omega_blackwell_engine.h"
#include "omega_blackwell_encoder.h"

static const OmegaGpuBackend *R;
static double T[16]; static long N[16];
static const char *NAMES[16]={"open_device","create_channel","alloc","copy_in","fill_poison","build","submit","wait_marker","wait_marker2","wait_release_semaphore","barrier","copy_out","diagnostics","free_buf","close"};
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
#define W0(i,call) static int w_##i(void*c){double t=now();int r=R->call(R->ctx);T[i]+=now()-t;N[i]++;return r;}
#define W1(i,call,A,a) static int w_##i(void*c,A){double t=now();int r=R->call(R->ctx,a);T[i]+=now()-t;N[i]++;return r;}
W0(0,open_device) W0(1,create_channel)
static int w_2(void*c,int role,size_t b){double t=now();int r=R->alloc(R->ctx,role,b);T[2]+=now()-t;N[2]++;return r;}
static int w_3(void*c,int role,const void*s,size_t b){double t=now();int r=R->copy_in(R->ctx,role,s,b);T[3]+=now()-t;N[3]++;return r;}
static int w_4(void*c,const uint32_t*w,size_t n){double t=now();int r=R->fill_poison(R->ctx,w,n);T[4]+=now()-t;N[4]++;return r;}
static int w_5(void*c,const OmegaGpuJob*j){double t=now();int r=R->build(R->ctx,j);T[5]+=now()-t;N[5]++;return r;}
W0(6,submit)
W1(7,wait_marker,uint64_t ms,ms) W1(8,wait_marker2,uint64_t ms,ms) W1(9,wait_release_semaphore,uint64_t ms,ms) W1(10,barrier,uint64_t ms,ms)
static int w_11(void*c,void*d,size_t b){double t=now();int r=R->copy_out(R->ctx,d,b);T[11]+=now()-t;N[11]++;return r;}
static void w_12(void*c,OmegaGpuBackendDiag*d){double t=now();R->diagnostics(R->ctx,d);T[12]+=now()-t;N[12]++;}
static int w_13(void*c,int role){double t=now();int r=R->free_buf(R->ctx,role);T[13]+=now()-t;N[13]++;return r;}
W0(14,close)

int main(int argc,char**argv){
    if(argc<2||strcmp(argv[1],"--chip")){puts("NOT_RUN");return 2;}
    long NJ=argc>2?atol(argv[2]):50;
    R=omega_blackwell_engine_backend();
    OmegaGpuBackend B={0};B.ctx=(void*)1;B.open_device=w_0;B.create_channel=w_1;B.alloc=w_2;B.copy_in=w_3;B.fill_poison=w_4;B.build=w_5;B.submit=w_6;B.wait_marker=w_7;B.wait_marker2=w_8;B.wait_release_semaphore=w_9;B.barrier=w_10;B.copy_out=w_11;B.diagnostics=w_12;B.free_buf=w_13;B.close=w_14;
    omega_gpu_engine_set_backend(&B);
    uint8_t code[OMEGA_BW_VECADD_CODE_SIZE];size_t cb=0;omega_blackwell_encode_vecadd(code,sizeof code,&cb);
    uint32_t n=65,a[65],b[65],out[65],poison[65];for(uint32_t i=0;i<n;i++){a[i]=i+1;b[i]=3*i;poison[i]=~(a[i]+b[i]);}
    double tot=0;long bad=0;
    for(long k=0;k<NJ;k++){OmegaGpuJob j;OmegaGpuResult r;memset(&j,0,sizeof j);
        j.program=code;j.program_len=cb;j.layout=OMEGA_GPU_LAYOUT_VECTOR_1D;j.element_count=n;j.input_a=a;j.input_a_len=sizeof a;j.input_b=b;j.input_b_len=sizeof b;j.output=out;j.output_len=sizeof out;j.poison=poison;j.poison_count=n;
        j.timeouts.marker_ms=5000;j.timeouts.release_semaphore_ms=5000;j.timeouts.marker2_ms=5000;j.timeouts.visibility_ms=5000;
        double t=now();int rc=omega_gpu_execute(&j,&r);tot+=now()-t;if(rc)bad++;if(omega_gpu_engine_is_blocked()){puts("BLOCKED");for(;;)pause();}}
    printf("callback,calls_per_job,ms_per_job,share\n");
    for(int i=0;i<15;i++)printf("%s,%.1f,%.3f,%.1f%%\n",NAMES[i],(double)N[i]/NJ,T[i]/NJ*1e3,T[i]/tot*100);
    printf("TOTAL,,%.3f,100%% (jobs=%ld failed=%ld)\n",tot/NJ*1e3,NJ,bad);return bad!=0;}
