# OMC-EFF-3 null control: OM-2 profile A/A of omc befb8199 against itself

Protocol: `bench/omc_efficiency/PROTOCOL-3.md`, sha256
`d559ddd87ba9a790bc115c6d2ebd4c57c666d69901f7c6bcb6bec9de315dfea5`, frozen in commit
`c0cb1f48134dc3df33596e75a1316eaccf8fcfec`. `PROTOCOL.md` and `PROTOCOL-2.md` are unchanged. This run was
declared in PROTOCOL-3 section 5 before it was made.

Command: `sh bench/omc_efficiency/run.sh --profile om2 --ab seed/out/omc seed/out/omc 20261029 /tmp/omc-eff-3-aa 19`.
BASE and CAND are the same compiler, omc `befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`.
The run was pinned to CPU 19 under `nice -n 19` on a shared machine; load averages and the time are in
`provenance.txt`. Evidence level: MEASURED on the host. This is not a candidate measurement.

## Validity: VALID

- Build B and build C each reported `S rows=77 ok=77 fail=0`. The program run reported
  `S rows=1176 ok=1008 fail=0 ctl_detected=168 ctl_missed=0`. All three exited 0.
- The case field of every `T`, `P` and `B` row is `om2/CASE`; no row is unlabelled.
- Every real `T` row carries a valid `runtime_ns`, so there are 0 bad rows.
- The OM-2 source relation check passed for all seven kernels: run.sh would otherwise exit 70 before any arm is built.
- C placement: every gcc -O2 `main` has two `clock_gettime` calls, nothing timed before the first, and the
  work in between.
- `selftest.log` reports 14/14 PASS, including `om2_profile_rows` and `om2_source_compiled`.
- `om1-identity.txt` reports `same` for all seven OM-1 kernels, as required for identical compilers.

## Result: every case NO_CHANGE; every instruction ratio is exactly 1

The PROTOCOL-2 section 4 rule was applied by WolframLanguageEvaluator; the query is in `wolfram-aa-query.txt`
and its output in `wolfram-aa.txt`. Ratios are omcC/omcB, where below 1 means faster. Each value is the median
paired ratio with its 95% interval, the 6th to the 16th of the 21 sorted ratios.

| case | verdict | runtime omcC/omcB | instr | wall (report only) | cycles | omcB runtime ns | cO2 runtime ns | omcB/cO2 runtime (report only) | omcB instructions |
|---|---|---|---|---|---|---|---|---|---|
| om2/arith | NO_CHANGE | 0.9999 [0.9995, 1.0001] | 1 | 1.0000 [0.9992, 1.0011] | 1.0000 | 119,848,427 | 34,202,958 | 3.504 [3.496, 3.512] | 680,001,089 |
| om2/branch | NO_CHANGE | 1.0004 [0.9993, 1.0013] | 1 | 0.9999 [0.9990, 1.0020] | 1.0003 | 137,492,264 | 73,821,077 | 1.863 [1.859, 1.864] | 2,029,125,018 |
| om2/fib | NO_CHANGE | 1.0002 [1.0000, 1.0008] | 1 | 1.0004 [0.9994, 1.0006] | 1.0000 | 135,909,142 | 33,614,623 | 4.042 [4.039, 4.044] | 2,340,101,985 |
| om2/leafcall | NO_CHANGE | 0.9997 [0.9982, 1.0005] | 1 | 0.9994 [0.9974, 1.0004] | 1.0000 | 53,801,326 | 15,435,321 | 3.485 [3.483, 3.491] | 930,001,215 |
| om2/sieve | NO_CHANGE | 1.0005 [0.9989, 1.0017] | 1 | 1.0002 [0.9986, 1.0014] | 1.0001 | 258,945,303 | 116,151,336 | 2.235 [2.219, 2.257] | 7,139,388,888 |
| om2/isort | NO_CHANGE | 1.0001 [0.9990, 1.0020] | 1 | 1.0001 [0.9988, 1.0017] | 1.0000 | 77,621,063 | 38,836,400 | 1.997 [1.582, 2.000] | 2,707,064,187 |
| om2/matmul | NO_CHANGE | 0.9994 [0.9944, 1.0061] | 1 | 1.0014 [0.9941, 1.0057] | 0.9990 | 17,889,476 | 10,336,747 | 1.731 [1.706, 1.749] | 600,130,956 |

The widest runtime interval is matmul's, [0.9944, 1.0061], and the medians lie between 0.9994 and 1.0005.
The thresholds hold for the OM-2 profile without widening.

## Facts about the OM-2 baseline, for the compiler owner (report only)

These compare against OMC-EFF-1 and OMC-EFF-2, which were separate runs, so none of these comparisons is paired.
Instruction counts are deterministic, so their differences are exact.

- om2/leafcall executes 930,001,215 instructions against 870,000,917 for the OM-1 build. That is
  60,000,298 more, or 2 more per call over 30,000,000 calls (integer bookkeeping). The OM-2 baseline is
  therefore not the OM-1 baseline. The candidate's A/B is against the OM-2 baseline above.
- The other six cases differ from their OM-1 builds by about 300 instructions (fib, for example, by
  2,340,101,985 against 2,340,101,671). The disassembly was not examined; I infer the source is
  OM-2 lowering in `putnum`.
- om2/sieve has an in-process runtime of 258,945,303 ns against 245,687,064 ns for the OM-1 build in OMC-EFF-2
  (unpaired, report only).

## Files

`run.raw`, `build-B.raw` and `build-C.raw` hold the rows. `provenance.txt`, `inputs.sha256` and
`arms.sha256` hold the provenance and hashes; the inputs include `kernels/om2/*.om` and all three protocols.
`om1-identity.txt`, `sizes.txt`, `disasm-c/`, `selftest.log`, `wl-data.m` and `wolfram-aa*.txt` hold the
remaining checks and data.
