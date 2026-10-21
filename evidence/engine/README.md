# evidence/engine (L312, the engine)

Receipts for `docs/speed/ENGINE.md`: every number in that doc comes from a JSON file in one of the folders below, written
by the script named beside it. Binary: `engine` from sovereign-core `crates/aien-engine` (branch feat/typewriter-engine).
No weights here; the models live in `~/models` on the Spark.
Drake, 2026-10-20 23:58Z: the motor is the 35B (Qwen3.6-35B-A3B). The 30B rows and folders are history (the stand-in used
while the 35B was brought up); no new 30B cell runs.

| Folder / script | What it measures | Model |
|---|---|---|
| `run-host-30b.sh` -> `30b-host-<ts>/` | The smallest full loop on the host: trace, plan, schedule + proof, movement alone (buffers 2/3, beat 0/2/4 ms), both engines with the identical-tokens gate, today's path | 30B |
| `run-engines-30b.sh` -> `30b-engines-<ts>/` | Engine count (1: NVMe -> cylinder; 2: NVMe engine -> staging pair -> RAM engine) x coupling (brigade, clock, reservoir) x buffers (2, 3): beat time, ignition gap, bytes/beat | 30B |
| `run-engines-35b.sh` -> `35b-engines-<ts>/` | The same cells on the 35B's expert files (40 layers x 256 experts of 3.146 MB) | 35B |
| `35b-plan-l308/` | Plans from L308's measured 90 percent heads (prose, code, math) and their schedule proofs (PASS) | 35B |
| `run-host-35b.sh` -> `35b-host-<ts>/` | The smallest full loop on the 35B host: L306 golden -> trace, plan, schedule + proof, the exact 35B forward with experts from the ring (identity gate: tokens and routes equal the golden), movement alone | 35B |
| (aien-sovereign-core `evidence/motor-chip-20261021/`) | The motor on the chip (2026-10-21): gate 1 identity on L306's 35B kernels (p1..p4, mutant 4 red, band written in advance) and gate 2 through the door on 18091 (greedy ids equal the golden, mutant server red); chip time per token on the plain path | 35B |
| `run-swap-35b.sh` -> `35b-swap-<ts>/` | H4 head swap mid-run on the 35B: L308's prose head -> code head at token 12 of 24, loader lead -1/0/1/2/4 | 35B |
| `run-stroke-35b.sh` -> `35b-stroke-<ts>/` | H6 on the 35B: 32 traces (exact forward, one token per step), union plan, strokes N = 1..32 layer by layer through the ring, one expert read per beat, identical tokens per head | 35B |
| `run-swap-30b.sh` -> `30b-swap-<ts>/` | H4 head swap mid-run: head B replaces head A at token 12 of 24, loader lead -1/0/1/2/4 tokens: extra time the user sees, hit rate after the swap | 30B |
| `run-stroke-30b.sh` -> `30b-stroke-<ts>/` | H6 N heads on one stroke, N = 1..32: tokens per stroke, bytes/beat flat, expert union vs N, identical tokens per head | 30B |
| `run-chip-piston.sh` -> `chip-piston-<ts>/` | The chip piston (omega `gpu_engine_piston_test`): power engine alone (pre) and both engines (live, host fills slots while the chip fires), chip clock stamps per beat | slot sizes of the 35B and 30B |
| `quiet-wait.sh` | Sourced by the run scripts: a measured cell starts only when the 1-minute load is under 50 | |
| `prompts-32.txt` | The 32 prompts behind the H6 traces | |

Folder suffixes: `-ABORTED-<why>` = stopped by the agent (reason in the name, partial log kept); `-INTERRUPTED-boot1` = killed by the
Spark reboot at 22:49Z on 2026-10-20 (Drake's attended native boot 1); nothing in them is a verdict. Each folder's `run.log` has
one line per cell with the load it started at and the summary numbers; each cell folder holds `summary.json`, `beats.json`
(every beat's stamps), `timing.txt` (the diagram) and `timing.png`.
