# Native mapped memory and checked device calls

This extends the existing native-host compiler. It supports the strict Omega
27B experiment's memory prerequisite. It does not yet implement a GPU launcher,
model execution, complete state rollback or a hardware performance comparison.
The seed compiler and added tests are handwritten Omega. The existing shell
bootstrap and test orchestration remain disclosed development tools.

| Expression | Result and contract |
| --- | --- |
| `devctl fd request A off n` | Signed Linux ioctl result. `fd` is int; `request` is a literal uint; `A` is writable bytes; offsets/counts are ulong. Exact encoded size, checked range. Accepts FIONREAD or encoded read/write NVRM F requests, matching the reviewed L605 device boundary. |
| `mapnew fd offset bytes access mode` | Positive long handle or negative Linux errno. fd is int, offset/bytes ulong, access/mode literal ints. Access 1 means read, 3 read/write. Modes 1/2 mean shared/private file; 33/34 shared/private anonymous. Anonymous mappings require fd -1 and offset zero. |
| `mapdrop handle` | Unmap the entire owned mapping. Zero on success or negative errno. Retires the slot on success; the next mapping gets a different generation. |
| `mapread handle offset A off n` | Copy n bytes from the mapping into writable byte array A. Return n as long. |
| `mapwrite handle offset A off n` | Copy n bytes from byte array A into a writable mapping. Return n as long. Read-only source views/text are accepted. |
| `devcall fd request H hoff 32 P poff n` | RM_ALLOC or RM_CONTROL only. Binds a checked writable parameter span in a private header, sets its length, and copies back non-pointer fields. Caller pointer bytes must be zero; header and parameter spans must not overlap. Parameter length is at most 16383. |
| `uvmctl fd command A off n` | Literal Linux UVM request from the explicit ABI580.173.02 allowlist, with its exact writable payload length. |
| `vfioctl fd request A off n` | Literal VFIO request from the allowlist below, with its exact byte count. Signed Linux ioctl result. `fd` is int; `request` a literal uint; `A` uchar bytes. |
| `vfiomap fd A off 32` | Read-only shared mapping of one VFIO region. `A off 32` is the `vfio_region_info` returned by `DEVICE_GET_REGION_INFO`; its flags must contain READ and MMAP, and the compiler maps exactly its size at its offset. Returns a mapping handle or negative errno. `mapset`/`mapwrite` through the handle trap. |
| `vfiomapw fd A off 32` | As `vfiomap`, but the region flags must contain READ, WRITE and MMAP and the mapping is shared read/write, so `mapget`/`mapset` drive device registers (one aligned access each, DSB SY on both sides). |
| `mapaddr handle` | The owned live mapping's base address as ulong, for driver address serialization. Does not permit access through an arbitrary integer address. Works on reservations. |
| `mapget handle offset width` | Aligned uint32 or uint64 load, returned as ulong; width is literal int4 or8. Exactly one load, bracketed by DSB SY. Requires readable memory. |
| `mapset handle offset width value` | Aligned uint32 or uint64 store from ulong, returning long0; width is literal int4 or8. A uint32 store refuses truncation. Exactly one store, bracketed by DSB SY. Requires writable memory. |
| `maphold address bytes` | Reserve an unused, page-aligned span with PROT_NONE and MAP_FIXED_NOREPLACE. Address is at least64KiB and the whole span is below1TiB. Return an owned handle or errno. A kernel that ignores NOREPLACE is detected by checking the returned address; an unexpected result is unmapped and returns -95. |
| `mapbind handle fd offset access` | Bind a shared file/device mapping to the entire owned reservation, with literal access1 or3. No existing data mapping may be replaced. Return long0 or errno. A failed bind unmaps the span and retires the handle. |
| `cpufence` | Statement, keyword 199: a standalone host CPU publication barrier, AArch64 `DSB SY`, x86-64 `MFENCE`. It orders ordinary stores, for example `mapwrite` or plain array stores, before a later `mapset` doorbell or flag. A `mapget`/`mapset` access already carries its own barriers. See OMEGESE §10, "Host CPU ordering words". Proof: `seed/host/cpufence.sh`. |
| `cpurelax` | Statement, keyword 200: spin-wait hint for a bounded `mapget` poll loop, AArch64 `YIELD`, x86-64 `PAUSE`. It gives no ordering. Proof: `seed/host/cpufence.sh`. |

All words except `cpufence` and `cpurelax` require program mode; those two are also accepted in function mode. A mapping handle is a long. `mapaddr` explicitly
exports its address as ulong; access still requires the live owned handle.
Each program has a private fixed 128-slot table. Each slot records address,
length, generation and access. Exhaustion returns -24 without calling mmap.
Mappings are between 1 byte and 64 GiB each. This bounds each extent, not the
total physical memory the OS permits. Closing a file descriptor does not close
its mapping. Close the mapping explicitly before reclaiming its backing object.

Invalid/stale handles, range overflow, writes through read-only mappings, zero
or oversized extents, inaccessible reservations, misaligned word access and
malformed anonymous arguments trap with exit 103.
An array's normal bounds and mutability checks still apply. Empty copies are
allowed at the exact end of both spans, through a valid handle. Slot generation
never wraps: exhausted generations are retired permanently. File offset alignment
and device mapping permission errors come from Linux. Fixed-address binding is
limited to entire owned reservations. There is no executable map, arbitrary
syscall or access through an arbitrary integer address in this extension.

