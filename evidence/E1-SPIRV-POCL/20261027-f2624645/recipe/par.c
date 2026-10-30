#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "omega_numeric_transc.h"
#include "omega_numeric.h"
static float tf(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static uint32_t tb(float f){uint32_t u;memcpy(&u,&f,4);return u;}
int main(int c,char**v){
 const char*op=v[1]; uint32_t s=(uint32_t)strtoul(v[2],0,0); uint64_t n=strtoull(v[3],0,0);
 float(*f)(float)=0;
 if(!strcmp(op,"exp2"))f=omega_math_exp2; else if(!strcmp(op,"log2"))f=omega_math_log2;
 else if(!strcmp(op,"sigmoid"))f=omega_math_sigmoid; else if(!strcmp(op,"tanh"))f=omega_math_tanh;
 else if(!strcmp(op,"rsqrt"))f=omega_math_rsqrt; else if(!strcmp(op,"erf"))f=omega_math_erf;
 else if(!strcmp(op,"gelu"))f=omega_math_gelu; else if(!strcmp(op,"sin"))f=omega_math_sin;
 else if(!strcmp(op,"cos"))f=omega_math_cos; else if(!strcmp(op,"exp"))f=omega_math_exp;
 else if(!strcmp(op,"log"))f=omega_math_log; else return 2;
 uint64_t bad=0; uint32_t o;
 for(uint64_t i=0;i<n;i++){ if(fread(&o,4,1,stdin)!=1){printf("SHORT at %llu\n",(unsigned long long)i);return 1;}
  uint32_t x=s+(uint32_t)i, r=tb(f(tf(x)));
  if(r!=o){ if(bad<5)printf("MISMATCH %s x=%08x cpu=%08x spv=%08x\n",op,x,r,o); bad++;}}
 printf("%s start=%u n=%llu mismatches=%llu\n",op,s,(unsigned long long)n,(unsigned long long)bad); return bad!=0;}
