# The engine: several machines, one schedule, one coupling (L312, 2026-10-20)

Drake's picture, in his words: "Think of a car engine." The calculation way (the bytes) and the math cores (the
arithmetic) are separate machines in one body, wired before the key turns: "boom, boom, and the pistons going back
and forth." Then: "Not two engines, three. Or hell, even more." And: "Do not assume you need a crankshaft."

Everything below was measured on the 30B MoE (qwen3-30b-a3b) on the Spark, on the host side (CPU and NVMe), with the
GPU untouched. The verdict model is the 35B when L306 serves it; every method here is model-independent and every
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

## Receipts (30B, host side; every number from a JSON file in `evidence/engine/`)

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

### 4. Engine count and coupling (movement engines alone)
`30b-engines-*/run.log` and `*/summary.json`: engines 1 (NVMe straight into the cylinder) vs 2 (NVMe engine ->
staging pair -> RAM engine), coupling brigade | clock | reservoir, buffers 2 | 3, same 1,920-beat schedule.
RESULTS PENDING (run in progress when this line was written; the table lands here).

### 5. One base, many heads, one stroke (H6)
`30b-stroke-*/`: 32 short prompts traced; one union plan; strokes at N = 1, 2, 4, 8, 16, 32 heads with every head
gated against its own trace; `union.json` = measured expert union per beat vs the independent-draw expectation
128 x (1 - (1 - 8/128)^N). RESULTS PENDING.

### 6. Today's path on the same prompt
`30b-host-20261020T2153Z/today.json`: the live server (18091) on the same prompt, agreement with the host trace as a
count, not a gate (the loop's gate is host loop == host trace). First run FAILED on a wrong model name (fixed); rerun
PENDING.

## What is not done, said plainly
* The chip piston (resident kernel on the GPU, cylinders in omega session memory, fire-done flag read by the host) is
  designed (see the handoff) and not built; the GPU lock queue (L131 window, L274, a training job) has not freed.
  Every chip number waits on `quiet-measure flock /tmp/aien-gb10.lock`.
* The fast-memory engine (L2 / shared memory staging) and the math engine exist only on the chip; the engine-count
  series on the host stops at 2 and continues there.
* The thesis gate (planned loop vs today's path, identical tokens, at least as fast at every context) needs the chip
  piston; on the host the planned loop is slower than the resident path by physics (10.6 vs 245 GB/s tiers), and the
  doc says so instead of arguing.
* The 35B: L306 is bringing it up; the adapter from L308's heads file is the first task when it serves.

## Next discriminating experiments
1. Movement engine at the NVMe ceiling: 3.0 to 3.4 GB/s measured against 10.6 GB/s possible. Variables, one per run:
   worker count (8 -> 16), one contiguous beat-ordered file instead of per-expert ranges in shards (fewer, larger
   reads), and a quiet box. The receipt is payload GB/s during fill vs the ceiling.
2. The chip piston: the same schedule, cylinders in GPU-visible memory, a resident kernel; the receipt is the same
   timing diagram with fire times in microseconds instead of milliseconds, and the thesis gate against 18091.
