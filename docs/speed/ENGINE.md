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
`35b-host-20261021T0025Z/loop/` (`run-host-35b.sh`): L306's exact 35B forward (`Qwen38Reference`, host oracle ppl 1.18)
runs unchanged; the only change is where its expert weights come from: a tensor source that serves every routed expert
from the ring the movement engines fill along the schedule, and refuses (misfire, fail closed) when a routed expert is
not in the cylinder. Beats are drawn inside that source: the first expert read of each (token, layer) is the fire
start. The plan is the golden's own routes (prefill union plus 32 decode steps: 53 to 136 experts per layer, 3,011
parts, 2 buffers x 136 slots); schedule proof PASS.

**Identity gate PASS: 32 of 32 tokens equal L306's golden (gen-p1), routing identical at 32 of 32 steps; expert reads
at open 0 (the experts are lazy: only the schedule decides what is read); 1,280 beats, 0 misfires, 0 late beats**
(`gate.json`, `summary.json`). Timing: ignition gap median 22.9 us (p90 2.4 ms); fill median 147 ms for 220 MB
(166 to 428 MB per beat, 1.19 GB/s payload under load 35); fire median 45.4 ms (the exact forward's own host
arithmetic and trunk reads per layer); beat median 101 ms, spread 2.33; first step 9.8 s (the 5-token prefill), later
steps 4.2 s median. Fuel-bound on the host, as the 30B loop was: the receipt is the mechanism (one static schedule,
the exact forward fed from the cylinder, identical tokens), not the speed.

### C. Engine count and coupling on the 35B (movement engines alone)
`35b-engines-20261020T2349Z/` (`run-engines-35b.sh`, `table-engines.sh`, folder README for the binary note): the same
twelve cells as the 30B history table on the 35B's 8-expert layer beat (25.2 MB), 1,600 beats each, host load 36 to 46
at every start. **12 of 12 cells PASS closed (0 late beats, 0 misfires)**; the three buffers-3 / engines-2 cells that
failed on the 30B before the staging-ring fix pass here with it.

| buffers | engines | coupling | beats | late | beat (median) | spread | ignition gap | idle | fill | GB/s in fill | NVMe stage | RAM stage | hand-off wait | load at start |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 1 | brigade | 1600 | 0 | 13.99 ms | 1.19 | 279 us (p90 667 us) | 11.79 ms | 13.68 ms | 1.29 | 13.68 ms | 0 ms | 0 us | 43.31 |
| 2 | 1 | clock | 1600 | 0 | 10 ms | 0.32 | 2869 us (p90 4429 us) | 8 ms | 6.68 ms | 3.3 | 6.68 ms | 0 ms | 0 us | 42.21 |
| 2 | 1 | reservoir | 1600 | 0 | 6.96 ms | 0.86 | 21 us (p90 200 us) | 4.81 ms | 6.86 ms | 3 | 6.86 ms | 0 ms | 0 us | 39.78 |
| 2 | 2 | brigade | 1600 | 0 | 15.8 ms | 2.29 | 17 us (p90 1744 us) | 13.65 ms | 25.5 ms | 0.62 | 12.54 ms | 11.04 ms | 180 us | 39.61 |
| 2 | 2 | clock | 1600 | 0 | 20.14 ms | 2.25 | 31 us (p90 1933 us) | 18.01 ms | 42.19 ms | 0.5 | 17.44 ms | 17.64 ms | 267 us | 41.19 |
| 2 | 2 | reservoir | 1600 | 0 | 14.17 ms | 1.56 | 23 us (p90 248 us) | 12.03 ms | 24.18 ms | 0.75 | 12.29 ms | 11.78 ms | 16 us | 40.04 |
| 3 | 1 | brigade | 1600 | 0 | 12.4 ms | 1 | 33 us (p90 469 us) | 10.16 ms | 12.28 ms | 1.65 | 12.28 ms | 0 ms | 0 us | 42.50 |
| 3 | 1 | clock | 1600 | 0 | 10.65 ms | 0.84 | 24 us (p90 407 us) | 8.58 ms | 10.55 ms | 2 | 10.55 ms | 0 ms | 0 us | 44.42 |
| 3 | 1 | reservoir | 1600 | 0 | 13.55 ms | 1.43 | 134 us (p90 586 us) | 11.37 ms | 13.27 ms | 1.1 | 13.27 ms | 0 ms | 0 us | 45.77 |
| 3 | 2 | brigade | 1600 | 0 | 25.25 ms | 2.59 | 231 us (p90 7819 us) | 22.79 ms | 62.09 ms | 0.31 | 22.96 ms | 22.82 ms | 5167 us | 45.82 |
| 3 | 2 | clock | 1600 | 0 | 30.01 ms | 2.43 | 160 us (p90 9574 us) | 26.81 ms | 75.23 ms | 0.25 | 26.51 ms | 26.91 ms | 7147 us | 42.82 |
| 3 | 2 | reservoir | 1600 | 0 | 11.84 ms | 0.81 | 26 us (p90 2975 us) | 9.63 ms | 27.08 ms | 0.9 | 11.4 ms | 11.33 ms | 3185 us | 36.32 |

