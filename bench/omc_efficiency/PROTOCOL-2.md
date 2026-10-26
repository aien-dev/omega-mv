# OMC-EFF-2: in-process runtime correction to OMC-EFF-1 (before any candidate measurement)

Status: frozen in the commit that adds this file. Written after the OMC-EFF-1 baseline
(`evidence/OMC-EFF-1/baseline/`, protocol `PROTOCOL.md` sha256
`ae1d1b2ace22bd30993f2ac14965d0e5ec62d9d66ae4b4e9521a417e5c74033a`, freeze `951de2eb`) and before
any candidate compiler was built or measured. `PROTOCOL.md` is not edited; this document says exactly
what changes from it. Everything it does not mention is unchanged.

## 1. Why

OMC-EFF-1 timed each program from `fork` to `waitpid`, so the wall metric includes process creation,
`exec`, loader work (static C start-up, the omc entry) and the output. The repository's A/B rule
(`ab-parity-bench`) requires the verdict metric to be an in-process time of the computation. The
correction was requested by the compiler owner before any candidate exists. The OMC-EFF-1 baseline
evidence stays as filed and stays valid as a statement about its own metric; its numbers are never
restated as in-process numbers.

## 2. What changes in the programs

- Every Omega kernel's `main` reads `monons` (Linux `clock_gettime(CLOCK_MONOTONIC)` by syscall 113,
  emitted by omc) before its computation and again after it, before any output, and writes the
  difference as one decimal line on **stderr** (`putnum 2 ...`; `putnum` now takes its descriptor).
  stdout is byte-for-byte unchanged, so `want/` is unchanged.
- Every C twin does the same with `clock_gettime(CLOCK_MONOTONIC)` (vDSO) and `fprintf(stderr, ...)`.
  Empty `asm volatile` statements are compiler barriers: after the first clock read they make the
  initial values (and, with a memory clobber, the globals) opaque, and before the second read they
  consume the results, so gcc cannot move the timed work outside the clock reads. They change no
  arithmetic. `run.sh` saves `objdump -dr` of every C object (`disasm-c/`), and a run is valid only if
  every C `main` has the timed loops and calls between its two `clock_gettime` calls.
- Timed region per case: arith, branch, leafcall: the loop; fib: `fib 38`; sieve: the 50 passes;
  isort: generation, sort and both checksum loops; matmul: fill, multiply and both sums. Process
  start-up, `putnum`/`printf` and exit are outside it.
- The clock read itself is inside the region once per run; at least 10 ms of work per case makes it
  negligible (not separately measured).

## 3. What changes in the conductor and coordinator

- The conductor sends the child's stderr to `time.out` in the timed pass. A `T` row gains a tenth
  field, `runtime_ns`. `ok` is 1 only when the status is 0, stdout equals `want/CASE.out`, and stderr
  is exactly 1 to 19 digits and one LF. The `P` (counter) rows and `B` (compile) rows are unchanged.
- `selftest.sh` adds three checks: a program with the right stdout and no runtime line fails; one with
  a malformed runtime line fails; a synthetic valid line `123` is read exactly as 123.
- `run.sh --ab BASE CAND SEED OUTDIR [CPU]` runs the section 8 A/B: arms `omcB` (BASE), `omcC` (CAND),
  `cO2` (reference only) and `neg` (mutants built by BASE), 3 warm-up and 21 measured rounds with the
  given seed, then compile time with build mode once per compiler (10 rounds each). Baseline mode
  (`run.sh OMC OUTDIR [CPU]`) is unchanged except for the runtime field.
- `sizes.txt` now sums every `.text*` section of the C objects (the OMC-EFF-1 deviation, corrected).

## 4. The verdict metric (replaces "wall" in PROTOCOL.md section 8)

The A/B rule of `PROTOCOL.md` section 8 is applied unchanged, with the in-process `runtime_ns` paired
ratio `omcC / omcB` in place of the fork-to-wait wall ratio:

- **REGRESSION** if the median instruction ratio is above 1.005, or the median runtime ratio is above
  1.03 with the interval's lower bound above 1;
- **IMPROVED** if the median runtime ratio is at most 0.97 and the interval's upper bound is below 1;
- **NO_CHANGE** otherwise.

The interval is the same distribution-free 95% interval of the median, x(6) .. x(16) of the 21 sorted
paired ratios. Ratios are written with their direction: `omcC/omcB runtime 0.87 (below 1 = faster)`.
The fork-to-wait wall ratio, cycles and the ratio to `cO2` are reported beside it, labelled report
only. Instruction counts still come from `perf stat` over the whole process (deterministic for omc;
output and start-up instructions are identical for both omc arms).

## 5. Validity additions

On top of `PROTOCOL.md` section 5: every real `T` row carries a valid runtime line; the C placement
check of section 2 holds; `selftest.sh` passes all of its checks. Absolute nanoseconds are reported as
*this machine, report only, not a gate*.

Null control (declared here, run once after this freeze, before any candidate): `run.sh --ab` with
BASE = CAND = the OMC-EFF-1 omc (`befb8199…a1d`), seed `20261028`. With identical compilers every case
must come out NO_CHANGE under section 4 and every instruction ratio must be exactly 1. Any other
verdict means the in-process metric or its thresholds are too noisy on this machine; that is reported
before any candidate verdict is given, and the thresholds are not widened.

## 6. Disclosures

The kernels and C twins were changed and run before this freeze only to check that stdout still equals
`want/`, that each program writes exactly one decimal stderr line, and that the C timed regions sit
between the clock reads; no runtime value was read. No candidate compiler exists in this tree.
