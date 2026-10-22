#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "omega_program.h"
#include "omega_realize_synth.h"
#include "omega_exec.h"
static int fault(const char *s){const char *f=getenv("L520_FAULT");return f&&!strcmp(f,s);}
int __real_omega_program_eval(const OmegaProgram*,const uint64_t*,size_t,uint64_t*);
int __wrap_omega_program_eval(const OmegaProgram*p,const uint64_t*x,size_t n,uint64_t*y){int r=__real_omega_program_eval(p,x,n,y);if(fault("eval-error"))return -1;if(fault("wrong-result")&&!r&&n)y[0]^=1;return r;}
int __real_omega_synthesize_for_dgx_spark(const OmegaProgram*,RealizationSynthesisResult*);
int __wrap_omega_synthesize_for_dgx_spark(const OmegaProgram*p,RealizationSynthesisResult*r){return fault("synth")?-1:__real_omega_synthesize_for_dgx_spark(p,r);}
int __real_omega_program_emit_schedule(const OmegaProgramBody*,uint16_t,OmegaRealizeSchedule,uint8_t*,size_t*,size_t);
int __wrap_omega_program_emit_schedule(const OmegaProgramBody*b,uint16_t w,OmegaRealizeSchedule s,uint8_t*c,size_t*n,size_t m){return fault("emit")?-1:__real_omega_program_emit_schedule(b,w,s,c,n,m);}
int __real_omega_exec_native_f3(const RealizationObject*,uint64_t,uint64_t,uint64_t,uint64_t*);
int __wrap_omega_exec_native_f3(const RealizationObject*r,uint64_t a,uint64_t b,uint64_t c,uint64_t*out){return fault("exec")?-1:__real_omega_exec_native_f3(r,a,b,c,out);}
size_t __real_omega_realize_differential_inputs(const OmegaProgram*,uint64_t*,size_t);
size_t __wrap_omega_realize_differential_inputs(const OmegaProgram*p,uint64_t*x,size_t n){return fault("no-inputs")?0:__real_omega_realize_differential_inputs(p,x,n);}
void *__real_mmap(void*,size_t,int,int,int,off_t);
void *__wrap_mmap(void*a,size_t n,int p,int f,int fd,off_t o){if(fault("mmap")){errno=ENOMEM;return MAP_FAILED;}return __real_mmap(a,n,p,f,fd,o);}
int __real_mprotect(void*,size_t,int);
int __wrap_mprotect(void*a,size_t n,int p){if(fault("mprotect")){errno=EACCES;return -1;}return __real_mprotect(a,n,p);}
