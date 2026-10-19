# Omega on an RTX 5070 under WSL2: the /dev/dxg submission backend

Status: host-tested only. Nothing here has touched a real `/dev/dxg` or a real RTX 5070.
Branch: `rig/gb20x`. Files: `src/omega_gpu_submit_iface.h`, `src/omega_submit_nvrm.c`,
`src/omega_submit_dxg.{c,h}`, `src/omega_submit_select.c`, `src/omega_dxg_abi.h`,
`src/omega_bw_target.{c,h}`, `tests/test_omega_submit_dxg.c`, `tools/rig/check_dxg_table.py`.

## 1. Why a second backend

On the Spark, Omega talks to the NVIDIA open kernel module (`/dev/nvidiactl`, `/dev/nvidia0`,
`/dev/nvidia-uvm`) with RM ioctls and rings the GPU through a GPFIFO channel it owns. Under WSL2 none
of those nodes exist. Windows owns the GPU; Linux user space reaches it only through
`/dev/dxg` (Microsoft's `dxgkrnl`, a paravirtual WDDM client that forwards each ioctl to the Windows
kernel graphics stack over VMBus). The ioctls are D3DKMT calls (`LX_DX*`, magic `0x47`), declared in
`d3dkmthk.h` (vendored in `third_party/wsl2-dxgkrnl/`, source in `third_party/README.rig.md`).

The GB10 launchers (`src/omega_blackwell_{submit,engine}.c`, `src/omega_gpu_*`) are not touched.
The new interface is additive: `git diff a8ffd2b9 HEAD --name-status` is all `A`. The Makefile's
`SRCS` list is explicit, so the new files are not linked into `omegatool`.

## 2. The interface (`omega_gpu_submit_iface.h`)

One table of function pointers, `OmegaSubmitOps`: `open, alloc, free_mem, create_queue, emit_fence,
submit, wait, retire, close, last_error`. Memory has two kinds, `HOST_VISIBLE` (CPU-mapped, used for
pushbuffers, QMDs, fences, small arguments; the "staging buffer") and `DEVICE_LOCAL` (VRAM; on GB10
the same system memory). Fences are one 64-bit value per queue.

| | nvrm (GB10, native Linux) | dxg (WSL2) |
|---|---|---|
| fence | device writes a payload with release methods that the caller appends (`emit_fence` returns 6 words) | kernel signals a monitored fence after the submit; `emit_fence` returns 0 words |
| submit | `nvrm_submit` on the Omega-owned GPFIFO | `D3DKMTSubmitCommand` (or `SubmitCommandToHwQueue`) with GPU VA + length + NVIDIA private data |
| VRAM | `nvrm_alloc_vidmem` only with a physics build that has `NVRM_HAS_VIDMEM`; otherwise unified system memory on GB10 | `D3DKMTCreateAllocation` + `MapGpuVirtualAddress` + `MakeResident` |

`omega_submit_select()` chooses: env `OMEGA_SUBMIT=nvrm|dxg`, else `/dev/nvidiactl`, else `/dev/dxg`.

The nvrm backend is a wrapper over the same `nvrm_open/nvrm_channel/nvrm_submit` calls the GB10
launchers use. It is not what the GB10 gates run; they run the original launchers.

## 3. The dxg call flow (`src/omega_submit_dxg.c`)

```
open          open("/dev/dxg")
              ENUMADAPTERS2 (count, then fill)           pick OMEGA_DXG_ADAPTER_INDEX, default 0
              QUERYADAPTERINFO(ADAPTERTYPE)              render / paravirtualized flags into the description
              CREATEDEVICE
              ISFEATUREENABLED(HWSCH)                    chooses submit path (see 3.1)
              CREATEPAGINGQUEUE                          its sync object is the paging fence
create_queue  CREATECONTEXTVIRTUAL(node, affinity, private data = blobs.context, hw_queue_supported = hwsch)
              software scheduling: CREATESYNCHRONIZATIONOBJECT(monitored fence)
              hardware scheduling: CREATEHWQUEUE(private data = blobs.hwqueue) -> progress fence
alloc         CREATEALLOCATION(per-allocation private data = blobs.alloc_device | alloc_host)
              MAPGPUVIRTUALADDRESS(paging queue, size in 4 KiB pages, write)  -> GPU VA
              MAKERESIDENT(paging queue)
              wait for the paging fence
              LOCK2 (host-visible only)                   -> CPU pointer, held until free
submit        software: SUBMITCOMMAND(command_buffer = GPU VA + offset, length, private data = blobs.submit)
                        then SIGNALSYNCHRONIZATIONOBJECTFROMGPU(context, queue fence, value)
              hardware: SUBMITCOMMANDTOHWQUEUE(hwqueue, progress fence id = value, GPU VA, length, private data)
wait          poll the fence's CPU address with a timeout; if the kernel gave none, the blocking
              WAITFORSYNCHRONIZATIONOBJECTFROMCPU (that ioctl has no timeout field)
close         UNLOCK2 / DESTROYALLOCATION2 for leftovers, DESTROYSYNC, DESTROYHWQUEUE, DESTROYCONTEXT,
              DESTROYPAGINGQUEUE, DESTROYDEVICE, CLOSEADAPTER, close(fd)
```

