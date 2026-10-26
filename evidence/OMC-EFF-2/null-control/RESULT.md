# OMC-EFF-2 null control: A/A of omc befb8199 against itself

Protocol: `bench/omc_efficiency/PROTOCOL-2.md` sha256
`28e8d77308baf4da3fdfedd1c00721bc1b6df578c2d19634da218080e62adac5`, frozen in commit
`2b7f0a2ce4ff9fd813c701891f7499e0ce5c1be6`, which also correct `PROTOCOL.md`; that file is
unchanged at sha256 `ae1d1b2a…033a`. The run was declared in PROTOCOL-2 section 5 before it was made.

Command: `sh bench/omc_efficiency/run.sh --ab seed/out/omc seed/out/omc 20261028 /tmp/omc-eff-2-aa 19`.
BASE and CAND are the same omc, sha256 `befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`.
The run was made on 2026-10-26T16:04:56Z, pinned to CPU 19 (Cortex-X925) under `nice -n 19`. Load average was 11.44 at the start and 15.29 at the end: the machine was shared and not quiesced.

Evidence level: MEASURED on the host. This is not a candidate measurement and not a hardware qualification.

## Validity: VALID

- Both build-mode runs report `S rows=77 ok=77 fail=0`, so each compiler reproduced its reference bytes. The run itself reports `S rows=1176 ok=1008 fail=0 ctl_detected=168 ctl_missed=0`. All three exited 0.
- Every real `T` row has 10 fields, ok 1 and a nonzero `runtime_ns`, which leaves 0 bad rows.
- C placement (PROTOCOL-2 section 2), read from `disasm-c/*.O2.s`: every `main` has exactly two `clock_gettime` calls. No loop branch or kernel call comes before the first one, and between them there are 1, 2, 1, 2, 2, 6 and 7 branches or calls, for arith, branch, fib, leafcall, sieve, isort and matmul respectively.
- `selftest.log` reports 12/12 PASS. Among them, a missing runtime line and a malformed runtime line are both rejected, and the synthetic line 123 is read back as exactly 123.

## Result: every case NO_CHANGE; every instruction ratio is exactly 1

Section 4 rule, applied by WolframLanguageEvaluator (`wolfram-aa-query.txt` -> `wolfram-aa.txt`). Each ratio is
omcC/omcB, where below 1 means C is faster. Each cell gives the median paired ratio and its 95% interval x(6)..x(16) over 21 rounds.

| case | verdict | runtime omcC/omcB | instr | wall omcC/omcB (report only) | cycles | omcB runtime ns | cO2 runtime ns | omcB/cO2 runtime (report only) |
|---|---|---|---|---|---|---|---|---|
| arith | NO_CHANGE | 1.0001 [0.9996, 1.0024] | 1 | 0.9996 [0.9977, 1.0071] | 1.0000 | 120,152,100 | 34,405,442 | 3.493 [3.479, 3.518] |
| branch | NO_CHANGE | 0.9996 [0.9954, 1.0031] | 1 | 1.0003 [0.9772, 1.0382] | 1.0004 | 139,195,306 | 74,147,794 | 1.876 [1.867, 1.880] |
| fib | NO_CHANGE | 1.0017 [0.9997, 1.0177] | 1 | 1.0020 [0.9988, 1.0181] | 1.0001 | 137,055,209 | 33,922,507 | 4.035 [3.979, 4.044] |
| leafcall | NO_CHANGE | 1.0016 [0.9981, 1.0040] | 1 | 1.0021 [0.9979, 1.0065] | 1.0000 | 54,580,780 | 15,558,364 | 3.481 [3.201, 3.495] |
| sieve | NO_CHANGE | 1.0011 [0.9978, 1.0150] | 1 | 1.0019 [0.9941, 1.0209] | 1.0003 | 245,687,064 | 116,898,351 | 2.106 [2.081, 2.129] |
| isort | NO_CHANGE | 1.0001 [0.9938, 1.0030] | 1 | 0.9991 [0.9856, 1.0025] | 1.0000 | 78,156,105 | 39,829,643 | 2.001 [1.897, 2.023] |
| matmul | NO_CHANGE | 1.0057 [0.9911, 1.0196] | 1 | 1.0067 [0.9960, 1.0310] | 1.0021 | 18,087,057 | 10,571,466 | 1.714 [1.685, 1.737] |

The verdict metric is precise enough for the 0.97 and 1.03 thresholds on this machine:

- The widest runtime interval in this A/A is matmul's, at [0.9911, 1.0196]. That spread is narrower than the band between the IMPROVED and REGRESSION thresholds.
- The median paired ratios lie between 0.9996 and 1.0057.

The absolute nanoseconds and the omcB/cO2 column are *this machine, report only, not a gate*. The in-process omcB/cO2 ratios sit slightly above the fork-to-wait ratios of OMC-EFF-1: fib is 4.035 here against 3.994 there, and matmul is 1.714 against 1.649. Excluding process start-up, which is the same for both arms, would push a ratio upward in this way, but that is [INFERENCE]: these are separate runs, so the comparison is not paired.

## Files

- `run.raw`, `build-B.raw` and `build-C.raw` are the conductor rows. `T` rows carry `runtime_ns` as their tenth field.
- Provenance and hashes: `provenance.txt`, `inputs.sha256` and `arms.sha256`.
- `sizes.txt` sums every `.text*` section.
- `disasm-c/` holds the C placement check, `selftest.log` the self-test output, and `wl-data.m` the rows as Wolfram data.
- `wolfram-aa*.txt` holds the verdict query and its output.
