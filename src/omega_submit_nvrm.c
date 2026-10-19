/*
 * omega_submit_nvrm.c -- OmegaSubmitOps over physics/nvrm (NVIDIA open kernel module, Linux).
 *
 * Same driver calls as the GB10 launchers (nvrm_open, nvrm_channel, nvrm_alloc_gpu_uncached,
 * nvrm_submit) behind the submit interface. The GB10 launchers themselves are not changed and do
 * not call this file.
 *
 * DEVICE_LOCAL memory:
 *   - physics with NVRM_HAS_VIDMEM (branch rig/gb20x of aien-dev/physics-mv): nvrm_alloc_vidmem,
 *     i.e. NV01_MEMORY_LOCAL_USER in VRAM, optional BAR1 CPU mapping. CHIP-UNVERIFIED.
 *   - pinned physics (physics.lock 6d7cf0d4) on a unified-memory target (GB10): the same
 *     GPU-uncached system memory as HOST_VISIBLE, which is what the GB10 has.
 *   - pinned physics on a discrete card: OMEGA_SUB_E_UNSUPPORTED.
 *
 * Fence: one 0x1000 host-visible page per backend; the compute queue releases a 32-bit payload
 * with the exact method words the GB10 launchers use for their completion marker
 * (nvrm_mthd(0, 0x005c, 5), addr lo, addr hi, payload, 0, RELEASE | WFI).
 * COPY queue: needs a second GPFIFO channel; physics nvrm keeps one channel per client today, so
 * create_queue(COPY) returns OMEGA_SUB_E_UNSUPPORTED (uploads use BAR1 or HOST_VISIBLE + compute).
 */
#include "omega_gpu_submit_iface.h"
#include "omega_bw_barrier.h"
#include "omega_bw_target.h"
#include "nvrm.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "nvos.h"
#include "nv-ioctl.h"
#include "nv-ioctl-numbers.h"
#include "nv_escape.h"
#include "ctrl/ctrl2080/ctrl2080mc.h"

#define NV_IOWR(nr, sz) _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, (nr), (sz))

typedef struct {
    Nvrm rm;
    int opened;
    int channel;
    NvrmMem fence;
    volatile uint32_t *fence_word;
    uint64_t last_fence;
    uint32_t last_put;
    const OmegaBwTarget *target;
    char err[256];
} SubNvrm;

static SubNvrm g_sub;

static int sfail(SubNvrm *s, int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->err, sizeof s->err, fmt, ap);
    va_end(ap);
    return code;
}

static int ctl(Nvrm *rm, uint32_t obj, uint32_t cmd, void *params, uint32_t psz)
{
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof p);
    p.hClient = rm->root;
    p.hObject = obj;
    p.cmd = cmd;
    p.params = NV_PTR_TO_NvP64(params);
    p.paramsSize = psz;
    if (ioctl(rm->fd_ctl, NV_IOWR(NV_ESC_RM_CONTROL, sizeof p), &p) != 0) return -1;
    return p.status == 0 ? 0 : -1;
}

static NvrmMem to_nvrm(const OmegaSubMem *m)
{
    NvrmMem n = { .handle = m->backend_handle, .va = m->gpu_va, .size = m->size, .cpu = m->cpu };
    return n;
}

static int nv_open(void *self, OmegaSubDeviceInfo *info)
{
    SubNvrm *s = (SubNvrm *)self;
    if (s->opened) return sfail(s, OMEGA_SUB_E_STATE, "already open");
    memset(s, 0, sizeof *s);
    if (access("/dev/nvidiactl", F_OK) != 0) return sfail(s, OMEGA_SUB_E_NO_DEVICE, "no /dev/nvidiactl");
    if (nvrm_open(&s->rm) != 0) {
        int code = sfail(s, OMEGA_SUB_E_DRIVER, "nvrm_open: %s", s->rm.err);
        nvrm_close(&s->rm);
        return code;
    }
    s->opened = 1;
    NV2080_CTRL_MC_GET_ARCH_INFO_PARAMS ai;
    memset(&ai, 0, sizeof ai);
    (void)ctl(&s->rm, s->rm.subdevice, NV2080_CTRL_CMD_MC_GET_ARCH_INFO, &ai, sizeof ai);
    s->target = omega_bw_target_identify(ai.architecture, ai.implementation, s->rm.sm_version, 0);
    if (info) {
        memset(info, 0, sizeof *info);
        info->arch = ai.architecture;
        info->impl = ai.implementation;
        info->rm_sm_version = s->rm.sm_version;
        info->compute_class = s->rm.compute_class;
        info->gpfifo_class = s->rm.gpfifo_class;
        info->usermode_class = s->rm.usermode_class;
        info->vram_bytes = s->target ? s->target->vram_bytes : 0;
        snprintf(info->description, sizeof info->description, "nvrm: %s",
                 s->target ? s->target->name : "unidentified Blackwell");
    }
    return OMEGA_SUB_OK;
}

