# om-bench measurement contract v1 (omega-mv #653)

Status: FROZEN at the commit that adds this file. `manifests/contract.sha256` holds the sha256 of this file;
it is committed before any held-out candidate is scored. A change after that is a new version (v2) with a
new hash, and results under v1 stay labelled v1. Owner and judge: OmegaEval. Candidate lanes (OmegaIntent,
OmegaMind, OmegaCorpus, OmegaFast3) never grade themselves under this contract.

Every number published under this contract carries one label on the same line: MEASURED (with an evidence
path), MODELED (with the formula or Wolfram query) or NOT_RUN.

## 1. Question

Can a model write working Omega (.om) for small, fully specified stdin/stdout tasks without a person fixing
the code by hand, and does it get the answer right? Compared with the same model writing Python and C.

## 2. Task set

- 176 tasks from 44 families, 4 variants each (v1-v4); 1633 test cases. Each task: a plain-English statement,
  2 visible example cases (cases 00 and 01, shown in the prompt), and hidden cases. Scoring uses every case.
- Expected outputs come from oracles written by OmegaEval (`families_public.py`; sealed families in a private
  file), never from a candidate lane and never from a model.
- Scoring: stdout bytes exactly equal AND exit code 0. stderr is ignored.
- Splits are partitioned by family, so no family appears in two splits:

| split | families | tasks | visibility | use |
|---|---|---|---|---|
| train | 8 | 32 | public | examples and corpus material |
| dev | 4 | 16 | public | driver development, smoke tests |
| heldA | 16 | 64 | sealed: aggregates only, task ids as opaque hashes | the scored set for v1 |
| final | 16 | 64 | sealed, untouched | one-time final test of a later version |

- Hashes: `manifests/splits.json` lists sha256 of every public file and, for each sealed task, a keyed opaque
  id (HMAC-SHA256 under a private key) plus one commitment: the sha256 of the sorted list of its file hashes.
  Per-file hashes of sealed files are NOT published, because a short expected output (`YES\n`) could be
  recovered by hashing guesses. The full private hash list is kept by OmegaEval
  (`handoffs/OmegaEval/heldout-sha256.txt`, 2569 lines) and can be checked against the commitments.
- Final-test consumption log: `CONSUMPTION.md`. Every read of the final split by anyone, including structural
  load checks that score nothing, is recorded there. A candidate scored on final consumes it.
- Leak check (`leakcheck.py`, v2): every cross-family pair of statements is compared by word Jaccard,
  character-5-gram Jaccard and content-word overlap after stop-word removal and a synonym map. A pair is
  flagged when word >= 0.6, or char5 >= 0.6, or (overlap >= 0.9 and word >= 0.45). Each flagged pair has a
  recorded hand verdict in `leak-reviews.json`. The run fails if a flag is unreviewed or a cross-split pair is
  a duplicate. Planted control PLANT1 (a paraphrase of a dev task) must be flagged; PLANT2 (a looser
  paraphrase of a train task) is reported but not required. v2 was tuned after seeing PLANT1, so PLANT1 is not
  a blind control; the v1 rule missed it, and that miss is reported. Result of record: `leakcheck-v3`
  (same rule as v2; ids re-keyed): 176 tasks, 15400 pairs, 8 flags all reviewed, 4 cross-split, 0 duplicates,
  PLANT1 flagged, PLANT2 not flagged, rc 0. Flag ids in `leak-reviews.json` are the keyed opaque ids.
- Leak-check verdicts that change the statistics: two heldA families share one computation pattern; they are
  ONE cluster in every family-clustered statistic (15 heldA clusters, not 16). One train family is a special
  case of one heldA family; it is reported as a near-transfer risk.

## 3. Arms

All arms use the same subject model, the same reference card (`REFERENCE-CARD.md`, sha256 in
`manifests/splits.json`), the same task statement, the same common I/O sentence and the same 2 visible
examples. Only the language and the loop differ.

| arm | what the model gets | attempts | status in v1 |
|---|---|---|---|
| a | plain generation, Omega | 1, greedy | RUN |
| b | a + compiler feedback: after a refusal the model sees the omc error line, the meaning of the error number and the offending source line, then tries again | up to 1+3, greedy | RUN |
| aeq | equal-attempt baseline for b: up to 4 independent samples at temperature 0.8 (seeds 654-657), first one that compiles is kept, no feedback | up to 4 | RUN on heldA if time allows |
| c | structured construction through OmegaIntent's omq (#838): the model fills `??` holes and omq answers each | - | NOT_RUN (omq not merged; needs an agent loop) |
| d | constrained decoding (OmegaMind) | - | NOT_RUN (not delivered) |
| e | trained Omega specialist | - | NOT_RUN (does not exist yet) |
| fpy | the same model writes Python 3 | 1, greedy | RUN |
| fc | the same model writes C11, built with `gcc -O2 -std=gnu11 -lm` | 1, greedy | RUN |
| pilot A / pilot B | OmegaIntent agent-loop pilot (section 9) | - | NOT_RUN (cloud model unfunded) |

