#include <stdint.h>
uint64_t calc(void) { return 7ULL; }

#include <stdio.h>
#include <inttypes.h>
int main(void){
printf("%" PRIu64 "\n",calc());return 0;}
