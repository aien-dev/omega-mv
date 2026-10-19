/*
 * tests/test_omega_submit_dxg.c -- host-side unit test of the /dev/dxg backend. Needs no GPU.
 *
 *   1. ABI: every ioctl the backend uses has the request number and argument size pinned below
 *      (values taken from third_party/wsl2-dxgkrnl/d3dkmthk.h and cross-checked against the
 *      dispatch table in the WSL2 kernel's drivers/hv/dxgkrnl/ioctl.c, see docs/rig/dxg-backend.md).
 *   2. Mock /dev/dxg: a fake kernel that hands out handles, checks argument fields and records the
 *      call order. Runs the full flow in both scheduler modes (software: SubmitCommand + GPU signal;
 *      hardware: HwQueue). Checks that vendor blobs reach the kernel byte-for-byte and that the
 *      backend refuses (OMEGA_SUB_E_VENDOR_BLOB) to call the ioctl when a blob is missing.
 *   3. The real device path: on a machine with no /dev/dxg, open() must return NO_DEVICE.
 * What this cannot show: that NVIDIA's driver accepts any of it. See the doc.
 */
#include "omega_dxg_abi.h"
#include "omega_submit_dxg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* backend symbols this test does not link: stand-ins so omega_submit_select.c links */
const OmegaSubmitOps *omega_submit_nvrm_ops(void) { return NULL; }
void *omega_submit_nvrm_self(void) { return NULL; }

typedef struct { const char *name; unsigned long req; unsigned size; unsigned nr; } Pin;
static const Pin pins[] = {
    { "LX_DXENUMADAPTERS2", 0xc0104714, 16, 0x14 },
    { "LX_DXOPENADAPTERFROMLUID", 0xc00c4701, 12, 0x01 },
    { "LX_DXQUERYADAPTERINFO", 0xc0184709, 24, 0x09 },
    { "LX_DXCREATEDEVICE", 0xc0404702, 64, 0x02 },
    { "LX_DXCREATEPAGINGQUEUE", 0xc0204707, 32, 0x07 },
    { "LX_DXCREATECONTEXTVIRTUAL", 0xc0284704, 40, 0x04 },
    { "LX_DXCREATESYNCHRONIZATIONOBJECT", 0xc0604710, 96, 0x10 },
    { "LX_DXCREATEALLOCATION", 0xc0484706, 72, 0x06 },
    { "LX_DXMAPGPUVIRTUALADDRESS", 0xc068470c, 104, 0x0c },
    { "LX_DXMAKERESIDENT", 0xc030470b, 48, 0x0b },
    { "LX_DXLOCK2", 0xc0184725, 24, 0x25 },
    { "LX_DXUNLOCK2", 0xc0084737, 8, 0x37 },
    { "LX_DXSUBMITCOMMAND", 0xc180470f, 384, 0x0f },
    { "LX_DXSIGNALSYNCHRONIZATIONOBJECTFROMGPU", 0xc0504732, 80, 0x32 },
    { "LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU", 0xc028473a, 40, 0x3a },
    { "LX_DXDESTROYALLOCATION2", 0xc0184713, 24, 0x13 },
    { "LX_DXDESTROYSYNCHRONIZATIONOBJECT", 0xc004471d, 4, 0x1d },
    { "LX_DXDESTROYCONTEXT", 0xc0044705, 4, 0x05 },
    { "LX_DXDESTROYPAGINGQUEUE", 0xc004471c, 4, 0x1c },
    { "LX_DXDESTROYDEVICE", 0xc0044719, 4, 0x19 },
    { "LX_DXCLOSEADAPTER", 0xc0044715, 4, 0x15 },
    /* hardware-scheduling rows and the feature query */
    { "LX_ISFEATUREENABLED", 0xc00c4749, 12, 0x49 },
    { "LX_DXCREATEHWQUEUE", 0xc0304718, 48, 0x18 },
    { "LX_DXSUBMITCOMMANDTOHWQUEUE", 0xc0384734, 56, 0x34 },
    { "LX_DXDESTROYHWQUEUE", 0xc004471b, 4, 0x1b },
};

