# The engine: several machines, one schedule, one coupling (L312, 2026-10-20)

Drake's picture, in his words: "Think of a car engine." The calculation way (the bytes) and the math cores (the
arithmetic) are separate machines in one body, wired before the key turns: "boom, boom, and the pistons going back
and forth." Then: "Not two engines, three. Or hell, even more." And: "Do not assume you need a crankshaft."

The motor is the 35B (Drake, 2026-10-20 23:58Z). The first receipts were taken on the 30B MoE as a stand-in on the host side (CPU and NVMe), with the
GPU untouched; they are kept as history below. Every method here is model-independent and every
number says which model it is from.

## The cutaway

```
   NVMe (the tank)        host RAM (the reservoir)        the cylinders (GPU-visible buffers)        the math cores
  +--------------+  read  +------------------+   copy    +---------------------+---------------------+   fire   +----------+
  | expert parts | =====> | staging pair A|B | ========> |  buffer 0 (fires)   |  buffer 1 (fills)   | =======> | pistons  |
  | in beat order|        | (verified here)  |           |  verified again     |  next beat's head   |          | N heads  |
  +--------------+        +------------------+           +---------------------+---------------------+          +----------+
     NVMe engine              RAM engine                       the schedule names every slot                   power engine
        |                        |                                        |                                         |
  ======+========================+========================================+=========================================+======
                 the coupling: one shared beat (crankshaft) | bucket brigade of ready flags | free-running reservoir
```

Every engine has one job, its own buffer pair and its own timing trace, and is measured alone before it is geared in.
The schedule (one static table, built once from the model's structure and the placement plan, checked on the host)
names every slot every engine touches at every beat. Nothing about placement is decided while running: no
allocation, no compile, no lookup that was not precomputed.

## The parts, named

| Car part | Engine part | Code | What it does | What it never does |
|---|---|---|---|---|
| Camshaft, firing order | **The schedule** | `crates/aien-engine/src/schedule.rs` (sovereign-core) | One static table per model + placement plan: which weight part lands in which buffer at which beat, which buffer each beat fires on. Proven on the host before anything runs: complete, acyclic, collision-free, and with exactly the true dependency edges (see below). | Decide anything at run time. |
| Fuel line from the tank | **The NVMe engine** | `movement.rs` on the heartbeat ring (`crates/aien-heartbeat`, L190) | Reads a beat's parts from the disk with the page cache bypassed, checks every part's fingerprint (BLAKE3) on arrival, fails the whole ring closed on any error. | Compute. It does not know what the bytes mean. |
| Fuel pump | **The RAM engine** | `movement.rs` (`Engines::Staged`) | Copies a verified staging slot into the cylinder and verifies again before the slot is published. Runs on its own thread with the staging pair between it and the NVMe engine, so the next beat's disk read overlaps this beat's copy. On the GB10 host RAM and GPU memory are one pool, so this is a same-pool copy. | Read the disk. |
| Pistons | **The power engine** | `piston.rs` (host piston today; chip piston next) | Fires on a buffer that is already full. The model's own routing picks the experts; the arithmetic is the exact per-expert reference (bit-equal to the resident reference, L190's gate). A routed expert that is not in the buffer is a misfire and the run fails closed. | Fetch. |
| Several pistons on one stroke | **The head engine** | `piston.rs::HostPistonBank` | N sequences (heads) on one weight read per beat: each beat's expert set is copied out of the cylinder once and applied to all N rows. Each head's tokens must equal its own N = 1 run. | Change the routing. |
| Crankshaft, or not | **The coupling** | `bin/engine.rs` (`--coupling`) | Three candidates measured on the same loop: `brigade` (an engine fires when its input is full; the fill for beat b+1 is issued as beat b starts), `clock` (a fixed period; fills issued at the tick), `reservoir` (the movement engines run as far ahead as free buffers allow; the piston only reports buffers free). | Assume. The numbers choose. |
| Timing light | **The timing diagram** | `timing.rs` | Per beat: fill issued, fill done, fire start, fire done. **Ignition gap** = fire start minus fill done (how long the fuel sat waiting). **Piston idle** = fire start minus the previous fire done (how long the piston waited for fuel). Text table and PNG, our own encoder. Every series carries min, p10, median, p90, max and spread = (p90 - p10) / median. | Average away the spread. |

