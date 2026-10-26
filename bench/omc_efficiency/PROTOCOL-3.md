# OMC-EFF-3: OM-2 source profile for the candidate A/B (additive, before any candidate measurement)

Status: frozen in the commit that adds this file. This file is additive. `PROTOCOL.md` (sha256 `ae1d1b2a…033a`) and `PROTOCOL-2.md`
(sha256 `28e8d773…dac5`) and their evidence are not edited. Their arms, rounds, warm-ups, metrics,
statistics, validity rules and the PROTOCOL-2 section 4 verdict thresholds apply unchanged. This file
adds one choice, the source profile.

## 1. Why

The compiler owner's candidate changes code generation only for OM-2 units: it adds a bounded cache of loop
constants in callee-saved registers and saves and restores exactly the registers it uses. OM-1 output is
declared byte-for-byte unchanged. The suite kernels are OM-1 units, so on them the candidate cannot
differ from the baseline. The candidate A/B therefore runs both arms on the same OM-2 sources.

## 2. The OM-2 sources (committed; never generated at run time)

`kernels/om2/CASE.om` holds one file per case. Each file is `kernels/CASE.om` with exactly two
differences:

1. One line `om2` is placed before the first line that is not a note. This is the opt-in, OMEGESE.md 9.13.
2. The global `out`, an OM-2 reserved word that is error 16 in an OM-2 unit, is spelled `obuf` everywhere.

The C twins, `want/` and the mutant edits are unchanged. `run.sh --profile om2` refuses to run, exiting 70,
unless every om2 file has exactly one `om2` line, that line is the first non-note line, and the file minus
that line equals `sed 's/\bout\b/obuf/g' kernels/CASE.om` byte for byte. The script checks this relation and
never produces a source.

Before the freeze I observed with the baseline omc `befb8199…a1d`:

- every om2 kernel compiles with rc 0;
- each prints the `want/` bytes and exactly one runtime line;
- each one's bytes differ from its OM-1 build. OM-2 units also take OM-2 lowering, for example an
  immediate offset for a constant index up to 4095.

The OMC-EFF-1 and OMC-EFF-2 numbers therefore do not describe OM-2 builds. No OM-2 timing was read
before this freeze.

## 3. Rows and provenance

- **Case names.** With `--profile om2` every case argument, and so the case field of every `T`, `P` and `B`
  row and of `sizes.txt`, is `om2/CASE`. Work paths are `src/om2/CASE.om`, `bin/ARM/om2/CASE` and
  `want/om2/CASE.out`. The conductor is unchanged. `--profile om1`, the default, keeps the
  OMC-EFF-2 behaviour and names.
- **Provenance.** `provenance.txt` records `profile` and the sha256 of all three protocols.
- **Hashes.** `inputs.sha256` covers `PROTOCOL*.md`, `conductor.om`, `run.sh`, `selftest.sh`, `kernels/*.om`,
  `kernels/om2/*.om`, `kernels/*.c` and `want/*.out`. `arms.sha256` covers every built file.
- **Self-test.** `selftest.sh` adds two checks: an om2 case is labelled `om2/fib` on every row and passes,
  and the om2 build of fib differs from the OM-1 build, which shows that the om2 source was compiled as OM-2.

## 4. Candidate A/B (the owner declares the candidate sha and the seed before the run)

Run `sh bench/omc_efficiency/run.sh --profile om2 --ab BASE CAND SEED OUTDIR 19`, with
BASE = omc `befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`. The verdict per case `om2/CASE`
follows PROTOCOL-2 section 4 unchanged:

- **REGRESSION** when the median instruction ratio is above 1.005, or when the median `runtime_ns` ratio
  `omcC/omcB` is above 1.03 and the interval's lower bound is above 1.
- **IMPROVED** when the median runtime ratio is at most 0.97 and the upper bound is below 1.
- **NO_CHANGE** otherwise.

The interval is x(6)..x(16) of 21 paired ratios. Each ratio is written with its direction (below 1 = faster).

Additional record: with `--ab --profile om2`, `om1-identity.txt` states for each OM-1 kernel whether
BASE and CAND emit identical bytes. Any `differs` contradicts the candidate's OM-1 claim. In that case
the OM-2 verdict is reported together with that finding, and an `--profile om1` A/B is owed before the
candidate is described as neutral on OM-1.

## 5. Null control (declared here, run once after this freeze, before any candidate)

Run `run.sh --profile om2 --ab` with BASE = CAND = `befb8199…a1d` and seed `20261029`. Every case must be
NO_CHANGE, every instruction ratio must be exactly 1, and `om1-identity.txt` must say `same` seven times.
Any other outcome is reported before any candidate verdict, and the thresholds are not widened.

## 6. Disclosures

- `out` is renamed because OM-2 reserves it. That is a source difference forced by the language and the
  only difference other than the `om2` line.
- The candidate's design is the owner's statement and has not been examined here.
- No optimization code exists in this tree. No candidate omc was built or run.
