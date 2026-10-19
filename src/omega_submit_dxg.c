/*
 * omega_submit_dxg.c -- OmegaSubmitOps over /dev/dxg (WSL2, Microsoft dxgkrnl).
 *
 * CHIP-UNVERIFIED. Never run against a real /dev/dxg. Every ioctl structure comes from the vendored
 * d3dkmthk.h (microsoft/WSL2-Linux-Kernel); sizes and request numbers are checked by
 * tests/test_omega_submit_dxg.c, the call order by a mock /dev/dxg in the same test.
 *
 * Call flow (all WDDM-generic, documented in docs/rig/dxg-backend.md):
 *   open          open("/dev/dxg"); ENUMADAPTERS2; QUERYADAPTERINFO(ADAPTERTYPE); CREATEDEVICE;
 *                 CREATEPAGINGQUEUE (its sync object is the paging fence)
 *   create_queue  CREATECONTEXTVIRTUAL(node, affinity, NVIDIA private data = blobs.context);
 *                 CREATESYNCHRONIZATIONOBJECT (monitored fence) = the queue fence
 *   alloc         CREATEALLOCATION(NVIDIA private data = blobs.alloc_*); MAPGPUVIRTUALADDRESS;
 *                 MAKERESIDENT; wait on the paging fence; LOCK2 for host-visible memory
 *   submit        SUBMITCOMMAND(command_buffer = GPU VA, private data = blobs.submit), then
 *                 SIGNALSYNCHRONIZATIONOBJECTFROMGPU(context, queue fence, value)
 *   wait          poll the fence's CPU address, else WAITFORSYNCHRONIZATIONOBJECTFROMCPU
 *   close         reverse order destroy, CLOSEADAPTER
 *
 * What is NOT known and therefore not invented: the bytes of the four private-data blobs, and
 * whether NVIDIA's GPU honours a GPU-side signal queued after SUBMITCOMMAND the way D3D12 does.
 * Without a blob the matching step returns OMEGA_SUB_E_VENDOR_BLOB and issues no ioctl.
 */
#include "omega_submit_dxg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "omega_dxg_abi.h"

#include "omega_bw_barrier.h"

#define MAX_ADAPTERS 16
#define MAX_ALLOCS 256

typedef struct {
    int used;
    struct d3dkmthandle alloc;
    int locked;
    uint64_t gpu_va;
    uint64_t size;
} Alloc;

typedef struct {
    int opened;
    int fd;
    struct d3dkmthandle adapter, device, paging_queue, paging_sync, context, qfence;
    volatile uint64_t *paging_fence_cpu;
    volatile uint64_t *qfence_cpu;
    int have_context;
    int hwsch;                       /* 1: hardware scheduling, use hwqueue calls */
    struct d3dkmthandle hwq, hwq_fence;
    int have_hwq;
    uint64_t paging_value;
    uint64_t submitted;
    Alloc allocs[MAX_ALLOCS];
    uint32_t node, affinity, hint;
    uint64_t va_min, va_max;
    char err[256];
} SubDxg;

static SubDxg g_sub;
static OmegaDxgBlobs g_blobs;
static OmegaDxgIoctlFn g_hook;
static void *g_hook_ctx;
/* storage for blobs loaded from files */
static void *g_file_blob[5];

void omega_dxg_set_blobs(const OmegaDxgBlobs *b)
{
    if (b) g_blobs = *b; else memset(&g_blobs, 0, sizeof g_blobs);
}

void omega_dxg_set_ioctl_hook(OmegaDxgIoctlFn fn, void *ctx)
{
    g_hook = fn;
    g_hook_ctx = ctx;
}

static int load_one(const char *dir, const char *name, OmegaDxgBlob *out, void **keep)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.bin", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 65536) { fclose(f); return 0; }
    void *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
    fclose(f);
    free(*keep);
    *keep = buf;
    out->data = buf;
    out->size = (uint32_t)n;
    return 1;
}

