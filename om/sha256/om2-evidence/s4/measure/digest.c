#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sha256.h"
int main(void){
  static uint8_t buf[1<<20]; for(size_t i=0;i<sizeof buf;i++) buf[i]=(uint8_t)(i*31u+7u);
  size_t ns[]={0,3,64,1024,1048576};
  for(unsigned k=0;k<5;k++){ uint8_t d[SHA256_DIGEST_SIZE]; (void)sha256_hash(buf,ns[k],d);
    printf("%zu ",ns[k]); for(int i=0;i<32;i++) printf("%02x",d[i]); printf("\n"); }
  sha256_ctx c; uint8_t d[SHA256_DIGEST_SIZE]; sha256_init(&c);
  for(size_t o=0;o<1048576;o+=4096) sha256_update(&c,buf+o,4096);
  sha256_final(&c,d); printf("stream4096 "); for(int i=0;i<32;i++) printf("%02x",d[i]); printf("\n");
  return 0;
}