Reading (host side; the box was shared with other agents' runs, so single cells move by a few ms between repeats):
- One engine, reservoir coupling, is the fastest host cell on the 35B: beat 7.0 ms for 25.2 MB (3.0 GB/s payload),
  ignition gap 21 us median, p90 200 us. Clock is as fast in fill (6.7 ms) but its 10 ms period holds the piston, so
  the gap is the period minus the fill (2.9 ms): the clock coupling needs its period set from the measured fill.
- Two engines cost the second stage again (RAM stage 11 to 27 ms, as long as the NVMe stage) and, with three buffers,
  a hand-off wait of 3 to 7 ms: the fixed staging ring is deeper and the RAM engine falls behind the NVMe engine. The
  receipts say what the RAM stage spends its time on: the digest sum per beat rises from 14.4 ms (one engine) to
  73.2 ms (two engines, `stages.digest_us_sum`, reservoir cells), so the RAM stage's BLAKE3 verify is 0.625 of its
  wall time (calc:ed063a94: 58.8 ms over 8 workers against 11.8 ms of stage). That verify is the ring's rule (nothing
  unverified is ever leased) and stays; the second stage is only worth its copy when it is the chip's (section F).
- Three buffers gain nothing over two with one engine (12.4 / 10.7 / 13.6 ms vs 14.0 / 10.0 / 7.0 ms).
- Against the 30B history table: the 35B beat is smaller (25.2 vs 33.3 MB) and the best cell is 1.99x faster per beat (calc:6c80453d)
  (7.0 vs 13.9 ms) at the same payload rate; the engine-count and coupling conclusions are the same on both models.

### D. Head swap mid-run on the 35B (H4)
`35b-swap-20261021T0029Z/` (`run-swap-35b.sh`, `table-swap.sh`): head A = L308's measured prose head resident in host
RAM, head B = the measured code head from token 12 of 24, loader lead -1 / 0 / 1 / 2 / 4 tokens; stand-in fire 2 ms per
beat, so the steady time per token is the RAM copy of a 12 GB head on the host. All five cells PASS closed (rc 0).

| loader lead (tokens before the swap) | steady ms/token before | first token after swap: extra | total extra the user sees | hit rate before | hit rate after | first all-RAM token after swap | head B load | B load start vs swap |
|---|---|---|---|---|---|---|---|---|
| 0 | 1808.26 ms | 1651.12 ms | 8793.33 ms | 1 | 0.963 | 13 | 3238.02 ms | 2.01 ms |
| -1 | 1717.05 ms | 1194.05 ms | 14846.34 ms | 1 | 0.356 | never | -1 ms | 0 ms |
| 1 | 1852.41 ms | 580.42 ms | 7400.16 ms | 1 | 1 | 12 | 3251.32 ms | -2167.07 ms |
| 2 | 1249.89 ms | 289.09 ms | 4322.58 ms | 1 | 1 | 12 | 2203.98 ms | -2750.86 ms |
| 4 | 1675.29 ms | 360.61 ms | 7858.69 ms | 1 | 1 | 12 | 3063.37 ms | -6537.57 ms |

