#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void omega_blake3_hash(const uint8_t *, size_t, uint8_t *); void orc_blake3_hash(const uint8_t *, size_t, uint8_t *);
static uint64_t s=88172645463325252ULL; static uint64_t rnd(void){s^=s<<13;s^=s>>7;s^=s<<17;return s;}
int main(int argc,char**argv){ static uint8_t buf[3<<20]; int cases=0,bad=0;
 /* official vectors: input i mod 251 */
 FILE *f=fopen(argv[1],"r"); char h[200]; size_t L; int nv=0, vb=0;
 while(fscanf(f,"%zu %199s",&L,h)==2){ for(size_t i=0;i<L;i++)buf[i]=(uint8_t)(i%251); uint8_t o[32]; omega_blake3_hash(buf,L,o); char x[65]; for(int j=0;j<32;j++)sprintf(x+2*j,"%02x",o[j]); nv++; if(strncmp(x,h,64)){vb++; printf("VEC FAIL %zu\n",L);} }
 size_t lens[]={0,1,63,64,65,1023,1024,1025,2047,2048,2049,3072,3073,4096,4097,8192,8193,16384,31744,102400};
 for(int t=0;t<1000;t++){ size_t n = t<20? lens[t] : (t<600? rnd()%9000 : rnd()%(3<<20)); for(size_t i=0;i<n;i++)buf[i]=(uint8_t)rnd();
  uint8_t a[32],b[32]; orc_blake3_hash(buf,n,a); omega_blake3_hash(buf,n,b); cases++; if(memcmp(a,b,32)){bad++; if(bad<5)printf("DIFF %zu\n",n);} }
 printf("vectors=%d vecfail=%d cases=%d diff=%d\n",nv,vb,cases,bad); return (bad||vb)!=0; }