int omega_dxg_load_blob_dir(const char *dir)
{
    int n = 0;
    if (!dir) return 0;
    n += load_one(dir, "context", &g_blobs.context, &g_file_blob[0]);
    n += load_one(dir, "hwqueue", &g_blobs.hwqueue, &g_file_blob[4]);
    n += load_one(dir, "alloc_device", &g_blobs.alloc_device, &g_file_blob[1]);
    n += load_one(dir, "alloc_host", &g_blobs.alloc_host, &g_file_blob[2]);
    n += load_one(dir, "submit", &g_blobs.submit, &g_file_blob[3]);
    return n;
}

#define ROW(n, T) { #n, n, (unsigned)sizeof(struct T), _IOC_NR(n) }
static const OmegaDxgIoctlRow g_rows[] = {
    ROW(LX_DXENUMADAPTERS2, d3dkmt_enumadapters2),
    ROW(LX_DXOPENADAPTERFROMLUID, d3dkmt_openadapterfromluid),
    ROW(LX_DXQUERYADAPTERINFO, d3dkmt_queryadapterinfo),
    ROW(LX_DXCREATEDEVICE, d3dkmt_createdevice),
    ROW(LX_DXCREATEPAGINGQUEUE, d3dkmt_createpagingqueue),
    ROW(LX_DXCREATECONTEXTVIRTUAL, d3dkmt_createcontextvirtual),
    ROW(LX_DXCREATESYNCHRONIZATIONOBJECT, d3dkmt_createsynchronizationobject2),
    ROW(LX_DXCREATEALLOCATION, d3dkmt_createallocation),
    ROW(LX_DXMAPGPUVIRTUALADDRESS, d3dddi_mapgpuvirtualaddress),
    ROW(LX_DXMAKERESIDENT, d3dddi_makeresident),
    ROW(LX_DXLOCK2, d3dkmt_lock2),
    ROW(LX_DXUNLOCK2, d3dkmt_unlock2),
    ROW(LX_DXSUBMITCOMMAND, d3dkmt_submitcommand),
    ROW(LX_DXSIGNALSYNCHRONIZATIONOBJECTFROMGPU, d3dkmt_signalsynchronizationobjectfromgpu),
    ROW(LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU, d3dkmt_waitforsynchronizationobjectfromcpu),
    ROW(LX_DXDESTROYALLOCATION2, d3dkmt_destroyallocation2),
    ROW(LX_DXDESTROYSYNCHRONIZATIONOBJECT, d3dkmt_destroysynchronizationobject),
    ROW(LX_DXDESTROYCONTEXT, d3dkmt_destroycontext),
    ROW(LX_DXDESTROYPAGINGQUEUE, d3dddi_destroypagingqueue),
    ROW(LX_DXDESTROYDEVICE, d3dkmt_destroydevice),
    ROW(LX_DXCLOSEADAPTER, d3dkmt_closeadapter),
    ROW(LX_ISFEATUREENABLED, d3dkmt_isfeatureenabled),
    ROW(LX_DXCREATEHWQUEUE, d3dkmt_createhwqueue),
    ROW(LX_DXSUBMITCOMMANDTOHWQUEUE, d3dkmt_submitcommandtohwqueue),
    ROW(LX_DXDESTROYHWQUEUE, d3dkmt_destroyhwqueue),
};

const OmegaDxgIoctlRow *omega_dxg_ioctl_rows(size_t *count)
{
    if (count) *count = sizeof g_rows / sizeof g_rows[0];
    return g_rows;
}

static int sfail(SubDxg *s, int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->err, sizeof s->err, fmt, ap);
    va_end(ap);
    return code;
}

static int dxg_ioctl(SubDxg *s, unsigned long req, void *arg)
{
    if (g_hook) return g_hook(g_hook_ctx, req, arg);
    return ioctl(s->fd, req, arg);
}

