# M20 metadata-v1 native boundary

This component implements the integer metadata and storage-lifetime checks
needed by Omega #663. It preserves the bounded M20 tensor contract at
`cd5897e3f92f72a015892c67b17f096e433b54f7`, specifically
`src/tensor/omega_tensor.h` and `src/tensor/omega_tensor.c`.
`src/autodiff/omega_autodiff.c` is a dependent legacy consumer, not a native
gradient implementation supplied by this change.

Owner: ecosystem303 tensor lane, ledger L787, parent L786. Exclusive paths:
`native/ecosystem663`, `tests/ecosystem663`, `docs/ecosystem663`, and
`evidence/ECOSYSTEM663`. L408 retains compiler ownership, L418 retains the
existing migration, and L491 retains training/schedule work. No existing
compiler, tensor, autograd or trainer source is changed.

## Scope

`native/ecosystem663/metadata.om` validates a tensor descriptor against the
allocator's current storage record. It detects shape/product overflow,
out-of-bounds views, incorrect dtypes and stale storage generations. It also
implements the storage record's release transition. These checks execute as
seeded native AArch64 code using integer metadata.

This component does not allocate memory, store tensor values, execute tensor
arithmetic, compute derivatives, authenticate an allocator record or make
records safe for concurrent use. `ES` must be populated by the trusted existing
allocator owner, under that owner's synchronization. Caller-controlled claims
about storage do not establish that storage exists. Changing `ES` is an ordinary
in-process operation, not a protected capability boundary.

An eventual native tensor allocator must call this single boundary; it must not
copy a second implementation of its validation rules. Consumer integration
must pin this source and test its actual allocation and release operations.
The distributed #7 consumer may use this module to validate each local operand
record, but metadata agreement alone does not execute a collective.

## Frozen subset and input records

Schema version is 1. Dtype 1 is F32; 2 is F16; 3 is BF16. These are storage
metadata tags here. No integer or complex tensor dtype is added. Rank is 0..8;
rank 0 has one element. Every active dimension is at least 1, with product at
most 1073741824. Empty tensors remain explicitly refused under inherited M20
semantics; supporting them requires a separately versioned extension.

Strides and offsets are unsigned element counts. Zero strides admit read-only
broadcast views. Negative strides are not representable. Inactive dimensions
and strides are ignored. Physical overlap is allowed for immutable views; this
does not authorize overlapping writes. A huge stride on a size-one dimension
does not access any extra element and is accepted.

`ED` is a 24-element `ulong` array:

| Index | Value |
|---|---|
| 0 | schema, exactly 1 |
| 1 | dtype, 1..3 |
| 2 | rank, 0..8 |
| 3 | declared element count, equal to shape product |
| 4 | storage offset in elements |
| 5 | storage slot, 0..4294967295 |
| 6 | storage generation, nonzero u64 |
| 7 | reserved, exactly zero |
| 8..15 | dimensions |
| 16..23 | element strides |

`ES` is an 8-element `ulong` array containing the trusted storage snapshot:

| Index | Value |
|---|---|
| 0 | live, exactly 1 for admission |
| 1 | retired, exactly 0 for admission |
| 2 | storage slot |
| 3 | current generation |
| 4 | dtype |
| 5 | allocation's element capacity, 1..1073741824 |
| 6..7 | reserved, zero |

The slot may be zero; the generation may not. Descriptor slot, generation and
dtype must equal the storage snapshot. Storage capacity does not require the
view to cover all storage. Each logical element must lie within the capacity.
The implementation checks remaining extent before multiplying, so untrusted
large sizes/strides refuse instead of overflowing.

## Calls and errors

- `emcheck() -> int`: read-only validation of `ED` against `ES`.
- `emlive(slot ulong, gen ulong) -> int`: storage-handle validity only.
- `emrelease(slot ulong, gen ulong) -> int`: when live, mark the snapshot dead
  and increment generation; at u64 maximum mark it retired without wrapping.
  Stale/double release returns -5 and changes nothing. This is the metadata
  transition only; the owner must also release actual backing storage.

`emshape` and `embounds` are internal helpers, not independently admitted entry
points. `embounds` requires shape validation first.

| Status | Meaning |
|---|---|
| 0 | metadata admitted |
| -1 | unsupported schema or reserved field |
| -2 | rank above 8 |
| -3 | zero dimension or inconsistent element count |
| -4 | unsupported or mismatched dtype |
| -5 | stale/retired/dead/invalid storage handle |
| -6 | view reaches outside storage |
| -8 | element-count or storage-capacity limit |

These negative values retain M20 status meanings. The new descriptor envelope
has its own schema version; it is not the C struct ABI or a shared transport
wire format. `emcheck` refusal precedence is envelope, dtype, shape/count,
handle, storage dtype, then extent. No mutation/version counter is invented:
the inherited M20 tensors are immutable. Saved tensor storage references
become stale after release; full autodiff node/tape lifetime is still #664.

## Acceptance and remaining gates

The native test includes analytic boundary cases, scalar and rank-eight
descriptors, both storage-only dtypes, maximum counts and generations, stale
references, release/reuse/retirement, and 14400 tiny views compared against an
independent element-enumeration algorithm. Compiled controls must detect
missing offset accounting, generation comparison and retirement.

Build by concatenating this module and `tests/ecosystem663/metadata-test.om`,
then feed the bytes to pinned compiler SHA256
`191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9` and execute
the resulting ELF on Linux AArch64. Existing development `cat`, redirection,
`chmod` and `quiet-build` remain disclosed host orchestration. No new shell
harness is delivered. Native import/process orchestration is separate owner
work; this source uses no foreign runtime or dynamic library.

FP arithmetic remains blocked on the compiler/numeric owners and their exact
qualified interfaces. Native forward/backward/update, actual allocator and
trainer integration, CUDA-free model execution, physical cross-vendor results,
checkpoint/resume and retirement are NOT_RUN. No legacy source is deleted
because no existing production consumer has switched. All parent issues stay
open. Independent final-source review and existing desk review are required.
