# Host memory fragmentation on the Spark (omega#528)

Question: what keeps the Spark's 128 GB from offering free 2 MiB blocks (one 2 MiB block = one
Linux pageblock), so that every order-9 allocation, such as the GPU channel's main context buffer
(omega#523, physics-mv#64), has to fight for one?

Short answer: the GPU memory. Every GPU buffer the Omega engine allocates (through physics
`nvrm_alloc`) arrives from the NVIDIA driver as 64 KiB pieces of unmovable kernel memory. The
resident 27B model server holds 34.6 GB of them spread across 29,938 pageblocks, and the 27B
benchmark loop allocates and frees up to 33 GB more every two minutes. Each cycle leaves free
memory chopped into 64 KiB to 1 MiB holes that cannot be merged into 2 MiB blocks while server
GPU pages and kernel slab objects sit between them. The proposed fix is in physics, outside
omega: ask the driver for 2 MiB pages for buffers of 2 MiB and up.

Claim tags: OBSERVED (measured here, receipt named), DOCUMENTED (vendor source quoted),
INFERRED (reasoned, confidence given), UNKNOWN, NOT RUN.

## Tools (this change)

- `tools/memfrag_sample.sh <out.tsv> [interval] [count]`: one line per sample with free 2 MiB
  blocks, free memory in 4 KiB-32 KiB pieces and in 64 KiB-1 MiB pieces, pageblock counts per
  migrate type, slab, page tables, page cache, anonymous memory, compaction and migration
  counters, pinned-page counters, the GPU memory each process has mapped, and the local judge's
  phase. Read-only. `/proc/pagetypeinfo` is root-only and is read with `sudo -n cat` when that is
  allowed (NA otherwise).
- `tools/memfrag_scan.c` (`make build/memfrag_scan`): walks `/proc/kpageflags` one pageblock at a
  time, sorts every page into free, gpu, slab, page table, reserved, other, movable, and reports
  which classes pin each block. With `--pid P` it reads the process's driver mappings
  (`/dev/nvidiactl`) through `/proc/P/pagemap` and tags those physical pages as gpu. Root is
  needed for both files; nothing is written.
- `make test-memfrag`: host test with fixtures, two mutants that must fail, and a fixture run of
  the sampler. It contains the claim in miniature: the same 512 GPU pages pin 16 pageblocks as
  scattered 64 KiB pieces and one pageblock as one 2 MiB piece.

## Documentation read first

- DOCUMENTED, driver 580.173.02 (`/usr/src/nvidia-580.173.02/nvidia/nv-vm.c`,
  `nv_compute_gfp_mask`): system pages are taken with `NV_GFP_KERNEL` (`GFP_KERNEL |
  __GFP_NOWARN`, `common/inc/nv-linux.h:288`) plus `__GFP_RETRY_MAYFAIL`, and `__GFP_COMP`
  when the order is above 0. No `__GFP_MOVABLE`: every GPU page on GB10 is unmovable memory to
  Linux.
- DOCUMENTED (`nvidia/nv.c`, `nv_alloc_pages`): for non-contiguous allocations
  `at->order = get_order(page_size);`, and RM passes the memory descriptor's page size
  (`osGetPagesInfo`: `*pageSize = memdescGetAdjustedPageSize(pMemDesc)`, open-gpu-kernel-modules
  `src/nvidia/arch/nvalloc/unix/src/os.c`, via Context7). So the requested page size decides the
  size of each piece Linux hands out.
- DOCUMENTED (`src/common/sdk/nvidia/inc/nvos.h:1053-1061`, `:1258-1266`): "HUGE_PAGE = 2 MB on
  PASCAL+ ... To request for a HUGE page size, set NVOS32_ATTR_PAGE_SIZE to _HUGE and
  NVOS32_ATTR2_PAGE_SIZE_HUGE to the desired size." The ATTR2 comment says "For virtual allocs";
  the ATTR comment says the field is also used for physical allocations. DOCS SILENT on whether
  a GB10 system-memory allocation honours `_HUGE`.
- OUR CODE: physics `nvrm/nvrm.c` `alloc_with_cacheability` (pin 6d7cf0d4, line 499; main
  14273c5c, line 579) sets `attr = PHYSICALITY_ALLOW_NONCONTIGUOUS | LOCATION_PCI`, page size
  left at DEFAULT, alignment 0x1000.

## Measurements (2026-10-19, uptime 1 day 5 h)

Receipts are in `~/workspace/investigations/2026-10-19-omega-528/` on the Spark (`scan-*.txt`,
`sample.tsv`, `sample2.tsv`).

1. OBSERVED, the piece size. Without any tagging, the server's pages show up as compound pages
   mapped to user space and not on any LRU list: 554,136 compound heads and 8,312,040 tails,
   exactly 16 pages (64 KiB, order 4) per piece (calc:5381faf8). With the server's pagemap
   tagged: 8,866,444 GPU pages, 8,865,519 of them compound (scan-3). The driver hands RM's
   default system page size of 64 KiB to Linux one piece at a time.
2. OBSERVED, the spread. The server's 8,866,444 GPU pages (34.6 GB) touch 29,938 pageblocks,
   296 pages per block on average (calc:88603e0c), where 17,318 whole blocks would hold them
   (calc:039d5dcd). Only 67 blocks are entirely GPU. Of the physically contiguous runs, 103,375 are 16 to 511
   pages long (calc:2ae2ffb4) and 122 are 512 or more (scan-1).
3. OBSERVED, the benchmark. While the 27B benchmark (`qwen38_omega_full`, restarted about every
   two minutes) was loaded, GPU pages totalled 17,527,356 (67 GB) over 46,815 pageblocks; 16.5 GB
   was free and only 254 pageblocks were free whole (scan-4-bench, 14:52:08Z).
4. OBSERVED, the holes. At 14:49:23Z, seconds after a benchmark run exited, 28,954 MiB was free
   and there were 0 free 2 MiB blocks; 6,963 MiB of the free memory was in 4-32 KiB pieces and
   the remaining 21,991 MiB in 64 KiB-1 MiB pieces, the size of the freed GPU pieces (sample.tsv;
   the 64 KiB-1 MiB band is logged directly in sample2.tsv). Over the first 41 samples, free
   2 MiB blocks averaged 142 with a benchmark loaded and 2,176 without.
5. OBSERVED, slab. Slab held 1,340,324 pages (5.1 GB) spread over 36,866 pageblocks, 36 pages per
   block (calc:ddb1969); it shares most blocks with GPU pages: of 55,551 pinned blocks, 29,938
   hold GPU pages and only 1,002 are pinned by GPU pages alone (scan-3).
6. OBSERVED, other fixed holders: 1,558,544 pages flagged reserved at boot (memblock, flag bit
   32) in 3,059 blocks, and about 1.0-1.3 M flag-less pages that no free block covers (kernel
   allocations without page flags: vmalloc, stacks, driver internals; UNKNOWN which).
7. OBSERVED, cross-check: the scanner's free count (7,332,521 pages) matches MemFree (29.3 GB)
   read in the same second.
8. OBSERVED, not the fragmenter: `nr_foll_pin_acquired == nr_foll_pin_released` in every sample
   (no long-term pinned user pages), as #528 found on 2026-10-17.
9. OBSERVED, machine settings differ from #528's text: `vm.min_free_kbytes` is now 1048576 (the
   issue recorded 45056) and `vm.compaction_proactiveness` 60 (issue: 20). Who changed them and
   when is UNKNOWN; this investigation changed nothing.