#define CALL(s, req, arg, what)                                                                      \
    do {                                                                                             \
        if (dxg_ioctl((s), (req), (arg)) != 0)                                                       \
            return sfail((s), OMEGA_SUB_E_DRIVER, "%s: %s (errno %d)", (what), strerror(errno), errno); \
    } while (0)

static uint64_t env_u64(const char *name, uint64_t dflt)
{
    const char *v = getenv(name);
    return (v && *v) ? strtoull(v, NULL, 0) : dflt;
}

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static uint64_t ptr64(const void *p) { return (uint64_t)(uintptr_t)p; }

static int dx_open(void *self, OmegaSubDeviceInfo *info)
{
    SubDxg *s = (SubDxg *)self;
    if (s->opened) return sfail(s, OMEGA_SUB_E_STATE, "already open");
    memset(s, 0, sizeof *s);
    s->fd = -1;
    s->node = (uint32_t)env_u64("OMEGA_DXG_NODE", OMEGA_DXG_TODO_NODE_ORDINAL);
    s->affinity = (uint32_t)env_u64("OMEGA_DXG_AFFINITY", OMEGA_DXG_TODO_ENGINE_AFFINITY);
    s->hint = (uint32_t)env_u64("OMEGA_DXG_HINT", OMEGA_DXG_TODO_CLIENT_HINT);
    s->va_min = env_u64("OMEGA_DXG_VA_MIN", OMEGA_DXG_TODO_VA_MIN);
    s->va_max = env_u64("OMEGA_DXG_VA_MAX", OMEGA_DXG_TODO_VA_MAX);
    if (getenv("OMEGA_DXG_BLOBS")) (void)omega_dxg_load_blob_dir(getenv("OMEGA_DXG_BLOBS"));

    if (!g_hook) {
        s->fd = open("/dev/dxg", O_RDWR | O_CLOEXEC);
        if (s->fd < 0) return sfail(s, OMEGA_SUB_E_NO_DEVICE, "open /dev/dxg: %s", strerror(errno));
    }

    /* adapters: first call with a NULL array returns the count */
    struct d3dkmt_enumadapters2 en;
    memset(&en, 0, sizeof en);
    CALL(s, LX_DXENUMADAPTERS2, &en, "enumadapters2(count)");
    if (en.num_adapters == 0) return sfail(s, OMEGA_SUB_E_NO_DEVICE, "dxgkrnl reports no adapters");
    uint32_t n = en.num_adapters > MAX_ADAPTERS ? MAX_ADAPTERS : en.num_adapters;
    struct d3dkmt_adapterinfo ai[MAX_ADAPTERS];
    memset(ai, 0, sizeof ai);
    memset(&en, 0, sizeof en);
    en.num_adapters = n;
    en.adapters = (__u64 *)(uintptr_t)ai;
    CALL(s, LX_DXENUMADAPTERS2, &en, "enumadapters2");
    uint32_t pick = (uint32_t)env_u64("OMEGA_DXG_ADAPTER_INDEX", 0);
    if (pick >= en.num_adapters) return sfail(s, OMEGA_SUB_E_ARG, "adapter index %u of %u", pick, en.num_adapters);
    s->adapter = ai[pick].adapter_handle;
    if (s->adapter.v == 0) {
        struct d3dkmt_openadapterfromluid oa;
        memset(&oa, 0, sizeof oa);
        oa.adapter_luid = ai[pick].adapter_luid;
        CALL(s, LX_DXOPENADAPTERFROMLUID, &oa, "openadapterfromluid");
        s->adapter = oa.adapter_handle;
    }
    s->opened = 1; /* from here close() cleans up */

    struct d3dkmt_adaptertype at;
    struct d3dkmt_queryadapterinfo qa;
    memset(&at, 0, sizeof at);
    memset(&qa, 0, sizeof qa);
    qa.adapter = s->adapter;
    qa.type = _KMTQAITYPE_ADAPTERTYPE;
    qa.private_data = ptr64(&at);
    qa.private_data_size = sizeof at;
    CALL(s, LX_DXQUERYADAPTERINFO, &qa, "queryadapterinfo(ADAPTERTYPE)");

    struct d3dkmt_createdevice cd;
    memset(&cd, 0, sizeof cd);
    cd.adapter = s->adapter;
    CALL(s, LX_DXCREATEDEVICE, &cd, "createdevice");
    s->device = cd.device;

    {   /* does Windows run this GPU with hardware scheduling (HAGS)? decides the submit path */
        struct d3dkmt_isfeatureenabled fe;
        const char *mode = getenv("OMEGA_DXG_MODE");
        memset(&fe, 0, sizeof fe);
        fe.adapter = s->adapter;
        fe.feature_id = _DXGK_FEATURE_HWSCH;
        if (dxg_ioctl(s, LX_ISFEATUREENABLED, &fe) == 0) s->hwsch = fe.result.enabled;
        if (!mode || !*mode) mode = OMEGA_DXG_TODO_MODE;
        if (strcmp(mode, "submit") == 0) s->hwsch = 0;
        else if (strcmp(mode, "hwqueue") == 0) s->hwsch = 1;
    }

    struct d3dkmt_createpagingqueue pq;
    memset(&pq, 0, sizeof pq);
    pq.device = s->device;
    pq.priority = _D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    CALL(s, LX_DXCREATEPAGINGQUEUE, &pq, "createpagingqueue");
    s->paging_queue = pq.paging_queue;
    s->paging_sync = pq.sync_object;
    s->paging_fence_cpu = (volatile uint64_t *)(uintptr_t)pq.fence_cpu_virtual_address;

    if (info) {
        memset(info, 0, sizeof *info);
        snprintf(info->description, sizeof info->description,
                 "dxg: adapter luid %08x:%08x %s %s%s%s", ai[pick].adapter_luid.a, ai[pick].adapter_luid.b,
                 s->hwsch ? "hwsch" : "swsch",
                 at.render_supported ? "render " : "", at.paravirtualized ? "paravirt " : "",
                 at.compute_only ? "compute-only" : "");
        /* arch, impl, classes, vram: not readable through generic D3DKMT queries. They come from the
         * NVIDIA escape/private queries, whose format is unknown. Left 0 on purpose. */
    }
    return OMEGA_SUB_OK;
}

