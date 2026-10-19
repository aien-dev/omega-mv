/*
 * omega_gpu_submit_iface.h -- one submission interface, two driver paths.
 *
 *   OMEGA_SUBMIT_NVRM  NVIDIA open kernel module on Linux (/dev/nvidiactl, /dev/nvidia0,
 *                      /dev/nvidia-uvm) through physics/nvrm. This is the GB10 path today and the
 *                      native-Linux path for an RTX 5070.
 *   OMEGA_SUBMIT_DXG   WSL2 on Windows (/dev/dxg, Microsoft dxgkrnl, D3DKMT-style ioctls). The
 *                      Windows NVIDIA driver owns the GPU; Linux user space reaches it only through
 *                      dxgkrnl. Drake's target machine (RTX 5070 PC) runs the engine here.
 *
 * Status (2026-10-19):
 *   - The existing GB10 launchers (src/omega_blackwell_{submit,engine}.c, src/omega_gpu_*) do NOT
 *     use this interface. They are unchanged. This interface is additive.
 *   - The NVRM implementation (src/omega_submit_nvrm.c) is exercised on the Spark by
 *     tests/test_omega_submit_nvrm_gb10.c (a semaphore-release round trip).
 *   - The DXG implementation (src/omega_submit_dxg.c) does every WDDM-generic step for real and
 *     returns OMEGA_SUB_E_VENDOR_BLOB for the steps that need NVIDIA's private driver data, whose
 *     format NVIDIA does not publish. docs/rig/WSL2_DXG_BACKEND.md maps each op to its ioctl.
 *
 * Model. A backend gives the caller:
 *   memory   HOST_VISIBLE (CPU-mapped, GPU-readable, GPU-uncached: pushbuffers, QMDs, fences,
 *            small arguments) and DEVICE_LOCAL (VRAM on a discrete card; on GB10 the same kind of
 *            system memory as HOST_VISIBLE, because the chip has no VRAM).
 *   queues   COMPUTE (BLACKWELL_COMPUTE_B on subchannel 1, as on GB10) and COPY
 *            (BLACKWELL_DMA_COPY_B on subchannel 4, discrete cards).
 *   fences   one monotonically increasing 64-bit value per queue. emit_fence() appends the
 *            device-side release to a pushbuffer when the backend signals by methods (NVRM);
 *            a backend that signals from the kernel driver (DXG) appends nothing and signals in
 *            submit(). wait() blocks until the value is reached or the timeout passes.
 */
#ifndef OMEGA_GPU_SUBMIT_IFACE_H
#define OMEGA_GPU_SUBMIT_IFACE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OMEGA_SUBMIT_NVRM = 1,
    OMEGA_SUBMIT_DXG = 2
} OmegaSubmitKind;

typedef enum {
    OMEGA_SUB_OK = 0,
    OMEGA_SUB_E_ARG = -1,
    OMEGA_SUB_E_NO_DEVICE = -2,     /* the device node is absent (e.g. no /dev/dxg on native Linux) */
    OMEGA_SUB_E_DRIVER = -3,        /* ioctl or RM call failed; text in last_error */
    OMEGA_SUB_E_TIMEOUT = -4,
    OMEGA_SUB_E_UNSUPPORTED = -5,   /* this backend or this physics pin cannot do it yet */
    OMEGA_SUB_E_VENDOR_BLOB = -6,   /* needs NVIDIA private driver data with no public format */
    OMEGA_SUB_E_STATE = -7          /* call order wrong (submit before open, ...) */
} OmegaSubStatus;

typedef enum {
    OMEGA_SUB_MEM_HOST_VISIBLE = 1,
    OMEGA_SUB_MEM_DEVICE_LOCAL = 2
} OmegaSubMemKind;

#define OMEGA_SUB_MEM_CPU_MAP 0x1u   /* DEVICE_LOCAL only: also map it for the CPU (BAR1 on PCIe) */

typedef enum {
    OMEGA_SUB_QUEUE_COMPUTE = 0,
    OMEGA_SUB_QUEUE_COPY = 1,
    OMEGA_SUB_QUEUE_COUNT = 2
} OmegaSubQueue;

typedef struct {
    uint64_t gpu_va;
    void *cpu;            /* NULL when not CPU mapped */
    uint64_t size;
    uint32_t kind;        /* OmegaSubMemKind */
    uint32_t backend_handle;
    uint64_t backend_aux; /* backend private (e.g. dxg allocation handle) */
} OmegaSubMem;

typedef struct {
    uint32_t arch, impl, rm_sm_version;   /* 0 when the backend cannot read them (DXG today) */
    uint16_t pci_vendor, pci_device;      /* 0 when unknown */
    uint32_t compute_class, gpfifo_class, usermode_class, copy_class;
    uint64_t vram_bytes;                  /* 0 = unified or unknown */
    char description[96];
} OmegaSubDeviceInfo;

typedef struct OmegaSubmitOps {
    OmegaSubmitKind kind;
    const char *name;
    int (*open)(void *self, OmegaSubDeviceInfo *info);
    int (*alloc)(void *self, OmegaSubMemKind kind, uint64_t size, uint32_t flags, OmegaSubMem *out);
    int (*free_mem)(void *self, OmegaSubMem *m);
    int (*create_queue)(void *self, OmegaSubQueue q);
    /* Words for the device-side fence release, appended by the caller at the end of a pushbuffer.
     * Returns the word count (0 = this backend signals in submit), or a negative status. */
    int (*emit_fence)(void *self, OmegaSubQueue q, uint32_t *pb, size_t cap, uint64_t value);
    /* Submit nwords at pb->gpu_va + off_bytes on queue q. fence_value is the value the caller's
     * emit_fence() wrote (NVRM) or the value the backend should signal after it (DXG). */
    int (*submit)(void *self, OmegaSubQueue q, const OmegaSubMem *pb, uint32_t off_bytes, uint32_t nwords,
                  uint64_t fence_value);
    int (*wait)(void *self, OmegaSubQueue q, uint64_t fence_value, uint64_t timeout_ms);
    /* Tell the backend every submission up to fence_value on q has completed (ring reuse). */
    void (*retire)(void *self, OmegaSubQueue q, uint64_t fence_value);
    int (*close)(void *self);
    const char *(*last_error)(void *self);
} OmegaSubmitOps;

typedef struct {
    const OmegaSubmitOps *ops;
    void *self;
} OmegaSubmitBackend;

/* Backends. Each returns a process-wide singleton (one device per process, like the GB10 engine). */
const OmegaSubmitOps *omega_submit_nvrm_ops(void);
void *omega_submit_nvrm_self(void);
const OmegaSubmitOps *omega_submit_dxg_ops(void);
void *omega_submit_dxg_self(void);

/* Pick a backend for this machine: OMEGA_SUBMIT env var ("nvrm" / "dxg") wins; otherwise
 * /dev/nvidiactl present -> NVRM, else /dev/dxg present -> DXG, else NO_DEVICE. */
int omega_submit_select(OmegaSubmitBackend *out);

const char *omega_sub_status_name(int status);

#endif /* OMEGA_GPU_SUBMIT_IFACE_H */
