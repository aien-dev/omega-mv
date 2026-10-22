/* Independent test/oracle only. The implementation under test is locality.om. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
static const char *exe;
static int pass, fail;
#define C(x) do { if(x) pass++; else {fail++; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static int run(const uint64_t *q, size_t n, uint64_t *r, size_t *nr) {
    int a[2], b[2]; if(pipe(a)||pipe(b)) abort();
    pid_t p=fork(); if(p<0)abort();
    if(!p){dup2(a[0],0);dup2(b[1],1);close(a[0]);close(a[1]);close(b[0]);close(b[1]);execl(exe,exe,(char*)0);_exit(127);}
    close(a[0]);close(b[1]); FILE *f=fdopen(a[1],"w");
    for(size_t i=0;i<n;i++)fprintf(f,"%llu ",(unsigned long long)q[i]);
    fclose(f);
    f=fdopen(b[0],"r"); *nr=0;
    unsigned long long v; while(fscanf(f,"%llu",&v)==1){if(*nr>=128)abort();r[(*nr)++]=(uint64_t)v;} fclose(f);
    int s;waitpid(p,&s,0);return WIFEXITED(s)?WEXITSTATUS(s):128;
}
/* Structurally independent set replay, units bytes. Node IDs topological. */
static int check(const uint64_t *q, size_t n, uint64_t *r) {
    size_t nn=q[1], ne=q[3], off=5+3*nn; uint64_t red=0,blue=0,done=0,used=0;
    memset(r,0,5*8);
    if(!nn||nn>16||!q[2]||ne>64||n!=off+ne*2||(q[4]>>nn))return 20;
    for(size_t i=0;i<nn;i++){
        const uint64_t *x=q+5+i*3;
        if(!x[0]||x[0]>1048576||(x[1]>>i)||x[2]>7||((x[2]&1)&&x[1]))return 20;
        if(x[2]&1)blue|=1ull<<i;
    }
    for(size_t e=0;e<ne;e++){
        uint64_t k=q[off+2*e],i=q[off+2*e+1];if(i>=nn||k>3)return 20;
        uint64_t bit=1ull<<i;const uint64_t *x=q+5+3*i;
        if(k<2){
            if(red&bit)return 23;
            if(k==0){if(!(blue&bit))return 23;r[0]+=x[0];r[3]++;}
            else {
                if(x[2]&1)return 23;
                if((red&x[1])!=x[1])return 21;
                if((done&bit)&&!(x[2]&2))return 23;
                done|=bit;r[2]++;
            }
            used+=x[0]; if(used>q[2])return 22;
            if(used>r[1])r[1]=used;
            red|=bit;
        } else if(k==2){if(!(red&bit))return 23;blue|=bit;r[0]+=x[0];r[4]++;}
        else {if(!(red&bit))return 23;red&=~bit;used-=x[0];}
    }
    uint64_t want=q[4];for(size_t i=0;i<nn;i++)if(q[5+3*i+2]&4)want|=1ull<<i;
    return (blue&want)==want?0:24;
}
static void sched(uint64_t *q,size_t n){uint64_t want[128],got[128];size_t nr;int rc=check(q,n,want);int actual=run(q,n,got,&nr);C(actual==rc);if(!rc){C(nr==5);if(nr==5)C(!memcmp(want,got,40));}}
/* Dijkstra over all placements for the 5-node diamond, independently proves
 * the two-level lower bound in units of one 8-byte value. No recompute cost. */
