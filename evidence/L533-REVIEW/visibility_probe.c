/* Reuse only the existing fake/trace; independent patterned producer oracle. */
#include "../../build/L533/visibility_base.c"
static uint8_t pattern(size_t i,unsigned round){return (uint8_t)((i*37u+(i>>8)*17u+round*101u)^((i>>3)&255));}
int main(void){
 case_ok=1;fk_reset();g_plat.sync_cpu_to_gpu=trace_c2g;g_plat.sync_gpu_to_cpu=trace_g2c;
 EXPECT(install()==0);omega_gpu_session_lock();EXPECT(omega_gpu_session_open()!=NULL);
 OmegaGpuMem m;EXPECT(omega_gpu_session_alloc(4097,&m)==0);expected=m;
 device_shadow=malloc(m.size);if(!device_shadow)return 1;armed=1;ntrace=0;fail_direction=0;
 EXPECT(fk.desc[m.handle.index].gpu_cache==OMEGA_GPU_CACHE_GPU_UNCACHED);
 for(unsigned r=0;r<8;r++){
  ntrace=0;
  for(size_t i=0;i<m.size;i++)((uint8_t*)m.cpu)[i]=pattern(i,r);
  memset(device_shadow,0,m.size);EXPECT(omega_gpu_session_sync_to_gpu(&m)==0);expect_event(0,1,1);
  for(size_t i=0;i<m.size;i++)EXPECT(device_shadow[i]==pattern(i,r));
  for(size_t i=0;i<m.size;i++)device_shadow[i]=pattern(i,r+8);
  EXPECT(omega_gpu_session_sync_to_cpu(&m)==0);expect_event(1,2,1);
  for(size_t i=0;i<m.size;i++)EXPECT(((uint8_t*)m.cpu)[i]==pattern(i,r+8));
 }
 /* Invalid generation while allocation remains live must dispatch and refuse, then recover. */
 OmegaGpuMem bad=m;bad.handle.generation++;ntrace=0;
 EXPECT(omega_gpu_session_sync_to_cpu(&bad)!=0);EXPECT(ntrace==1);EXPECT(trace[0].result!=0);
 ntrace=0;EXPECT(omega_gpu_session_sync_to_cpu(&m)==0);expect_event(0,2,1);
 /* Claim beyond allocation must refuse without touching host/shadow. */
 bad=m;bad.size++;ntrace=0;EXPECT(omega_gpu_session_sync_to_gpu(&bad)!=0);EXPECT(ntrace==1);EXPECT(trace[0].result!=0);
 ntrace=0;EXPECT(omega_gpu_session_sync_to_gpu(&m)==0);expect_event(0,1,1);
 unsigned before=ntrace;OmegaGpuMem empty={0};EXPECT(omega_gpu_session_sync_to_gpu(NULL)==0);EXPECT(omega_gpu_session_sync_to_cpu(&empty)==0);EXPECT(ntrace==before);
 armed=0;omega_gpu_session_free(&m);free(device_shadow);device_shadow=NULL;omega_gpu_session_close();omega_gpu_session_unlock();EXPECT(fk.locks_held==0);
 printf("L533 patterned visibility: 8 repeated full-range crossings, live bad-generation/oversized-range refusal and recovery: %s; uncached fake only\n",case_ok?"PASS":"FAIL");return case_ok?0:1;
}