Reading: with no loader (lead -1) the swap costs 14.8 s in total and the RAM hit rate never recovers (0.36: head B
keeps coming from the disk). Loading head B takes 2.2 to 3.3 s, 1.79 steady tokens (calc:f290a1e6); a lead of 2 tokens
covers it: the first token after the swap costs 0.29 s extra, every token after it is served from RAM, and the total
the user sees is 4.3 s. Lead 0 (start loading at the swap) still costs 1.65 s on the first token and 8.8 s in total;
lead 4 is worse than 2 (7.9 s total) because the loader competes with the steady path's reads for two tokens longer.
The rule the receipt gives: start the next head's loader about two tokens before its first beat, not earlier.

### E. One base, many heads, one stroke on the 35B (H6)
`35b-stroke-20261021T0035Z/` (`run-stroke-35b.sh`, one binary for every cell): 32 prompts traced through the exact
35B forward one token at a time (`trace35 --prompt`, the first 12 positions; positions 0 to 2 are the chat template),
one union plan and one schedule over all 32 traces (480 beats = 12 strokes x 40 layers, 2 buffers x 223 slots, check
PASS), strokes at N = 1, 2, 4, 8, 16, 32 heads on that schedule. The forward is factored into its own pieces (embed,
read layer, mix, MoE, finish: `forward` is composed of exactly these, 14 tests), and the stroke steps all N sequences
one layer at a time, so every head reads its experts of layer l inside the same beat, the trunk of layer l is read
once for all N, and each expert is copied out of the cylinder once per beat (repeats come from that copy). Gate:
every head's token equals its own trace at every position, routes too; a missing expert is a misfire, fail closed.

| N heads | beats | misfires | identical tokens every head | expert union per beat (mean) | bytes per beat (median) | fill | fire | beat | ignition gap | tokens/s total |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 480 | 0 | true | 8 | 544.3 MB | 114.33 ms | 74.94 ms | 114.5 ms | 0.03 ms | 0.21 |
| 2 | 480 | 0 | true | 12.7 | 544.3 MB | 140 ms | 133.48 ms | 144.87 ms | 0.18 ms | 0.3 |
| 4 | 480 | 0 | true | 19.8 | 544.3 MB | 149.18 ms | 201.33 ms | 201.42 ms | 49.66 ms | 0.48 |
| 8 | 480 | 0 | true | 30.5 | 544.3 MB | 200.59 ms | 312.74 ms | 334.21 ms | 107.15 ms | 0.56 |
| 16 | 480 | 0 | true | 44.8 | 544.3 MB | 226.27 ms | 528.41 ms | 540.15 ms | 277.92 ms | 0.67 |
| 32 | 480 | 0 | true | 59.5 | 544.3 MB | 128.22 ms | 608.82 ms | 609.24 ms | 480.58 ms | 1.14 |

Expert union per beat, measured vs independent draws (256 x (1 - (1 - 8/256)^N)): N=2 12.7 vs 15.8; N=4 19.8 vs
30.5; N=8 30.5 vs 57.4; N=16 44.8 vs 102; N=32 59.5 vs 163.3. (Those traces are one prompt family, correlated rows: eight DIFFERENT real prompts give 54.7 at N=8, see the eight-conversation receipts below.)

Reading:
* Gate PASS in every cell: each head identical to the exact forward at all 12 positions (32 of 32 heads at N = 32),
  0 misfires, 0 expert reads at open.
* Sharing is real, and it is the point of H6: at N = 32 the heads need 59.5 experts per beat, 0.364 of what
  independent draws would (calc:37e83101), so one expert copy serves 4.3 heads (calc:085b073c; 2.86 at N = 16
  calc:e71c2375, 2.1 at N = 8 calc:13146300). Prompts on one base route to the same experts far more than chance.