static int fill_mem(SubNvrm *s, const NvrmMem *n, uint32_t kind, OmegaSubMem *out)
{
    (void)s;
    memset(out, 0, sizeof *out);
    out->gpu_va = n->va;
    out->cpu = n->cpu;
    out->size = n->size;
    out->kind = kind;
    out->backend_handle = n->handle;
    return OMEGA_SUB_OK;
}

static int nv_alloc(void *self, OmegaSubMemKind kind, uint64_t size, uint32_t flags, OmegaSubMem *out)
{
    SubNvrm *s = (SubNvrm *)self;
    NvrmMem n;
    if (!s->opened) return sfail(s, OMEGA_SUB_E_STATE, "alloc before open");
    if (!out || size == 0) return sfail(s, OMEGA_SUB_E_ARG, "alloc: bad arguments");
    if (kind == OMEGA_SUB_MEM_HOST_VISIBLE) {
        if (nvrm_alloc_gpu_uncached(&s->rm, size, &n) != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "alloc: %s", s->rm.err);
        return fill_mem(s, &n, kind, out);
    }
    if (kind != OMEGA_SUB_MEM_DEVICE_LOCAL) return sfail(s, OMEGA_SUB_E_ARG, "alloc: unknown kind");
#ifdef NVRM_HAS_VIDMEM
    if (!s->target || s->target->mem_model == OMEGA_BW_MEM_DISCRETE_VRAM) {
        if (nvrm_alloc_vidmem(&s->rm, size, (flags & OMEGA_SUB_MEM_CPU_MAP) ? 1 : 0, &n) != 0)
            return sfail(s, OMEGA_SUB_E_DRIVER, "alloc vidmem: %s", s->rm.err);
        return fill_mem(s, &n, kind, out);
    }
#endif
    if (s->target && s->target->mem_model == OMEGA_BW_MEM_UNIFIED_SYSMEM) {
        (void)flags; /* always CPU mapped on GB10 */
        if (nvrm_alloc_gpu_uncached(&s->rm, size, &n) != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "alloc: %s", s->rm.err);
        return fill_mem(s, &n, kind, out);
    }
    return sfail(s, OMEGA_SUB_E_UNSUPPORTED, "DEVICE_LOCAL on a discrete card needs physics with NVRM_HAS_VIDMEM");
}

static int nv_free(void *self, OmegaSubMem *m)
{
    SubNvrm *s = (SubNvrm *)self;
    if (!m) return OMEGA_SUB_E_ARG;
    if (m->size == 0) return OMEGA_SUB_OK;
    NvrmMem n = to_nvrm(m);
    if (nvrm_free(&s->rm, &n) != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "free: %s", s->rm.err);
    memset(m, 0, sizeof *m);
    return OMEGA_SUB_OK;
}

static int nv_create_queue(void *self, OmegaSubQueue q)
{
    SubNvrm *s = (SubNvrm *)self;
    if (!s->opened) return sfail(s, OMEGA_SUB_E_STATE, "queue before open");
    if (q == OMEGA_SUB_QUEUE_COPY)
        return sfail(s, OMEGA_SUB_E_UNSUPPORTED, "copy queue needs a second GPFIFO channel (physics nvrm has one)");
    if (q != OMEGA_SUB_QUEUE_COMPUTE) return sfail(s, OMEGA_SUB_E_ARG, "unknown queue");
    if (s->channel) return OMEGA_SUB_OK;
    if (nvrm_channel(&s->rm) != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "channel: %s", s->rm.err);
    s->channel = 1;
    if (nvrm_alloc_gpu_uncached(&s->rm, 0x1000, &s->fence) != 0)
        return sfail(s, OMEGA_SUB_E_DRIVER, "fence page: %s", s->rm.err);
    s->fence_word = (volatile uint32_t *)s->fence.cpu;
    *s->fence_word = 0;
    omega_bw_mb();
    return OMEGA_SUB_OK;
}

