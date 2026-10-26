# OMC-EFF-1: fixed efficiency protocol for the seeded Omega compiler

Status: frozen before any suite timing was taken. The commit that adds this file is the freeze; the
evidence directory records this file's sha256. Any change to the cases, arms, rounds, seeds, metrics or
rules below is a new protocol version (OMC-EFF-2), never an edit of this one.

## 1. Question

How efficient are the programs, the compile step and the code size of `seed/out/omc` at omega-mv
`3364a1dfd0ca50c1b82bf19ba34932d8bc10a7a9` (omc sha256
`befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`), against C twins built by the
host gcc, on a fixed suite of scalar arithmetic, control, call and memory kernels? Where does the
excess go? This is a baseline and a hotspot finding, host only (Linux arm64, no GPU). It is not a
hardware qualification and it is not a verdict on any optimization.

## 2. Cases (fixed)

Each case is `kernels/CASE.om` with a C twin `kernels/CASE.c` that runs the same algorithm and prints
the same bytes. `want/CASE.out` holds the expected output. Every expected value was computed by
Wolfram, independently of both compilers, before the freeze:

| case | kind | work | expected | Wolfram query (WolframLanguageEvaluator) |
|---|---|---|---|---|
| arith | wrapping mul/add, xor, shift, rem | 40,000,000 iterations | `257048857779055872`, `19997288181122` | h: `{{31,1,0},{0,1,1},{0,0,1}}` raised to 40000000 by squaring mod 2^64, applied to `{0,0,1}`; s: `Sum` over 40 chunks of `Total[Mod[BitXor[r^2, BitShiftRight[r,3]], 1000003]]` |
| branch | data-dependent branch in a bounded while | Collatz steps for 1 .. 1,000,000 | `131434424`, `524` | compiled `While[x!=1, If[EvenQ[x], x=Quotient[x,2], x=3x+1]; k++]` over n = 1 .. 10^6 |
| fib | recursive calls | fib(38) | `39088169` | `Fibonacci[38]` |
| leafcall | a 2-parameter leaf call with values live across it | 30,000,000 calls | `14033628548985435713`, `2091994731324739872` | 32-bit halves: `Accumulate` of `BitXor[r K, BitShiftRight[r,5]]` with carries, xor-fold by bit parity |
| sieve | byte array stores and loads | 50 passes, limit 2,000,000 | `148933`, `7446650` | `PrimePi[2000000]`, `50 PrimePi[2000000]` |
| isort | ulong array, inner while | insertion sort of 20,000 values | `13308148862445`, `0`, `5`, `99999` | `NestList` of the LCG, `Sort`, `Total[v Range[20000]]`, `First`, `Last`, unsorted neighbours |
| matmul | 3 nested for, array index arithmetic | 300 x 300 x 300 | `809974868`, `2699759` | `Total[a.b, 2]`, `Tr[a.b]` |

Disclosed shape decisions, made before timing:

- All arithmetic is unsigned, so no signed-overflow check is in any hot loop. Omega still checks every
  run-time array index (trap 3) and every `while` against its pass bound (trap 4); C checks nothing.
  The comparison therefore measures omc's code generation plus Omega's mandatory checks, against
  unchecked C. It is not a same-semantics comparison and is never reported as one.
- `matmul` keeps the row offset and the column walk in names (`r`, `q`) in both languages. The direct
  form `add s mul get ma add mul i 300 k get mb add mul k 300 j` is refused by this omc with error 12
  (more than 16 registers). That refusal is itself a finding, reported with the baseline.
- `sieve` runs 50 passes because a single `while` stops after 1,000,000 passes; the limit 2,000,000 keeps
  the p = 2 marking loop at 999,999 passes.
- `leafcall` and `sieve` mark the C function `noinline` so the call structure matches the Omega kernel.
  `fib` is plain C; whatever gcc -O2 does with it is the yardstick.

## 3. Arms

| arm | build | role |
|---|---|---|
| `omc` | `OMC < CASE.om > bin` with the pinned omc | subject |
| `cO2` | `env -i /usr/bin/gcc -B/usr/bin/ -O2 -static` (gcc 13.3.0) | C oracle and yardstick: the ratio denominator |
| `cO0` | the same with `-O0` | reference only; never decides anything |
| `neg` | omc build of a one-edit mutant of the case (edits listed in `run.sh`) | negative control: its output must not match |

Both C arms are static so that neither side pays for dynamic loading. `-B/usr/bin/` is needed because
the conductor runs every child with an empty environment (no `PATH`).

## 4. Design

- **Conductor.** `conductor.om` (Omega, compiled by the same omc) does all timing, output checking and
  counter reading; `run.sh` only builds the arms, records provenance and starts it. Rows are raw: no
  statistics are computed by the conductor.
- **Runs.** One process per (round, case, arm). Within each round and case the arm order is a
  Fisher-Yates shuffle driven by xorshift64 from the declared seed; the position is in every row.
