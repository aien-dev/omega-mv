/* Host-only test/campaign adapter. Search policy is exclusively policy.osc.
 * Uses existing OSC compiler, native ABI and Cortex checkpoint contracts.
 * The independent oracle below has no policy transitions or ranking code. */
#include "osc_front.h"
#include "osc_interp.h"
#include "osc_cg.h"
#include "osc_native.h"
#include "runtime/rx_cortex.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

#define NW 64
#define PW 88
#define MAX_JOURNAL 24392
static OscUnit *unit;
static OscCode code;
static OscNative native;
static int init_fn, step_fn, reserve_fn, abandon_fn, checks, failures;
static uint64_t reloads, replayed_transitions;
static uint8_t policy_hash[32], freeze_hash[32];
static void ck(int ok, const char *what) {
    checks++; if (!ok) { failures++; fprintf(stderr,"FAIL %s\n",what); }
}
static void fatal(const char *s) { fprintf(stderr,"REFUSED %s\n",s); exit(2); }
static int function(const char *name) {
    for (int i=0;i<unit->nfuncs;i++) if (!strcmp(unit->funcs[i].name,name)) return i;
    fatal("missing OSC function"); return -1;
}
static char *read_file(const char *path, size_t *n) {
    FILE *f=fopen(path,"rb"); if(!f) fatal("open source");
    if(fseek(f,0,SEEK_END)) fatal("seek");
    long z=ftell(f);
    if(z<0 || z>65536 || fseek(f,0,SEEK_SET)) fatal("source size");
    char *p=calloc((size_t)z+1,1); if(!p) fatal("allocation");
    *n=fread(p,1,(size_t)z,f); if(*n!=(size_t)z || fclose(f)) fatal("read source"); return p;
}
static void load(const char *path) {
    size_t n; char *p=read_file(path,&n); sha256_hash((uint8_t*)p,n,policy_hash);
    unit=calloc(1,sizeof *unit); if(!unit) fatal("unit allocation"); OscDiag d;
    if(osc_compile(p,n,unit,&d,NULL)) {
        fprintf(stderr,"OSC %u:%u %s\n",d.line,d.col,d.message); exit(2);
    }
    free(p); p=read_file("research/exploration-600/FREEZE.md",&n);
    sha256_ctx contract;sha256_init(&contract);sha256_update(&contract,(uint8_t*)p,n);free(p);
    p=read_file("research/exploration-600/AMENDMENTS.md",&n);
    sha256_update(&contract,(uint8_t*)p,n);sha256_final(&contract,freeze_hash);free(p);
    char why[256]; if(osc_cg_compile(unit,&code,why,sizeof why)) { fprintf(stderr,"CG %s\n",why);exit(2); }
    /* Artifact identity binds the host adapter and its linked compiler/Cortex
     * as well as OSC source and emitted native bytes. No stale build resumes. */
    sha256_ctx artifact;sha256_init(&artifact);sha256_update(&artifact,policy_hash,32);
    sha256_update(&artifact,code.code,code.len);
    FILE *self=fopen("/proc/self/exe","rb");if(!self)fatal("host artifact identity");
    uint8_t chunk[4096];size_t got;
    while((got=fread(chunk,1,sizeof chunk,self))!=0)sha256_update(&artifact,chunk,got);
    if(ferror(self)||fclose(self))fatal("host artifact read");
    sha256_final(&artifact,policy_hash);
    if(osc_native_map(&native,code.code,code.len)) fatal("native map");
    init_fn=function("initialize"); step_fn=function("advance");
    reserve_fn=function("reserve");abandon_fn=function("abandon");
}
/* Every actual policy call is interpreter/native differential; input references
 * are independently allocated in the existing runtime's live pool. */