static int wait_sync(SubDxg *s, struct d3dkmthandle sync, volatile uint64_t *cpu, uint64_t value, uint64_t timeout_ms)
{
    if (cpu) {
        uint64_t end = now_ms() + timeout_ms;
        for (;;) {
            uint64_t v = *cpu;
            if (v >= value) {
                omega_bw_mb();
                return OMEGA_SUB_OK;
            }
            if (now_ms() >= end)
                return sfail(s, OMEGA_SUB_E_TIMEOUT, "wait: fence %llu, wanted %llu", (unsigned long long)v,
                             (unsigned long long)value);
            usleep(50);
        }
    }
    /* No CPU address: blocking kernel wait. The ioctl has no timeout field, so timeout_ms is not honoured. */
    struct d3dkmt_waitforsynchronizationobjectfromcpu w;
    struct d3dkmthandle objs[1] = { sync };
    uint64_t vals[1] = { value };
    memset(&w, 0, sizeof w);
    w.device = s->device;
    w.object_count = 1;
    w.objects = ptr64(objs);
    w.fence_values = ptr64(vals);
    CALL(s, LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU, &w, "waitforsynchronizationobjectfromcpu");
    return OMEGA_SUB_OK;
}

static const OmegaDxgBlob *alloc_blob(OmegaSubMemKind kind)
{
    return kind == OMEGA_SUB_MEM_DEVICE_LOCAL ? &g_blobs.alloc_device : &g_blobs.alloc_host;
}

