#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void orc_sha256_init(sha256_ctx *);void orc_sha256_update(sha256_ctx *,const uint8_t*,size_t);void orc_sha256_final(sha256_ctx*,uint8_t*);void orc_sha256_hash(const uint8_t*,size_t,uint8_t*);
static uint64_t s=88172645463325252ULL; static uint64_t rnd(void){s^=s<<13;s^=s>>7;s^=s<<17;return s;}
int main(void){ static uint8_t buf[1<<21]; int cases=0, bad=0;
 size_t lens[]={0,1,3,55,56,57,63,64,65,119,120,127,128,129,1000,4095,4096,4097};
 for(int t=0;t<2000;t++){ size_t n = t<18? lens[t] : (t<1000? rnd()%300 : rnd()%(1<<21));
  for(size_t i=0;i<n;i++) buf[i]=(uint8_t)rnd();
  uint8_t a[32],b[32],c[32],d[32]; sha256_ctx x,y;
  orc_sha256_hash(buf,n,a); sha256_hash(buf,n,b);
  orc_sha256_init(&x); sha256_init(&y); size_t off=0; int parts=rnd()%5;
  for(int p=0;p<parts && off<n;p++){ size_t k=rnd()%(n-off+1); orc_sha256_update(&x,buf+off,k); sha256_update(&y,buf+off,k); off+=k; }
  orc_sha256_update(&x,buf+off,n-off); sha256_update(&y,buf+off,n-off);
  orc_sha256_final(&x,c); sha256_final(&y,d);
  cases++; if(memcmp(a,b,32)||memcmp(c,d,32)||memcmp(a,c,32)){bad++; if(bad<5) printf("DIFF n=%zu\n",n);} }
 /* NIST */
 const char *v[]={"","abc","abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
 for(int i=0;i<3;i++){uint8_t h[32]; sha256_hash((const uint8_t*)v[i],strlen(v[i]),h); for(int j=0;j<32;j++)printf("%02x",h[j]); printf("\n");}
 { sha256_ctx m; sha256_init(&m); memset(buf,'a',1000); for(int i=0;i<1000;i++) sha256_update(&m,buf,1000); uint8_t h[32]; sha256_final(&m,h); for(int j=0;j<32;j++)printf("%02x",h[j]); printf("\n"); }
 printf("cases=%d diff=%d\n",cases,bad); return bad!=0; }