static void test_abi(void)
{
    size_t n;
    const OmegaDxgIoctlRow *r = omega_dxg_ioctl_rows(&n);
    CHECK(n == sizeof pins / sizeof pins[0]);
    for (size_t i = 0; i < n; i++) {
        const Pin *p = NULL;
        for (size_t k = 0; k < sizeof pins / sizeof pins[0]; k++)
            if (strcmp(pins[k].name, r[i].name) == 0) p = &pins[k];
        if (!p) { fprintf(stderr, "no pin for %s\n", r[i].name); g_fail++; continue; }
        if (p->req && r[i].request != p->req) { fprintf(stderr, "%s request 0x%lx != pinned 0x%lx\n", r[i].name, r[i].request, p->req); g_fail++; }
        if (p->size && r[i].size != p->size) { fprintf(stderr, "%s size %u != pinned %u\n", r[i].name, r[i].size, p->size); g_fail++; }
        if (r[i].nr != p->nr) { fprintf(stderr, "%s nr 0x%x != pinned 0x%x\n", r[i].name, r[i].nr, p->nr); g_fail++; }
        CHECK(_IOC_TYPE(r[i].request) == 0x47);
        CHECK(_IOC_DIR(r[i].request) == (_IOC_READ | _IOC_WRITE));
        CHECK(_IOC_SIZE(r[i].request) == r[i].size);
        if (!p->req) printf("  [fill pin] %s 0x%lx %u\n", r[i].name, r[i].request, r[i].size);
    }
    CHECK(sizeof(struct d3dkmthandle) == 4);
    CHECK(sizeof(struct winluid) == 8);
    CHECK(sizeof(struct dxgk_gpuclockdata) == 28); /* the one packed struct: __packed must be a real attribute */
}

/* ---------------------------------------------------------------- mock kernel */
#define MAXLOG 64
typedef struct {
    int hwsch;
    const char *log[MAXLOG];
    int nlog;
    uint32_t next_handle;
    uint64_t next_va;
    uint64_t pagefence, qfence_dummy;
    uint64_t fence_mem[4];       /* [0] paging fence, [1] queue fence / progress fence */
    uint64_t queue_fence_val;
    int n_alloc, n_unlock, n_destroy_alloc;
    unsigned char *host_mem[8];
    int n_host;
    unsigned char got_ctx_priv[64], got_submit_priv[64], got_alloc_priv[64];
    uint32_t got_ctx_priv_size, got_submit_priv_size, got_alloc_priv_size;
    uint64_t last_cmd_va;
    uint32_t last_cmd_len;
    uint64_t last_fence_id;
    uint32_t ctx_node;
    int ctx_hw_flag;
    int fail_req;                /* request number to fail with EIO, 0 none */
} Mock;

static void logc(Mock *m, const char *s) { if (m->nlog < MAXLOG) m->log[m->nlog++] = s; }
static struct d3dkmthandle mkh(Mock *m) { struct d3dkmthandle h; h.v = ++m->next_handle; return h; }

