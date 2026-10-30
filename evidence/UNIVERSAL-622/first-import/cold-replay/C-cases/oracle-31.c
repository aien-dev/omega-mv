#include <stdint.h>
uint64_t calc(uint64_t x) { return x; }

#include <stdio.h>
#include <inttypes.h>
int main(void){
uint64_t x[6]={0};while(scanf("%" SCNu64 "",&x[0])==1)printf("%" PRIu64 "\n",calc(x[0]));return 0;}
