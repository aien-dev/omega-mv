#include "sha256.h"
#include <stdio.h>
#include <time.h>
void orc_sha256_hash(const uint8_t*,size_t,uint8_t*);
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){ static uint8_t b[256]; uint8_t h[32]; int N=400000; size_t L[]={0,32,55,64,100,200};
 for(int j=0;j<6;j++){ double t=now(); for(int i=0;i<N;i++) sha256_hash(b,L[j],h); double o=now()-t; t=now(); for(int i=0;i<N;i++) orc_sha256_hash(b,L[j],h); double c=now()-t;
 printf("len %3zu: omega %.0f ns  C %.0f ns\n",L[j],o/N*1e9,c/N*1e9);} }