static int mock_ioctl(void *ctx, unsigned long req, void *arg)
{
    Mock *m = (Mock *)ctx;
    if (m->fail_req && _IOC_NR(req) == (unsigned)m->fail_req) { errno = EIO; return -1; }
    switch (_IOC_NR(req)) {
    case 0x14: {
        struct d3dkmt_enumadapters2 *e = arg;
        logc(m, "enum");
        if (!e->adapters) { e->num_adapters = 1; return 0; }
        struct d3dkmt_adapterinfo *ai = (struct d3dkmt_adapterinfo *)(uintptr_t)e->adapters;
        ai[0].adapter_handle = mkh(m);
        ai[0].adapter_luid.a = 0xBEEF; ai[0].adapter_luid.b = 0;
        e->num_adapters = 1;
        return 0;
    }
    case 0x49: {
        struct d3dkmt_isfeatureenabled *f = arg;
        logc(m, "feature");
        CHECK(f->feature_id == _DXGK_FEATURE_HWSCH);
        f->result.enabled = m->hwsch;
        return 0;
    }
    case 0x09: {
        struct d3dkmt_queryadapterinfo *q = arg;
        logc(m, "queryadapter");
        CHECK(q->type == _KMTQAITYPE_ADAPTERTYPE && q->private_data_size == sizeof(struct d3dkmt_adaptertype));
        struct d3dkmt_adaptertype *t = (struct d3dkmt_adaptertype *)(uintptr_t)q->private_data;
        t->render_supported = 1; t->paravirtualized = 1;
        return 0;
    }
    case 0x02: { struct d3dkmt_createdevice *c = arg; logc(m, "createdevice"); CHECK(c->adapter.v != 0); c->device = mkh(m); return 0; }
    case 0x07: {
        struct d3dkmt_createpagingqueue *p = arg;
        logc(m, "createpagingqueue");
        p->paging_queue = mkh(m); p->sync_object = mkh(m);
        p->fence_cpu_virtual_address = (uint64_t)(uintptr_t)&m->fence_mem[0];
        return 0;
    }
    case 0x04: {
        struct d3dkmt_createcontextvirtual *c = arg;
        logc(m, "createcontext");
        m->ctx_node = c->node_ordinal;
        m->ctx_hw_flag = c->flags.hw_queue_supported;
        m->got_ctx_priv_size = c->priv_drv_data_size;
        memcpy(m->got_ctx_priv, (void *)(uintptr_t)c->priv_drv_data, c->priv_drv_data_size);
        c->context = mkh(m);
        return 0;
    }
    case 0x10: {
        struct d3dkmt_createsynchronizationobject2 *s = arg;
        logc(m, "createsync");
        CHECK(s->info.type == _D3DDDI_MONITORED_FENCE);
        s->sync_object = mkh(m);
        s->info.monitored_fence.fence_cpu_virtual_address = (__u64 *)(uintptr_t)&m->fence_mem[1];
        return 0;
    }
    case 0x18: {
        struct d3dkmt_createhwqueue *h = arg;
        logc(m, "createhwqueue");
        h->queue = mkh(m); h->queue_progress_fence = mkh(m);
        h->queue_progress_fence_cpu_va = (uint64_t)(uintptr_t)&m->fence_mem[1];
        return 0;
    }
    case 0x06: {
        struct d3dkmt_createallocation *c = arg;
        struct d3dddi_allocationinfo2 *ai = (struct d3dddi_allocationinfo2 *)(uintptr_t)c->allocation_info;
        logc(m, "createalloc");
        CHECK(c->alloc_count == 1);
        m->got_alloc_priv_size = ai->priv_drv_data_size;
        memcpy(m->got_alloc_priv, (void *)(uintptr_t)ai->priv_drv_data, ai->priv_drv_data_size);
        ai->allocation = mkh(m);
        m->n_alloc++;
        return 0;
    }
    case 0x0c: {
        struct d3dddi_mapgpuvirtualaddress *v = arg;
        logc(m, "mapva");
        CHECK(v->size_in_pages > 0 && v->minimum_address < v->maximum_address && v->protection.write);
        v->virtual_address = m->next_va;
        m->next_va += v->size_in_pages * 4096;
        v->paging_fence_value = ++m->pagefence;
        m->fence_mem[0] = m->pagefence; /* paging completes instantly */
        return 0;
    }
    case 0x0b: { struct d3dddi_makeresident *r = arg; logc(m, "makeresident"); CHECK(r->alloc_count == 1); r->paging_fence_value = m->pagefence; return 0; }
    case 0x25: {
        struct d3dkmt_lock2 *l = arg;
        logc(m, "lock2");
        unsigned char *p = calloc(1, 1 << 16);
        m->host_mem[m->n_host++] = p;
        l->data = (uint64_t)(uintptr_t)p;
        return 0;
    }
    case 0x37: logc(m, "unlock2"); m->n_unlock++; return 0;
    case 0x0f: {
        struct d3dkmt_submitcommand *s = arg;
        logc(m, "submit");
        CHECK(s->broadcast_context_count == 1 && s->broadcast_context[0].v != 0);
        m->last_cmd_va = s->command_buffer; m->last_cmd_len = s->command_length;
        m->got_submit_priv_size = s->priv_drv_data_size;
        memcpy(m->got_submit_priv, (void *)(uintptr_t)s->priv_drv_data, s->priv_drv_data_size);
        return 0;
    }
    case 0x32: {
        struct d3dkmt_signalsynchronizationobjectfromgpu *g = arg;
        logc(m, "signalgpu");
        CHECK(g->object_count == 1);
        uint64_t *vals = (uint64_t *)(uintptr_t)g->monitored_fence_values;
        m->fence_mem[1] = vals[0]; /* the "GPU" finishes the work and signals */
        return 0;
    }
    case 0x34: {
        struct d3dkmt_submitcommandtohwqueue *s = arg;
        logc(m, "submithw");
        m->last_cmd_va = s->command_buffer; m->last_cmd_len = s->command_length;
        m->last_fence_id = s->hwqueue_progress_fence_id;
        m->got_submit_priv_size = s->priv_drv_data_size;
        memcpy(m->got_submit_priv, (void *)(uintptr_t)s->priv_drv_data, s->priv_drv_data_size);
        m->fence_mem[1] = s->hwqueue_progress_fence_id;
        return 0;
    }
    case 0x3a: logc(m, "waitcpu"); return 0;
    case 0x13: logc(m, "destroyalloc"); m->n_destroy_alloc++; return 0;
    case 0x1d: logc(m, "destroysync"); return 0;
    case 0x1b: logc(m, "destroyhwq"); return 0;
    case 0x05: logc(m, "destroyctx"); return 0;
    case 0x1c: logc(m, "destroypq"); return 0;
    case 0x19: logc(m, "destroydev"); return 0;
    case 0x15: logc(m, "closeadapter"); return 0;
    default:
        fprintf(stderr, "mock: unexpected ioctl nr 0x%x\n", (unsigned)_IOC_NR(req));
        g_fail++;
        errno = ENOTTY;
        return -1;
    }
}

