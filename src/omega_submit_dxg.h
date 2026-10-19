/*
 * omega_submit_dxg.h -- test and configuration surface of the WSL2 /dev/dxg submission backend.
 *
 * The backend itself is reached through omega_submit_dxg_ops() (omega_gpu_submit_iface.h). This
 * header adds three things the interface does not carry:
 *   1. NVIDIA private driver data ("vendor blobs"). dxgkrnl passes the blobs below to the Windows
 *      NVIDIA kernel-mode driver untouched. Their layout is not published. The backend takes them as
 *      opaque bytes and refuses (OMEGA_SUB_E_VENDOR_BLOB) to call the matching ioctl without one.
 *   2. An ioctl hook, so the whole flow runs against a mock /dev/dxg on a machine without one.
 *   3. A table of every ioctl the backend uses (name, request number, argument size) so a test can
 *      compare it with d3dkmthk.h.
 * See docs/rig/dxg-backend.md.
 */
#ifndef OMEGA_SUBMIT_DXG_H
#define OMEGA_SUBMIT_DXG_H

#include <stddef.h>
#include <stdint.h>

#include "omega_gpu_submit_iface.h"

/* ---- TODO constants: values NVIDIA's WSL user-mode driver chooses and nobody public documents.
 * Each one is used in exactly one place in omega_submit_dxg.c and is overridable by the env var
 * named next to it so the first experiment on the 5070 can sweep them without a rebuild.  ---- */
#define OMEGA_DXG_TODO_NODE_ORDINAL 0u        /* OMEGA_DXG_NODE      WDDM node of the 3D/compute engine */
#define OMEGA_DXG_TODO_ENGINE_AFFINITY 0u     /* OMEGA_DXG_AFFINITY  0 = driver default; D3D12 passes 1 << node */
#define OMEGA_DXG_TODO_VA_MIN 0x100000ull     /* OMEGA_DXG_VA_MIN    lowest GPU VA we ask MapGpuVirtualAddress for */
#define OMEGA_DXG_TODO_VA_MAX 0x7fffffffffffull /* OMEGA_DXG_VA_MAX  highest (47-bit); NVIDIA's real limit unknown */
#define OMEGA_DXG_TODO_CLIENT_HINT 0u         /* OMEGA_DXG_HINT      d3dkmt_clienthint; CUDA's value unknown */
#define OMEGA_DXG_TODO_MODE "auto"          /* OMEGA_DXG_MODE     auto | submit | hwqueue; auto asks dxgkrnl whether HWSCH is on */
#define OMEGA_DXG_ALLOC_ALIGN 4096ull         /* WDDM GPU VA pages are 4 KiB; NVIDIA may want 64 KiB (unverified) */

typedef struct {
    const void *data;
    uint32_t size;
} OmegaDxgBlob;

typedef struct {
    OmegaDxgBlob context;       /* D3DKMTCreateContextVirtual PrivateDriverData */
    OmegaDxgBlob hwqueue;       /* D3DKMTCreateHwQueue PrivateDriverData (hardware scheduling only) */
    OmegaDxgBlob alloc_device;  /* D3DKMTCreateAllocation, per-allocation private data, VRAM */
    OmegaDxgBlob alloc_host;    /* same, host-visible (system memory, CPU-lockable) */
    OmegaDxgBlob submit;        /* D3DKMTSubmitCommand PrivateDriverData */
} OmegaDxgBlobs;

/* Provide blobs from memory. The pointers must outlive the backend. NULL clears. */
void omega_dxg_set_blobs(const OmegaDxgBlobs *b);
/* Provide blobs from files <dir>/{context,hwqueue,alloc_device,alloc_host,submit}.bin (what the strace
 * capture in docs/rig/dxg-backend.md section 6 produces). Missing files leave that blob empty.
 * Returns the number of files loaded. Also done automatically from $OMEGA_DXG_BLOBS at open(). */
int omega_dxg_load_blob_dir(const char *dir);

/* Test hook. When fn is non-NULL, open() skips /dev/dxg and every ioctl goes to fn(ctx, request, arg),
 * which returns what ioctl() would (0 or -1 with errno set). Pass NULL to restore the real device. */
typedef int (*OmegaDxgIoctlFn)(void *ctx, unsigned long request, void *arg);
void omega_dxg_set_ioctl_hook(OmegaDxgIoctlFn fn, void *ctx);

typedef struct {
    const char *name;
    unsigned long request;   /* the full _IOWR(0x47, nr, T) value */
    unsigned size;           /* sizeof(T) */
    unsigned nr;
} OmegaDxgIoctlRow;

/* Rows for every ioctl the backend issues. */
const OmegaDxgIoctlRow *omega_dxg_ioctl_rows(size_t *count);

#endif /* OMEGA_SUBMIT_DXG_H */
