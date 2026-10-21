# The geometry of the motor (L323)

Drake, 2026-10-21: "What if it can go in a figure eight? It can go from the CPU straight to the GPU through a fast
port somewhere. You find the best configuration possible geometrically."

This page answers that for the 35B model (Qwen3.6-35B-A3B FP8) on this Spark. Every shape was scored by one solver
that we wrote ourselves (`tools/geometry/geometry.c`, plain C, no outside libraries), and every number comes from a
measured receipt or is marked UNKNOWN, together with the experiment that will measure it.

## The answer in four lines

1. On this one machine, the best shape is the **figure eight**: the CPU keeps the DeltaNet memory (the model's running
   notes, 62.9 MB per conversation) in its own caches and does that step, so only small vectors cross to the GPU and
   back. Predicted: **62.2 words/s against 61.3 for the straight line, +1.4 percent**, for one conversation at FP8.
2. That gain is small, and it is small for a physical reason. Each word needs **3.48 GB of weights** read from main
   memory. All the fast memory on the chip together (CPU caches about 73 MB, GPU L2 24 MB) can hold only **2.8 percent**
   of that (calc:2f1a633a). No arrangement of loops can move more than the on-chip memory holds. Even with every
   unknown set to its best plausible value, the best shape inside one machine stays under +5 percent.
3. The only shape that clearly breaks the one-pipe ceiling is a **second machine with its own memory pipe**: +30 to
   +64 percent for 2 to 32 conversations (pipeline split), and +4.8 percent for one conversation if the cable's round
   trip is under about 55 microseconds per layer. We do not have a second Spark, so this is a price tag, not a plan.
4. The big levers are still fewer bytes per word (4-bit experts and head: 61 to 90 words/s) and more words per read
   (serving 8 conversations at once: 157 words/s total). Geometry adds a few percent on top of those.

## The shapes, drawn

```
 Straight line (today)              Figure eight (Drake's idea)            Two machines
                                                                           
  DRAM ===pipe 228 GB/s===> GPU      DRAM ==pipe==> GPU <==link==> CPU      DRAM A ==> GPU A --cable--> GPU B <== DRAM B
  (weights + state + KV)             (weights + KV)   q,k,v out  (state       (layers 0..k)              (layers k..39
                                                      result back in caches)                               + head)
```

- **Straight line**: the GPU reads everything (weights, DeltaNet memory, attention cache) through the one memory pipe.
- **Three pistons** (L321): the CPU does the DeltaNet step, the handoff box (the cylinder, 49 KB per layer) sits in
  main memory and is passed by a flag.
- **Figure eight**: the same, but the handoff goes cache to cache over the CPU-GPU link, so it never touches the pipe.
  The crossing happens 60 times per word (out and back for each of 30 DeltaNet layers).
- **Two machines**: a second machine reads its share of the bytes from its own memory; the 4 KB hidden vector crosses
  the cable once per word (pipeline split) or once per layer (experts split).
- **Solver free**: the solver may also pin state in the GPU's L2 cache, let the GPU read CPU-cache data over the link
  (the "second road", L321 E1b), move the router or part of the head to the CPU, and stagger conversations into groups.

## Ranking, one conversation, FP8 (predicted, `evidence/geometry/run-20261021/rank.tsv`)

| Shape | Words/s | What limits it | vs straight line |
|---|---|---|---|
| Two machines, experts split (no second Spark exists) | 64.28 | cable round trip, 40 per word | +4.79% |
| **Figure eight** (best buildable) | **62.18** | the 30 crossings | **+1.38%** |
| Three pistons | 62.01 | the 30 crossings | +1.09% |
| Straight line | 61.34 | the memory pipe | 0 |
| Two machines, pipeline split (2nd Spark) | 61.22 | cable round trip | -0.20% |
| Two machines, Mac over Wi-Fi | 35.22 | 5.9 ms Wi-Fi round trip | -42.58% |
| Solver free | 62.18 | same as figure eight: nothing else helped | +1.38% |

Straight line time per word: 16.302 ms (calc:8c2a5ce6), from 3.631 GB read at 228 GB/s plus 65.9 MB written at 175 GB/s.

## Ranking, many conversations and the lighter formats (best buildable shape)

| Format | N=1 | N=8 total | N=32 total | Best buildable shape |
|---|---|---|---|---|
| FP8, state f32 | 62.18 (+1.4%) | 158.35 (+0.6%) | 239.24 (+0.2%) | figure eight (30, 4, 1 layers of state on the CPU) |
| FP8, state bf16 | 62.62 | 166.14 | 259.29 | straight line |
| 4-bit experts and head, state f32 | 91.38 (+2.0%) | 239.20 (+0.9%) | 351.24 (+0.3%) | figure eight |
| 4-bit, state bf16 | 92.32 | 257.42 | 396.22 | straight line |
| Second Spark, pipeline split (FP8) | 61.22 | 248.17 (+58%) | 379.76 (+59%) | needs a machine we do not have |

Why the figure eight fades as conversations grow: every conversation has its own 62.9 MB of DeltaNet memory, and the
CPU caches hold about one conversation's worth. With 32 conversations only one layer each fits.
Why it loses with bf16 state: halving the state halves what the figure eight saves (11.1 us per layer instead of 22.2,
calc:30880ab9 and calc:9f178980), while the CPU step and the round trip stay the same size.

## The rule that decides it (for L321's measurements)