Arm b's attempt 0 uses the same prompt and greedy decoding as arm a and is charged in full to b. Whether it is
byte-identical to arm a's reply is reported as a reproducibility measurement, not assumed.

## 4. Subject model and environment profile

- Model: Qwen3-4B-Instruct-2507, Q8_0 GGUF, sha256
  `7e0200979d2b23423891febbd8d84b3c65783cbffafc7a4d38cdea9a0b3d044f`, quantized with llama.cpp 0f3a71be1 from
  the bf16 release. Chosen as a small CPU model so the run needs no GPU and costs $0 marginal.
- Server: llama.cpp `llama-server` 0.3.0-dev commit 0f3a71be1 (the build bundled at
  `/usr/local/lib/ollama/llama-server`, sha256 `f6e05586c5e7dd2110b2d8efadf23ff3b081dcdd53be67cfd32884af7b476163`),
  CPU only (`-ngl 0`, no GPU visible), `taskset -c 3-4`, `-t 2 -c 16384 --parallel 1`, port 18653.
  `max_tokens` 2048 per reply; greedy = temperature 0, top_k 1, seed 653; prompt cache on. Cores 0-2 run
  another lane's decoder (agreed split with OmegaMind, 2026-10-26); a 2-core probe decoded 150 tokens at
  8.13 tok/s (MEASURED, `handoffs/OmegaEval/env/llm-probe-2core.json`).
- Omega compiler: `seed/out/omc` sha256 `2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f`,
  as built at omega-mv `0b77e6ea294aa3d69ccbdb86b9c14d133abea689`; this branch's base `369dbc96` has no
  change under `seed/`. The judge refuses any other omc hash.
- Host: DGX Spark, arm64, Linux 7.0.0-1019-nvidia; candidate builds and runs pinned to cores 3-4; gcc 13.3.0;
  Python 3.12.3. The box runs other work (load around 25 on 20 cores), so every time is "host, busy box".
- Limits: compile 60 s, each test case 10 s wall. A timeout or signal is a mechanical runtime failure.
- No sandbox: unprivileged user namespaces are refused on this host (`unshare -rn` fails). Mitigation: the
  authorization scan (section 6) runs before any candidate executes, and a flagged candidate is never run.

## 5. Judging

- Omega arms: judged by `gate.om` (seed/qualification/om2), the seeded .om runner, built by the pinned omc and
  given the pinned omc hash. The host comparator in `ombench.py judge` must agree with gate.om case by case
  (`ombench.py crosscheck`); any disagreement makes the arm's result INVALID.
- Python and C arms: judged by the host comparator alone (disclosed below).
- A task with no recorded generation is NOT_RUN and is never counted as a pass. The judge prints
  `NOT_RUN k of N`, exits 2, and the denominator is the full split size. `DENOMINATOR FAULT` if the number of
  judged tasks differs from the split size. A malformed task prints `CORPUS FAULT [task] reason` and exits 1.

## 6. Outcome classes (one per task)

| class | meaning | counts as |
|---|---|---|
| pass | every case correct | success |
| wrong_output | builds and runs cleanly, some answer wrong | correctness failure |
| compile_refused | the compiler refused the final program | mechanical intervention |
| no_code | no code block in the reply | mechanical intervention |
| runtime_mechanical | crash, Omega runtime trap (exit 100-114), signal, timeout, other non-zero exit or empty output on some case | mechanical intervention |
| authz_violation | the program reaches outside stdin/stdout/stderr: Omega host words (ids >= 100 in seed/omc/genkw.awk) or read/write on other descriptors; Python: any import outside the allowlist `sys math collections itertools functools heapq bisect string re fractions decimal operator array statistics typing dataclasses copy`, or calls to `open exec eval compile __import__ breakpoint globals vars`, or `.system .popen .fork .remove .unlink` (parsed with `ast`); C: file, process, network or dynamic-load calls | authorization violation, never executed |
| NOT_RUN | no generation recorded | never a pass, stays in the denominator |

## 7. Metrics (all reported together; none alone)

- Primary: mechanical intervention rate = (compile_refused + no_code + runtime_mechanical) / N. Target 0.
  A lower rate never counts as an improvement if correctness drops.
- Correctness: pass / N.
- Authorization violations: count.
- Regressions: tasks that passed under the previous run of the same arm and contract version and fail now
  (first run: none to compare, reported as such).