static uint64_t call(uint64_t s[NW], const uint64_t cost[16], int fi,
                     unsigned arm, unsigned seed, unsigned budget, unsigned cap) {
    OscRt *a=calloc(1,sizeof *a), *b=calloc(1,sizeof *b); if(!a||!b) fatal("rt alloc");
    osc_rt_init(a);osc_rt_init(b);
    uint64_t ai[6]={0}, bi[6]={0}, ri=999,rn=998;
    ai[0]=osc_rt_alloc(a,NW,0);bi[0]=osc_rt_alloc(b,NW,0);
    memcpy((void*)(uintptr_t)ai[0],s,NW*8);memcpy((void*)(uintptr_t)bi[0],s,NW*8);
    unsigned nargs=5;
    if(fi!=init_fn) {
        ai[1]=osc_rt_alloc(a,16,0);bi[1]=osc_rt_alloc(b,16,0);
        memcpy((void*)(uintptr_t)ai[1],cost,128);memcpy((void*)(uintptr_t)bi[1],cost,128);nargs=2;
    } else { ai[1]=bi[1]=arm;ai[2]=bi[2]=seed;ai[3]=bi[3]=budget;ai[4]=bi[4]=cap; }
    int ti=osc_interp_run(unit,fi,ai,nargs,a,&ri);
    int tn=osc_rt_call_native(b,osc_native_at(&native,code.entry[fi]),bi,nargs,&rn);
    ck(ti==0&&tn==0&&ri==rn&&osc_rt_same_outcome(a,b),"OSC native/interpreter identity");
    if(ti||tn) fatal("policy trap");
    memcpy(s,(void*)(uintptr_t)ai[0],NW*8);free(a);free(b);return ri;
}
static unsigned depth(unsigned n) { unsigned d=0; while(n){ n=(n-1)/2;d++; } return d; }
static void fixture(unsigned family,unsigned seed,uint64_t c[16]) {
    for(unsigned n=0;n<15;n++) {
        if(family==0) c[n]=100+10*n;
        else if(family==1) c[n]=100-10*depth(n);
        else if(family==2) c[n]=100+depth(n);
        else if(family==3) c[n]=100;
        else if(family==4) c[n]=n>=7?20+(n*17+seed)%70:140;
        else if(family==5) c[n]=140;
        else if(family==6) c[n]=110;
        else c[n]=100+(n*13+seed)%40;
    }
    c[0]=100;c[15]=32767;
    if(family==0) c[14]=20;
    if(family==3) c[7+seed%8]=30;
    if(family==4){c[1]=105;c[2]=115;}
    if(family==5){c[1]=60;c[2]=120;c[6]=130;c[14]=10;}
    if(family==6){c[2]=1;c[6]=2;c[14]=3;c[7]=70;c[15]^=4;}
}
/* Independent exhaustive oracle: inspect every syntactic word and its prefix
 * validity, no frontier, retention decision, budget or OSC function consulted. */
static int reachable(unsigned n,const uint64_t c[16]) {
    for(;;){if(!(c[15]&(1u<<n)))return 0;if(!n)return 1;n=(n-1)/2;}
}
static uint64_t optimum(const uint64_t c[16]) {
    uint64_t v=100;for(unsigned n=1;n<15;n++)if(reachable(n,c)&&c[n]<v)v=c[n];return v;
}
static void start(uint64_t s[NW],unsigned arm,unsigned seed,unsigned budget,unsigned cap) {
    memset(s,0,NW*8);call(s,NULL,init_fn,arm,seed,budget,cap);
}
static void finish(uint64_t s[NW],const uint64_t c[16]) {
    for(unsigned k=0;k<16&&s[10]==0;k++)call(s,c,step_fn,0,0,0,0);
    ck(s[10]!=0,"bounded completion");ck(s[4]<=s[3],"budget conserved");
    ck(s[7]<=100&&s[19]==100,"qualified reference intact");
    ck(reachable((unsigned)s[6],c)&&s[7]==c[s[6]],"best has valid ancestry");
    ck(s[4]==s[20]+s[21]+s[22]+s[23],"all work categories charged");
}
/* 24 context words: source/contract/input/model/assumptions SHA256 plus
 * arm/seed/budget/capacity. Hash bytes copied as opaque words, not arithmetic. */
