/* L491 independent integration tests of existing M20/M21/M22 APIs only.
 * No model training campaign, new optimizer, GPU operator, or product runtime. */
#include "omega_autodiff.h"
#include "train/tg_sgd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned pass,fail;
#define C(x) do {if(x)pass++;else{fail++;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)
#define A(x) do{int rc=(x);if(rc){fprintf(stderr,"API rc=%d line=%d\n",rc,__LINE__);exit(2);}}while(0)
static const float input[2]={1.25f,-0.75f}, target[2]={0.5f,-0.25f};
static void masks(uint32_t *rng,float *m) {
    for(int i=0;i<2;i++){*rng=*rng*1664525u+1013904223u;m[i]=(*rng>>31)?2.0f:0.0f;}
}
static uint32_t record(OmegaAdTape *t,OmegaTensor *x){
    uint32_t w,in,m,y,a,b,e,s,root;
    A(omega_ad_leaf(t,x[0],true,&w));A(omega_ad_leaf(t,x[1],false,&in));
    A(omega_ad_leaf(t,x[2],false,&m));A(omega_ad_leaf(t,x[3],false,&y));
    A(omega_ad_binary(t,OMEGA_AD_MUL,w,in,&a));
    A(omega_ad_binary(t,OMEGA_AD_MUL,a,m,&b));
    A(omega_ad_binary(t,OMEGA_AD_SUB,b,y,&e));
    A(omega_ad_binary(t,OMEGA_AD_MUL,e,e,&s));
    A(omega_ad_reduce(t,OMEGA_AD_SUM,s,0,false,&root));return root;
}
static float grads(const float *w,const float *mask,int recompute,float *g) {
    OmegaTensorCtx *ctx=NULL;OmegaAdTape t;OmegaAdNode nodes[24];OmegaTensor x[4],loss;
    uint64_t shape[1]={2};A(omega_tensor_ctx_create(128,omega_tensor_cpu_realization(),&ctx));
    A(omega_tensor_from_f32(ctx,1,shape,w,x));A(omega_tensor_from_f32(ctx,1,shape,input,x+1));
    A(omega_tensor_from_f32(ctx,1,shape,mask,x+2));A(omega_tensor_from_f32(ctx,1,shape,target,x+3));
    A(omega_ad_tape_init(&t,ctx,nodes,24,NULL,0));uint32_t root=record(&t,x);
    A(omega_ad_value(&t,root,&loss));float before;A(omega_tensor_read_f32(ctx,loss,&before,1));
    if(recompute){omega_ad_tape_release(&t);A(omega_ad_tape_init(&t,ctx,nodes,24,NULL,0));root=record(&t,x);}
    A(omega_ad_backward(&t,root));uint32_t ids[1]={0};
    A(omega_ad_grads_flatten(&t,ids,1,g,2));
    C(omega_ad_backward(&t,root)==OMEGA_AD_ERR_STATE);
    C(omega_ad_grads_flatten(&t,ids,1,g,1)==OMEGA_AD_ERR_COUNT);
    omega_ad_tape_release(&t);
    for(int i=0;i<4;i++)A(omega_tensor_release(ctx,x[i]));
    omega_tensor_ctx_destroy(ctx);return before;
}
static double objective(const double *w,const float *m) {
    double s=0;for(int i=0;i<2;i++){double e=w[i]*(double)input[i]*m[i]-target[i];s+=e*e;}return s;
}
static void numerical(void){
    uint32_t rng=491;unsigned rng_changes=0;
    for(int t=0;t<24;t++){
        float w[2]={0.25f+(float)t/32,0.5f-(float)t/64},mask[2],again[2],bad[2],g[2],r[2];
        uint32_t saved=rng;masks(&rng,mask);uint32_t replay=saved;
#ifdef L491_MUTATE_RNG
        replay=rng; /* Deliberately reuse advanced stochastic state. Must fail. */
#endif
        masks(&replay,again);C(!memcmp(mask,again,sizeof mask));C(replay==rng);
        float a=grads(w,mask,0,g),b=grads(w,again,1,r);C(a==b);C(!memcmp(g,r,sizeof g));
        uint32_t wrong=rng;masks(&wrong,bad);rng_changes+=memcmp(mask,bad,sizeof mask)!=0;
        for(int i=0;i<2;i++){
            double exact=2.0*input[i]*mask[i]*((double)w[i]*input[i]*mask[i]-target[i]);
            C((exact==0&&g[i]==0)||(exact!=0&&fabs(g[i]-exact)<=1e-3*fabs(exact)));
            double hi[2]={w[0],w[1]},lo[2]={w[0],w[1]};hi[i]+=0.0001;lo[i]-=0.0001;
            double fd=(objective(hi,mask)-objective(lo,mask))/0.0002;
            C(fabs(fd-exact)<=1e-8*(1+fabs(exact)));
        }
    }C(rng_changes>0);
}
static int valid(const tg_shadow *sh,const tg_snapshot *old,void *ctx){
    (void)old;(void)ctx;if(sh->param_bytes!=8||sh->opt_bytes!=8)return 1;
    const float *p=(const float*)(const void*)sh->params;return !(isfinite(p[0])&&isfinite(p[1]));
}
static void step(tg_store *st,int recompute,float *ref,uint32_t *ref_rng){
    tg_shadow sh;A(tg_shadow_begin(st,&sh));uint32_t state[2];memcpy(state,sh.opt,8);C(state[1]==491);
    float mask[2],g[2],expected_mask[2];uint32_t next=state[0];masks(&next,mask);
    masks(ref_rng,expected_mask);C(!memcmp(mask,expected_mask,8));
    float w[2];memcpy(w,sh.params,8);grads(w,mask,recompute,g);
    for(int i=0;i<2;i++){
        /* Scalar calculus oracle with every f32 rounding boundary explicit.
         * The separate double/finite-difference check above bounds real error. */
        float prod=ref[i]*input[i];float masked=prod*mask[i];float err=masked-target[i];
        float twice=err+err;float masked_grad=twice*mask[i];float deriv=masked_grad*input[i];
        float delta=0.03125f*deriv;ref[i]=ref[i]-delta;
    }
    A(tg_sgd_step(st,&sh,g,2,0.03125f,0.0f,sh.base_step+1));
    state[0]=next;memcpy(sh.opt,state,8);
    tg_ref refs[1]={{TG_BUF_OPT,0,8}};
    A(tg_dispatch(st,&sh,0x4c343931u,491,next,refs,1)); /* fixture RNG checkpoint record */
    A(tg_commit(st,&sh,valid,NULL));const tg_snapshot *s=tg_committed(st);
    C(!memcmp(s->params,ref,8));C(!memcmp(s->opt,state,8));
}
static void checkpoint(const char *base){
    char a[4096],b[4096];if(snprintf(a,sizeof a,"%s/retain",base)>=(int)sizeof a)exit(2);
    if(snprintf(b,sizeof b,"%s/recompute",base)>=(int)sizeof b)exit(2);
    float init[2]={0.25f,0.5f};uint32_t state[2]={123456,491};
    A(tg_create(a,init,8,state,8));A(tg_create(b,init,8,state,8));
    tg_store *st[2];A(tg_open(a,st,NULL));A(tg_open(b,st+1,NULL));
    float ref[2][2]={{0.25f,0.5f},{0.25f,0.5f}};uint32_t rng[2]={123456,123456};
    for(int t=0;t<12;t++){
        step(st[0],0,ref[0],rng);step(st[1],1,ref[1],rng+1);
        const tg_snapshot *s=tg_committed(st[0]),*r=tg_committed(st[1]);
        C(s->step==(uint64_t)t+1&&r->step==s->step);C(!memcmp(s->params,r->params,8));C(!memcmp(s->opt,r->opt,8));
        C(!memcmp(s->digest,r->digest,TG_DIGEST));
        if(t==3||t==7){tg_close(st[1]);st[1]=NULL;A(tg_open(b,st+1,NULL));}
    }
    tg_shadow old,now;A(tg_shadow_begin(st[0],&old));A(tg_shadow_begin(st[0],&now));
    float g[2]={1,2};A(tg_sgd_step(st[0],&now,g,2,0.03125f,0.0f,99));A(tg_commit(st[0],&now,valid,NULL));
    C(tg_commit(st[0],&old,valid,NULL)==TG_E_STALE);tg_shadow_discard(&old);
    A(tg_shadow_begin(st[0],&now));C(tg_sgd_step(st[0],&now,g,2,NAN,0,0)==TG_E_ARG);tg_shadow_discard(&now);
    for(int i=0;i<2;i++)tg_close(st[i]);
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    numerical();checkpoint(argv[1]);
    printf("L491 actual CPU autodiff/SGD/RNG/checkpoint: pass=%u fail=%u\n",pass,fail);return fail?1:0;
}
