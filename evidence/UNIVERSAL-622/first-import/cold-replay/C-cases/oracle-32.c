#include <stdint.h>
uint64_t calc(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) { return a+b+c+d+e+f; }

#include <stdio.h>
#include <inttypes.h>
int main(void){
uint64_t x[6]={0};while(scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 "",&x[0],&x[1],&x[2],&x[3],&x[4],&x[5])==6)printf("%" PRIu64 "\n",calc(x[0],x[1],x[2],x[3],x[4],x[5]));return 0;}