* Bytes per beat are flat by construction: one schedule (the 32-head plan, 544.3 MB per beat, 21.77 GB per stroke
  calc:4fac99fb) serves every cell, so the fill is identical at N = 1 and N = 32 and the cells differ only in what the
  piston does with it. The price is paid at N = 1: a 4.68 s token (calc:0517a33b) against the trace's 0.8 s, because
  that cell fills 544 MB per beat where its own 8 experts would be 25.2 MB (1.007 GB per token, calc:4c541485). The
  32-head union is 187.2 MB per beat (calc:0d274014); a per-N plan is the next experiment.
* The host piston is the limit from N = 4 up: the ignition gap (fire start minus fill done) is 30 us at N = 1
  (fuel-bound, beat = fill) and 480.6 ms at N = 32 (the fuel waits for the arithmetic). Fire grows 8.12x for 32x the
  heads (calc:d414dff1): 74.9 ms per head-beat at N = 1, 19.0 ms at N = 32, 3.94x less per head (calc:bb80aae8),
  because the trunk is read once per beat and each expert once. Total tokens per second rise from 0.21 to 1.14,
  5.33x (calc:7cb0d299); a 32-head stroke is 24.4 s for 32 tokens (calc:20f527b9). This is the shape the chip needs:
  one fill, many heads, and the math engine the only thing that grows with N.

### F. The motor on the chip (power engine = L306's 35B kernels, served first)
Drake, 2026-10-21 01:15Z: the motor IS the 35B server, and serving comes first. The power engine on the chip is
therefore L306's 35B kernels (`aien-sovereign-core` branch `q36/serve`, continued on `motor/serve`; omega GPU lib
sha 678df626, the same lib the pinned server binary `bin-1cf8fb09` was built with), gated for identity against the
host goldens before anything else fires them. The order: (1) the identity gate on the chip; (2) the same build served
on 18091 behind the OpenAI-style door with an HTTP gate; (3) only then the piston of section 8 fires those kernels on
this schedule with cylinders filled by the movement engine, strokes of N conversations on the chip, head swap, levers.

**Gate 1, the mathematics** (`qwen38_omega_full`, sc `motor/serve` 71ab16c6): teacher forcing over the golden's
generated tokens (`evidence/q36-host-20261020/gen-p1..p4.json`, 32 tokens each), one 40-layer window so the whole
35B is resident, the chip's argmax at every position against the golden's token. The rule: the words are identical,
except at a position where the host reference's own top-2 logit margin is below a band written in advance (0.1 logit,
`MARGIN_MIN`): a decision the reference itself made by less than that may land the other way under a different float
order, so such flips are counted, reported and allowed. A flip at a wider margin is a wrong kernel and fails, and the
chip's greedy choice must agree with the reference at every position (prompt included) outside the band. The band is
never taken from the run under test: the first cut used the run's own max logit error as the band, and the mutant
(max error 23.8) excused its own 22 flips and passed (01:58Z); the coordinator caught it and the rule was fixed
(71ab16c6) before any receipt was summarized here. Routes: the device keeps one ids slot per router, so after a token's
graph only the last layer's picks remain; the routes half of the gate rests on L306's router probe
(`q36_router_check`, 6,520 rows identical, weights within 6e-7, `sc-q36/evidence/q36-chip-20261020/`). Controls:
the shifted-by-one reference must be caught, the host reference must equal the golden, and the mutant
(`AIEN_Q36_TEST_MUTANT=4`, the shared expert dropped from COMBINE) must fail the same rule.