static int dx_alloc(void *self, OmegaSubMemKind kind, uint64_t size, uint32_t flags, OmegaSubMem *out)
{
    SubDxg *s = (SubDxg *)self;
    if (!s->opened) return sfail(s, OMEGA_SUB_E_STATE, "alloc before open");
    if (!out || size == 0) return sfail(s, OMEGA_SUB_E_ARG, "alloc: bad arguments");
    if (kind != OMEGA_SUB_MEM_HOST_VISIBLE && kind != OMEGA_SUB_MEM_DEVICE_LOCAL)
        return sfail(s, OMEGA_SUB_E_ARG, "alloc: unknown kind");
    if (kind == OMEGA_SUB_MEM_DEVICE_LOCAL && (flags & OMEGA_SUB_MEM_CPU_MAP))
        return sfail(s, OMEGA_SUB_E_UNSUPPORTED, "dxg: CPU mapping of VRAM is not offered (Lock2 on VRAM is driver policy)");
    const OmegaDxgBlob *blob = alloc_blob(kind);
    if (!blob->data || blob->size == 0)
        return sfail(s, OMEGA_SUB_E_VENDOR_BLOB, "no NVIDIA allocation private data for %s memory",
                     kind == OMEGA_SUB_MEM_DEVICE_LOCAL ? "device-local" : "host-visible");
    int slot = -1;
    for (int i = 0; i < MAX_ALLOCS; i++)
        if (!s->allocs[i].used) { slot = i; break; }
    if (slot < 0) return sfail(s, OMEGA_SUB_E_UNSUPPORTED, "alloc: table full (%d)", MAX_ALLOCS);
    uint64_t sz = (size + OMEGA_DXG_ALLOC_ALIGN - 1) & ~(OMEGA_DXG_ALLOC_ALIGN - 1);

    struct d3dddi_allocationinfo2 ainfo;
    struct d3dkmt_createallocation ca;
    memset(&ainfo, 0, sizeof ainfo);
    memset(&ca, 0, sizeof ca);
    ainfo.priv_drv_data = ptr64(blob->data);
    ainfo.priv_drv_data_size = blob->size;
    ca.device = s->device;
    ca.alloc_count = 1;
    ca.allocation_info = ptr64(&ainfo);
    CALL(s, LX_DXCREATEALLOCATION, &ca, "createallocation");
    Alloc *a = &s->allocs[slot];
    memset(a, 0, sizeof *a);
    a->used = 1;
    a->alloc = ainfo.allocation;
    a->size = sz;

    struct d3dddi_mapgpuvirtualaddress mv;
    memset(&mv, 0, sizeof mv);
    mv.paging_queue = s->paging_queue;
    mv.minimum_address = s->va_min;
    mv.maximum_address = s->va_max;
    mv.allocation = a->alloc;
    mv.size_in_pages = sz / OMEGA_DXG_ALLOC_ALIGN;
    mv.protection.write = 1;
    CALL(s, LX_DXMAPGPUVIRTUALADDRESS, &mv, "mapgpuvirtualaddress");
    a->gpu_va = mv.virtual_address;
    s->paging_value = mv.paging_fence_value;

    struct d3dkmthandle list[1] = { a->alloc };
    struct d3dddi_makeresident mr;
    memset(&mr, 0, sizeof mr);
    mr.paging_queue = s->paging_queue;
    mr.alloc_count = 1;
    mr.allocation_list = ptr64(list);
    CALL(s, LX_DXMAKERESIDENT, &mr, "makeresident");
    if (mr.paging_fence_value > s->paging_value) s->paging_value = mr.paging_fence_value;
    int rc = wait_sync(s, s->paging_sync, s->paging_fence_cpu, s->paging_value, 5000);
    if (rc != OMEGA_SUB_OK) return rc;

    void *cpu = NULL;
    if (kind == OMEGA_SUB_MEM_HOST_VISIBLE) {
        struct d3dkmt_lock2 lk;
        memset(&lk, 0, sizeof lk);
        lk.device = s->device;
        lk.allocation = a->alloc;
        CALL(s, LX_DXLOCK2, &lk, "lock2");
        cpu = (void *)(uintptr_t)lk.data;
        a->locked = 1;
    }
    memset(out, 0, sizeof *out);
    out->gpu_va = a->gpu_va;
    out->cpu = cpu;
    out->size = sz;
    out->kind = kind;
    out->backend_handle = a->alloc.v;
    out->backend_aux = (uint64_t)slot;
    return OMEGA_SUB_OK;
}