## The dependency edges (Drake: "there's no real reason it has to go in any particular order, right?")

Right, except physics. The schedule's host proof enumerates its edges and refuses any other order:
* **fuel**: fill(for beat b) before fire(b): the buffer must be full before the beat fires;
* **residual**: fire(layer L) before fire(layer L+1) of the same token: layer L+1 reads what L wrote;
* **token**: last layer of token t before first layer of token t+1 of the same sequence;
* **release**: a buffer is fired on before it is filled again.
Everything else is free and stays free: the parts of one fill land in any order on any worker (`free_parts` counts
them), different heads sit at different layers at the same time, and the next head's bytes load during this head's
compute. Test: `cargo test -p aien-engine` (`exactly_the_true_edges_*`); the schedule receipt carries `edges` and
`edges_expected`, which must be equal.

## Receipts on the 35B (the motor; every number from a JSON file in `evidence/engine/`)

Drake, 2026-10-20 23:58Z: "It needs to be built on 35B, and then just split 35B apart." The 35B (Qwen3.6-35B-A3B FP8,
40 layers x 256 routed experts of 3.146 MB each, 32.2 GB of experts on the disk) is the motor's model from here on;
the 30B receipts below are kept as history and no new 30B cell runs.

### A. The split, named by the schedule
The plan names the 35B's own parts: 40 layers; per layer the trunk (DeltaNet or full attention, norms, router, the
shared expert) read from the shards, and the routed experts as the parts the engines move. The heads come from L308's
measured per-domain files (`evidence/engine/35b-plan-l308/`: prose, code, math at 90 percent coverage, 48 to 194 experts
per layer, schedule proofs PASS: complete, acyclic, no collision, exactly the true edges) and, for the identity gate,
from L306's golden (the experts the exact host forward actually routed to on the gate prompt).

### B. The smallest full loop on the 35B host (identity gate)
`35b-host-*/` (`run-host-35b.sh`): L306's exact 35B forward (`Qwen38Reference`, host oracle ppl 1.18) runs unchanged;
the only change is where its expert weights come from: a tensor source that serves every routed expert from the ring
the movement engines fill along the schedule, and refuses (misfire, fail closed) when a routed expert is not in the
cylinder. Beats are drawn inside that source: the first expert read of each (token, layer) is the fire start. Gate:
the 32 greedy tokens and the routes equal the golden's. The receipt also counts expert reads at open (must be 0:
experts are lazy, so nothing but the schedule decides what is read). RESULTS PENDING (queued behind the engine-count run).

### C. Engine count and coupling on the 35B (movement engines alone)
`35b-engines-*/run.log` (`run-engines-35b.sh`, `table-engines.sh`): the same twelve cells as the 30B history table
(engines 1 vs 2, coupling brigade | clock | reservoir, buffers 2 | 3) on the 35B's 8-expert layer beat (25.2 MB).
RESULTS PENDING (running; the three buffers-3 / engines-2 cells of this run still use the binary before the staging
fix and are expected to fail closed again; they are re-run after it).

### D. Head swap mid-run on the 35B (H4)
`35b-swap-*/` (`run-swap-35b.sh`, `table-swap.sh`): head A = L308's measured prose head resident in host RAM, head B =
the measured code head from token 12 of 24, loader lead -1 / 0 / 1 / 2 / 4 tokens. RESULTS PENDING.

### E. One base, many heads, one stroke on the 35B (H6)
Needs the 35B forward factored per layer (as the 30B piston was) so N heads share one expert read per beat; the 30B
stroke receipts below are the method. NOT RUN YET on the 35B.

### F. The chip piston (power engine on the GB10)
See section 8 below (slot size = the 35B layer, 26.7 MB; GB10 run queued in the measurement-window line).