## Root cause

INFERRED, confidence medium-high. Linux keeps unmovable allocations together by retyping whole
pageblocks as unmovable when it runs out (an order-4 request is at least half a pageblock's
order, so it may claim the whole block). The GPU pieces are 64 KiB, so a pageblock taken for GPU
memory still has room for slab objects and other small kernel allocations, and the server's
long-lived pieces end up interleaved with them (finding 5). When a benchmark run frees its 33 GB,
its pieces come back as 64 KiB holes beside pieces that stay (server GPU pages, slab), and those
holes cannot merge into 2 MiB blocks (finding 4). Compaction cannot help: none of these pages is
movable. The count of free 2 MiB blocks then depends on which pieces happen to free together,
which is the second-by-second flicker #528 reported.

What this does not explain: the flag-less kernel pages and the reserved pages (finding 6) pin
about 14,600 blocks by themselves; they are fixed or small and not driven by the benchmark cycle.

## Proposed fix (physics, outside omega)

In physics `nvrm/nvrm.c` `alloc_with_cacheability`, for buffers of 2 MiB and up:

```c
#define NVRM_HUGE_BYTES (2ull << 20)
int huge = size >= NVRM_HUGE_BYTES;
if (huge) size = (size + NVRM_HUGE_BYTES - 1) & ~(NVRM_HUGE_BYTES - 1);
uint64_t va = va_take(rm, size, huge ? NVRM_HUGE_BYTES : 0x1000);
...
mp.attr = (NVOS32_ATTR_PHYSICALITY_ALLOW_NONCONTIGUOUS << 27) | (NVOS32_ATTR_LOCATION_PCI << 25) |
          (huge ? NVOS32_ATTR_PAGE_SIZE_HUGE << 23 : 0);
mp.attr2 = (gpu_cacheable << 2) | NVOS32_ATTR2_ZBC_PREFER_NO_ZBC |
           (huge ? NVOS32_ATTR2_PAGE_SIZE_HUGE_2MB << 20 : 0);
mp.alignment = huge ? NVRM_HUGE_BYTES : 0x1000;
```

Then each piece is an order-9 compound page: a whole pageblock. GPU memory stops sharing blocks
with slab, and a freed GPU buffer returns whole 2 MiB blocks. Smaller buffers keep today's path.

Cost, OBSERVED on the live server's 702 driver mappings: 292 buffers of 2 MiB or more hold
34,534 MiB; rounding them up to 2 MiB adds 111.4 MiB. The 410 smaller buffers (100 MiB) are
unchanged.

Open points, all NOT RUN (they need the GPU, and this lane was host-only):
- whether RM accepts `_HUGE` for GB10 system memory (DOCS SILENT); `scan --pid` on a test
  process shows it directly (`gpu_compound` with 512 pages per head, `gpu_runs_order9`);
- the refusal path: today any `rm_alloc` failure sets `rm->faulted`. A refused `_HUGE` request
  should fall back once to the current attributes, and that fallback needs its own chip test;
- whether the GPU mapping calls (`NVOS46_FLAGS_PAGE_SIZE_4KB` in the DMA map, the UVM external
  mapping) accept 2 MiB physical pages mapped with 4 KiB GPU page table entries;
- load-time cost: an order-9 `GFP_KERNEL | __GFP_RETRY_MAYFAIL` request may run direct
  compaction when no free 2 MiB block exists. Measure model load time before and after;
- the effect itself: rerun `tools/memfrag_sample.sh` across several benchmark cycles before and
  after and compare free 2 MiB blocks and the 64 KiB-1 MiB band.

No machine-wide change (`vm.min_free_kbytes`, `vm.compaction_proactiveness`, hugepage settings)
is proposed; the orchestrator's 2026-10-17 decision stands.
