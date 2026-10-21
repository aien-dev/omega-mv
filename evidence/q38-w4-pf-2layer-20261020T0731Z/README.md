# 4-bit reading routine: load-ahead (pf) and grid knobs, 2-layer graph timeline (L127, 2026-10-20)

Window 20261020T073108Z-0beaa5 (gb10 exclusive). Base = omega dd5e89ff, new = 7905186f (binary hashes in ground.txt).
Script win1.sh: chip tests (pf 0, 1, 2 and base), then the 2-layer graph timeline for six setups, three rounds in rotated order.

## Result
Load-ahead gives no measurable gain. Base and pf 0 run the same instructions (program hashes match), and they differ by up
to 5% between rounds, so the run-to-run spread is about 5%. Every pf and grid setup lands inside that spread.

Median of three rounds, GB/s per step (fraction of the 273 GB/s memory wall):

| setup | mix | o_proj | gate_up | down | lm_head |
|---|---|---|---|---|---|
| base | 186.3 | 170.8 | 207.7 | 190.3 | 199.5 |
| pf 0 | 201.6 (0.738) | 179.6 (0.658) | 220.9 (0.809) | 198.5 (0.727) | 210.5 (0.771) |
| pf 1 | 198.2 | 175.2 | 217.9 | 202.8 | 214.5 |
| pf 2 | 191.9 | 168.1 | 212.4 | 196.6 | 211.6 |
| pf 1 + 96 CTAs | 200.5 | 169.6 | 212.8 | 198.1 | 205.3 |
| pf 1 + 96 CTAs + rows 1 | 200.5 | 167.1 | 213.4 | 196.7 | 210.5 |

The four weight steps together (round 1 times): pf 0 207.3 GB/s (calc:c64faeff), 0.759 of 273 (calc:b15dc1a1);
pf 1 205.9 GB/s (calc:2e1df933). Yesterday's figure was 209 GB/s (calc:832d43a7).

Correctness: every chip check passed (base 11, new 17 per run), pf 1 and pf 2 give the same bytes as pf 0 at every 27B
shape, and the pf mutant (decode without waiting for its loads) was caught on the chip. All 18 graph runs: 37 checks, 0 failed.
The one-shot chip timings in chip-table.txt are rounded to about 102 us steps by the timer and are not used for ranking.

## What this means
The 4-bit routine already keeps enough reads in flight: holding a second step in registers does not move it closer to the
wall. The FP8 routine reaches 4 to 10% more per step with its "deep" mode (input vector staged in shared memory, several
steps in flight with longer contiguous reads). That is the next lever to try for 4-bit; it is not built or measured here.