**Open precision item (parked with L306, recorded here, not gated):** the cosine floor. L306's chip receipts put the
window's cosine(min) at 0.99953, 0.99487, 0.99953 and 0.99892 on p1 to p4 (`sc-q36/evidence/q36-chip-20261020/
gate1-p*.json`); whether the gate should hold 0.9999 or accept 0.995 is unanswered. The tool prints and records
whether 0.9999 held; the words rule above is the gate. On p4 the chip flipped position 11 (margin 0.0096, inside the
written band): allowed. The correct runs' vocabulary-wide max logit error reaches 2.425 (p2) while the words stay
identical: the error at the top two is far below the vocabulary max, which is why a vocabulary-wide error cannot serve
as the band.

**Gate 2, the door** (`~/handoffs/motor-scripts/gate2-http.sh`): one greedy chat completion through `/v1/chat/
completions` (thinking off, 24 tokens, `aien_return_token_ids`), its 24 ids compared with `gen-chat-max-24.json`;
run first against the mutant build on 18093 (must be red), then against 18091 (must be green).

Status 02:10Z: **18091 serves the 35B** (`qwen3.6-35b-a3b-fp8`, pinned `~/.local/state/aien-q36/bin-5b726818`, sc
`motor/serve` 5b726818: the 35B chat template was unknown to the tokenizer and L306's pinned binary died at start; the
template is now pinned as `ChatTemplate::Qwen36` with a renderer checked against 48 jinja2 oracle cases and 5 refusals,
and the golden prompt tokenizes to the reference's 24 ids). Gate 2: the mutant server on 18093 went red (ids differ),
the real one on 18091 green at 01:58:28Z (24 ids identical to `gen-chat-max-24.json`); `ledger note L305/L306`
written so Renda points at it. Gate 1 first pass (bin 8f86188a, receipts `gate1-p*.json`): p1..p3 32/32 words
identical, p4 31/32 with the one flip at margin 0.0096, cosine(min) 0.99953 / 0.99487 / 0.99953 / 0.99892 (floor
0.9999 not held, recorded); re-judged under the written band: p1..p3 strict, p4 allowed, mutant 4 FAIL (22 flips,
smallest margin 0.214). Rerun with the fixed tool (bin 7c75dd62, `gate1b-*.json`), red before green: the mutant FAIL first (exit 1, 0 flips inside the band, 22 beyond, 02:04:30Z), then p1..p3 32/32 and p4 31/32 with its one flip inside the band, every run PASS by 02:08:54Z. Chip time
per token on the plain path (one token per command list, shared chip, no window, `chip_ns` from the gate runs),
medians: p1 21.864 ms = 45.74 tok/s (calc:131a3962, calc:593290c1), p2 22.989 ms = 43.50 tok/s (calc:d18b9629,
calc:e9362744), p3 21.990 ms = 45.48 tok/s (calc:78354ed1), p4 22.601 ms = 44.25 tok/s (calc:dde6add0); the
coordinator's read through the door at 02:04Z: 42.0 to 44.4 tok/s decode, 0.67 of the 63.25 tok/s ceiling at
today's bytes. Receipts: `aien-sovereign-core` `evidence/motor-chip-20261021/` (README there).

**Stroke v1 on the chip, done (sc `motor/serve` 5637a828 + 3dbbad4d over omega `motor/serve` a8036530, receipts
`evidence/motor-chip-20261021/stroke-*.json`, README there):** `create_heads` lays out four slots of `x`, `hidden`, `logits`,
`token` and `xq`; each head owns a DeltaNet state, an attention cache and a position; `stroke(tokens[4])` records every head's
layer l before layer l + 1 and submits once; `swap_heads(a, b)` re-points two slots (no copy). The first run caught a real bug:
the fused COMBINE|NORM carries the next layer's normalised input in one scratch slot across layers, so with heads interleaved
every head read the last head's value (head 0 differed from the one-token path at every position under the same mutant);
that slot is per head now, and head 0 under the mutant equals the one-token path 36/36. Gate, red before green: mutant 4 FAIL
on every head; swap-control (states of slots 0 and 1 exchanged at step 18, feeds not) FAIL from step 18 on while the untouched
heads stay identical (p3 32/32, p4 31/32 with its known in-band flip); the real run PASS (p1, p2, p3 32/32, p4 31/32 with that same in-band flip; every head equals its
single-head run token for token, `gate1b-p<n>.json`); swap as a feature (feeds move with the states) PASS, p1 ends in slot 1
and p2 in slot 0, all four still identical: a conversation follows its state and cache, not its slot. Chip time per stroke
(shared chip, four tokens per stroke, a receipt only under `spark-window`): real run median 92.887 ms per stroke of four
tokens over 46 strokes = 43.06 tok/s across the heads (calc:9c07e26f), 23.22 ms per token-equivalent (calc:3ae78e68),
against 21.86 to 22.99 ms per single-head token on the same shared chip: a v1 stroke costs about four single tokens, as
built. An observation next to the 18091 server, not a receipt; the window line was stuck during these runs. Weights are still read once per head
per stroke (v1); v2 batches the dense GEMVs at M = 4 through the prefill GEMM.

**Eight conversations per command list, done (omega `motor/serve` 52dd18a8: list cap 4096 launches, eight heads; sc
`motor/serve` 4e0d36d2 and the evidence commits after it, receipts `evidence/motor-chip-20261021/stroke8b-*.json`,
`stroke1-*.json`, `stroke{1,2,4}-real.json`, README there):** Drake's "eight at once". Four new goldens from the host
reference (a Rust function, a prose paragraph, a hiking chat, a C code review) join the four fact/code/science/list
prompts. Red before green: mutant 4 FAIL on every head, swap-control FAIL from step 18 on with the untouched heads
unchanged; then the real run PASS on all eight and the head swap as a feature PASS, and every head's words equal the same
conversation run alone on the chip, eight of eight. One prompt (the first prose paragraph) fails the written band rule at
its 63rd position with the same words alone as in the stroke, 67 per-step lines identical: that is the one-token chip
precision (cosine 0.9914 at one step, a 0.98-logit error at the flip), not the stroke; recorded, handed to the precision
lane (L313), and a second prose prompt that passes alone took its place. The ladder, same command and binary back to back
on the shared chip (not a speed receipt): 24.380, 49.096, 97.805, 194.143 ms per stroke at N = 1, 2, 4, 8, which is 24.3
to 24.6 ms per token at every N (calc:eab1e7fb, calc:946a5354, calc:73a5fb9b, calc:dbf658ca) and 41 tok/s total whatever
N is. v1 is linear, as built: each head reads the weights once per stroke. Expert union per layer per beat on the eight
DIFFERENT real prompts, from the host's recorded routes: 54.7 of 256 (min 33, max 63) against 57.42 for independent draws
(calc:aa517f3a); four chat prompts 29.7 against 30.53. Different conversations route almost independently; the 30.5 at
N = 8 in the stroke table above came from one prompt family (correlated rows) and is the optimistic case. The byte budget
for eight at once must count about 55 experts per layer per beat.

**v2, the ignition (next, in this order):** (1) a record path for the segment-table FP8 GEMM (`omega_gpu_fp8gemm_api`,
bit-identical to the GEMV per row), grid_y = the largest segment count, and a guard so an empty segment (cnt 0) is skipped
(today its pass loop is do-while); (2) a chip-side "group" glue kernel that turns the N routers' picks of a layer into the
segment table, the permuted x' rows and a row-to-head map, so the expert GEMM streams each distinct expert once for all the
heads that chose it and the dense mix/o/lm_head rows once for all heads; (3) the stroke records layer-major with the batched
pieces, GDN/attention stay per head; (4) gate: the same eight-conversation gate, every head equal to its run alone (the GEMM's
bit-identity makes this exact), then the ladder under a `spark-window`. Expected from the bytes (the coordinator's model with
union 55): about 11 GB per beat of eight against 8 x 2.6 GB today.

**Finding, the door and the chip lock:** `aien-serve-http` takes `/tmp/aien-gb10.lock` for the model load and around
every generation (`omega_engine.rs`: waits 30 s, then answers HTTP 503 busy); it never holds the lock idle. While
another agent's chip job holds the lock (a gate run, training, the judge), 18091 waits up to 30 s and then refuses.
Serving Drake and Renda reliably needs either a chip reservation for the server or the other jobs yielding; callers
retry on 503.

**Next, step (3), the stroke on the chip (design, from `omega_gpu_q36_graph.c`):** today one token is one command list:
`begin_token` (the embedding into the single `x` slot), `record_layer` x 40 (GDN layer 10 launches, attention layer 11;
the AB bf16 GEMV and the MIX fp8 GEMV overlapped; ROUTER top-8 plus the shared slot; GATE_UP fp8 grouped over 9 groups;
SWIGLU; DOWN; COMBINE|NORM), `record_final` (NORM, LM_HEAD, ARGMAX x 2), `end_token` (one submit, counters, the token
read). The layout holds one `x`, `token`, `logits` and `hidden` slot, and the conversation state lives in objects the
graph only points at (`cfg.gdn`, `cfg.attn`, `cfg.router`). Stroke v1 = N heads per command list: the layout gains N
slots for `x`, `hidden`, `logits`, `token` and the per-layer scratch, `begin_stroke(x[N])` / `record_head(h, layer, pos)`
/ `end_stroke(token[N])` record the same 40 layers N times with a per-head slot and a per-head state object (head swap =
pointer swap, no copy), and one submit fires the stroke; the weights are read N times in v1 (the chip's cache keeps the
expert rows warm between heads inside one stroke, measured not assumed), the state objects once each. Gate per head:
the same words rule on p1..p4 interleaved (head h runs prompt h), negative control = mutant 4 or two heads with their
state objects swapped (must be caught). v2 = the dense GEMVs batched at M=N through the prefill GEMM and its plan
builder (today hardcoded to the 30B's 768/128 shapes; the 35B needs 2048/512), one weight read per stroke, which is the
H6 number of section 7 measured on the chip. Timings only under `spark-window open --wait` (ticket `l312-piston`).

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
* The chip piston is built and proven in its host simulator (section 8). Under Drake's 01:15Z order its GB10 run
  comes third: after the identity gate on L306's 35B kernels and after 18091 serves (section F). Every chip timing
  comes from inside a measurement window (ticket l312-piston still in line); correctness runs take only the chip lock.
* The fast-memory engine (L2 / shared memory staging) and the math engine exist only on the chip; the engine-count
  series on the host stops at 2 and continues there.
* The thesis gate (planned loop vs today's path, identical tokens, at least as fast at every context) needs the chip
  piston; on the host the planned loop is slower than the resident path by physics (10.6 vs 245 GB/s tiers), and the
  doc says so instead of arguing.
* On the 35B (the motor): sections B, C, D and E have receipts (host loop identity gate, 12 engine-count cells with
  the staging fix, head swap, strokes to 32 heads). The stroke's fill was the 32-head plan at every N; a per-N plan
  would show the fill shrink with N and is listed below. No 30B cell runs again.

## Next discriminating experiments
1. Movement engine at the NVMe ceiling: 3.0 to 3.4 GB/s measured against 10.6 GB/s possible. Variables, one per run:
   worker count (8 -> 16), one contiguous beat-ordered file instead of per-expert ranges in shards (fewer, larger
   reads), and a quiet box. The receipt is payload GB/s during fill vs the ceiling.
3. The RAM stage re-verify: answered from the receipts (section C): 0.625 of the RAM stage is the second BLAKE3 verify.
   Dropping it would break the ring's rule, so the host two-engine path stays as it is; the variable worth a run is
   the chip's second stage (section F), where the copy is the chip's own and the check rides with it.
4. The chip piston: the same schedule, cylinders in GPU-visible memory, a resident kernel; the receipt is the same
   timing diagram with fire times in microseconds instead of milliseconds, and the thesis gate against 18091.
5. The stroke with a per-N plan: plan and schedule over the N traces the cell runs, not all 32, so the fill per
   beat is the measured union (25.2 MB at N = 1, 187.2 MB at N = 32) and bytes per token fall with N instead of
   staying flat. The receipt is the same table with the fill column moving and the gate still true in every cell.
