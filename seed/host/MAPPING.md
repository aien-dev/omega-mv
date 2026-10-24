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
| `mapaddr handle` | The owned live mapping's base address as ulong, for driver address serialization. Does not permit access through an arbitrary integer address. Works on reservations. |
| `mapget handle offset width` | Aligned uint32 or uint64 load, returned as ulong; width is literal int4 or8. Exactly one load, bracketed by DSB SY. Requires readable memory. |
| `mapset handle offset width value` | Aligned uint32 or uint64 store from ulong, returning long0; width is literal int4 or8. A uint32 store refuses truncation. Exactly one store, bracketed by DSB SY. Requires writable memory. |
| `maphold address bytes` | Reserve an unused, page-aligned span with PROT_NONE and MAP_FIXED_NOREPLACE. Address is at least64KiB and the whole span is below1TiB. Return an owned handle or errno. A kernel that ignores NOREPLACE is detected by checking the returned address; an unexpected result is unmapped and returns -95. |
| `mapbind handle fd offset access` | Bind a shared file/device mapping to the entire owned reservation, with literal access1 or3. No existing data mapping may be replaced. Return long0 or errno. A failed bind unmaps the span and retires the handle. |

All words require program mode. A mapping handle is a long. `mapaddr` explicitly
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

Kernel constants/layouts are from Linux asm-generic/unistd.h, mman-common.h and
linux/mman.h. AArch64 encodings use seed/rung2/table.s2. The device request rule
comes from the reviewed L605 src/device/omc.om, source 0dee699. Existing host
source is f0499f415571bdd7496b6de391096915e78f7556. This remains a proposed
extension to that branch; main does not yet contain this compiler profile.