static int log_has(Mock *m, const char *s) { for (int i = 0; i < m->nlog; i++) if (!strcmp(m->log[i], s)) return 1; return 0; }
static int log_idx(Mock *m, const char *s) { for (int i = 0; i < m->nlog; i++) if (!strcmp(m->log[i], s)) return i; return -1; }

static const unsigned char CTX_BLOB[8] = { 0xC0, 1, 2, 3, 4, 5, 6, 7 };
static const unsigned char HWQ_BLOB[4] = { 0x77, 1, 2, 3 };
static const unsigned char DEV_BLOB[6] = { 0xDE, 1, 1, 1, 1, 1 };
static const unsigned char HOST_BLOB[6] = { 0x40, 2, 2, 2, 2, 2 };
static const unsigned char SUB_BLOB[16] = { 0x5B, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };

static void set_all_blobs(void)
{
    OmegaDxgBlobs b = {
        .context = { CTX_BLOB, sizeof CTX_BLOB }, .hwqueue = { HWQ_BLOB, sizeof HWQ_BLOB },
        .alloc_device = { DEV_BLOB, sizeof DEV_BLOB }, .alloc_host = { HOST_BLOB, sizeof HOST_BLOB },
        .submit = { SUB_BLOB, sizeof SUB_BLOB },
    };
    omega_dxg_set_blobs(&b);
}