static int destroy_alloc(SubDxg *s, Alloc *a)
{
    int rc = OMEGA_SUB_OK;
    if (a->locked) {
        struct d3dkmt_unlock2 ul;
        memset(&ul, 0, sizeof ul);
        ul.device = s->device;
        ul.allocation = a->alloc;
        if (dxg_ioctl(s, LX_DXUNLOCK2, &ul) != 0) rc = sfail(s, OMEGA_SUB_E_DRIVER, "unlock2: errno %d", errno);
        a->locked = 0;
    }
    struct d3dkmthandle list[1] = { a->alloc };
    struct d3dkmt_destroyallocation2 da;
    memset(&da, 0, sizeof da);
    da.device = s->device;
    da.allocations = ptr64(list);
    da.alloc_count = 1;
    if (dxg_ioctl(s, LX_DXDESTROYALLOCATION2, &da) != 0) rc = sfail(s, OMEGA_SUB_E_DRIVER, "destroyallocation2: errno %d", errno);
    memset(a, 0, sizeof *a);
    return rc;
}

static int dx_free(void *self, OmegaSubMem *m)
{
    SubDxg *s = (SubDxg *)self;
    if (!m) return OMEGA_SUB_E_ARG;
    if (m->size == 0) return OMEGA_SUB_OK;
    if (m->backend_aux >= MAX_ALLOCS || !s->allocs[m->backend_aux].used)
        return sfail(s, OMEGA_SUB_E_ARG, "free: not a dxg allocation");
    int rc = destroy_alloc(s, &s->allocs[m->backend_aux]);
    memset(m, 0, sizeof *m);
    return rc;
}

static int dx_create_queue(void *self, OmegaSubQueue q)
{
    SubDxg *s = (SubDxg *)self;
    if (!s->opened) return sfail(s, OMEGA_SUB_E_STATE, "queue before open");
    if (q == OMEGA_SUB_QUEUE_COPY)
        return sfail(s, OMEGA_SUB_E_UNSUPPORTED, "dxg copy queue: needs the copy node ordinal and its context blob (TODO)");
    if (q != OMEGA_SUB_QUEUE_COMPUTE) return sfail(s, OMEGA_SUB_E_ARG, "unknown queue");
    if (s->have_context) return OMEGA_SUB_OK;
    if (!g_blobs.context.data || g_blobs.context.size == 0)
        return sfail(s, OMEGA_SUB_E_VENDOR_BLOB, "no NVIDIA context private data (blobs.context)");

    struct d3dkmt_createcontextvirtual cc;
    memset(&cc, 0, sizeof cc);
    cc.device = s->device;
    cc.node_ordinal = s->node;
    cc.engine_affinity = s->affinity;
    cc.priv_drv_data = ptr64(g_blobs.context.data);
    cc.priv_drv_data_size = g_blobs.context.size;
    cc.client_hint = (enum d3dkmt_clienthint)s->hint;
    cc.flags.hw_queue_supported = s->hwsch ? 1 : 0;
    CALL(s, LX_DXCREATECONTEXTVIRTUAL, &cc, "createcontextvirtual");
    s->context = cc.context;
    s->have_context = 1;

    if (s->hwsch) {
        if (!g_blobs.hwqueue.data || g_blobs.hwqueue.size == 0)
            return sfail(s, OMEGA_SUB_E_VENDOR_BLOB, "no NVIDIA hwqueue private data (blobs.hwqueue)");
        struct d3dkmt_createhwqueue hq;
        memset(&hq, 0, sizeof hq);
        hq.context = s->context;
        hq.priv_drv_data = ptr64(g_blobs.hwqueue.data);
        hq.priv_drv_data_size = g_blobs.hwqueue.size;
        CALL(s, LX_DXCREATEHWQUEUE, &hq, "createhwqueue");
        s->hwq = hq.queue;
        s->hwq_fence = hq.queue_progress_fence;
        s->qfence_cpu = (volatile uint64_t *)(uintptr_t)hq.queue_progress_fence_cpu_va;
        s->have_hwq = 1;
        return OMEGA_SUB_OK;
    }

    struct d3dkmt_createsynchronizationobject2 so;
    memset(&so, 0, sizeof so);
    so.device = s->device;
    so.info.type = _D3DDDI_MONITORED_FENCE;
    so.info.monitored_fence.engine_affinity = s->affinity;
    CALL(s, LX_DXCREATESYNCHRONIZATIONOBJECT, &so, "createsynchronizationobject2(monitored fence)");
    s->qfence = so.sync_object;
    s->qfence_cpu = (volatile uint64_t *)(uintptr_t)so.info.monitored_fence.fence_cpu_virtual_address;
    return OMEGA_SUB_OK;
}

