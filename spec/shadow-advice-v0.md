# Shadow advice v0: default-off scoring of admissible matvec arms (omega #731, ORIG-4)

Integration leaf of aien-train #19 (ORIG-1 aien-train #24, ORIG-2 aien-train #25,
ORIG-3 benchmarks #45). Consumer: the empirical cost model's matvec arm choice
(`src/runtime/rx_costmodel.{c,h}`, harness `tests/runtime/rx_empirical_optimizer.c`,
`spec/empirical-optimizer.md`). Nothing here changes a decision, a measurement, a
verdict, a weight, a listener or the GPU.

## Rule

```
consumer (deterministic)  ──► generates and verifies the candidate arms, selects, runs
          │  default off: OMEGA_EMPIRICAL_RECORDS unset ──► nothing is written
          └─ tap on ──► omexp-v1 records (state + every arm's digest-checked cost)
shadow advisor (.om)      ──► admits the checkpoint and the records, scores the admissible
                              arms, prints ADVICE or FALLBACK lines; reads nothing back
```

The advisor's output is scores and abstentions, never executable text, a new candidate,
a permission or authority. A later opt-in active selection is a separate release
decision after ORIG-3 evidence; it is not part of this specification.

## Pieces

| piece | where | language |
|---|---|---|
| record tap, default off | `tests/runtime/rx_empirical_optimizer.c` (`truth_table_tap`, `OMEGA_EMPIRICAL_RECORDS`) | existing C harness, 82 added lines, observation only |
| record format `omexp-v1`, admission, features, baselines | aien-train `native/si19/omexp.om` (pinned in `native/orig4/orig4.pin`) | handwritten .om |
| scorer forward and checkpoint admission | aien-train `native/si19/select.om` (pinned) | handwritten .om |
| shadow advisor | `native/orig4/shadow.om` (+ `shadowmain.om`, the `fn main` dispatch) | handwritten .om |
| tests and host glue | `tests/orig4/shadow-test.om`, `tests/orig4/run.sh`, gate `tests/gates/omega-orig4-shadow.gate` (step `orig4-shadow` in `tests/gates/run.sh`) | .om, POSIX sh glue |

The .om units are built by this repository's own seeded compiler `seed/out/omc`
(`seed/out/omc.sha256`); the aien-train units were written for the R5b compiler and
compile unchanged under it (checked by the gate).

## Admission (every check before the first score)

The stream is one mode byte `S`, a 64-byte config block (8 little-endian words: enable,
scoring budget in records, image length in the stream, five reserved zero words), the
4096-byte allow-list block, the checkpoint image, then the records to end of input.

1. Enable word in the config block must be 1; otherwise `SHADOW disabled` and exit 0
   (the reversible kill switch; the tap env var is the second, consumer-side switch).
   Then the config: budget at most 2048, image length 1..32768 (the image buffer),
   reserved words zero; otherwise exit 68.
2. Dataset allow-list status must be AUTHORIZED or SYNTHETIC (hash lookup; REVOKED or absent
   refuses, `FALLBACK all reason=allow-N`, exit 67, no advice).
3. Records must pass every omexp-v1 refusal class (`oxload`): size, magic, enumerations,
   shape, eligible mask, unmeasured or unverified arm, forged mismatch word, sequence,
   wrong hardware profile (cache bytes per core class), held-out contamination
   (`FALLBACK all reason=records-N`, exit 66).
4. Checkpoint must pass `sladmit`: length, magic, version, kind/width/parameter count,
   FNV-1a 64, state ranges, finite parameters (`FALLBACK all reason=image-N`, exit 0,
   nothing else printed). The dataset binding word is information, not a check: the
   training dataset's hash is recorded in the image, the advisor prints it and the
   current file's hash (`data_bound=`, `bound_matches=yes|no`) so a reviewer sees the
   lineage; a mismatch is reported, not a fallback, since the advisor by design runs over
   records the model never trained on (`slload`, the resume-mode admission, refuses it).
5. Resource budget: at most 2048 records per invocation and a declared scoring budget
   of records per run from the config block; past it, every further record is
   `FALLBACK budget`.

Scoring uses the image's best-on-DEV parameters (`slbest`, as selmain's evaluation mode
does); the advised arm is the argmin score over the admissible arms, lowest index on a
tie, the same choice as `slpick`.

## Per record

| condition | output |
|---|---|
| record index at or past the scoring budget | `FALLBACK seq=N reason=budget` |
| core class 2 or pressure above 1 (never in training) | `FALLBACK seq=N reason=ood` |
| fewer than two admissible arms | `FALLBACK seq=N reason=single` (nothing to rank) |
| arm count above 8 or unknown arm bits | refused earlier by admission |
| any score bit pattern non-finite | `FALLBACK seq=N reason=nonfinite` |
| otherwise | `ADVICE seq=N arm=A incumbent=R oracle=O score=BITS` |

`incumbent` is the fixed rule's arm for that record (what the deterministic path chose
without any model), `oracle` the cheapest admissible arm by the record's own
measurements (available only because the tap records every arm; it is printed for the
cost comparison, never fed back). After the last record one `COST records=N scored=M
fallback=F adv_cost=A oracle_cost=O incumbent_cost=I` line. Every line is one of
`SHADOW`, `ADVICE`, `FALLBACK`, `COST`; the gate greps for anything else and fails.

## Cost

The glue times the whole advisor run (load + validation + features + scoring + output)
for the record file and prints `COST_WALL records=N ms=T`; the advisor's own `COST` line
carries the record, scored and fallback counts and the advised, oracle and incumbent cost
sums (ps per call over the scored records). The deterministic path's own cost for the
same decision is the fixed rule (a comparison in C, nanoseconds). Energy is
NOT_MEASURED. GB10 is NOT_RUN.

## Not claimed

Any change to `rx_cm_decide` or `omega_matvec_dispatch`; online weight updates; a model
server; a Cortex writer; a GPU realization; production promotion.