## History: the 30B stand-in (2026-10-20, before Drake retired it at 23:58Z)
Everything in this section was measured on the 30B MoE (qwen3-30b-a3b) on the host side (CPU and NVMe) while the 35B
was being brought up. The methods are the same; only the model differs.



### 1. The schedule is sound
`30b-host-20261020T2153Z/schedule-trace.check.json`: 66 tokens x 48 layers = 3,168 beats, 3,476 parts, 2 buffers x 99
slots; complete, acyclic, no collision: PASS. Bytes per beat 264 to 467 MB (the head here is the union of routed
experts per layer over the traced tokens, 56 to 99 experts; a measured per-domain head from L308 will be smaller).

### 2. The movement engine alone (bytes per beat flat, no misfires)
`30b-host-20261020T2153Z/move-*/summary.json`, fixed head 8 experts x 48 layers, 37.75 MB per beat, 1,920 beats per cell:

| buffers | stand-in fire | beats | misfires | late beats | ignition gap (median) | fill (median) | beat (median) | payload during fill |
|---|---|---|---|---|---|---|---|---|
| 2 | 0 us | 1920 | 0 | 0 | 20.9 us | 9.21 ms | 9.22 ms | 3.04 GB/s |
| 2 | 2000 us | 1920 | 0 | 0 | 28.3 us | 9.88 ms | 9.89 ms | 3.41 GB/s |
| 2 | 4000 us | 1920 | 0 | 0 | 24.7 us | 9.67 ms | 9.67 ms | 3.36 GB/s |
| 3 | 2000 us | 1920 | 0 | 0 | 26.1 us | 10.22 ms | 10.23 ms | 2.86 GB/s |

Bytes per beat: flat at 37,753,344 (min = max). The ignition gap sits at 21 to 28 us: when the piston is faster than
the fuel line, it fires within 30 us of the fuel arriving, thousands of beats in a row, no misfire. The fuel line
itself ran at 3.0 to 3.4 GB/s on a box with load average 23 and other NVMe users; the NVMe ceiling is 10.6 GB/s
(L151, measured). The gap between those two numbers is the next movement-engine experiment (below).

### 3. Both engines together on the host: identical tokens, thousands of beats
`30b-host-20261020T2153Z/loop-host/`: the host piston (exact per-expert reference) fired 3,168 beats on buffers the
movement engine filled from the NVMe along the schedule. **Identity gate PASS: 66 of 66 tokens equal the resident
reference's, routing identical at 66 of 66 positions** (`gate.json`). 0 misfires, 0 late beats. Ignition gap median
5.67 ms; fill median 86.5 ms (264 to 467 MB per beat at 3.0 GB/s); fire median 78.8 ms (host arithmetic); beat median
101 ms, spread 1.19. This loop is fuel-bound on the host because the synthetic head is 56 to 99 experts; the receipt
is the mechanism (schedule, pistons, gap, identical tokens), not the speed.

### 4. Engine count and coupling (movement engines alone, 30B)
`30b-engines-20261020T2343Z/run.log` and `*/summary.json` (`table-engines.sh`): engines 1 (NVMe straight into the
cylinder) vs 2 (NVMe engine -> staging ring -> RAM engine), coupling brigade | clock | reservoir, buffers 2 | 3, the
same 1,920-beat schedule (240 tokens x 8 layers, 8 experts of 4.16 MB per beat), host load 30 to 36 at start.
Medians; "spread" is p90/p10 of the beat time; "gap" is fire start minus fill done (the ignition gap).