static void context(uint64_t out[24],const uint64_t c[16],unsigned arm,unsigned seed,unsigned budget,unsigned cap) {
    uint8_t d[32];memcpy(out,policy_hash,32);memcpy(out+4,freeze_hash,32);
    sha256_hash((uint8_t*)c,128,d);memcpy(out+8,d,32);
    const char *m="synthetic-cost-v1:search2:check3:checkpoint2:rent1";
    sha256_hash((const uint8_t*)m,strlen(m),d);memcpy(out+12,d,32);
    const char *a="finite15:depth3:horizons1,8,32:no-promotion:reference0";
    sha256_hash((const uint8_t*)a,strlen(a),d);memcpy(out+16,d,32);
    out[20]=arm;out[21]=seed;out[22]=budget;out[23]=cap;
}
static int append(CxStore *cx,const uint64_t ctx[24],const uint64_t s[NW]) {
    if(cx->n>=29)return CX_ERR_ARG;
    uint64_t p[PW],id;memcpy(p,ctx,24*8);memcpy(p+24,s,NW*8);
    CxHeader h={.cls=CX_CLAIM,.kind=CX_K_CANDIDATE,.subject=0,.t=cx->n+1,
        .generation=6001,.branch=1,.protect=CX_PROT_GENERATION_ID};
    h.links[0]=cx->n;
    if(cx->n) {
        const uint64_t *old=cx_payload(cx,cx_get(cx,cx->n))+24;
        h.tag=s[27]?1:s[26]>old[26]?3:2;
    }
    return cx_append(cx,&h,p,PW,&id);
}
/* Reconstruct and replay each recorded transition. A correctly rehashed lie
 * still fails. A stale context never gets a free initialized search. */