static int nv_emit_fence(void *self, OmegaSubQueue q, uint32_t *pb, size_t cap, uint64_t value)
{
    SubNvrm *s = (SubNvrm *)self;
    if (q != OMEGA_SUB_QUEUE_COMPUTE || !s->fence_word) return sfail(s, OMEGA_SUB_E_STATE, "fence: no compute queue");
    if (!pb || cap < 6) return sfail(s, OMEGA_SUB_E_ARG, "fence: pushbuffer too small");
    pb[0] = nvrm_mthd(0, 0x005c, 5);
    pb[1] = (uint32_t)s->fence.va;
    pb[2] = (uint32_t)(s->fence.va >> 32);
    pb[3] = (uint32_t)value;
    pb[4] = 0;
    pb[5] = 0x1u | (1u << 20); /* RELEASE | WFI */
    return 6;
}

static int nv_submit(void *self, OmegaSubQueue q, const OmegaSubMem *pb, uint32_t off, uint32_t nwords, uint64_t fence_value)
{
    SubNvrm *s = (SubNvrm *)self;
    if (q != OMEGA_SUB_QUEUE_COMPUTE || !s->channel) return sfail(s, OMEGA_SUB_E_STATE, "submit: no compute queue");
    if (!pb || pb->kind != OMEGA_SUB_MEM_HOST_VISIBLE) return sfail(s, OMEGA_SUB_E_ARG, "submit: pushbuffer must be HOST_VISIBLE");
    NvrmMem n = to_nvrm(pb);
    omega_bw_wmb();
    if (nvrm_submit(&s->rm, &n, off, nwords) != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "submit: %s", s->rm.err);
    s->last_fence = fence_value;
    s->last_put = s->rm.put;
    return OMEGA_SUB_OK;
}

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static int nv_wait(void *self, OmegaSubQueue q, uint64_t value, uint64_t timeout_ms)
{
    SubNvrm *s = (SubNvrm *)self;
    if (q != OMEGA_SUB_QUEUE_COMPUTE || !s->fence_word) return sfail(s, OMEGA_SUB_E_STATE, "wait: no compute queue");
    uint32_t want = (uint32_t)value;
    uint64_t end = now_ms() + timeout_ms;
    for (;;) {
        uint32_t v = *s->fence_word;
        if ((int32_t)(v - want) >= 0) {
            omega_bw_mb();
            return OMEGA_SUB_OK;
        }
        if (now_ms() >= end)
            return sfail(s, OMEGA_SUB_E_TIMEOUT, "wait: fence 0x%x, wanted 0x%x", v, want);
        usleep(20);
    }
}

static void nv_retire(void *self, OmegaSubQueue q, uint64_t value)
{
    SubNvrm *s = (SubNvrm *)self;
    if (q == OMEGA_SUB_QUEUE_COMPUTE && s->channel && value >= s->last_fence) nvrm_retire(&s->rm, s->last_put);
}

static int nv_close(void *self)
{
    SubNvrm *s = (SubNvrm *)self;
    if (!s->opened) return OMEGA_SUB_OK;
    s->fence_word = NULL;
    int rc = nvrm_close(&s->rm); /* frees the fence page and every caller buffer still live */
    s->opened = 0;
    s->channel = 0;
    if (rc != 0) return sfail(s, OMEGA_SUB_E_DRIVER, "close: %s", s->rm.err);
    return OMEGA_SUB_OK;
}

static const char *nv_last_error(void *self) { return ((SubNvrm *)self)->err; }

static const OmegaSubmitOps g_ops = {
    .kind = OMEGA_SUBMIT_NVRM,
    .name = "nvrm",
    .open = nv_open,
    .alloc = nv_alloc,
    .free_mem = nv_free,
    .create_queue = nv_create_queue,
    .emit_fence = nv_emit_fence,
    .submit = nv_submit,
    .wait = nv_wait,
    .retire = nv_retire,
    .close = nv_close,
    .last_error = nv_last_error,
};

const OmegaSubmitOps *omega_submit_nvrm_ops(void) { return &g_ops; }
void *omega_submit_nvrm_self(void) { return &g_sub; }