static int dx_emit_fence(void *self, OmegaSubQueue q, uint32_t *pb, size_t cap, uint64_t value)
{
    (void)self; (void)q; (void)pb; (void)cap; (void)value;
    return 0; /* the fence is signalled by the kernel after submit; no device-side words */
}

static int dx_submit(void *self, OmegaSubQueue q, const OmegaSubMem *pb, uint32_t off, uint32_t nwords, uint64_t fence_value)
{
    SubDxg *s = (SubDxg *)self;
    if (q != OMEGA_SUB_QUEUE_COMPUTE || !s->have_context) return sfail(s, OMEGA_SUB_E_STATE, "submit: no compute queue");
    if (!pb || pb->kind != OMEGA_SUB_MEM_HOST_VISIBLE) return sfail(s, OMEGA_SUB_E_ARG, "submit: pushbuffer must be HOST_VISIBLE");
    if (nwords == 0 || (uint64_t)off + (uint64_t)nwords * 4u > pb->size)
        return sfail(s, OMEGA_SUB_E_ARG, "submit: range outside the pushbuffer");
    if (fence_value <= s->submitted) return sfail(s, OMEGA_SUB_E_ARG, "submit: fence value must increase");
    if (!g_blobs.submit.data || g_blobs.submit.size == 0)
        return sfail(s, OMEGA_SUB_E_VENDOR_BLOB, "no NVIDIA submit private data (blobs.submit)");

    omega_bw_wmb();
    if (s->hwsch) {
        struct d3dkmt_submitcommandtohwqueue hs;
        memset(&hs, 0, sizeof hs);
        hs.hwqueue = s->hwq;
        hs.hwqueue_progress_fence_id = fence_value;
        hs.command_buffer = pb->gpu_va + off;
        hs.command_length = nwords * 4u;
        hs.priv_drv_data = ptr64(g_blobs.submit.data);
        hs.priv_drv_data_size = g_blobs.submit.size;
        CALL(s, LX_DXSUBMITCOMMANDTOHWQUEUE, &hs, "submitcommandtohwqueue");
        s->submitted = fence_value;
        return OMEGA_SUB_OK;
    }
    struct d3dkmt_submitcommand sc;
    memset(&sc, 0, sizeof sc);
    sc.command_buffer = pb->gpu_va + off;
    sc.command_length = nwords * 4u;
    sc.broadcast_context_count = 1;
    sc.broadcast_context[0] = s->context;
    sc.priv_drv_data = ptr64(g_blobs.submit.data);
    sc.priv_drv_data_size = g_blobs.submit.size;
    CALL(s, LX_DXSUBMITCOMMAND, &sc, "submitcommand");

    struct d3dkmthandle objs[1] = { s->qfence };
    uint64_t vals[1] = { fence_value };
    struct d3dkmt_signalsynchronizationobjectfromgpu sg;
    memset(&sg, 0, sizeof sg);
    sg.context = s->context;
    sg.object_count = 1;
    sg.objects = ptr64(objs);
    sg.monitored_fence_values = ptr64(vals);
    CALL(s, LX_DXSIGNALSYNCHRONIZATIONOBJECTFROMGPU, &sg, "signalsynchronizationobjectfromgpu");
    s->submitted = fence_value;
    return OMEGA_SUB_OK;
}