- **Program timing.** 3 warm-up rounds (recorded, flagged `warm=1`, excluded) then 21 measured
  rounds, seed `20261026`. Per case: a timed pass over all four arms (wall time = `monons` before
  `fork` to `monons` after `waitpid`, so fork, exec and exit are included for every arm alike), then a
  counted pass over the three real arms under `/usr/bin/perf stat -x , -e instructions:u,cycles:u`.
- **Compile timing.** Build mode: 1 warm-up and 10 measured rounds, seed `20261027`, arms `omc`, `cO2`,
  `cO0`. Wall time from `fork` to `waitpid` of the whole compiler (gcc includes cc1, as and the static
  link). Each output must equal the reference build byte for byte (compiler determinism).
- **Placement.** `taskset -c 19 nice -n 19` (CPU 19 is a Cortex-X925 core). The Spark is shared with
  other agents and is not quiesced: load average is recorded at start and end. Instruction counts are
  insensitive to load; cycles are counted only while the process runs; wall time is the most
  load-sensitive metric and is reported with that caveat.
- **One declared run.** `run.sh OMC OUTDIR` refuses an existing OUTDIR. A rerun is allowed only for a
  documented machine fault, and then both runs are reported.

## 5. Validity (before any number is read)

A run is VALID only when all of these hold; otherwise it is INVALID and no number from it is quoted:

1. both conductor invocations exit 0; build: every `B` row `same=1`, status 0;
2. every real `T` and `P` row has status 0 and ok 1; every `neg` row is detected (`ctl_missed=0`);
3. for every (case, real arm), measured `instructions:u` max/min is at most 1.01 (counts must be stable;
   a wider spread is reported as a measurement fault, not averaged away);
4. `selftest.sh` passes on the same omc (wrong expected output, a matching control and a wrong build
   reference are each rejected; usage errors exit 2).

## 6. Metrics and statistics

Per (case, arm), over the 21 measured rounds: median, min and max of wall ns, `instructions:u` and
`cycles:u`. Per case and round, paired ratios `omc / cO2` (wall, cycles, instructions; `cO0 / cO2` as
reference). For each ratio: the median and the distribution-free 95% interval for the median, the
order statistics x(6) .. x(16) of the 21 sorted ratios (WolframLanguageEvaluator:
`n=21; k=Max[Select[Range[1,10], 2 CDF[BinomialDistribution[n,1/2], #-1] <= 1/20 &]]` -> `k = 6`, upper
rank 16, coverage 0.9734). Across cases: the geometric mean of the seven median ratios. Compile time:
median, min, max per (case, arm) over 10 rounds, and the median paired ratio `omc / cO2`. Code size:
the omc ELF file bytes (the whole program: header, code, trap stubs; no libc exists) against the
`.text` bytes of `gcc -c` of the twin (the kernel's own code; libc excluded), plus the static C file
bytes for reference. All statistics are computed with Wolfram from the raw rows; no p90 is promised.

## 7. Hotspot rule (declared before the data)

Cases are ranked by the median instruction ratio `omc / cO2` and by absolute excess instructions. For
the top-ranked cases the excess is attributed to compiler seams by disassembling the omc hot loops
(`objdump -D -b binary -m aarch64`) and multiplying the per-iteration instruction classes by trip
counts computed with Wolfram. The attribution is labelled MODELED; it must reconcile with the MEASURED
instruction total within 3%, otherwise it is reported as UNRECONCILED. The facts go to the compiler
owner; this protocol does not choose or implement the optimization.

## 8. Rule for a later A/B of a candidate compiler (declared now)

Arms `omcB` (this omc) and `omcC` (the candidate), `cO2` reference only, same cases, same conductor,
21 + 3 rounds, a seed declared in the candidate's evidence before its run. Correctness: every row
matches `want/`; any mismatch voids the candidate. Per case, on paired ratios `omcC / omcB`:

- **REGRESSION** if the median instruction ratio is above 1.005, or the median wall ratio is above 1.03
  with the interval's lower bound above 1;
- **IMPROVED** if the median wall ratio is at most 0.97 and the interval's upper bound is below 1;
- **NO_CHANGE** otherwise.

The candidate's compile time is measured with the build mode once per compiler (the v1 conductor
takes one omc per build invocation, so compile time is not paired); a median compile ratio above 1.10
on any case is reported as a compile-time regression. Code size is reported for every case.

## 9. Disclosures

Before the freeze, two exploratory programs that are not suite cases (a 50,000,000-pass xorshift loop
and fib(30)) were timed once with the shell's `time` while reading omc's code generation. The suite
programs were run before the freeze only to check their outputs, without timing. Conductor smoke runs
before the freeze printed the wall and counter columns masked.

## 10. Replay

```sh
git checkout <freeze commit>
sh bench/omc_efficiency/selftest.sh seed/out/omc /tmp/omc-eff-selftest
sh bench/omc_efficiency/run.sh seed/out/omc /tmp/omc-eff-run 19
```
