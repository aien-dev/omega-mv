/* M1 block 4 (omega-mv#577): CPU-side unified-memory copy bandwidth on the Spark.
 * memcpy src->dst over the whole buffer, T threads, R repeats; reports GB/s counting
 * bytes read + bytes written (2 x size). Pages are touched before timing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
typedef struct{char*s,*d;size_t n;}A;
static void*w(void*p){A*a=p;memcpy(a->d,a->s,a->n);return 0;}
int main(int argc,char**argv){
    double gb=atof(argv[1]);int T=atoi(argv[2]),R=atoi(argv[3]);size_t n=(size_t)(gb*1e9);
    char*s=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),*d=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(s==MAP_FAILED||d==MAP_FAILED){perror("mmap");return 1;}
    memset(s,1,n);memset(d,2,n);
    pthread_t th[64];A a[64];size_t per=n/T;
    for(int r=0;r<R;r++){double t0=now();
        for(int i=0;i<T;i++){a[i].s=s+i*per;a[i].d=d+i*per;a[i].n=i==T-1?n-i*per:per;pthread_create(&th[i],0,w,&a[i]);}
        for(int i=0;i<T;i++)pthread_join(th[i],0);
        double t=now()-t0;printf("%.0f,%d,%d,%.4f,%.1f\n",gb,T,r,t,2.0*n/t/1e9);fflush(stdout);}
    if(d[n-1]!=1)return 2;return 0;}
