/* L159 wait-strategy variants, CPU only. Each of N iterations has a 'kernel' deadline kern_ns ahead; the waiter must notice it.
 * modes: spin | yield | sleep<us> (nanosleep poll, timer slack 1 us) | hybrid<us> (spin that long, then sleep 20 us polls). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/prctl.h>
static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec; }
static void schedstat(uint64_t *c,uint64_t *w,uint64_t *s){ FILE *f=fopen("/proc/self/schedstat","r"); *c=*w=*s=0; if(f){ if(fscanf(f,"%lu %lu %lu",c,w,s)!=3){} fclose(f);} }
int main(int argc,char**argv){
    const char *mode = argc>1?argv[1]:"spin"; int iters = argc>2?atoi(argv[2]):256; uint64_t kern_ns = argc>3?strtoull(argv[3],0,10):1000000ull;
    uint64_t sleep_ns = 0, spin_ns = 0;
    if(!strncmp(mode,"sleep",5)) sleep_ns = strtoull(mode+5,0,10)*1000ull;
    if(!strncmp(mode,"hybrid",6)) { spin_ns = strtoull(mode+6,0,10)*1000ull; sleep_ns = 20000; }
    uint64_t predict_margin_ns = 0; if(!strncmp(mode,"predict",7)) predict_margin_ns = strtoull(mode+7,0,10)*1000ull;
    prctl(PR_SET_TIMERSLACK, 1000);
    uint64_t c0,w0,s0,c1,w1,s1; schedstat(&c0,&w0,&s0);
    uint64_t late_sum=0, late_max=0, hist[6]={0};
    uint64_t t_start=now_ns();
    for(int i=0;i<iters;i++){
        uint64_t t0=now_ns(), dl=t0+kern_ns, t;
        if(predict_margin_ns && kern_ns > predict_margin_ns){ struct timespec ts={0,(long)(kern_ns-predict_margin_ns)}; nanosleep(&ts,0); }
        for(;;){
            t=now_ns(); if(t>=dl) break;
            if(predict_margin_ns){ __asm__ __volatile__("yield"); continue; }
            if(!strcmp(mode,"spin")) __asm__ __volatile__("yield");
            else if(!strcmp(mode,"yield")) sched_yield();
            else if(spin_ns && t < t0+spin_ns) __asm__ __volatile__("yield");
            else { struct timespec ts={0,(long)sleep_ns}; nanosleep(&ts,0); }
        }
        uint64_t late=t-dl; late_sum+=late; if(late>late_max) late_max=late;
        hist[late<10000?0:late<100000?1:late<1000000?2:late<4000000?3:late<16000000?4:5]++;
    }
    uint64_t wall=now_ns()-t_start; schedstat(&c1,&w1,&s1);
    printf("SNAP mode=%s iters=%d kern_us=%lu wall_ms=%.1f late_sum_ms=%.1f late_max_ms=%.3f hist=%lu,%lu,%lu,%lu,%lu,%lu cpu_ms=%.1f rq_wait_ms=%.1f slices=%lu\n",
           mode,iters,kern_ns/1000,wall/1e6,late_sum/1e6,late_max/1e6,hist[0],hist[1],hist[2],hist[3],hist[4],hist[5],(c1-c0)/1e6,(w1-w0)/1e6,(unsigned long)(s1-s0));
    return 0;
}