static void run_flow(int hwsch)
{
    Mock m;
    memset(&m, 0, sizeof m);
    m.hwsch = hwsch;
    m.next_va = 0x200000;
    omega_dxg_set_ioctl_hook(mock_ioctl, &m);
    set_all_blobs();
    const OmegaSubmitOps *ops = omega_submit_dxg_ops();
    void *self = omega_submit_dxg_self();
    OmegaSubDeviceInfo info;
    CHECK(ops->open(self, &info) == OMEGA_SUB_OK);
    CHECK(strstr(info.description, hwsch ? "hwsch" : "swsch") != NULL);
    CHECK(ops->open(self, &info) == OMEGA_SUB_E_STATE);
    CHECK(ops->create_queue(self, OMEGA_SUB_QUEUE_COPY) == OMEGA_SUB_E_UNSUPPORTED);
    CHECK(ops->create_queue(self, OMEGA_SUB_QUEUE_COMPUTE) == OMEGA_SUB_OK);
    CHECK(m.got_ctx_priv_size == sizeof CTX_BLOB && memcmp(m.got_ctx_priv, CTX_BLOB, sizeof CTX_BLOB) == 0);
    CHECK(m.ctx_hw_flag == hwsch);
    CHECK(log_has(&m, hwsch ? "createhwqueue" : "createsync"));

    OmegaSubMem stage, vram;
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_HOST_VISIBLE, 5000, 0, &stage) == OMEGA_SUB_OK);
    CHECK(stage.cpu != NULL && stage.gpu_va >= 0x200000 && stage.size == 8192 && stage.kind == OMEGA_SUB_MEM_HOST_VISIBLE);
    CHECK(m.got_alloc_priv_size == sizeof HOST_BLOB && memcmp(m.got_alloc_priv, HOST_BLOB, sizeof HOST_BLOB) == 0);
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_DEVICE_LOCAL, 1 << 20, 0, &vram) == OMEGA_SUB_OK);
    CHECK(vram.cpu == NULL && vram.gpu_va != stage.gpu_va && vram.kind == OMEGA_SUB_MEM_DEVICE_LOCAL);
    CHECK(m.got_alloc_priv_size == sizeof DEV_BLOB && memcmp(m.got_alloc_priv, DEV_BLOB, sizeof DEV_BLOB) == 0);
    OmegaSubMem bad;
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_DEVICE_LOCAL, 4096, OMEGA_SUB_MEM_CPU_MAP, &bad) == OMEGA_SUB_E_UNSUPPORTED);
    CHECK(log_idx(&m, "mapva") < log_idx(&m, "makeresident") && log_idx(&m, "createalloc") < log_idx(&m, "mapva"));

    /* "program": the caller writes pushbuffer words into the CPU mapping; the mock records what the
     * kernel would be told to run (GPU VA + length), which must be the staging buffer + offset */
    uint32_t *pb = (uint32_t *)stage.cpu;
    for (int i = 0; i < 8; i++) pb[i] = 0xF00D0000u + (uint32_t)i;
    CHECK(ops->emit_fence(self, OMEGA_SUB_QUEUE_COMPUTE, pb + 8, 8, 1) == 0);
    CHECK(ops->submit(self, OMEGA_SUB_QUEUE_COMPUTE, &stage, 16, 6, 1) == OMEGA_SUB_OK);
    CHECK(m.last_cmd_va == stage.gpu_va + 16 && m.last_cmd_len == 24);
    CHECK(m.got_submit_priv_size == sizeof SUB_BLOB && memcmp(m.got_submit_priv, SUB_BLOB, sizeof SUB_BLOB) == 0);
    CHECK(log_has(&m, hwsch ? "submithw" : "signalgpu"));
    CHECK(ops->wait(self, OMEGA_SUB_QUEUE_COMPUTE, 1, 100) == OMEGA_SUB_OK);
    CHECK(ops->wait(self, OMEGA_SUB_QUEUE_COMPUTE, 2, 30) == OMEGA_SUB_E_TIMEOUT);
    CHECK(ops->submit(self, OMEGA_SUB_QUEUE_COMPUTE, &stage, 0, 4, 1) == OMEGA_SUB_E_ARG);              /* fence must rise */
    CHECK(ops->submit(self, OMEGA_SUB_QUEUE_COMPUTE, &stage, 8160, 16, 2) == OMEGA_SUB_E_ARG);         /* out of range */
    CHECK(ops->submit(self, OMEGA_SUB_QUEUE_COMPUTE, &vram, 0, 4, 2) == OMEGA_SUB_E_ARG);              /* VRAM is not a pushbuffer */
    CHECK(ops->free_mem(self, &vram) == OMEGA_SUB_OK && vram.size == 0);
    CHECK(ops->close(self) == OMEGA_SUB_OK);   /* frees stage, queue objects, device, adapter */
    CHECK(m.n_unlock == 1 && m.n_destroy_alloc == 2);
    CHECK(strcmp(m.log[m.nlog - 1], "closeadapter") == 0);
    CHECK(log_idx(&m, "destroydev") < log_idx(&m, "closeadapter") && log_idx(&m, "destroyctx") < log_idx(&m, "destroydev"));
    for (int i = 0; i < m.n_host; i++) free(m.host_mem[i]);
    omega_dxg_set_ioctl_hook(NULL, NULL);
}

