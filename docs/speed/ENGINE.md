# The engine: two machines, one schedule, one heartbeat (L312, 2026-10-20)

Drake's picture, in his words: "Think of a car engine." The calculation way (the bytes) and the math cores (the
arithmetic) are two separate machines in one body, wired before the key turns, and both geared to one crankshaft:
"boom, boom, and the pistons going back and forth."

## The parts, named

| Car part | Engine part | Code | What it does | What it never does |
|---|---|---|---|---|
| Camshaft and firing order | **The schedule** | `crates/aien-engine/src/schedule.rs` | One static table, built once per model and placement plan: which weight part lands in which buffer at which beat, and which buffer each beat fires on. Checked on the host before anything runs: complete (every part a beat fires on was delivered to that buffer earlier and not overwritten since), acyclic (every delivery happens strictly before the beat it feeds), collision-free (a buffer is never filled while it fires). | Decide anything at run time. No allocation, no compile, no lookup that was not precomputed. |
| Intake, fuel lines | **The movement engine** | `crates/aien-engine/src/movement.rs` on the heartbeat ring (`crates/aien-heartbeat`, L190) | Delivers the bytes for a beat along the schedule: reads straight from the NVMe (page cache bypassed) into the buffer, checks every part's fingerprint (BLAKE3) before the buffer is published, fails the whole ring closed on any error. | Compute. It does not know what the bytes mean. |
| Pistons | **The power engine** | `crates/aien-engine/src/piston.rs` (host piston today; chip piston next) | Fires on a buffer that is already full: the model's own routing picks the experts, the arithmetic is the exact per-expert reference (bit-equal to the resident reference, L190's gate 64/64). An expert the model wants that is not in the buffer is a misfire, and the run fails closed. | Fetch. It never asks the ring for anything the schedule did not place. |
| Crankshaft | **The crank** | `piston.rs::Crank` | Issues the next fill before the current beat fires, then hands the piston the stamp of the fill that feeds it. Two buffers in flight = pistons alternating. | Reorder beats. |
| Timing light | **The timing diagram** | `crates/aien-engine/src/timing.rs` | Per beat: fill issued, fill done, fire start, fire done. Ignition gap = fire start minus fill done. Positive: the fuel waited (early). Negative: the piston waited (late, a misfire by Drake's definition). Text table and PNG, our own encoder. | Average away the spread: every series carries min, p10, median, p90, max and (p90 - p10) / median. |

The placement plan (which parts go to which tier) is L308's; until its measured plan exists the engine builds a
synthetic one and says so in the file (`synthetic: true`). The tier table (what each tier moves, measured) is L151's
`docs/MACHINE_CEILING.md`; nothing here re-measures it.

## One physical fact that shapes everything on this box

On the GB10 the host RAM and the GPU memory are the same 128 GB of LPDDR5X. "Load from RAM into GPU fast memory" is a
copy inside one pool, or no copy at all: a buffer the host fills is a buffer the GPU reads (omega's session memory is
host-visible and GPU-uncached). So the tiers that exist here are: NVMe (10.6 GB/s measured), the shared memory
(245 GB/s measured, GPU reading), and the chip's own L2 (24 MB) and shared memory per block. The whole 30B at 8 bits
(31 GB) sits in the shared memory today, so a head delivered from the NVMe per beat can only be slower than leaving it
where it is; the movement engine earns its keep when the heads do not fit (many models, many heads, small machines),
and its first job on this box is to prove the mechanism with the timing lines, not to beat the resident path.

## Receipts

(filled in below as each measurement lands; every number names the model, the code sha and the raw file)
