# 35b-stroke-20261021T0035Z: one base, many heads, one stroke on the 35B (H6)

`run-stroke-35b.sh`, binary sha256 0367f0be4341f76c for every cell (stamped per line in `run.log`), cells pinned to the
X925 cores, load 17 to 39 at each start. Model: Qwen3.6-35B-A3B (fp8 shards). The 32 prompts in the script were each
run through the exact 35B forward one token at a time (`trace35 --prompt`, 20 prompt tokens, `--positions 12`: the
first 12 positions; positions 0 to 2 are the chat template and identical for every prompt), giving 32 traces of
tokens and routes (`traces/tNN.json`, `tNN.side.json`; host 677 to 1698 ms per token, median 804 ms, shards from the
file cache). Then `union` (measured expert union per beat vs the independent-draw expectation, `union.json`),
`plan union` over all 32 traces (`plan-union.json`: per-layer head sizes 151 to 223 experts), one `schedule`
(`schedule-union.json`, 480 beats = 12 strokes x 40 layers, 6998 parts, 2 buffers x 223 slots x 3146112 bytes,
`schedule-union.check.json` PASS: complete, acyclic, no collision, edges exact), then `stroke35` at N = 1, 2, 4, 8,
16, 32 heads on that one schedule, 8 fill workers.

What a stroke is: N sequences (the first N traces) step one layer at a time; at beat (stroke t, layer l) the ring
holds the plan's experts of layer l, the trunk of layer l is read once for all N heads, every head's attention runs,
then every head's MoE runs with its experts copied out of the cylinder once per beat (a second head routing to the
same expert gets the copy). Gate per head: its token at every position equals its own trace (the exact forward's
token), and its routes equal the trace's routes. A missing expert is a misfire; the cell fails closed.

Table: `bash table-stroke.sh 35b-stroke-20261021T0035Z`; `stroke-nN/summary.json` holds the cell, `gate.json` the
per-head verdict, `strokes.json` every beat (union, bytes, attention and MoE time), `diagram.txt` the beat picture.

| N heads | beats | misfires | identical tokens every head | expert union per beat (mean) | bytes per beat (median) | fill | fire | beat | ignition gap | tokens/s total |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 480 | 0 | true | 8 | 544.3 MB | 114.33 ms | 74.94 ms | 114.5 ms | 0.03 ms | 0.21 |
| 2 | 480 | 0 | true | 12.7 | 544.3 MB | 140 ms | 133.48 ms | 144.87 ms | 0.18 ms | 0.3 |
| 4 | 480 | 0 | true | 19.8 | 544.3 MB | 149.18 ms | 201.33 ms | 201.42 ms | 49.66 ms | 0.48 |
| 8 | 480 | 0 | true | 30.5 | 544.3 MB | 200.59 ms | 312.74 ms | 334.21 ms | 107.15 ms | 0.56 |
| 16 | 480 | 0 | true | 44.8 | 544.3 MB | 226.27 ms | 528.41 ms | 540.15 ms | 277.92 ms | 0.67 |
| 32 | 480 | 0 | true | 59.5 | 544.3 MB | 128.22 ms | 608.82 ms | 609.24 ms | 480.58 ms | 1.14 |

Expert union per beat, measured mean vs the independent-draw expectation 256 x (1 - (1 - 8/256)^N):
N=1: 8 vs 8; N=2: 12.7 vs 15.8; N=4: 19.8 vs 30.5; N=8: 30.5 vs 57.4; N=16: 44.8 vs 102; N=32: 59.5 vs 163.3.

Reading (receipts as calc ids):
* Gate: every head identical to its trace at all 12 positions in every cell (32 of 32 heads at N = 32), 0 misfires,
  0 expert reads at open, 480 beats per cell.
* Bytes per beat are flat by construction: one schedule (the 32-head plan, 544.3 MB per beat median, 21.77 GB per
  stroke calc:4fac99fb) serves every cell, so the fill is the same at N = 1 and N = 32 and the cells differ only in
  what the piston does with it. The price at N = 1 is a 4.68 s token (calc:0517a33b) against the trace's 0.8 s,
  because the N = 1 cell fills 544 MB per beat where its own 8 experts would be 25.2 MB (1.007 GB per token,
  calc:4c541485). A per-N plan is the next experiment, not this one.
* Sharing is real: at N = 32 the heads read 59.5 experts per beat, 0.364 of the 163.3 independent draws would need
  (calc:37e83101), so each expert copy serves 4.3 heads (calc:085b073c; 2.86 at N = 16 calc:e71c2375, 2.1 at N = 8
  calc:13146300). The 32-head union is 187.2 MB per beat (calc:0d274014); 8 heads need 95.8 MB (calc:ee895c44).
* The host piston is the limit from N = 4 up: the ignition gap (fire start minus fill done) is 30 us at N = 1
  (fuel-bound, beat = fill), 49.7 ms at N = 4 and 480.6 ms at N = 32: the fuel waits for the arithmetic. Fire grows
  8.12x for 32x the heads (calc:d414dff1): 74.9 ms per head-beat at N = 1, 19.0 ms at N = 32, 3.94x less per head
  (calc:bb80aae8), because the trunk is read once per beat and each expert is copied once. MoE time per beat grows
  5.86x (calc:f175360b) and attention 7.78x (calc:9e532bee) over the 32x.
* Tokens: every head advances one token per stroke, so tokens per stroke = N; total 0.21 tokens/s at N = 1 to 1.14
  at N = 32, 5.33x (calc:7cb0d299); a 32-head stroke takes 24.4 s for 32 tokens (calc:20f527b9). On the host this
  is the shape H6 needs on the chip: one fill, many heads, the math engine as the only thing that grows.

Log cosmetic: the `traces done` line in `run.log` lists 64 "positions per trace" values with 32 zeros; the script's
glob `t*.json` also matched the `tNN.side.json` files (no `argmax` key, counted as 0). The 32 traces each hold 12
positions (`traces/tNN.side.json` positions 12). Fixed in `run-stroke-35b.sh` after this run (glob `t??.json`).