| buffers | engines | coupling | beats | late | beat (median) | spread | ignition gap | idle | fill | GB/s in fill | NVMe stage | RAM stage | hand-off wait | load at start |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 1 | brigade | 1920 | 0 | 19.72 ms | 1.22 | 44 us (p90 827 us) | 17.56 ms | 19.78 ms | 1.68 | 19.78 ms | 0 ms | 0 us | 33.98 |
| 2 | 1 | clock | 1920 | 0 | 14.04 ms | 0.69 | 17 us (p90 334 us) | 12.01 ms | 13.99 ms | 2.41 | 13.99 ms | 0 ms | 0 us | 36.73 |
| 2 | 1 | reservoir | 1920 | 0 | 13.85 ms | 0.61 | 19 us (p90 331 us) | 11.84 ms | 13.82 ms | 2.5 | 13.82 ms | 0 ms | 0 us | 33.84 |
| 2 | 2 | brigade | 1920 | 0 | 19.96 ms | 0.86 | 29 us (p90 399 us) | 17.82 ms | 36.43 ms | 0.97 | 17.38 ms | 17.93 ms | 76 us | 34.43 |
| 2 | 2 | clock | 1920 | 0 | 21 ms | 0.73 | 29 us (p90 345 us) | 18.94 ms | 38.67 ms | 0.96 | 18.4 ms | 18.99 ms | 63 us | 32.87 |
| 2 | 2 | reservoir | 1920 | 0 | 27.5 ms | 1.39 | 39 us (p90 1385 us) | 25.2 ms | 49.93 ms | 0.64 | 23.4 ms | 24.02 ms | 364 us | 29.96 |
| 3 | 1 | brigade | 1920 | 0 | 18.98 ms | 0.99 | 47 us (p90 734 us) | 16.84 ms | 18.75 ms | 1.8 | 18.75 ms | 0 ms | 0 us | 35.96 |
| 3 | 1 | clock | 1920 | 0 | 16.23 ms | 0.91 | 34 us (p90 491 us) | 14.21 ms | 16.14 ms | 1.97 | 16.14 ms | 0 ms | 0 us | 34.63 |
| 3 | 1 | reservoir | 1920 | 0 | 19.95 ms | 0.89 | 199 us (p90 711 us) | 17.79 ms | 19.58 ms | 1.62 | 19.58 ms | 0 ms | 0 us | 34.83 |

The three buffers-3, engines-2 cells failed closed at beats 4, 11 and 5: `ram engine: staging slot not published`.
Root cause (fixed, sc `3a9285da`): with B buffers the crank issues the fill for beat b+B at the start of beat b+1,
before the RAM engine has finished moving beat b+1; the staging ring had only two slots per lane (for_beat mod 2),
so beats b+1 and b+3 shared a slot and the NVMe engine overwrote a stage the RAM engine had not yet consumed. The
staging ring is now B x slots deep, indexed by for_beat mod B. The cells are re-run on the 35B with the fixed binary.

Reading of the 30B table (host side only; the chip piston changes the second stage):
- One movement engine is enough on the host: the beat is the NVMe read (13.8 to 19.8 ms for 33 MB, 1.7 to 2.5 GB/s
  payload), the piston waits for it (idle = beat minus 2 ms of fire), and the ignition gap is 17 to 47 us median
  with p90 under 1 ms: the fill lands just before the fire wants it, as the schedule intends.
- Two engines did not shorten the beat on the host: the RAM stage takes as long as the NVMe stage (17 to 24 ms
  each) because it re-verifies the BLAKE3 digest and copies 33 MB; the hand-off wait between them is 63 to 364 us.
  The second engine is the chip's stage in the real motor (section 8); on the host it only adds a copy.
- Clock and reservoir are the fastest couplings for one engine (14 ms); brigade costs 5 ms per beat on the host
  because each ready flag is a thread hand-off on a loaded machine. For two engines brigade and clock tie (20 ms)
  and reservoir falls behind (27 ms, p90 gap 1.4 ms): free-running engines collide on the staging ring.
- Three buffers are no better than two on the host: the NVMe read is the beat, and a third buffer only adds a
  slot the engine cannot fill faster.

### 5. One base, many heads, one stroke (H6)
`30b-stroke-*/`: 32 short prompts traced; one union plan; strokes at N = 1, 2, 4, 8, 16, 32 heads with every head
gated against its own trace; `union.json` = measured expert union per beat vs the independent-draw expectation
128 x (1 - (1 - 8/128)^N). The 30B run was interrupted by the attended native boot at 22:49Z (folder
`30b-stroke-20261020T2251Z-INTERRUPTED-boot1/`, kept) and is not re-run on the 30B; H6 is measured on the 35B (section E above).