These are process-local resource handles, not an authority or sandbox boundary.
Fork inherits independent tables and Linux's ordinary shared/private map behavior;
the new process must manage its copies. A successful file mapping does not verify
the file's contents, prevent truncation, guarantee residency, or suppress Linux
mapping faults. The owner must keep mapped files/device objects valid for the
mapping's full lifetime. The model runner must freeze and verify weight identities.

Device calls use checked byte spans. `devcall` supplies one scoped parameter
pointer in a private32-byte NVOS21/NVOS54 header. It never copies that pointer
back. This work does not qualify arbitrary device commands.
GPU submission and mapped-device coherency require their separate existing
runtime qualifications. Tests here open ordinary files, anonymous memory and
pipes; compiler host tests do not open a GPU device or alter a running model.

The UVM allowlist is INITIALIZE805306369/16bytes, MM_INITIALIZE75/8,
REGISTER_GPU37/40, UNREGISTER_GPU38/20, REGISTER_GPU_VASPACE25/32,
UNREGISTER_GPU_VASPACE26/20, REGISTER_CHANNEL27/56, UNREGISTER_CHANNEL28/28,
MAP_EXTERNAL_ALLOCATION33/9264, FREE34/24 and CREATE_EXTERNAL_RANGE73/24.
Only the Linux ABI from the pinned NVIDIA580.173.02 headers is covered.
Independent GCC header-layout and GNU assembler instruction oracles were used
while developing the runtime. They are not linked into the compiler or products.

The VFIO allowlist (`_IO(';', 100+k)`, Linux `include/uapi/linux/vfio.h`) is
GET_API_VERSION15204/0, CHECK_EXTENSION15205/4, SET_IOMMU15206/4 (value must be
TYPE1v2 = 3), GROUP_GET_STATUS15207/8, GROUP_SET_CONTAINER15208/4,
GROUP_GET_DEVICE_FD15210 (nonempty device name without zero bytes, copied with a
terminator to private stack memory), DEVICE_GET_INFO15211/24,
DEVICE_GET_REGION_INFO15212/32, IOMMU_MAP_DMA15217/32 and IOMMU_UNMAP_DMA15218/24
(both checked as described under DMA below). DEVICE_RESET15215 is refused: W0 is approved
without a device reset, so Omega code cannot request one. For spans of 8 bytes or
more the compiler writes `argsz = n` before the call, so the kernel copies at most
n bytes back. CHECK_EXTENSION and SET_IOMMU pass the 4-byte value read from the
span. Every other request, a non-literal request, a non-uchar span or library
mode is refused at compile time; a wrong count, a bad SET_IOMMU value or an empty
device name traps 3 at run time. IRQ, reset and migration
requests are not covered. Note that closing a vfio-pci device fd lets Linux reset the
function (FLR) or, when every reset method is disabled, the bus (SBR); the allowlist
cannot prevent that kernel behaviour.

DMA: IOMMU_MAP_DMA15217/32 (`struct vfio_iommu_type1_dma_map`, vfio.h) never
takes an address from Omega code. The caller writes an Omega mapping handle into
the vaddr field (bytes 8..15) and the IOVA into bytes 16..23. The compiler checks
the handle exactly as `mapget` does (live slot, matching generation), requires a
read/write mapping and flags exactly READ|WRITE (3), then stores the mapping's own
base address into vaddr and its whole length into size before the call; any
failed check traps 3. So only a whole live mapping that the program owns can be
handed to the IOMMU, and the IOVA is the only free choice. IOMMU_UNMAP_DMA15218/24
requires flags 0 (no dirty bitmap, no ALL, no VADDR). Request numbers are shared by
`_IO(';', 113/114)` with VFIO_DEVICE_PCI_HOT_RESET and VFIO_DEVICE_QUERY_GFX_PLANE on a
device fd: hot reset refuses any non-zero flags with EINVAL (Linux
drivers/vfio/pci/vfio_pci_core.c, `vfio_pci_ioctl_pci_hot_reset`, `hdr.flags` check),
and MAP_DMA always passes flags 3; vfio-pci does not implement QUERY_GFX_PLANE.
Unmapping or dropping an Omega mapping while it is DMA-mapped leaves the pages pinned
by the kernel until the container is closed; the driver must unmap before dropping.
`vfiomap` refuses a size of zero and region flags
without READ|MMAP; it never requests PROT_WRITE. `vfiomapw` additionally requires
WRITE and is the only VFIO word that maps device memory writable. Host tests use an
invalid fd and a memfd stand-in; the device path is exercised only in the QEMU bench
of aien-dev/omega-gpu-driver, not on hardware. All three words are AArch64 only: a
`target x86linux` program that uses any of them is refused with error 25, because the
x86 backend has no VFIO lowering.

Kernel constants/layouts are from Linux asm-generic/unistd.h, mman-common.h and
linux/mman.h. AArch64 encodings use seed/rung2/table.s2. The device request rule
comes from the reviewed L605 src/device/omc.om, source 0dee699. Existing host
source is f0499f415571bdd7496b6de391096915e78f7556. These device words are on
main: `hdevctl` is defined in seed/omc/omc.om (verified at main e5b32567).
