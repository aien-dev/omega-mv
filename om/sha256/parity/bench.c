#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
void orc_sha256_hash(const uint8_t*,size_t,uint8_t*);
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){ size_t n=64<<20; uint8_t *b=malloc(n),h[32]; for(size_t i=0;i<n;i++)b[i]=(uint8_t)i;
 double t=now(); orc_sha256_hash(b,n,h); double c=now()-t; t=now(); sha256_hash(b,n,h); double o=now()-t;
 printf("C %.1f MB/s  omega %.1f MB/s  ratio %.1fx\n", n/c/1e6, n/o/1e6, o/c);
 t=now(); for(int i=0;i<200000;i++) sha256_hash(b,32,h); double s=now()-t; t=now(); for(int i=0;i<200000;i++) orc_sha256_hash(b,32,h); double sc=now()-t;
 printf("32-byte hash: C %.2f us  omega %.2f us\n", sc/2e5*1e6, s/2e5*1e6); }