### 6. Today's path on the same prompt
`30b-host-20261020T2153Z/today.json`: the live server (18091) on the same prompt, agreement with the host trace as a
count, not a gate (the loop's gate is host loop == host trace). First run FAILED on a wrong model name (fixed); the
rerun waits for a model served on 18091 (empty since 23:33Z) and will be taken on the 35B.

### 7. Head swap mid-run (H4)
`30b-swap-*/run.log` and `lead*/summary.json` (`evidence/engine/run-swap-30b.sh`): head A (8 experts x 48 layers)
sits in host RAM before the run; from token 12 of 24 every beat fires on head B, a disjoint head of the same size.
A loader starts bringing head B into RAM `lead` tokens before the swap (lead -1 = never: every B part comes from the
disk when its beat needs it). The receipt per lead: the steady ms per token before the swap, the extra ms on the first
token after it, the total extra the user sees, the hit rate (parts served from RAM instead of the disk) after the swap,
and the first token after the swap served entirely from RAM. NOT RUN on the 30B: the queue was stopped before this
cell when Drake retired the 30B; the same script runs on the 35B (`run-swap-35b.sh`, section D above).

### 8. The chip piston (power engine on the GB10)
`chip-piston-*/` (`evidence/engine/run-chip-piston.sh`, binary `build/l312/gpu_engine_piston_test` from
`src/omega_blackwell_codegen_piston.c`): a resident launch on the GPU fires one beat each time the host's fill flag for
that beat lands (thread 0 of every block spins on the flag through the system-coherent load path), reads the beat's
slot, stamps the chip clock at pickup and at done, and writes the done flag the host reads. Mode `pre`: all fuel in
place before the run (power engine alone: chip beat time, tokens per beat with no movement). Mode `live`: a host
thread copies each beat's image into its slot while the chip fires on the other (both engines: the on-chip ignition
gap = chip spin time per beat). The slot size is one MoE layer's 8 routed experts on the 35B (26.7 MB); the 30B slot was dropped when Drake retired
the 30B, the chip runs the 35B only. Host simulator (`--host-only`): PASS, 5 beats, 2 slots, 0 wrong bytes; negative control (a stale slot)
caught 64 of 64 times. GB10 RUN QUEUED (measurement window ticket l312-piston).

## What is not done, said plainly
* The chip piston is built and proven in its host simulator (section 8); its GB10 run waits in the measurement-window
  line behind L306, L274 and L131. Every chip number comes from inside that window (chip lock held for it).
* The fast-memory engine (L2 / shared memory staging) and the math engine exist only on the chip; the engine-count
  series on the host stops at 2 and continues there.
* The thesis gate (planned loop vs today's path, identical tokens, at least as fast at every context) needs the chip
  piston; on the host the planned loop is slower than the resident path by physics (10.6 vs 245 GB/s tiers), and the
  doc says so instead of arguing.
* On the 35B (the motor): the engine-count cells are running (section C); the host loop identity gate (B) and the
  head swap (D) are queued behind them; the stroke (E) needs the 35B forward factored per layer; the three
  buffers-3, engines-2 cells are re-run with the staging fix. No 30B cell runs again.

## Next discriminating experiments
1. Movement engine at the NVMe ceiling: 3.0 to 3.4 GB/s measured against 10.6 GB/s possible. Variables, one per run:
   worker count (8 -> 16), one contiguous beat-ordered file instead of per-expert ranges in shards (fewer, larger
   reads), and a quiet box. The receipt is payload GB/s during fill vs the ceiling.
3. The RAM stage re-verify: on the host the two-engine path doubled the fill (section 4 history) because the RAM engine
   verifies the BLAKE3 digest again and copies the beat. One variable: verify once at the NVMe stage and let the RAM
   stage copy only; the receipt is the RAM stage time vs the NVMe stage time at identical tokens.
4. The chip piston: the same schedule, cylinders in GPU-visible memory, a resident kernel; the receipt is the same
   timing diagram with fire times in microseconds instead of milliseconds, and the thesis gate against 18091.
