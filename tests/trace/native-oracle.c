/* TEST ORACLE ONLY. Existing native encoders independently produce adapter fixtures.
 * No engine linkage, hot-path change or new production implementation.
 */
#include "argus_abi.h"
#include "dual/rx_dual.h"
#include <stdio.h>
#include <string.h>
static int put(const char *root,const char *name,const unsigned char *b,size_t n){
 char p[4096];if(snprintf(p,sizeof p,"%s/%s",root,name)>=(int)sizeof p)return 1;
 FILE *f=fopen(p,"wb");if(!f)return 1;int bad=fwrite(b,1,n,f)!=n;if(fclose(f))bad=1;return bad;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 ArgusEvent ev={0},decoded={0};unsigned char raw[ARGUS_EVENT_SIZE];
 ev.version=ARGUS_ABI_VERSION;ev.class_=ARGUS_CLASS_SECURITY;ev.kind=ARGUS_EV_WORLD_COMMITTED;
 ev.outcome=ARGUS_OUTCOME_OK;ev.flags=ARGUS_FLAG_SYNTHETIC|(3u<<ARGUS_FLAG_STREAM_SHIFT);
 ev.sequence=1;ev.tick=7;ev.cap_id=ARGUS_CAP_NONE;ev.machine_id[31]=3;ev.evidence_digest[31]=8;
 if(argus_event_validate(&ev)||argus_event_encode(&ev,raw)||argus_event_decode(raw,&decoded)||decoded.tick!=7||put(argv[1],"argus.bin",raw,sizeof raw))return 1;
 RxDualRecommendation r={0},d={0};unsigned char buf[16384];size_t n=0;
 r.decision_site.b[31]=11;r.price_vector.b[31]=12;r.generation=1;r.n_candidates=1;r.candidates[0].b[31]=13;
 r.n_consequences=1;r.c[0].resource_id=0;r.c[0].unit=RX_DUAL_UNIT_NS;
 r.c[0].predicted=-10;r.c[0].predicted_sd=2;r.c[0].has_measured=1;r.c[0].measured=-8;r.c[0].error=1;
 if(rx_dual_encode_recommendation(&r,buf,sizeof buf,&n)||rx_dual_decode_recommendation(buf,n,&d)||d.c[0].measured!=-8||put(argv[1],"dual.bin",buf,n))return 1;
 puts("native oracle PASS: ARGUS v1.1 and DUAL v1 existing encoders/decoders, 2 synthetic records");return 0;
}
