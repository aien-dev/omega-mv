# L717 bounded native buffers v1.1

## Scope and capacity

`ubnew(type,size,rights,place)` creates an actual contiguous `uchar` buffer in a fixed CPU-host pool. Supported values are `type=1` (`uchar`), `place=1` (CPU host), `size=1..256`, and rights `READ=1`, `WRITE=2`, or `READ|WRITE=3`. The pool has exactly 16 slots, each backed by 256 bytes, with at most 4096 bytes owned at once. GPU residency, other element types, strided views, external addresses, async queues, and implicit fallback are unsupported. `ubnew` returns zero on refusal.

## Handle and lifetime

A handle is `generation * 256 + (slot + 1)`. Its low byte is 1..16 for slots 0..15; its high portion is a nonzero 56-bit generation. Zero is never issued. Validation requires a live slot and exact generation equality. Allocation scans slots in ascending order. `ubfree` invalidates the descriptor, zeroes the entire 256-byte backing slot, releases its owned length, and advances the generation. If the issued generation is `2^56-1`, release permanently retires the slot rather than wrapping.

## Rights and transfer

`ubstore` requires WRITE, a valid byte offset, and a value in 0..255. `ubread(handle,offset)` requires READ and a valid offset. It returns an explicit status and publishes the byte in module-owned `ubvalue[0]` only on success. Every refusal leaves `ubvalue` unchanged; callers must test status before consuming or copying the value. Calls are serialized within this module. `ubload` remains a compatibility-only convenience returning zero on refusal and must not be used as an interoperability error boundary. `ubcopy(src,dst,count)` requires a live readable source, writable destination, distinct slots, and `count` within both lengths. It validates all preconditions before changing a destination byte or transfer counter. A successful copy copies actual bytes in ascending order, in fixed 16-byte checkpoints. Same-slot aliasing refuses even when count is zero. A zero-length transfer between distinct handles succeeds and is counted as one accepted copy.

A canceled descriptor refuses later reads, writes, and transfers, but can always be freed. `ubcancel` requires WRITE and marks that descriptor canceled. `ubcsched(src,dst,count,after)` is the deterministic cancellation-delivery seam: `after=0` is an ordinary transfer; otherwise it delivers a cancellation request at that numbered 16-byte checkpoint (or the final short-chunk checkpoint). A scheduled cancellation requires WRITE on the source, just like `ubcancel`; without it, RIGHTS is returned before any transfer, counter update or cancellation. A checkpoint beyond the transfer length is never delivered. Already copied bytes form the explicitly permitted destination prefix; the suffix remains byte-for-byte unchanged. It marks the source canceled and returns `CANCELLED`. Ordinary validation failures have no partial effects. This makes mid-transfer cancellation executable without threads or an unbounded scheduler.

## Status and counters

Statuses are `OK=0`, `BAD_HANDLE=1` (including stale), `RIGHTS=2`, `RANGE=3`, `UNSUPPORTED=4`, `CAPACITY=5`, `ALIAS=6`, `CANCELLED=7`, and `RETIRED=8`. `ubnew` uses zero rather than a status. `ubstat(1)` returns bytes actually copied, `ubstat(2)` accepted copy operations (including a copy canceled at a checkpoint), `ubstat(3)` current owned bytes, and `ubstat(4)` peak owned bytes; unknown selectors return zero. Counters reflect actual pool changes and copied bytes, not estimated transfer work.

No raw addresses escape the module. No heap allocation, threads, GPU, model, service, compiler, platform/queue ABI, or L689 streaming source is used.

## Boundary and correction record

Revision1.1 adds explicit read status and enforces source cancellation rights. Native globals and helpers are private by contract, not a sandbox against hostile code concatenated into the same compilation unit. Serialized cooperative cancellation is not signal/thread/device cancellation or an async scheduler. `ubstat` counts these module copy operations only, not all physical memory traffic.

The parent reproduced cancellation of a READ-only source and a test status wrap at assertion256 on the original candidate. Both exact behavioral REDs and subsequent GREENs are retained under `evidence/UNIVERSAL-ADAPTER/L717/parent-correction/`. Native tests now return1 on every failure instead of truncating an assertion index into a process exit. All previous cases remain.
