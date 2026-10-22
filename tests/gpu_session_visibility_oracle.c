/* #519 additive session-level host trace. Reuse the existing fake platform,
 * not a second GPU implementation. No graph recovery/native execution claim. */
#define main existing_session_suite_main
#include "test_omega_gpu_session_platform.c"
#undef main

#define TRACE_MAX 128
static struct {
    unsigned direction, sequence, lock_depth;
    OmegaGpuBufferHandle handle;
    uint64_t offset, bytes;
    int result;
} trace[TRACE_MAX];
static unsigned ntrace;
static int armed, fail_direction;
static OmegaGpuMem expected;
static uint8_t *device_shadow;

static int trace_sync(void *ctx, OmegaGpuBufferHandle h, uint64_t off, uint64_t len, unsigned direction) {
    int rc=f_sync(ctx,h,off,len);
    if(!armed) return rc;
    if(!rc && fail_direction==(int)direction) rc=OMEGA_GPU_PLAT_REFUSED_DEVICE;
    EXPECT(ntrace<TRACE_MAX);
    if(ntrace<TRACE_MAX) {
        trace[ntrace].direction=direction; trace[ntrace].sequence=ntrace;
        trace[ntrace].lock_depth=fk.locks_held;
        trace[ntrace].handle=h; trace[ntrace].offset=off; trace[ntrace].bytes=len;
        trace[ntrace++].result=rc;
    }
    if(!rc) {
        EXPECT(h.index==expected.handle.index && h.generation==expected.handle.generation);
        EXPECT(off<=expected.size && len<=expected.size-off);
        if(h.index==expected.handle.index && h.generation==expected.handle.generation &&
            off<=expected.size && len<=expected.size-off) {
            uint8_t *host=(uint8_t *)expected.cpu;
            if(direction==1) memcpy(device_shadow+off,host+off,(size_t)len);
            else memcpy(host+off,device_shadow+off,(size_t)len);
        }
    }
    return rc;
}
static int trace_c2g(void *c, OmegaGpuBufferHandle h, uint64_t off, uint64_t len) { return trace_sync(c,h,off,len,1); }
static int trace_g2c(void *c, OmegaGpuBufferHandle h, uint64_t off, uint64_t len) { return trace_sync(c,h,off,len,2); }

static void expect_event(unsigned index,unsigned direction,int success) {
    EXPECT(ntrace==index+1);
    if(ntrace!=index+1) return;
    EXPECT(trace[index].sequence==index);
    EXPECT(trace[index].direction==direction && trace[index].lock_depth==1);
    EXPECT(trace[index].handle.index==expected.handle.index);
    EXPECT(trace[index].handle.generation==expected.handle.generation);
    EXPECT(trace[index].offset==0 && trace[index].bytes==expected.size);
    EXPECT((trace[index].result==0)==success);
    printf("TRACE seq=%u direction=%s offset=%llu bytes=%llu lock=%u result=%d\n",
           index,direction==1?"CPU_TO_GPU":"GPU_TO_CPU",
           (unsigned long long)trace[index].offset,(unsigned long long)trace[index].bytes,
           trace[index].lock_depth,trace[index].result);
}
static int bytes_are(const void *p,size_t size,uint8_t value) {
    const uint8_t *b=p;
    for(size_t i=0;i<size;++i) if(b[i]!=value) return 0;
    return 1;
}
int main(void) {
    case_ok=1; fk_reset();
    g_plat.sync_cpu_to_gpu=trace_c2g; g_plat.sync_gpu_to_cpu=trace_g2c;
    if(install()!=0) { puts("FAIL fake install; no default fallback permitted"); return 1; }
    omega_gpu_session_lock();
    if(!omega_gpu_session_open()) { puts("FAIL fake open"); return 1; }
    const size_t sizes[]={1,4096,4097,16384};
    for(size_t size=0;size<sizeof sizes/sizeof sizes[0];++size) {
        OmegaGpuMem m;
        if(omega_gpu_session_alloc(sizes[size],&m)!=0) { puts("FAIL allocation"); return 1; }
        expected=m; device_shadow=malloc(m.size);
        if(!device_shadow) return 1;
        ntrace=0; armed=1; fail_direction=0;
        memset(device_shadow,0xa5,m.size); memset(m.cpu,0x3c,m.size);
        EXPECT(omega_gpu_session_sync_to_gpu(&m)==0); expect_event(0,1,1);
        EXPECT(bytes_are(device_shadow,m.size,0x3c));
        /* New device output becomes host-visible only across the actual callback. */
        memset(device_shadow,0x6d,m.size);
        EXPECT(omega_gpu_session_sync_to_cpu(&m)==0); expect_event(1,2,1);
        EXPECT(bytes_are(m.cpu,m.size,0x6d));
        /* Failed sync reports failure and publishes nothing, then recovers. */
        memset(m.cpu,0x11,m.size); fail_direction=1;
        EXPECT(omega_gpu_session_sync_to_gpu(&m)!=0); expect_event(2,1,0);
        EXPECT(bytes_are(device_shadow,m.size,0x6d));
        fail_direction=0;
        EXPECT(omega_gpu_session_sync_to_gpu(&m)==0); expect_event(3,1,1);
        EXPECT(bytes_are(device_shadow,m.size,0x11));
        memset(device_shadow,0x22,m.size); fail_direction=2;
        EXPECT(omega_gpu_session_sync_to_cpu(&m)!=0); expect_event(4,2,0);
        EXPECT(bytes_are(m.cpu,m.size,0x11));
        fail_direction=0;
        EXPECT(omega_gpu_session_sync_to_cpu(&m)==0); expect_event(5,2,1);
        EXPECT(bytes_are(m.cpu,m.size,0x22));
        /* Null/empty buffer is the existing documented no-op, not a DMA. */
        OmegaGpuMem empty={0};
        EXPECT(omega_gpu_session_sync_to_cpu(NULL)==0);
        EXPECT(omega_gpu_session_sync_to_gpu(&empty)==0); EXPECT(ntrace==6);
        OmegaGpuMem stale=m;
        omega_gpu_session_free(&m);
        EXPECT(omega_gpu_session_sync_to_gpu(&stale)!=0); expect_event(6,1,0);
        EXPECT(!omega_gpu_session_is_blocked());
        armed=0; free(device_shadow); device_shadow=NULL;
    }
    /* Closed-session refusal must not dispatch a callback or dereference memory. */
    OmegaGpuMem m;
    EXPECT(omega_gpu_session_alloc(4096,&m)==0);
    unsigned before=ntrace;
    omega_gpu_session_close();
    armed=1;
    EXPECT(omega_gpu_session_sync_to_cpu(&m)!=0);
    EXPECT(ntrace==before); armed=0;
    omega_gpu_session_unlock(); EXPECT(fk.locks_held==0);
    printf("gpu session visibility: 4 sizes, 28 exact events, failures/recovery/stale/close: %s; graph/native NOT_RUN\n",case_ok?"PASS":"FAIL");
    return case_ok?0:1;
}