Moving one DeltaNet layer of one conversation to the CPU saves **22.2 microseconds** of pipe time at f32
(read 2.2 MB at 228 GB/s plus write it at 175 GB/s, calc:9f17898045bf) and 11.1 at bf16 (calc:30880ab9dd9d).
It costs one round trip across the link plus the CPU's state step, and nothing else hides it.
So the figure eight pays only if **round trip + CPU step < 22.2 us per layer** (f32). Today's numbers: CPU step about
11.8 us on 10 big cores (from L321's smoke run, 59 us on 2 cores; not evidence yet), round trip UNKNOWN.
The coordinator's first pass gave 17.7 us as the break-even with one rate for reads and writes and no CPU step time; the
solver reproduces that exactly in its self test, and the 22.2 replaces it because writes are measured slower (175 GB/s).

## Corrections to the first pass (checked by the solver's self test)

- One routed expert is 3,146,112 bytes (calc:065912cb), not 3.22 MB: the 33.02 GB in the first pass also counted the
  MTP layer's experts. Weights per word are 3.481 GB, not 3.537 (headers: `evidence/geometry/parts-35b.tsv`).
- Writes go through the pipe at 175 GB/s, not 237 (MACHINE_CEILING.md row 58). This makes every byte of DeltaNet
  memory dearer and helps the figure eight.
- The CPU step time sits on the chain of each word. With it, the figure eight is +1.4 percent, not +3.5.
- The conv state (2.9 MB per conversation) was missing; it is small and now counted.

## The machine as a graph (`evidence/geometry/run-20261021/edges.tsv`)

| Road or place | Value used | Source |
|---|---|---|
| Memory pipe, GPU reading | 228.0 GB/s held 30 s (245.1 best burst, swept) | MACHINE_CEILING.md:57 |
| Memory pipe, GPU writing | 175.1 GB/s | MACHINE_CEILING.md:58 |
| Cost of sharing the pipe | GPU loses 1.17 B per B the CPU or drive takes | MACHINE_CEILING.md:105-107 |
| CPU reading memory while GPU streams | 22.7 GB/s | MACHINE_CEILING.md:63 |
| CPU caches that can hold state | 49 MB (lscpu) to 73 MB (NVIDIA deck) | MACHINE_CEILING.md:69-70, L321 handoff |
| GPU L2 | 24 MB, 5.1 TB/s, little reuse between launches | MACHINE_CEILING.md:64-65 |
| CPU DeltaNet step, one layer | 118 core-microseconds | L321 smoke, NOT evidence; L321 E3a measures |
| CPU-GPU link round trip | **UNKNOWN**, 2 us used, swept 0 to 20 | asked of L321 (16-49 KB round trip) |
| Second road (GPU reading CPU caches) | **UNKNOWN**, 0 used, swept 100 and 300 GB/s | L321 E1b |
| Pinning state in GPU L2 | **UNKNOWN**, 0 used, swept 22 MB | experiment: hold 2-22 MB, stream 1.5 GB, re-read |
| Read-ahead during a crossing | **UNKNOWN**, 0 used, swept 16 MB | resident kernel test (L312) |
| NVMe | 10.54 GB/s into main memory | MACHINE_CEILING.md:72 |
| 10 GbE | 1.25 GB/s on paper, round trip **UNKNOWN** (no partner) | MACHINE_CEILING.md:74, L258 |
| Mac over Wi-Fi | 5.93 ms round trip, 100 GB/s memory (spec only) | L258 brief |
| GPU launch | 23-26 us per dependent launch, the same for every shape | MACHINE_CEILING.md:78 |

The drive never appears in a winning shape: the 35B fits in memory, and anything the drive reads lands in main memory
and then crosses the pipe again, so it can only cost.

## What each unknown would change (FP8, one conversation; `sweep-*.tsv`)

| If we measure | Best buildable | vs straight |
|---|---|---|
| link round trip 0 us | figure eight 62.42 | +1.8% |
| link round trip 10 us or more | straight line | 0 |
| CPU step 30 core-us (better code) | figure eight 63.22 | +3.1% |
| read-ahead of 16 MB during crossings | figure eight 63.83 | +4.1% |
| second road at 100 GB/s | state read by the GPU from CPU caches, 63.96 | +4.3% |
| state pinned in GPU L2 (22 MB) | 20 layers on CPU + 10 in L2, 62.72 | +2.3% |
| everything at its best together | 64.35 | +4.9% |
| CPU caches only 49 MB | figure eight with 22 layers, 61.96 | +1.0% |

## How we know the solver is right

- It reproduces all five of the coordinator's first-pass numbers (63.25, 65.45, 65.19, 64.19 words/s, 17.7 us).
- The fast search (it checks only the points where two of the time limits cross) equals a full brute-force search on
  72,576 of 72,576 test cases, and the whole ranking table is identical byte for byte (`exact.txt`).
- A planted wrong search (ends only) is caught in 15,896 cases, so the check has teeth.
- Run it: `evidence/geometry/run.sh` (no chip, a few seconds of arithmetic).

## Predicted vs measured

| Shape | Predicted | Measured | Gap |
|---|---|---|---|
| Straight line, FP8, N=1 | 61.34 words/s | not measured yet (L306 speed gate on the 35B) | |
| Figure eight, FP8, N=1, 30 layers on CPU | 62.18 words/s | not measured yet (handed to L321 E3) | |

A gap, when it comes, names the edge that was wrong; it is not fudged.
