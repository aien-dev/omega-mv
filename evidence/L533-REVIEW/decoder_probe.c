/* Raw v1 wire bytes, hand-built from exact PR499 spec, no encoder oracle. */
#include "osc_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}++checks;}while(0)
static OscUnit *out;
static void check(const uint8_t *p,size_t n,int want){
 memset(out,0xa5,sizeof *out);int rc=osc_ir_decode(p,n,out);
 if(rc!=want){fprintf(stderr,"rc=%s want=%s len=%zu\n",osc_ir_refusal_name(rc),osc_ir_refusal_name(want),n);exit(1);}++checks;
 if(rc){const uint8_t *b=(const uint8_t*)out;for(size_t i=0;i<sizeof *out;i++)REQUIRE(b[i]==0);}
}
int main(void){
 out=malloc(sizeof *out);REQUIRE(out!=NULL);
 /* fn f() -> u8 { CONST v0=7; RET v0 }, 49 bytes, little endian. */
 uint8_t b[50]={'O','S','C','1','I','R',0,1,1,0,1,'f',0,2,1,0,2,1,0,
 0,0,0,0,2,0,0,0,2,0,0,0,1,0,0,7,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0};
 check(b,49,OSC_IR_OK);REQUIRE(out->funcs[0].insns[0].imm==7);
 for(size_t n=0;n<49;n++)check(b,n,OSC_IR_REFUSE_TRUNCATED);
 check(b,50,OSC_IR_REFUSE_TRAILING);
 uint8_t m[50];
 memcpy(m,b,50);m[14]=255;m[15]=255;check(m,49,OSC_IR_REFUSE_OVERSIZED);
 memcpy(m,b,50);m[17]=255;m[18]=255;check(m,49,OSC_IR_REFUSE_OVERSIZED);
 memcpy(m,b,50);memset(m+27,255,4);check(m,49,OSC_IR_REFUSE_OVERSIZED);
 memcpy(m,b,50);memset(m+19,255,4);memset(m+23,255,4);check(m,49,OSC_IR_REFUSE_INVALID);
 memcpy(m,b,50);m[33]=128;check(m,49,OSC_IR_REFUSE_RANGE);
 memcpy(m,b,50);m[32]=255;m[33]=255;check(m,49,OSC_IR_REFUSE_INVALID);
 memcpy(m,b,50);m[31]=255;check(m,49,OSC_IR_REFUSE_RANGE);
 memcpy(m,b,50);m[45]=255;m[46]=255;check(m,49,OSC_IR_REFUSE_OVERSIZED);
 check(b,(size_t)OSC_IR_ENCODED_MAX+1,OSC_IR_REFUSE_OVERSIZED);
 check(NULL,49,OSC_IR_REFUSE_ARG);
 printf("L533 raw-wire boundaries and complete output-zeroing checks=%u PASS\n",checks);free(out);return 0;
}
