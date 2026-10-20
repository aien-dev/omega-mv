# BYTES-0: hand-written GPU words vs the memory speed limit (ledger L098)

**Question (Drake):** can a program reach the GB10 as raw instruction words we wrote ourselves, and does
the C-built path cost speed?

**Answer from this session:**
1. Yes. 32 hand-written sm_121 instructions, handed to the in-repo launcher as raw bytes (no CUDA
   library, no NVIDIA compiler, no cubin or ELF), added two 1,048,576-element arrays on the chip and
   matched the CPU reference on every element, on every repetition.
2. At this size the C-built words (the existing encoder) are not slower than the hand words: both ran in
   about 21% of the memory limit, with launch overhead included. The time goes to the fixed cost of one
   launch, not to the instructions.

## Window

| | |
| --- | --- |
| window id | `BYTES0-20261020T032412Z-2611707` |
| locks | `quiet-measure flock /tmp/aien-gb10.lock tools/omega_bytes/run_chip.sh` |
| box load at start / end (`uptime`) | 68.26, 57.49, 55.61 / 68.26, 57.49, 55.61 |
| MemAvailable | 59,654,180 kB |
| other chip users resident | two 27B model servers (aien-serve-http) |
| omega commit | 23e738e8 (+ this evidence directory) |
| tool sha256 | 7ea04f9e060113aa73abf340fd55a5baa0133a742dbe6fa278ac305695ffed0c |
| raw results | `chip-20261020T030300Z/` (one JSON + log per step, `steps.txt`, `window.txt`) |

**This was not a quiet box.** Load average was 68, so treat the timings as a first reading. The
correctness results do not depend on load.

## Ceiling used

273 GB/s, the GB10 unified memory bandwidth from NVIDIA's DGX Spark hardware page
(https://docs.nvidia.com/dgx/dgx-spark/hardware.html). The same figure is used by the repo's earlier
speed work (`~/handoffs/2026-10-12-qwen3-8b-speed-*.md`, docs-read recorded 2026-10-12).

Bytes moved per vector add: read a, read b, write out = 3 x 4 bytes x 1,048,576 = 12,582,912 bytes.

## Gates and controls (all as required: session PASS)

| step | words | expected | result |
| --- | --- | --- | --- |
| gate_hand | hand vadd, sha256 59e6bdc8... | every element = a+b | PASS, 0 mismatches |
| gate_encoder | encoder vadd, sha256 39f3dfdf... | every element = a+b | PASS, 0 mismatches |
| control_mutant | hand vadd with one word changed | must be caught as wrong | caught (WRONG_RESULT) |
| control_empty | kernel that only exits | output must stay poison | poison untouched |
| time_hand, time_encoder, time_empty | as above, 11 reps each | all reps correct | 11/11 each |
| time_cpu_c | gcc C loop on the CPU, 21 reps | every element = a+b | PASS |

## Speed (median of 11 GPU reps / 21 CPU reps)

GPU time is measured from just before the work is handed to the chip until the chip's "done" word is
seen, so it includes the fixed cost of one launch. The empty kernel measures that fixed cost
(median 114,865 ns), and the second column takes it off.

| path | median time | fraction of 273 GB/s | fraction with launch cost removed |
| --- | --- | --- | --- |
| (a) hand-written words | 224,145 ns | **0.2056** (calc:482b8fdc) | 0.4218 (calc:36b7265e) |
| (b) existing encoder words (C-built) | 222,114 ns | **0.2075** (calc:8fa7d26e) | 0.4298 (calc:4345e266) |
| (c) gcc C loop on the CPU, one core | 696,485 ns | **0.06618** (calc:99523bfa) | n/a |

Fastest single reps: hand 211,729 ns, encoder 170,290 ns, empty 112,753 ns, CPU 413,475 ns
(CPU fastest = 0.1115 of the limit, calc:1688416e).

## What this means

- The words we typed by hand run on the chip and are exactly right. Nothing between us and the chip
  needs a vendor format: the launcher takes the raw instruction bytes.
- Hand words and C-built words are equally fast here (0.2056 vs 0.2075, inside the noise of a box at
  load 68). For a job this small, the C toolchain is not what holds us back.
- About half of every GPU run (about 115 microseconds) is the fixed cost of one launch, measured by the
  empty kernel. With that removed both kernels reach about 0.42-0.43 of the memory limit. The next speed
  is in the launch path (fewer, larger launches; less fixed work per launch) and a quiet window,
  not in the instruction words.
- The CPU, one core, plain C, reaches 0.066 of the limit; it is the reference point, not a competitor.

## Earlier session (kept, failed)

`chip-20261020T020548Z/`: most steps failed before any word was loaded, because the driver refused to
open a new work channel to the chip (CREATE_CHANNEL). Two 27B servers were resident; another lane
(L064) logged the same refusal as driver status 0x51, "Out of memory"
(`nvstatuscodes.h` line 110). The tool now prints the driver code and message for each rep. The
diagnostic run `diag-20261020T0302Z/` (hand vadd, 3/3 PASS) and session 2 then ran clean.
