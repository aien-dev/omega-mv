# Resident kernel control block (design note, OWN-30B W-L2f, 2026-10-14)

Status: proof only (one resident GEMV chain, `src/omega_gpu_resident_api.h`). Not wired into decode. Not part of
aienos#327 (N5 excludes persistent kernels); native use needs its own issue after N5.

## Three memory objects, three rules

| Object | Who writes | Who reads | Memory | Why |
|---|---|---|---|---|
| Stage table (program) | caller, once, before first launch | GPU only | GPU-cached | never host-polled or rewritten (omega_gpu_mem_cached.h invariant) |
| Barrier word | GPU only (GPU-scope atomics) | GPU only | GPU-cached, fresh allocation holding 0 | the only hot word; uncached system-scope atomics cost ~1.7 us each, serialized (chip r1: ~300 us per barrier at 192 CTAs) |
| Control block | caller before launch; GPU writes progress/error | caller (poll during or read after) | GPU-uncached | host-visible both ways (nvos.h:1117) |

The control block is never read by every CTA at every barrier: chip r3 measured 2 uncached loads x 192 CTAs = ~95 us per
barrier. Only CTA 0 reads it (cancel), batched with its own arrive, off everyone else's critical path.

## Control block v1 (64 bytes, u32 words)

| Offset | Word | Writer | Meaning |
|---|---|---|---|
| 0x00 | magic `RSD1` | caller | refuse a foreign block |
| 0x04 | version 1 | caller | layout version |
| 0x08 | n_stages | caller | copy for the poller (kernel takes its own copy from launch args) |
| 0x0c | grid | caller | CTAs; must be <= measured co-resident capacity |
| 0x10 | progress | GPU (CTA 0, strong system store) | barriers passed; poll it while the kernel runs |
| 0x14 | error | GPU (system-scope atomic) | nonzero = a barrier wait expired (non-co-resident grid or a fault); results invalid |
| 0x18 | cancel | caller | nonzero = stop; CTA 0 reads it at every barrier, the chain stops within one stage |
| 0x1c | spin_max | caller | polls per barrier wait before the error path (a spin count, no clock) |

For the token loop (next step) the same block grows, version 2: input token, position, max tokens, EOS list base/count,
done flag, ring base, ring write index. Same rules: caller-written fields are read once per token boundary by CTA 0 only.

## Protocol a host or a native AIENOS caller follows

1. Create once: stage table (cached), barrier word (cached, 0), control block (uncached). Measure or look up capacity.
2. Per launch: write the control block (magic, version, n_stages, grid, progress = error = cancel = 0, spin_max),
   pass `base` (the barrier word's value at launch start, the caller's running sum of (n_stages - 1) x grid) and the
   three addresses as launch arguments, submit one launch.
3. While it runs: poll progress (optional). To stop: write cancel = 1.
4. After completion: read error and cancel. Either nonzero: the barrier word's count is unknown, replace it with a fresh
   zeroed allocation and reset base to 0. Also refresh before base would reach 2^23 (OMEGA_RES_ABORT_UNIT).

Only steps 2-4's submit and poll calls differ between Linux (nvrm channel) and AIENOS native (platform contract).
The kernel uses no clock, no host call and no driver state; every wait is a bounded spin count.

## Safety properties proven on the chip (evidence/OWN30B-L2F-RESIDENT-20261014/r4)

- Co-residency: launcher refuses grid > capacity (48 SMs x 4 CTAs of 8 warps at 64 registers = 192, matches the on-chip
  probe exactly: 192 completes, 193 times out).
- Forced oversize (768 CTAs): finishes in ~0.6 ms with error set, no hang, no session latch; the next launch is exact.
- Cancel: stage 0 exact, stage 2 never written, progress 0; the next launch is exact.
- Abort fan-out: an aborting CTA adds 2^23 to the barrier word, so every waiter leaves at once.

## Open for native use

- Native hang recovery (channel reset/quiesce) must be proved in N4 before a multi-token loop runs natively.
- Uncached host-visible memory and system-scope stores must be proved on the native platform (not assumed).
- Ledger granularity (per chunk, not per token) needs an aienos#327 follow-up issue (UNVERIFIED that it is accepted).
