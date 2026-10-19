# Flash-Next M3 kernels: host and simulator results (2026-10-19). NOT CHIP RESULTS.

Four new Omega kernel families for Qwen3.8-Flash-Next, all new files (no existing kernel touched):

| family | what | files |
|---|---|---|
| fnattn | decode attention, 24 q heads over 2 KV heads, head_dim 256, partial RoPE, SPARSE list of attended positions | omega_blackwell_codegen_fnattn.*, omega_gpu_fnattn_api.*, tests/gpu_fnattn_test.c |
| fnhc | the 4-lane gated residual phases (norm, silu(d/4), combine, inject) | omega_blackwell_codegen_fnhc.*, omega_gpu_fnhc_api.*, tests/gpu_fnhc_test.c |
| fnidx | QSA indexer: block scores and EXACT top-512 selection (ties to the lower index), list with the tail | omega_blackwell_codegen_fnidx.*, omega_gpu_fnidx_api.*, tests/gpu_fnidx_test.c |

What was run: `--host-only` (codegen budgets, refusals, oracle self-checks) and `--sim` (the kernel program through the repo's warp
simulator tests/bw_warp_sim*.h against double-precision or exact host oracles, with mutants). The logs are `*-host.log`, `*-sim.log`.
They were run on a Mac against the generated instruction programs because the Spark's build queue was starved; the simulator is
host C, so the result does not depend on the machine, but a simulator pass is never a chip result. Chip runs of all of these
(and of the NV4 and router kernels) are queued in `tools/run_flashnext_chip.sh` and are NOT MEASURED yet.

## Results
- fnattn: host 54/54, sim 23/23 (dense contexts 1 to 701, sparse lists like the indexer's with and without the new token's own
  block, 1 to 3 splits, per_split 256 and 512, both fragment orders; mutants 1..8 all caught).
- fnhc: host 31/31, sim 10/10 (every phase vs the double oracle, worst relative error 1.6e-5; mutants 1..6 caught).
- fnidx: host 22/22, sim 23/23 (SCORE vs double oracle; SELECT list equal word for word to an exact host selection of the same scores
  for nb 1 to 6000 including heavy ties; mutants 1..7 caught).

## fnidx SELECT mutant 5 (found by the coordinator, 2026-10-19)
Mutant 5 = the threshold search never tries bit 0 of the K-th key. The first test fed it random DISTINCT scores with the K-th key
forced odd and it was not caught ("2050 words equal"). Reason, from the code: the search then lands on T - 1. With distinct keys
no key equals T - 1, so the keys above T - 1 are exactly the K keys >= T, Eneed = 0, and the output is identical: the mutant is
EQUIVALENT on distinct keys, so that input could not detect it. It is NOT equivalent in general: when several blocks share an odd
K-th value, count(> T - 1) exceeds K and the kernel selects too many blocks. Fix: a new input kind (heavy ties on values whose bit
pattern is odd, `make_scores kind 3`) now drives mutant 5 in the sim and on the chip, and the test also CHECKs that mutant 5 on
distinct keys equals the honest list (so the equivalence is a tested fact). Result: caught (list differs at word 1112: got 1880, want 1888).
Real-model relevance: exact ties at the cut among nonzero scores are rare but possible, so the shape exists; no refusal is needed
because the honest kernel handles ties exactly (kind 1 and kind 3 pass).
Also changed: mutant runs get an output buffer big enough for "selects too many blocks" (it used to fault out of the buffer).

## Limits (said plainly)
- The indexer KEYS phase (norm + RoPE of the index query and key, pooled block key append) is not written; SCORE takes ready q and
  block keys. SELECT is exact for nb up to 65536 (the model's context) but its speed is NOT MEASURED; it reads the scores from
  GPU-uncached memory 31 times.
- fnattn and fnidx are not yet wired into a layer; the 27B graph is untouched.
- Hyper-connection weights are bf16: 96 sub-blocks x 13 MB = about 1.26 GB read per token, which the moonshot-c byte count
  (4.82 GB per word) does not include. ON PAPER that lowers the 273 GB/s ceiling from 56.6 to about 45 tok/s unless those
  weights are quantised (arithmetic: 2 x 320 x 10240 + 4 x 10240 = 6.6M params per sub-block, x 96 = 0.63B params, x 2 bytes).