static int reload(CxStore *cx,const uint64_t ctx[24],const uint64_t c[16],uint64_t s[NW]) {
    reloads++;
    uint64_t expected[NW];start(expected,(unsigned)ctx[20],(unsigned)ctx[21],(unsigned)ctx[22],(unsigned)ctx[23]);
    if(!cx->n||cx->n>29)return -1;
    for(uint64_t id=1;id<=cx->n;id++) {
        const CxObject *o=cx_get(cx,id);
        if(!o||o->cls!=CX_CLAIM||o->kind!=CX_K_CANDIDATE||o->n!=PW||o->generation!=6001||
           o->branch!=1||o->links[0]!=id-1||o->subject!=0||o->t!=id||
           o->protect!=CX_PROT_GENERATION_ID||o->links[1]||o->links[2]||o->links[3]||cx_verify(cx,id))return -1;
        const uint64_t *p=cx_payload(cx,o);
        int stale=memcmp(p,ctx,24*8)!=0;
#ifdef EX600_SKIP_CONTEXT
        stale=0; /* Negative control, never set for a qualification binary. */
#endif
        if(stale)return -1;
        if(id==1&&o->tag!=0)return -1;
        if(id>1){
            if(expected[10]||o->tag<1||o->tag>3)return -1;
            if((o->tag==1&&expected[27])||(o->tag!=1&&!expected[27]))return -1;
            int fi=o->tag==1?reserve_fn:o->tag==2?step_fn:abandon_fn;
            call(expected,c,fi,0,0,0,0);replayed_transitions++;
        }
        if(memcmp(p+24,expected,NW*8))return -1;
    }
    memcpy(s,expected,NW*8);return 0;
}
static void saved_tick(CxStore *cx,const uint64_t ctx[24],uint64_t s[NW],const uint64_t c[16]) {
    if(s[10])return;
    call(s,c,reserve_fn,0,0,0,0);
    if(append(cx,ctx,s))fatal("reservation append");
    if(getenv("EX600_CUT")&&!strcmp(getenv("EX600_CUT"),"reserved"))_exit(99);
    if(s[10])return;
    call(s,c,step_fn,0,0,0,0);
    if(getenv("EX600_CUT")&&!strcmp(getenv("EX600_CUT"),"evaluated"))_exit(99);
    if(append(cx,ctx,s))fatal("result append");
}
static void path(char *out,size_t n,const char *dir,const char *name) {
    if(snprintf(out,n,"%s/%s",dir,name)<0 || strlen(out)>=n-1)fatal("path");
}
static void write_landscapes(const char *dir,unsigned first,unsigned last,unsigned seed_first,unsigned seed_last) {
    char p[4096];path(p,sizeof p,dir,"landscapes.csv");FILE *f=fopen(p,"wx");if(!f)fatal("landscapes exists");
    fprintf(f,"family,seed,node,parent,depth,cost,valid,reachable,optimum\n");
    for(unsigned family=first;family<=last;family++)for(unsigned seed=seed_first;seed<=seed_last;seed++) {
        uint64_t c[16];fixture(family,seed,c);
        for(unsigned node=0;node<15;node++)fprintf(f,"%u,%u,%u,%u,%u,%"PRIu64",%u,%d,%"PRIu64"\n",
            family,seed,node,node?(node-1)/2:0,depth(node),c[node],(unsigned)((c[15]>>node)&1),reachable(node,c),optimum(c));
    }
    if(fclose(f))fatal("landscape close");
}
static void checkpoint_tests(const char *dir) {
    uint64_t c[16],ctx[24],base[NW];fixture(0,1,c);context(ctx,c,3,6001,160,4);
    start(base,3,6001,160,4);finish(base,c);
    for(unsigned cut=0;cut<=base[5];cut++) {
        char name[64],p[4096];snprintf(name,sizeof name,"cut-%u.journal",cut);path(p,sizeof p,dir,name);
        CxStore cx;ck(cx_open(&cx,p,1,CX_OPEN_SYNC)==0,"open private Cortex journal");
        uint64_t s[NW];start(s,3,6001,160,4);ck(!append(&cx,ctx,s),"initial append");
        for(unsigned k=0;k<cut;k++)saved_tick(&cx,ctx,s,c);
        cx_free(&cx);ck(cx_open(&cx,p,1,CX_OPEN_SYNC)==0,"reopen checkpoint");
        memset(s,0,sizeof s);ck(!reload(&cx,ctx,c,s),"recover exact history");
        for(unsigned k=0;k<16&&s[10]==0;k++)saved_tick(&cx,ctx,s,c);
        ck(!memcmp(s,base,sizeof s),"cut versus uncut exact state");
        uint64_t n=cx.n;ck(!reload(&cx,ctx,c,s)&&cx.n==n,"repeat completed has no append or effect");
        for(unsigned word=0;word<24;word++){uint64_t wrong[24];memcpy(wrong,ctx,sizeof ctx);wrong[word]^=1;
            ck(reload(&cx,wrong,c,s)!=0,"stale context refused");}
        uint64_t forged[NW];memcpy(forged,s,sizeof forged);forged[4]=0;
        /* Terminal histories with29 records cannot append: test a lie by
         * mutating an in-memory record then rebuilding its Cortex digest. */
        CxObject *last=&cx.obj[cx.n-1];
        memcpy(cx.arena+last->off+24,forged,sizeof forged);
        cx_digest(last,cx_payload(&cx,last),last->digest);
        ck(reload(&cx,ctx,c,s)!=0,"rehashed budget lie refused");cx_free(&cx);
        struct stat st;ck(!stat(p,&st)&&st.st_size<=MAX_JOURNAL,"checkpoint storage ceiling");
    }
    /* Real journal byte corruption and torn writes, separate from forged state. */
    for(unsigned torn=0;torn<2;torn++) {
        char p[4096];path(p,sizeof p,dir,torn?"torn.journal":"corrupt.journal");CxStore cx;
        ck(!cx_open(&cx,p,1,CX_OPEN_SYNC),"open damage fixture");
        ck(!append(&cx,ctx,base),"damage fixture append");cx_free(&cx);
        FILE *f=fopen(p,"r+b");if(!f)fatal("damage open");
        if(torn){ck(!ftruncate(fileno(f),40),"tear journal");}
        else {ck(!fseek(f,-1,SEEK_END),"seek damage");int v=fgetc(f);ck(v!=EOF&&!fseek(f,-1,SEEK_END)&&fputc(v^1,f)!=EOF,"flip digest byte");}
        fclose(f);ck(cx_open(&cx,p,1,CX_OPEN_SYNC)!=0,"corrupt/torn Cortex refused");
    }
}
static void tests(const char *dir) {
    write_landscapes(dir,0,2,1,3);
    uint64_t c[16],s[NW],a[NW];
    for(unsigned f=0;f<3;f++)for(unsigned seed=1;seed<=3;seed++) {
        fixture(f,seed,c);ck(optimum(c)==(f==0?20:f==1?70:100),"independent finite optimum");
        for(unsigned arm=0;arm<4;arm++){
            start(s,arm,seed,160,arm<2?8:4);finish(s,c);
            if(f==0&&arm==0)ck(s[7]==100,"greedy misses valley");
            if(f==0&&arm==2)ck(s[7]==20,"C crosses inferior valley");
            if(f==1)ck(s[7]==70,"monotone optimum");
            if(f==2)ck(s[7]==100&&s[4]>0,"no-payoff cost retained");
        }
    }
    fixture(0,1,c);
    for(unsigned b=0;b<20;b++){start(s,2,1,b,4);finish(s,c);ck(s[10]==2,"insufficient budget unresolved");}
    start(s,2,1,9,4);call(s,c,step_fn,0,0,0,0);ck(s[4]==9&&s[5]==1,"exact first tranche boundary");
    start(s,2,1,1025,4);ck(s[10]==3,"oversize budget refused");
    start(s,2,0,160,4);ck(s[10]==3,"zero seed refused");
    start(s,2,1,160,0);ck(s[10]==3,"zero capacity refused");
    start(s,2,1,160,1);finish(s,c);ck(s[13]==1&&s[12]>0,"capacity abandonment charged");
    start(s,2,1,160,4);s[4]=161;call(s,c,step_fn,0,0,0,0);ck(s[10]==3,"spent overflow refused");
    start(s,2,1,160,4);s[8]|=1;call(s,c,step_fn,0,0,0,0);ck(s[10]==3,"duplicate frontier refused");
    start(s,2,1,160,4);s[9]=0;call(s,c,step_fn,0,0,0,0);ck(s[10]==3,"missing reference identity refused");
    start(s,2,1,160,4);s[5]=14;call(s,c,step_fn,0,0,0,0);ck(s[10]==3,"false exposure count refused");
    start(s,2,1,160,4);c[15]^=1;call(s,c,step_fn,0,0,0,0);ck(s[10]==3,"invalid reference refused");c[15]^=1;
    start(s,2,1,160,4);call(s,c,reserve_fn,0,0,0,0);ck(s[4]==9&&s[27]==1,"prepaid interruption reservation");
    call(s,c,abandon_fn,0,0,0,0);ck(s[4]==9&&s[26]==1&&s[5]==0&&s[8]==6,"interruption keeps failed work charged");
    call(s,c,step_fn,0,0,0,0);ck(s[4]==18&&s[28]==2&&s[5]==1,"retry pays fresh tranche");
    fixture(6,6001,c);start(s,2,1,160,4);finish(s,c);ck(s[7]==70&&s[11]==1,"invalid shortcut never selected or expanded");
    fixture(2,1,c);start(s,2,1,160,4);call(s,c,step_fn,0,0,0,0);
    ck(s[4]==9&&s[21]==3&&s[22]==2&&s[23]==2&&s[20]==2,"unsuccessful trial fully charged");
    memcpy(a,s,sizeof a);finish(s,c);call(s,c,step_fn,0,0,0,0);ck(s[5]>a[5],"repeat completion remains bounded");
    checkpoint_tests(dir);
}
static unsigned false_pruned(const uint64_t s[NW],const uint64_t c[16]) {
    unsigned n=0;for(unsigned i=1;i<15;i++)if(reachable(i,c)&&!(s[9]&(1u<<i))&&c[i]<s[7])n++;return n;
}
static void campaign(const char *dir) {
    write_landscapes(dir,3,7,6001,6005);
    char p[4096];path(p,sizeof p,dir,"rows.csv");FILE *rows=fopen(p,"wx");if(!rows)fatal("rows exists");
    fprintf(rows,"family,seed,arm,horizon,stop,trials,best_node,best_cost,optimum,regret,spent,search,checking,checkpoint,storage,peak_branches,checkpoint_bytes,abandoned,refused,false_pruned,lifecycle,net,break_even,journal_bytes\n");
    path(p,sizeof p,dir,"trace.csv");FILE *trace=fopen(p,"wx");if(!trace)fatal("trace exists");
    fprintf(trace,"family,seed,arm,trial,node,valid,spent,best,best_node,frontier,abandoned,refused\n");
    int overall=1;
    for(unsigned f=3;f<8;f++)for(unsigned seed=6001;seed<=6005;seed++){
        uint64_t c[16],all[4][NW];fixture(f,seed,c);
        for(unsigned arm=0;arm<4;arm++) {
            uint64_t *s=all[arm];start(s,arm,seed,160,arm<2?8:4);
            uint64_t ctx[24];context(ctx,c,arm,seed,160,arm<2?8:4);
            char name[80];snprintf(name,sizeof name,"f%u-s%u-a%u.journal",f,seed,arm);
            path(p,sizeof p,dir,name);CxStore cx;
            if(cx_open(&cx,p,1,CX_OPEN_SYNC)||cx.n)fatal("campaign journal is not new");
            if(append(&cx,ctx,s))fatal("campaign initial checkpoint");
            for(unsigned k=0;k<16&&s[10]==0;k++) {
                saved_tick(&cx,ctx,s,c);
                fprintf(trace,"%u,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n",f,seed,arm,s[5],s[24],s[25],s[4],s[7],s[6],s[8],s[12],s[11]);
            }
            finish(s,c);
            uint64_t recovered[NW];cx_free(&cx);
            if(cx_open(&cx,p,1,CX_OPEN_SYNC))fatal("campaign reopen");
            ck(!reload(&cx,ctx,c,recovered)&&!memcmp(recovered,s,NW*8),"campaign recovery exact");
            cx_free(&cx);struct stat st;
            if(stat(p,&st)||st.st_size>MAX_JOURNAL)fatal("campaign journal capacity");
            ck((uint64_t)st.st_size==32+840*(2*s[5]+1),"all journal bytes accounted");
            const unsigned horizons[]={1,8,32};
            for(unsigned h=0;h<3;h++) {
                uint64_t H=horizons[h],life=s[4]+H*s[7],opt=optimum(c);
                int64_t net=(int64_t)(H*100)-(int64_t)life;
                uint64_t be=s[7]<100?s[4]/(100-s[7])+1:0;
                fprintf(rows,"%u,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%"PRIu64",%"PRId64",%"PRIu64",%jd\n",
                    f,seed,arm,H,s[10],s[5],s[6],s[7],opt,s[7]-opt,s[4],s[22],s[21],s[23],s[20],s[13],s[14],s[12],s[11],false_pruned(s,c),life,net,be,(intmax_t)st.st_size);
            }
        }
        uint64_t c_life=all[2][4]+32*all[2][7];
        if(c_life>=3200||c_life>=all[0][4]+32*all[0][7]||c_life>=all[3][4]+32*all[3][7])overall=0;
    }
    if(fclose(rows)||fclose(trace))fatal("output close");
    path(p,sizeof p,dir,"experiment.json");FILE *j=fopen(p,"wx");if(!j)fatal("experiment exists");
    fprintf(j,"{\"hypothesis\":\"%s\",\"provenance\":\"synthetic\",\"hardware\":\"NOT_RUN\",\"model\":\"NOT_RUN\",\"joules\":\"UNMEASURED\",\"independent_review\":\"PENDING\"}\n",overall?"PASS":"FAIL");fclose(j);
}
/* Separate process restart seam, fixed dev fixture/arm. ticks=0 is a read-only
 * repetition. ticks=1 checkpoints one tranche. No independent file format. */