static int lower(int cap) {
    unsigned d[1024]; int seen[1024]={0};for(int i=0;i<1024;i++)d[i]=999;
    d[3<<5]=0;const int deps[5]={0,0,3,3,12};
    for(int it=0;it<1024;it++){
        int s=-1;for(int i=0;i<1024;i++)if(!seen[i]&&(s<0||d[i]<d[s]))s=i;
        if(s<0||d[s]==999)break;
        seen[s]=1;
        int red=s&31,blue=s>>5;if(blue&16)return (int)d[s]*8;
        for(int i=0;i<5;i++)for(int k=0;k<4;k++){
            int t=s,cost=0,bit=1<<i;
            if(k==0){if((red&bit)||!(blue&bit))continue;t=s|bit;cost=1;}
            if(k==1){if(i<2||(red&bit)||(red&deps[i])!=deps[i])continue;t=s|bit;}
            if(k==2){if(!(red&bit))continue;t=s|(bit<<5);cost=1;}
            if(k==3){if(!(red&bit))continue;t=s&~bit;}
            if(__builtin_popcount((unsigned)t&31)>cap)continue;
            if(d[t]>d[s]+cost)d[t]=d[s]+cost;
        }
    }return -1;
}
static void recurrence(void) {
    uint64_t q[512]={3},r[128],ref[64];size_t nr;
    const int shapes[]={1,7,32,64}, depths[]={1,2,17,64};
    for(int a=0;a<4;a++)for(int b=0;b<4;b++){
        size_t n=shapes[a],d=depths[b];q[1]=n;q[2]=d;q[4]=4096;q[5]=99;q[6]=1;
        for(size_t i=0;i<n;i++){q[7+i]=UINT64_MAX-i;q[7+n+i]=ref[i]=i+491;}
        for(size_t t=0;t<d;t++)for(size_t i=0;i<n;i++)ref[i]=ref[i]*(q[7+i]+3)+(i+1);
        for(int arm=0;arm<5;arm++){
            q[3]=arm;C(run(q,7+n*2,r,&nr)==0);C(nr==n+4);
            if(nr!=n+4)continue;
            C(!memcmp(r+4,ref,n*8));
            uint64_t bytes=24*n*d,ops=n*d,peak=24;
            if(arm==1){bytes+=16*n;ops=n;}
            if(arm==2){bytes=24*n;ops=n;peak=16*n+8;}
            if(arm==3){bytes=24*n;peak=16*n+8;}
            if(arm==4){bytes=32*n+8*n*d;ops=n;peak=8*n+16;}
            C(r[0]==bytes&&r[1]==ops&&r[2]==n*d&&r[3]==peak);
            q[4]=peak-1;C(run(q,7+n*2,r,&nr)==22&&nr==0);
            q[4]=peak;C(run(q,7+n*2,r,&nr)==0&&nr==n+4);
            q[4]=4096;q[5]=d/2;C(run(q,7+n*2,r,&nr)==25&&nr==0);q[5]=99;
        }
    }
    q[1]=65;C(run(q,137,r,&nr)==20);q[1]=1;
    q[2]=0;C(run(q,9,r,&nr)==20);q[2]=65;C(run(q,9,r,&nr)==20);q[2]=1;
    q[6]=2;C(run(q,9,r,&nr)==20);q[6]=1;q[3]=5;C(run(q,9,r,&nr)==20);
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    exe=argv[1];
    uint64_t q[512]={1,5,32,11,16, 8,0,1, 8,0,1, 8,3,2, 8,3,2, 8,12,2,
        0,0,0,1,1,2,1,3,3,0,3,1,1,4,2,4,3,2,3,3,3,4};
    sched(q,42);C(lower(4)==24);C(lower(3)==40);C(lower(2)==-1);
    q[2]=24;sched(q,42);q[2]=32;
    /* Preserve both input values while spilling the pure intermediate. */
    uint64_t spill[]={1,5,24,14,16,8,0,1,8,0,1,8,3,2,8,3,2,8,12,2,
        0,0,0,1,1,2,2,2,3,2,1,3,3,0,3,1,0,2,1,4,2,4,3,2,3,3,3,4};
    sched(spill,sizeof spill/8);
    uint64_t result[128];size_t count;
    C(run(spill,sizeof spill/8,result,&count)==0&&count==5&&result[0]==40&&result[1]==24);
    C(run(q,42,result,&count)==0&&count==5&&result[0]==24&&result[1]==32);
    uint64_t twice[]={1,2,16,6,2,8,0,1,8,1,2,0,0,1,1,2,1,3,1,1,1,2,1};
    sched(twice,sizeof twice/8);twice[10]=0;sched(twice,sizeof twice/8);
    twice[10]=6;twice[3]=2;twice[4]=0;sched(twice,15); /* persistent output is absent */
    q[20]=1;q[21]=4;sched(q,42);q[20]=0;q[21]=0;
    q[6]=1;sched(q,42);q[6]=0;
    q[4]=32;sched(q,42);q[4]=16;
    q[20]=4;sched(q,42);q[20]=0;
    q[21]=16;sched(q,42);q[21]=0;
    /* Adversarial event streams checked against independent set model. */
    uint32_t rng=491;
    for(int t=0;t<160;t++){
        uint64_t x[512];memcpy(x,q,sizeof q);x[3]=8;x[4]=0;x[2]=(t%5+1)*8;
        for(int i=0;i<16;i++){rng=rng*1664525u+1013904223u;x[20+i]=(i%2)?rng%5:rng%4;}
        sched(x,36);
    }
    uint64_t id[27]={2},r[128];size_t nr;
    for(int i=0;i<13;i++)id[i+1]=id[i+14]=(uint64_t)(i+9);
    C(run(id,27,r,&nr)==0&&nr==1&&r[0]==1);
    for(int i=0;i<13;i++){id[i+14]++;C(run(id,27,r,&nr)==0&&nr==1&&r[0]==0);id[i+14]--;}
    id[1]=id[14]=0;C(run(id,27,r,&nr)==0&&nr==1&&r[0]==0);
    C(run(id,26,r,&nr)==20);
    recurrence();
    printf("L491 independent oracle: pass=%d fail=%d; diamond lower(cap4)=24 lower(cap3)=40 cap2=REFUSED\n",pass,fail);
    return fail?1:0;
}