static void test_missing_blobs(void)
{
    Mock m;
    const OmegaSubmitOps *ops = omega_submit_dxg_ops();
    void *self = omega_submit_dxg_self();
    OmegaSubMem mem;
    OmegaDxgBlobs none;

    memset(&m, 0, sizeof m);
    memset(&none, 0, sizeof none);
    omega_dxg_set_ioctl_hook(mock_ioctl, &m);
    omega_dxg_set_blobs(&none);
    CHECK(ops->open(self, NULL) == OMEGA_SUB_OK);        /* the generic part works with no blobs */
    int before = m.nlog;
    CHECK(ops->create_queue(self, OMEGA_SUB_QUEUE_COMPUTE) == OMEGA_SUB_E_VENDOR_BLOB);
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_HOST_VISIBLE, 4096, 0, &mem) == OMEGA_SUB_E_VENDOR_BLOB);
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_DEVICE_LOCAL, 4096, 0, &mem) == OMEGA_SUB_E_VENDOR_BLOB);
    CHECK(m.nlog == before);                              /* no ioctl was issued for any refused step */
    CHECK(strstr(ops->last_error(self), "private data") != NULL);
    CHECK(ops->close(self) == OMEGA_SUB_OK);

    /* submit blob alone missing */
    memset(&m, 0, sizeof m);
    m.next_va = 0x200000;
    OmegaDxgBlobs b = { .context = { CTX_BLOB, sizeof CTX_BLOB }, .alloc_host = { HOST_BLOB, sizeof HOST_BLOB } };
    omega_dxg_set_blobs(&b);
    CHECK(ops->open(self, NULL) == OMEGA_SUB_OK);
    CHECK(ops->create_queue(self, OMEGA_SUB_QUEUE_COMPUTE) == OMEGA_SUB_OK);
    CHECK(ops->alloc(self, OMEGA_SUB_MEM_HOST_VISIBLE, 4096, 0, &mem) == OMEGA_SUB_OK);
    before = m.nlog;
    CHECK(ops->submit(self, OMEGA_SUB_QUEUE_COMPUTE, &mem, 0, 4, 1) == OMEGA_SUB_E_VENDOR_BLOB);
    CHECK(m.nlog == before);
    CHECK(ops->close(self) == OMEGA_SUB_OK);
    for (int i = 0; i < m.n_host; i++) free(m.host_mem[i]);

    /* a failing kernel call surfaces as DRIVER with errno text */
    memset(&m, 0, sizeof m);
    m.fail_req = 0x02;
    CHECK(ops->open(self, NULL) == OMEGA_SUB_E_DRIVER);
    CHECK(strstr(ops->last_error(self), "createdevice") != NULL);
    CHECK(ops->close(self) == OMEGA_SUB_OK);              /* partial open still cleans up */
    CHECK(log_has(&m, "closeadapter"));
    omega_dxg_set_ioctl_hook(NULL, NULL);
}

static void test_blob_dir(void)
{
    char dir[] = "/tmp/omega-dxg-blobs-XXXXXX";
    if (!mkdtemp(dir)) { g_fail++; return; }
    char path[256];
    snprintf(path, sizeof path, "%s/submit.bin", dir);
    FILE *f = fopen(path, "wb");
    fwrite(SUB_BLOB, 1, sizeof SUB_BLOB, f);
    fclose(f);
    omega_dxg_set_blobs(NULL);
    CHECK(omega_dxg_load_blob_dir(dir) == 1);
    snprintf(path, sizeof path, "rm -rf %s", dir);
    if (system(path)) g_fail++;
}

static void test_real_device_absent(void)
{
    if (access("/dev/dxg", F_OK) == 0) { printf("  /dev/dxg exists here; skipping the absent-device check\n"); return; }
    omega_dxg_set_ioctl_hook(NULL, NULL);
    const OmegaSubmitOps *ops = omega_submit_dxg_ops();
    CHECK(ops->open(omega_submit_dxg_self(), NULL) == OMEGA_SUB_E_NO_DEVICE);
    OmegaSubmitBackend be;
    setenv("OMEGA_SUBMIT", "dxg", 1);
    CHECK(omega_submit_select(&be) == OMEGA_SUB_OK && be.ops == omega_submit_dxg_ops());
    setenv("OMEGA_SUBMIT", "bogus", 1);
    CHECK(omega_submit_select(&be) == OMEGA_SUB_E_ARG);
    unsetenv("OMEGA_SUBMIT");
}

int main(void)
{
    test_abi();
    run_flow(0);
    run_flow(1);
    test_missing_blobs();
    test_blob_dir();
    test_real_device_absent();
    if (g_fail) { printf("test_omega_submit_dxg: %d FAILED\n", g_fail); return 1; }
    printf("test_omega_submit_dxg: ok\n");
    return 0;
}
