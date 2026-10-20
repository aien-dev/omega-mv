# SPEED-7: 8-bit attention memory for Qwen3.8-27B decode (ledger L131, 2026-10-20)

What was built: the decode attention step for the 16 full-attention layers can now keep its memory of earlier
tokens (the KV cache) in 8 bits per number instead of 16, for both halves (K and V), V only, or K only.
An 8-bit row is 256 e4m3 bytes plus one power-of-two scale. Code: omega `3efab8c1` (kernel, launcher, test).

## Verdict

- Correctness of the kernel: every mode matches the double-precision reference on the chip, and every
  deliberately broken variant is caught (OBSERVED, files below).
- Quality on the whole model (measured in aien-sovereign-core, drift gate fixed before any run):
  8-bit K and V together FAIL (3 of 6 texts), K only FAIL (4 of 6), V only PASS (6 of 6). 8-bit K loses
  sharp "copy an earlier passage" attention; V does not need that precision (INFERRED from where the loss sits).
- So only "V 8-bit, K bf16" (mode 2) was a candidate to ship, on condition that it was timed against bf16 first.
  That timing is NOT MEASURED: the one window for it shared the GPU with other work (2.58 times spread between
  repeats), and the 27B model was retired on 2026-10-20, so it cannot be repeated. Mode 2 is not shipped.
  Whole-model results: aien-sovereign-core `qwen38/fp8-kv`, `evidence/qwen38-speed7-kv8-20261020`.

## Files

| File | What it shows |
|---|---|
| `host-only.log` | 179 checks without the chip: code generation for every mode, depth and mutant, refusals, the 8-bit rounding against the simulator's model on every bf16 pattern |
| `sim-{bf16,kv8,kv8v,kv8k}.log` | the instruction-level simulator: each mode PASS, every mutant caught |
| `sim-{kv8,kv8v,kv8k}-c9c7251f.log` | the simulator again after c9c7251f: 8-bit mutant 3 caught by its output |
| `chip-k3-*.log/.json` | the chip, every mode: bf16 30/30, kv8 34/34, kv8v 33/33, kv8k 33/33, and the 128k point per mode |
| `chip-k2-*` | the 128k slowdown check: 32k of context in a cache sized for 128k |
| `chip-k4-*` | timing repeats (50 reps, two rounds in reversed order) under heavy box load |
| `*.ground.txt` | binary and library sha, git heads, free memory, load average at the run |
| `*.receipt.txt` | wall-power receipt line from spark-receipt |

## Speed of the kernel alone (one layer)

Measured against the chip's 273 GB/s memory ceiling.

- bf16, 32k context in a 128k cache: 1.949 ms, 68.9 GB/s = 0.252 of the ceiling (calc:06c4ff4b). bf16 at 128k:
  7.50 ms, 71.5 GB/s = 0.262 (calc:6c994d71).
- 8-bit K and V, same 32k point: 1.565 ms, but only 43.5 GB/s = 0.159 of the ceiling (calc:8e2ac86c). Half the
  bytes buy about a fifth less time: this kernel is not limited by memory traffic (OBSERVED), so the smaller
  format saves less than its size suggests. The biggest lever left here is the kernel reaching more of the
  ceiling, not the number format.
- The 128k "8-bit is slower" reading from the first window (12.76 ms) did not repeat: 7.37 ms at 128k and an
  unchanged 32k time in a 128k-sized cache. Not a layout effect; the first reading was mostly box noise.
- Box load during these runs was 70 to 100 (other agents' builds sharing the memory bus). Repeats of the same
  point differ by 2 to 5 times (k4: bf16 32k 6.37 and 10.51 ms against 1.95 ms when quieter). Treat single
  kernel timings in this folder as noisy; the load is recorded beside each run.

## Not done here

- The prompt reader (prefill) still writes bf16 only; any 8-bit side is decode only, and prefill refuses it.
- Mutant 3 (new row not written) in 8-bit mode was caught only by a refusal at build time in these runs. c9c7251f
  makes it launch so the wrong output must catch it: the simulator now catches it by output in all three 8-bit modes
  (`sim-*-c9c7251f.log`, 2026-10-20 21:3xZ, 18 checks PASS each); the chip recheck (k5) was queued
  at close-out and its result goes to ledger L131, not into this folder.
- Shared change: the code generator's instruction cap went from 1024 to 2048 (the mixed kernels are about
  1040 instructions); the gdn, tern, prefill attention and fp8 host checks still pass.

CALC-EXEMPT: figures without a calc id are measured readings copied from the logs in this folder.
