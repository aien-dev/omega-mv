#include "sha256.h"
#include <stdio.h>
#include <string.h>
void orc_sha256_hash(const uint8_t*,size_t,uint8_t*); void orc_sha256_init(sha256_ctx*); void orc_sha256_update(sha256_ctx*,const uint8_t*,size_t); void orc_sha256_final(sha256_ctx*,uint8_t*);
int main(void){ static uint8_t b[9001]; for(int i=0;i<9001;i++) b[i]=(uint8_t)(i*131+7); int bad=0;
 for(size_t n=0;n<=9000;n++){ uint8_t a[32],c[32],d[32],e[32]; orc_sha256_hash(b,n,a); sha256_hash(b,n,c);
  sha256_ctx x,y; orc_sha256_init(&x); sha256_init(&y); size_t h=n/3; orc_sha256_update(&x,b,h); sha256_update(&y,b,h); orc_sha256_update(&x,b+h,n-h); sha256_update(&y,b+h,n-h); orc_sha256_final(&x,d); sha256_final(&y,e);
  if(memcmp(a,c,32)||memcmp(d,e,32)||memcmp(a,d,32)) { if(bad++<5) printf("DIFF %zu\n",n);} }
 printf("lengths 0..9000: diff=%d\n",bad); return bad!=0; }