### 3.1 Why two scheduler modes
A GPU running with Windows hardware-accelerated GPU scheduling (HAGS) reports `HWSCH` through
`ISFEATUREENABLED`. WDDM 2.x drivers in that mode expect work on HW queues, not through
`SubmitCommand`. I cannot tell from public sources which mode NVIDIA's WSL driver requires on a given
Windows setting, so the backend asks the kernel and lets `OMEGA_DXG_MODE=submit|hwqueue` override.
This is an assumption to test (section 6).

## 4. What NVIDIA's WSL user-mode driver puts in the private driver data

**Unknown, and this document does not guess it.** dxgkrnl copies the bytes in `priv_drv_data` to the
Windows NVIDIA kernel-mode driver unexamined (`dxgkio_submit_command`, `dxgkio_create_allocation`, ...
only check size limits). NVIDIA does not publish the layout. Public facts:

- The Microsoft kernel side is open: `dxgkrnl` in microsoft/WSL2-Linux-Kernel.
- NVIDIA's user-mode side for WSL2 is closed (`libcuda.so` / `libnvidia-*` under `/usr/lib/wsl/lib`,
  driven through `libdxcore.so`). The AMD path (ROCm's WDDM layer, `wddm_device.cpp`) is open and
  shows the shape of a user-mode driver (what it sends in `CreateContextVirtual`, `SubmitCommand`,
  `SubmitCommandToHwQueue`, `CreateHwQueue`), but its blobs are AMD's.
- So there are four opaque blobs, kept as named fields in `OmegaDxgBlobs`: `context`, `hwqueue`,
  `alloc_device`, `alloc_host`, `submit`. Each is empty by default. Without a blob the backend returns
  `OMEGA_SUB_E_VENDOR_BLOB` and issues no ioctl for that step (tested).
- Other constants NVIDIA chooses and nobody documents are `OMEGA_DXG_TODO_*` in `omega_submit_dxg.h`:
  node ordinal, engine affinity, GPU VA range, client hint, scheduler mode, allocation alignment.
  Each is overridable by an `OMEGA_DXG_*` environment variable.
- Also unknown: the content of the command buffer that NVIDIA's driver expects at `command_buffer`.
  Omega's own pushbuffer is built for a GPFIFO that Omega rings itself. Whether the Windows driver
  accepts raw Blackwell methods in a WDDM command buffer, or wraps/validates them, is the largest open
  question. If it does not accept them, this backend cannot run Omega's GPU encoder and a different
  path is needed.

## 5. Verified and unverified

Verified on the Spark (aarch64, gcc 13.3, `-Wall -Wextra -Werror`, also ASAN+UBSAN):
- Every ioctl the backend uses has the request number, direction, `0x47` magic and argument size
  pinned in `tests/test_omega_submit_dxg.c` and equal to the compiled header. Sizes are layout of
  `u64`-pointer structs, so identical on x86_64 (the 5070 PC) and aarch64. The one `__packed` struct
  (`dxgk_gpuclockdata`, 28 bytes) checks that `omega_dxg_abi.h` really defines `__packed`; without it
  the uapi header silently makes a global variable instead.
- All 24 ioctl numbers match the kernel's dispatch table in `drivers/hv/dxgkrnl/ioctl.c`
  (`evidence/rig/dxg_ioctl_table_check.txt`; the script is `tools/rig/check_dxg_table.py`). The
  `ioctl.c` copy came from the same repository as the header; its commit was not recorded.
- Mock `/dev/dxg` test: full open, queue, alloc, submit, wait, close in both scheduler modes; blobs
  reach the kernel byte-for-byte; command GPU VA = staging VA + offset, length = 4 * words;
  fence must rise; pushbuffer must be host-visible and in range; refused steps issue zero ioctls;
  partial-open cleanup; `NO_DEVICE` when `/dev/dxg` is absent (`evidence/rig/dxg_unit_test.txt`).

Unverified (everything that needs the real driver):
- That `dxgkrnl` accepts these argument values (e.g. `min/max` VA range, flags, `size_in_pages`).
- That the order of calls is the one NVIDIA's driver tolerates (paging queue before context, etc.).
- Contents of all five private blobs and of the command buffer (section 4).
- That `Lock2` on a system-memory allocation returns a pointer that stays valid until `Unlock2`.
- That `SignalSynchronizationObjectFromGpu` after `SubmitCommand` completes only after the work.
- CPU fence address from a monitored fence / HW queue progress fence being non-null.
- Cache coherence and write ordering from the x86 side: `omega_bw_barrier.h` uses `sfence`/`mfence`.
- The sm_120 target row in `omega_bw_target.c` (QMD fields, shared memory config) is from public
  sources only. Not touched by this backend but needed before any kernel launch.
- Not written: an `OmegaGpuBackend` (the vtable in `omega_gpu_engine.h`) that sits on `OmegaSubmitOps`
  so `omega_gpu_execute` can use it. It needs buffer roles, QMD, descriptors and the program bytes for
  sm_120, none of which can be tested before the first experiment answers section 4.
- The copy queue (`create_queue(COPY)` returns `UNSUPPORTED`): needs the copy node ordinal and a blob.

## 6. First experiment on the RTX 5070 under WSL2

Goal: learn the blobs and see whether a command buffer of Omega's choosing runs, using the same
vector add and poison words as the GB10 gate (`src/omega_blackwell_submit.c`: poison word is
`~(a[i]+b[i])`, so a word the GPU leaves untouched can never look correct).

Step A. Capture (no Omega code yet). On the PC, in WSL2 with a CUDA toolkit installed:
```
strace -f -e trace=ioctl -s 4096 -xx -o dxg.trace  ./vecadd_cuda        # any CUDA vector add
```
Keep the `ioctl(N, 0xc....47xx, ...)` lines on `/dev/dxg`. For `LX_DXCREATECONTEXTVIRTUAL`,
`LX_DXCREATEHWQUEUE`, `LX_DXCREATEALLOCATION`, `LX_DXSUBMITCOMMAND` / `LX_DXSUBMITCOMMANDTOHWQUEUE`,
the argument struct holds a user pointer (`priv_drv_data`) and a size; `strace -s` prints only the
struct, so dump the pointed-to bytes with `-e raw=ioctl` plus `gdb`/`ltrace`, or LD_PRELOAD a small
`ioctl` shim that writes `priv_drv_data[0..size)` to `context.bin`, `hwqueue.bin`, `alloc_device.bin`,
`alloc_host.bin`, `submit.bin`. Record which of `SUBMITCOMMAND` or `SUBMITCOMMANDTOHWQUEUE` CUDA uses,
which `node_ordinal` and `engine_affinity` it passes, and the GPU VA range it maps into. Those answer
the TODO constants. Note which blobs vary per call (sizes, fence ids, addresses): a blob that contains
a GPU VA or a fence id cannot be replayed verbatim and must be patched per submit.

Step B. Replay under Omega's backend:
```
OMEGA_SUBMIT=dxg OMEGA_DXG_BLOBS=./blobs OMEGA_DXG_MODE=<what CUDA used> ./rig_dxg_smoke
```
`rig_dxg_smoke` (still to be written on the PC, on top of the existing test): open, create COMPUTE
queue, allocate a HOST_VISIBLE staging buffer, write the pushbuffer there, submit, wait with a 5 s
timeout, print `omega_sub_status_name` and `last_error` for each step. Expected first results, each
informative: (1) `open` OK and the adapter description says `render paravirt`; (2) `create_queue` OK or
a clean kernel errno; (3) `alloc` OK with a plausible GPU VA; (4) `submit` returns 0.

Step C. The pass condition: the vector-add program bytes (sm_120 SASS) uploaded to a DEVICE_LOCAL or
HOST_VISIBLE buffer, a QMD for it, `a` and `b` inputs, the output pre-filled with `~(a+b)`; after the
fence, every output word equals `a+b` and none equals its poison word. Failure modes to tell apart:
`E_DRIVER` (ioctl rejected: the errno names the call), `E_TIMEOUT` (accepted but never signalled: the
Windows driver did not run it or rejected the buffer asynchronously, check `dmesg` and Windows event
log), all poison words (ran but wrote nothing), wrong values (ran, encoder or QMD for sm_120 wrong).

Note: WSL2 has no `/dev/nvidiactl`, so `tools/rig/omega_bw_probe.c` cannot run there. On a native Linux install
with the NVIDIA open kernel module it is the first test for the target table.