- End-to-end time per task: model wall time for all attempts + compile + judge (MEASURED, busy box).
- Tokens: prompt tokens sent, prompt tokens actually processed (after cache), completion tokens, all attempts.
- Cost: $0 marginal (local model). Compute MODELED as model wall time x 5 cores. Energy NOT_MEASURED.
- Reproducibility: arm a is re-run on the 16 dev tasks; report how many replies are byte-identical (sha256).
- Runtime: for passing tasks, run time per case for Omega vs the model's own C and Python on the same task
  (MEASURED). Runtime as a percentage of OmegaReview's measured machine ceiling is NOT_RUN for this task set:
  the inputs are at most a few KiB, so time is process start-up, not compute. The pilot families (section 9)
  are where the ceiling comparison applies.

## 8. Statistics and predeclared verdicts

All computed in Wolfram; queries and outputs filed with the results.

- Rates get 95% Wilson intervals over tasks, and a family-clustered interval (cluster = family; the merged
  heldA cluster counts once).
- Sample size (Wolfram, two-sided 5%, power 80%, unpaired normal approximation):
  `twoProp[0.8,0.55] -> 54`, `twoProp[0.75,0.5] -> 58`, `twoProp[0.5,0.25] -> 58` tasks per arm; Wilson
  half-width at p = 0.5 is 0.122 for n = 64 and 0.245 for n = 16 (`z*Sqrt[0.25/n]`). heldA has 64 tasks, so a
  25-point difference is detectable; smaller differences are reported as not resolved.
- Paired arm comparisons (same tasks): exact two-sided McNemar test on discordant tasks.
  - "b reduces mechanical interventions vs a": McNemar p < 0.05 AND b's correctness is not lower than a's.
  - "b beats an equal number of plain attempts": the same test of b vs aeq.
  - "Omega is as writable as Python/C for this model": NOT claimed unless the Omega pass rate's interval
    overlaps the Python/C rate AND the mechanical rates are within 10 points. Otherwise the gap is reported.
- One declared run per arm. A re-run is allowed only for a documented load or infrastructure fault; then both
  runs are reported.

## 9. Pilot task set (OmegaIntent agent loop, ADRs aien-architecture-mv #340/#341)

Families: fixed-point log2 to micro-bits, PRNG plus hash train/eval split, mean and variance, histogram
entropy, dot product. Arms: A = plain .om authoring with final omc feedback; B = A plus omq answers at each
`??` hole. Measures: first-try success, fixes per task, tokens, wall time, runtime as % of OmegaReview's
measured X925 ceiling. Tasks, oracles, splits and verdicts belong to OmegaEval under this contract. The cloud
model run is NOT_RUN in v1 (no funded account); the family definitions are added as a separate task set
with its own hashes when built.

## 10. Controls (must pass before any result is published; `controls/run-controls.sh`)

- Reference programs pass; planted wrong programs (mutants) are classed wrong_output by both judges.
- Empty output, runtime trap, timeout, compile error and authorization-violating programs land in their
  classes; the authorization program is never executed.
- gate.om given a wrong omc hash returns INVALID.
- A run with 9 of 10 generations missing prints `NOT_RUN 9 of 10`, exits 2.
- Corpus faults (task with no cases; case missing its expected output) print `CORPUS FAULT` and exit 1.
- Python and C wrong and authorization controls land in their classes.
Each control asserts the diagnostic text, not only the exit code.

Pre-freeze fixes found by these controls (control run 1, `controls-r1.txt`; reruns `controls-r2.txt` and,
from the published copy in this directory, `controls-r3.txt`: CONTROLS FAILS=0):
- brackets mutant (depth-at-end check removed) passed every case: no case was never-negative yet unclosed.
  Added such cases to brackets.v1 and brackets.v3 (dev/public families), 1629 -> 1633 cases. Sealed task
  files unchanged.
- crosscheck read gate.om verdict lines in the wrong shape and reported every case missing; parser fixed.
- Python authorization control `import sys, os` slipped past a regex scan; replaced with the `ast` allowlist above.
- Python wrong-output control printed an integer too large for `str()` and crashed (runtime class); control
  replaced with an off-by-one program.

Reproduction: `bench/materialize.py PUB SEALED manifests/splits.json` regenerated every public file
byte-identically and every sealed file byte-identically (diff -r empty, run from this directory). Scored runs
use `run-arms.sh RUNROOT SPLIT ARM...` (generate, judge, gate.om cross-judge for Omega arms, summary).

## 11. Tools outside the Omega closure (disclosed)

| tool | does | Omega replacement |
|---|---|---|
| Python 3.12.3: `materialize.py`, `families_*.py` | writes tasks and expected outputs | port oracles to .om, cross-checked against the Python oracle |
| Python: `ombench.py` | prompts the model, extracts code, runs authorization scan, host comparator | gate.om already judges Omega; port the scan to .om using genkw word ids |
| Python: `leakcheck.py` | similarity screen | .om text tokenizer + set overlap |
| llama.cpp `llama-server` | runs the subject model | out of scope (model runtime) |
| gcc 13.3.0 | builds arm fc | none needed: it is the C baseline |

gate.om is the only judge in the Omega closure. Nothing here is "verified by CI": everything was run on the
Spark, not in CI.
