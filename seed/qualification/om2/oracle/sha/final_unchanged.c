/* SPDX-License-Identifier: Apache-2.0
 * ORACLE (outside the delivered closure), L836 phaseB/sha-f5d14cb2. Informational check of the unit's own
 * comment (not a contract requirement, section 7 promises less): final leaves the context unchanged.
 * Build: gcc -std=gnu11 -O2 -I<bind> final_unchanged.c -L<bind> -lomega_native -Wl,-rpath,<bind> */
#include <stdio.h>
#include <string.h>
#include "omega_api.h"
int main(void){ sha256_ctx c, c0; uint8_t d1[32], d2[32]; uint8_t m[300]; for (int i=0;i<300;i++) m[i]=i*7;
 int bad=0; for (int n=0;n<300;n++){ sha256_init(&c); sha256_update(&c,m,n); memcpy(&c0,&c,sizeof c); sha256_final(&c,d1); bad |= memcmp(&c,&c0,sizeof c)!=0; sha256_final(&c,d2); bad |= memcmp(d1,d2,32)!=0; }
 printf("final-leaves-context-unchanged n=0..299: %s\n", bad?"NO":"yes"); return bad; }