static void session(const char *dir,unsigned ticks) {
    uint64_t c[16],ctx[24],s[NW];fixture(0,1,c);context(ctx,c,3,6001,160,4);
    char p[4096];path(p,sizeof p,dir,"session.journal");CxStore cx;
    if(cx_open(&cx,p,1,CX_OPEN_SYNC))fatal("session journal integrity");
    if(!cx.n){start(s,3,6001,160,4);if(append(&cx,ctx,s))fatal("session initial");}
    else if(reload(&cx,ctx,c,s))fatal("session binding/history");
    if(s[27]) {
        call(s,c,abandon_fn,0,0,0,0);if(append(&cx,ctx,s))fatal("abandon interrupted reservation");
    }
    for(unsigned k=0;k<ticks&&s[10]==0;k++){
        saved_tick(&cx,ctx,s,c);
    }
    printf("session stop=%"PRIu64" trials=%"PRIu64" spent=%"PRIu64" records=%"PRIu64" interrupted=%"PRIu64" attempts=%"PRIu64"\n",s[10],s[5],s[4],cx.n,s[26],s[28]);
    cx_free(&cx);
}
int main(int argc,char **argv) {
    struct timespec t0,t1,c0,c1;clock_gettime(CLOCK_MONOTONIC,&t0);clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&c0);
    if(argc!=4|| (strcmp(argv[1],"checks")&&strcmp(argv[1],"heldout")&&strcmp(argv[1],"one")&&strcmp(argv[1],"resume")&&strcmp(argv[1],"repeat")))fatal("usage: ex600 checks|heldout|one|resume|repeat policy.osc DIR");
    struct stat st;if(stat(argv[3],&st)||!S_ISDIR(st.st_mode))fatal("output directory");
    load(argv[2]);
    if(!strcmp(argv[1],"checks"))tests(argv[3]);
    else if(!strcmp(argv[1],"heldout"))campaign(argv[3]);
    else session(argv[3],!strcmp(argv[1],"one")?1:!strcmp(argv[1],"resume")?16:0);
    clock_gettime(CLOCK_MONOTONIC,&t1);clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&c1);
    struct rusage usage;if(getrusage(RUSAGE_SELF,&usage))fatal("rusage");
    printf("host_wall_seconds=%.9f host_cpu_seconds=%.9f host_peak_rss_kib=%ld reloads=%"PRIu64" replayed_transitions=%"PRIu64" (diagnostic_shared_host_only)\n",
        (double)(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9,
        (double)(c1.tv_sec-c0.tv_sec)+(c1.tv_nsec-c0.tv_nsec)*1e-9,usage.ru_maxrss,reloads,replayed_transitions);
    printf("checks=%d failures=%d native_code_bytes=%zu\n",checks,failures,code.len);
    osc_native_unmap(&native);osc_cg_free(&code);free(unit);return failures?1:0;
}
