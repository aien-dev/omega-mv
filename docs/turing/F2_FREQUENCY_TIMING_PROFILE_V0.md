# F2 FREQUENCY timing profile v0 (omega-mv#570)

Pre-registration. Written and committed before the held-out trace set exists. Part of the FORTRAN 1956 program
(aien-architecture-mv#274, idea 2): the 1956 FREQUENCY statement let the programmer give "his estimate, for each
branch-point of control, of the frequencies with which the several branches will actually be executed". Here the
odds are measured, not estimated, and the question is whether they predict kernel time better than the
instruction count does, scored with the Turing protocol T(M; B, D) = [L(B) + L(D|B)] - [L(M) + L(D|M)].

## The branch

Qwen3.8-27B decode attention (`src/omega_blackwell_codegen_attn256.c`, V pass): per step, per warp, the rescale
block (6 MUFU.EX2 + 30 FMUL) runs only when some head's running max moved. Warp-uniform, data dependent.
`OmegaDecodeAttn256Spec.rescale = 1` builds the same kernel without the test (always rescale, same output bits).

## Measurement

- Tool: `tests/gpu_attn256_freq_test.c --chip-traces`, one TSV line per launch, `elapsed_ns` = marker-to-marker chip
  time of the launch. Timed program: the default kernel (mode 0, byte-identical to base 6e24f7fd) and mode 1
  (rescale 1, used for the scheduling step only). The counting build is never timed.
- Odds covariate: share = R / S, rescale steps over V steps of the launch, from the host replica of the branch (a
  pure function of the inputs). Chip counters matched the replica in every warp (chip-check.log, 29/29).
- Inputs: classes `rho<permille>` (a new max with probability rho per warp step) and `iid`; positions
  1023, 4095, 16383, 65535; per_split 256; 1 warm-up launch per (class, position, mode) block discarded.
- Train set: tag `train`, seed 101, classes rho0, iid, rho250, rho500, 25 reps per (class, position, mode).
- Held-out set (sealed: its sha256 is committed before `score` runs on it): tag `held`, seed 202, the same four classes
  (in-distribution block) plus rho750 and rho1000 (the intervention block: same code, input distribution changed so
  the odds flip from at most one half to three quarters and one).

## Models and code (tools/f2_timing_score.c)

- B, instruction count: t = mu_B[pos]. At fixed branch odds the instruction count of a launch is a function of the
  position alone, so one free value per position is everything a pure instruction-count model can express.
- M, frequency: t = mu_M[pos] + g[pos] * share.
- Fit on train mode-0 lines only: least squares, rounded to integer ns. Frozen in `model.txt`.
- Data code: r = round((t - prediction) / 1000 ns), zigzag, Rice code with parameter k fitted on train (5 bits).
- L(B) = 5 + 8 + npos * (32 + 32) bits; L(M) = L(B) + npos * 32 bits (a deliberate, conservative 32 bits per
  coefficient). Lengths and T through `src/turing/ty_math.h` (ty_bits_to_ub, ty_dl, ty_gain), micro-bit exact.

## Decision rule (fixed now)

The frequency term earns its keep only if all of these hold on the held-out set:
1. T > 0 on all held-out mode-0 launches;
2. T > 0 on the intervention block (rho750, rho1000) alone;
3. negative control: the same score with the shares shuffled among launches of the same position (`--shuffle 7`)
   is lower than (1) and not above 0;
4. correctness: `f2_timing_score bits` reports 0 launches whose output bits differ within their (class, position)
   group, across both modes, on both files.

If it earns its keep, the scheduling step uses the measured odds to pick mode 0 or mode 1 per input class and
reports the measured time of both. If not, the result is reported as a null: the branch odds do not matter for
this kernel's time at these sizes, and the reason is stated against the bandwidth ceiling.

## v1 amendment (2026-10-19, before any held-out trace existed)

The v0 train set (`evidence/attn256-freq-20261019T0151Z/train.tsv`, one chip run per class, the two modes in
sequential blocks) showed run-to-run drift of the machine larger than any branch effect: at position 4095 the same
default kernel took a median 408 us in the first two runs (rho0, iid) and 306 us in the next two (rho250, rho500), and
mode 1 minus mode 0 swung from -44 to +101 us at 16383 within one run. Because class was tied to the run, the v0 fit
turned that drift into a share coefficient (g = -188155 ns at 4095). That design cannot separate the odds from the
clock of the box, so it is replaced before the held-out set is collected. The v0 train file is kept as the evidence.

v1 changes only the collection order; models, code, prices and the decision rule are unchanged:
- `--chip-traces-mixed`: all classes in ONE chip run, each in its own cache layer (class-to-layer order shuffled by
  the seed), one shared V history; each rep runs both modes in a shuffled order (one discarded warm-up launch after
  each kernel switch) and inside a mode every (class, position) pair in a shuffled order.
- Train: tag `train1`, seed 111, classes rho0, iid, rho250, rho500, 40 reps. Held-out: tag `held1`, seed 222,
  classes rho0, iid, rho250, rho500, rho750, rho1000, 40 reps, sealed by sha256 before scoring.

## Result (2026-10-19, evidence/attn256-freq-20261019T0151Z, repeat evidence/attn256-freq-20261019T0159Z-repeat)

| block | T, run 1 (bits) | T, repeat (bits) |
|---|---|---|
| all held-out (960 mode-0 launches) | -195 | -59 |
| intervention rho750 + rho1000 | -193 | -52 |
| in-distribution | -130 | -135 |
| shuffled control | -182 | -39 |

Output bits: 0 differing launches in 3200 (run 1) and 3200 (repeat), both modes. Rule 1 and 2 fail, so the frequency
term does not earn its keep and the scheduling step (pick the branchy or the branchless rescale by odds) is not taken:
the default kernel stays as it is. Across odds 0.016 to 1.0 the time moves less than the box's run-to-run noise.
The kernel is far from the bandwidth ceiling (0.23 to 0.25 of 273 GB/s), so its time is set by something else
than this branch; that is the speed lead this study hands on.
