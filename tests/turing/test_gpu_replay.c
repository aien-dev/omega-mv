/* SYNTHETIC qualification oracle only. No real receipt is qualified here. */
#include "turing_gpu_replay.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); } } while (0)
static tgr_query q;
static tgr_candidate c[3];
static tgr_observation observations[3];
static int rejected[3];
static const uint8_t fixtures[3][2] = {{'A','0'},{'B','1'},{'C','2'}};
static int verify_fixture(const uint8_t *p, size_t n, tgr_observation *out, void *ctx) {
    (void)ctx;
    for (size_t i=0;i<3;++i) if (n==2 && !memcmp(p,fixtures[i],2) && !rejected[i]) {
        *out=observations[i]; return 0;
    }
    return -1;
}
static void reset(void) {
    memset(&q,0,sizeof q); memset(c,0,sizeof c); memset(observations,0,sizeof observations);
    memset(rejected,0,sizeof rejected);
    for (size_t j=0;j<TGR_BINDINGS;++j) memset(q.binding.id[j],(int)j+1,32);
    q.binding.reuse=1; q.now=100; q.max_age=10;
    q.max_registers=64; q.max_resident_bytes=4096; q.max_scratch_bytes=512;
    for (size_t i=0;i<3;++i) {
        memset(c[i].id,(int)i+40,32); memset(c[i].binary,(int)i+60,32);
        c[i].bytes=fixtures[i]; c[i].size=2;
        sha256_hash(c[i].bytes,c[i].size,c[i].receipt_sha256);
        observations[i].binding=q.binding;
        memcpy(observations[i].binary,c[i].binary,32);
        observations[i].measured_at=95; observations[i].total_ns=100-i*20;
        observations[i].submit_host_ns=50; observations[i].device_ns=40;
        observations[i].resident_bytes=2048; observations[i].scratch_bytes=256;
        observations[i].registers=32; observations[i].samples=10;
    }
}
static tgr_decision replay(size_t n) {
    tgr_decision d;
    CHECK(tgr_replay(&q,c,n,verify_fixture,NULL,&d)==0);
    return d;
}
static void refuses(tgr_reason reason) {
    tgr_decision d=replay(2);
    CHECK(d.chosen==0); CHECK(d.fallback); CHECK(d.reason[1]==reason);
    CHECK(d.total_ns[1]==0);
}
int main(void) {
    reset(); tgr_decision d=replay(3); CHECK(d.chosen==2); CHECK(!d.fallback);
    /* Independent exact min oracle over every small positive cost combination. */
    for (unsigned a=1;a<=5;++a) for (unsigned b=1;b<=5;++b) for (unsigned z=1;z<=5;++z) {
        reset(); observations[0].total_ns=a; observations[1].total_ns=b; observations[2].total_ns=z;
        for (int i=0;i<3;++i) observations[i].submit_host_ns=observations[i].device_ns=1;
        unsigned costs[3]={a,b,z}; int best=0;
        for(int i=1;i<3;++i) if(costs[i]<costs[best]) best=i;
        d=replay(3); CHECK(d.chosen==best);
    }
    for (size_t j=0;j<TGR_BINDINGS;++j) { reset(); observations[1].binding.id[j][0]^=1; refuses(TGR_BINDING); }
    reset(); observations[1].binding.reuse=2; refuses(TGR_BINDING);
    reset(); c[1].receipt_sha256[0]^=1; refuses(TGR_DIGEST);
    reset(); c[1].bytes=(const uint8_t *)"B2"; refuses(TGR_DIGEST);
    reset(); rejected[1]=1; refuses(TGR_UNQUALIFIED);
    reset(); observations[1].binary[0]^=1; refuses(TGR_BINARY);
    reset(); observations[1].measured_at=89; refuses(TGR_STALE);
    reset(); observations[1].measured_at=101; refuses(TGR_STALE);
    reset(); observations[1].registers=65; refuses(TGR_RESOURCE);
    reset(); observations[1].resident_bytes=4097; refuses(TGR_RESOURCE);
    reset(); observations[1].scratch_bytes=513; refuses(TGR_RESOURCE);
    reset(); observations[1].total_ns=0; refuses(TGR_COST);
    reset(); observations[1].samples=0; refuses(TGR_COST);
    reset(); observations[1].device_ns=51; refuses(TGR_COST);
    reset(); observations[1].submit_host_ns=81; refuses(TGR_COST);
    reset(); observations[1].total_ns=UINT64_MAX; refuses(TGR_COST);
    reset(); c[1].bytes=NULL; refuses(TGR_INVALID);
    reset(); c[1].size=0; refuses(TGR_INVALID);
    reset(); observations[1].registers=0; refuses(TGR_RESOURCE);
    reset(); rejected[0]=1; CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==1); CHECK(d.chosen==-1);
    reset(); CHECK(tgr_replay(&q,c,3,NULL,NULL,&d)==1); CHECK(d.chosen==-1);
    reset(); memcpy(c[1].id,c[0].id,32); CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==-1);
    reset(); q.incumbent=3; CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==-1);
    reset(); memset(q.binding.id[0],0,32); CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==-1);
    reset(); q.binding.reuse=0; CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==-1);
    reset(); q.max_age=0; CHECK(tgr_replay(&q,c,3,verify_fixture,NULL,&d)==-1);
    reset(); observations[1].measured_at=90; observations[1].registers=64;
    observations[1].resident_bytes=4096; observations[1].scratch_bytes=512;
    d=replay(2); CHECK(d.chosen==1); /* inclusive boundaries */
    reset(); q.incumbent=2; observations[0].total_ns=observations[1].total_ns=60;
    d=replay(3); CHECK(d.chosen==2); /* incumbent beats equal earlier index */
    reset(); q.incumbent=2; observations[0].total_ns=observations[1].total_ns=55;
    d=replay(3); CHECK(d.chosen==0); /* nonincumbent ties in declared order */
    reset(); tgr_decision before=replay(3), after=replay(3);
    CHECK(!memcmp(&before,&after,sizeof before));
    printf("SYNTHETIC gpu admission/replay: %u checks, %u failed; real qualification NOT_RUN\n",checks,failed);
    return failed ? 1:0;
}
