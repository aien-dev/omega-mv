#include <stdint.h>
/* α comment retained */
uint64_t calc(uint64_t left,uint64_t right) { /* keep */ return left*right; }

#include <stdio.h>
#include <inttypes.h>
int main(void){
uint64_t x[6]={0};while(scanf("%" SCNu64 " %" SCNu64 "",&x[0],&x[1])==2)printf("%" PRIu64 "\n",calc(x[0],x[1]));return 0;}