static int dx_wait(void *self, OmegaSubQueue q, uint64_t value, uint64_t timeout_ms)
{
    SubDxg *s = (SubDxg *)self;
    if (q != OMEGA_SUB_QUEUE_COMPUTE || !s->have_context) return sfail(s, OMEGA_SUB_E_STATE, "wait: no compute queue");
    return wait_sync(s, s->hwsch ? s->hwq_fence : s->qfence, s->qfence_cpu, value, timeout_ms);
}

static void dx_retire(void *self, OmegaSubQueue q, uint64_t value) { (void)self; (void)q; (void)value; }

static int dx_close(void *self)
{
    SubDxg *s = (SubDxg *)self;
    int rc = OMEGA_SUB_OK;
    if (!s->opened) return OMEGA_SUB_OK;
    for (int i = 0; i < MAX_ALLOCS; i++)
        if (s->allocs[i].used && destroy_alloc(s, &s->allocs[i]) != OMEGA_SUB_OK) rc = OMEGA_SUB_E_DRIVER;
    if (s->qfence.v) {
        struct d3dkmt_destroysynchronizationobject d = { .sync_object = s->qfence };
        if (dxg_ioctl(s, LX_DXDESTROYSYNCHRONIZATIONOBJECT, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
    }
    if (s->have_hwq) {
        struct d3dkmt_destroyhwqueue d = { .queue = s->hwq };
        if (dxg_ioctl(s, LX_DXDESTROYHWQUEUE, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
        s->have_hwq = 0;
    }
    if (s->have_context) {
        struct d3dkmt_destroycontext d = { .context = s->context };
        if (dxg_ioctl(s, LX_DXDESTROYCONTEXT, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
    }
    if (s->paging_queue.v) {
        struct d3dddi_destroypagingqueue d = { .paging_queue = s->paging_queue };
        if (dxg_ioctl(s, LX_DXDESTROYPAGINGQUEUE, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
    }
    if (s->device.v) {
        struct d3dkmt_destroydevice d = { .device = s->device };
        if (dxg_ioctl(s, LX_DXDESTROYDEVICE, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
    }
    if (s->adapter.v) {
        struct d3dkmt_closeadapter d = { .adapter_handle = s->adapter };
        if (dxg_ioctl(s, LX_DXCLOSEADAPTER, &d) != 0) rc = OMEGA_SUB_E_DRIVER;
    }
    if (s->fd >= 0) close(s->fd);
    s->opened = 0;
    s->have_context = 0;
    if (rc != OMEGA_SUB_OK) return sfail(s, rc, "close: a destroy ioctl failed");
    return OMEGA_SUB_OK;
}

static const char *dx_last_error(void *self) { return ((SubDxg *)self)->err; }

static const OmegaSubmitOps g_ops = {
    .kind = OMEGA_SUBMIT_DXG,
    .name = "dxg",
    .open = dx_open,
    .alloc = dx_alloc,
    .free_mem = dx_free,
    .create_queue = dx_create_queue,
    .emit_fence = dx_emit_fence,
    .submit = dx_submit,
    .wait = dx_wait,
    .retire = dx_retire,
    .close = dx_close,
    .last_error = dx_last_error,
};

const OmegaSubmitOps *omega_submit_dxg_ops(void) { return &g_ops; }
void *omega_submit_dxg_self(void) { return &g_sub; }
