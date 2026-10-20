/* L159 spin-wait repro: 256 iterations, each waits (busy spin, arm yield hint) until a 1 ms deadline, then records how late
 * the thread noticed the deadline. A thread that keeps the CPU is late by ~0; a preempted thread is late by its run-queue wait.
 * Models omega_gpu_session_launch's session_wait(spin_us) seeing the completion marker. Also samples its own schedstat. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec; }
static void schedstat(uint64_t *cpu, uint64_t *wait, uint64_t *sl){ FILE *f=fopen("/proc/self/schedstat","r"); if(!f){*cpu=*wait=*sl=0;return;} if(fscanf(f,"%lu %lu %lu",cpu,wait,sl)!=3){*cpu=*wait=*sl=0;} fclose(f); }
int main(int argc,char**argv){
    int iters = argc>1 ? atoi(argv[1]) : 256; uint64_t kern_ns = argc>2 ? strtoull(argv[2],0,10) : 1000000ull;
    uint64_t c0,w0,s0,c1,w1,s1; schedstat(&c0,&w0,&s0);
    uint64_t late_sum=0, late_max=0, hist[6]={0}; /* <10us, <100us, <1ms, <4ms, <16ms, >=16ms */
    uint64_t t_start=now_ns();
    for(int i=0;i<iters;i++){
        uint64_t t0=now_ns(), dl=t0+kern_ns, t;
        do { __asm__ __volatile__("yield"); t=now_ns(); } while(t<dl);
        uint64_t late=t-dl; late_sum+=late; if(late>late_max) late_max=late;
        hist[late<10000?0:late<100000?1:late<1000000?2:late<4000000?3:late<16000000?4:5]++;
    }
    uint64_t wall=now_ns()-t_start; schedstat(&c1,&w1,&s1);
    printf("SNAP site=spinwait_repro iters=%d kern_us=%lu wall_ms=%.1f expected_ms=%.1f late_sum_ms=%.1f late_max_ms=%.3f "
           "hist(<10us,<100us,<1ms,<4ms,<16ms,>=16ms)=%lu,%lu,%lu,%lu,%lu,%lu schedstat cpu_ms=%.1f rq_wait_ms=%.1f slices=%lu\n",
           iters,kern_ns/1000,wall/1e6,iters*kern_ns/1e6,late_sum/1e6,late_max/1e6,hist[0],hist[1],hist[2],hist[3],hist[4],hist[5],
           (c1-c0)/1e6,(w1-w0)/1e6,(unsigned long)(s1-s0));
    return 0;
}
