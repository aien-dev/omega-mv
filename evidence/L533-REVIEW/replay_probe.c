/* Independent L533 host-only review. Synthetic bytes, never GPU qualification. */
#include "turing_gpu_replay.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x); exit(1);} ++checks;}while(0)
static unsigned checks,calls;
static tgr_query q;
static tgr_candidate c[TGR_MAX];
static tgr_observation obs[TGR_MAX];
static uint8_t bytes[TGR_MAX][2];
static int mode;
static int verifier(const uint8_t *p,size_t n,tgr_observation *out,void *ctx){
 (void)ctx; ++calls; REQUIRE(n==2); unsigned i=p[0]; REQUIRE(i<TGR_MAX);
 REQUIRE(p!=c[i].bytes); REQUIRE(p[1]==0x51);
 if(mode==1){bytes[i][1]=0x99; REQUIRE(p[1]==0x51);} /* original storage changes after snapshot */
 if(mode==2){((uint8_t*)p)[1]=0x99;} /* violates verifier contract, intentionally */
 if(mode==3)return -1;
 *out=obs[i];return 0;
}
static void setup(void){
 memset(&q,0,sizeof q);memset(c,0,sizeof c);memset(obs,0,sizeof obs);mode=0;calls=0;
 q.binding.reuse=7; q.now=UINT64_MAX; q.max_age=20;
 q.max_registers=64;q.max_resident_bytes=4096;q.max_scratch_bytes=1024;
 for(unsigned j=0;j<TGR_BINDINGS;j++)memset(q.binding.id[j],j+1,32);
 for(unsigned i=0;i<TGR_MAX;i++){
  bytes[i][0]=i;bytes[i][1]=0x51;c[i].bytes=bytes[i];c[i].size=2;
  memset(c[i].id,i+1,32);memset(c[i].binary,i+20,32);
  sha256_hash(bytes[i],2,c[i].receipt_sha256);
  obs[i].binding=q.binding;memcpy(obs[i].binary,c[i].binary,32);
  obs[i].measured_at=UINT64_MAX-20;obs[i].total_ns=100+i;
  obs[i].submit_host_ns=obs[i].device_ns=1;
  obs[i].registers=64;obs[i].resident_bytes=4096;obs[i].scratch_bytes=1024;obs[i].samples=1;
 }
}
int main(void){
 tgr_decision d;
 /* Independent integer min, all set sizes/incumbents and 16 rotations near double's exact limit. */
 for(unsigned n=1;n<=TGR_MAX;n++)for(unsigned incumbent=0;incumbent<n;incumbent++)for(unsigned r=0;r<16;r++){
  setup();q.incumbent=incumbent;
  for(unsigned i=0;i<n;i++)obs[i].total_ns=((UINT64_C(1)<<53)-1)-((i+r)%7);
  unsigned best=incumbent;for(unsigned i=0;i<n;i++)if(obs[i].total_ns<obs[best].total_ns || (obs[i].total_ns==obs[best].total_ns && best!=incumbent && i<best))best=i;
  REQUIRE(tgr_replay(&q,c,n,verifier,NULL,&d)==0);REQUIRE(d.chosen==(int)best);REQUIRE(calls==n);
 }
 setup();mode=1;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(calls==2);
 setup();mode=2;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);
 puts("LIMIT: contract-violating verifier can mutate private snapshot and still admit; callback is trusted executable code, no isolation promised");
 setup();mode=3;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==1);REQUIRE(d.chosen==-1);
 setup();REQUIRE(tgr_replay(&q,c,2,NULL,NULL,&d)==1);REQUIRE(d.chosen==-1);
 setup();memcpy(c[15].id,c[0].id,32);REQUIRE(tgr_replay(&q,c,16,verifier,NULL,&d)==-1);REQUIRE(calls==0);
 setup();c[1].size=1024*1024+1;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_INVALID);REQUIRE(calls==1);
 setup();obs[1].total_ns=UINT64_C(1)<<53;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_COST);
 setup();obs[1].measured_at=UINT64_MAX-21;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_STALE);
 setup();obs[1].measured_at=0;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_STALE);
 setup();q.now=5;obs[0].measured_at=5;obs[1].measured_at=UINT64_MAX;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_STALE);
 setup();q.max_scratch_bytes=0;obs[0].scratch_bytes=0;REQUIRE(tgr_replay(&q,c,2,verifier,NULL,&d)==0);REQUIRE(d.reason[1]==TGR_RESOURCE);
 setup();REQUIRE(tgr_replay(NULL,c,2,verifier,NULL,&d)==-1);REQUIRE(tgr_replay(&q,NULL,2,verifier,NULL,&d)==-1);REQUIRE(tgr_replay(&q,c,0,verifier,NULL,&d)==-1);REQUIRE(tgr_replay(&q,c,17,verifier,NULL,&d)==-1);REQUIRE(tgr_replay(&q,c,2,verifier,NULL,NULL)==-1);
 printf("L533 replay independent checks=%u PASS; no real parser/runtime/hardware exercised\n",checks);return 0;
}
