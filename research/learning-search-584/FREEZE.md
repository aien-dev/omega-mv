# LS584 freeze: bounded learning-mechanism search (omega-mv#584, ledger L048)

Status: FROZEN before implementation, 2026-10-19. Base omega-mv main a8ffd2b9.
Parent aien-architecture#276; reuse contract #246 (E03 extension, net(H)); objective
contracts #264; prediction rule #262; cost schema omega-mv#583 `spec/offline-evidence-v1.md`
(OEV1, frozen at research/cost-sim-583@5d864920).

A change to anything in sections 1 to 9 after the first sealed run is a scope amendment
recorded in `AMENDMENTS.md` next to this file, never an edit here.

## 0. What is and is not claimed

This is one bounded experiment on one host (Spark GB10, CPU only). It asks whether a
finite set of three learning mechanisms, on one finite task suite, gives a better
lifecycle tradeoff (seconds, peak bytes; joules UNMEASURED) than the no-learning
incumbent, subject to frozen quality floors. It does not assume an LLM, does not claim
general transfer, and a PASS means only "the bounded hypothesis passed on this suite".
FAIL and INCONCLUSIVE are acceptable results.

## 1. State, action, update (the contract of #584)

- Objective x: a bounded arithmetic transformation objective (Phase 0 family 1, #264):
  key k (64-bit), width w = 32, a set of n_ex = 12 input/output examples (x_i, y_i) from a
  hidden rule R_k, and a kind tag (SOLVE, RECALL, COMPOSE, CORRECT).
- Hidden rule R_k: a program in the Omega V0 realizable subset (`omega_program_realize_check`):
  1 to 3 steps, each (op, imm) with op in {ADD, SUB, MUL, AND, OR} and imm in the
  constant domain D (section 3). Width 32 so every constant fits.
- State z (per arm, bounded capacity, section 3): what the arm retains between objectives.
- Action a = pi_theta(z, x): a program P (1 to 6 steps, same op set) and a verified
  Omega realization of P (`omega_synthesize_for_dgx_spark`, differential check against
  `omega_program_eval` on the 84 fixed differential inputs), or ABSTAIN.
- Examples: 4 structured inputs (0, 1, 2^32-1, 2^31) plus 8 random 32-bit inputs.
- Feedback: the arm checks P on the examples itself (native execution, charged to the
  arm); the harness scores P on a sealed check set of 16 fresh random inputs per objective
  (harness cost, not charged to the arm). A CORRECT objective delivers one counterexample (x, R_k(x)).
- Update z_next = U_theta(z, x, a, feedback): what the arm stores (section 2).

## 2. The three arms (theta ranges over exactly these)

| arm | pi (action) | U (update) | z capacity |
|---|---|---|---|
| A incumbent, no learning | enumerate programs in fixed canonical order (section 3) until one matches all examples; realize it | none (z empty) | 0 B |
| B existing verified reuse | look up k in a verified-program table (semantic id of P, status CANDIDATE/VERIFIED/STALE as in `rx_plan.h`); applicability = every example reproduces under P (the analogue of rx_plan's required-state check); on a CANDIDATE hit, re-synthesize and re-verify the realization (the analogue of rx_plan's reference-first run), then promote; on a VERIFIED hit, reuse the stored machine-bound realization after its triple id recomputes from the stored bytes (no reference re-run, as `rx_plan` VERIFIED skips the reference); on miss or refusal, arm A path, then store CANDIDATE | store on first verified solve (dedupe by semantic id); promote to VERIFIED on a verified reuse; mark STALE on a failed reuse; table full: refuse to store (same as `rx_plan_cache_store`, no eviction) | 32 entries (PL_MAX_TEMPLATES), each the canonical program bytes + 32-byte semantic id + counters + (VERIFIED) the realization bytes and ids |
| C bounded incremental rule learner | (1) B's lookup; (2) if the objective is COMPOSE(a, b) and both a and b are retained, propose concatenation P_a ++ P_b and verify; (3) else enumerate programs in LEARNED order: shapes (op sequences) ranked by retained frequency count, constants ranked by retained frequency, ties in canonical order; realize | B's update plus: increment the count of P's shape and of each (op, imm) pair; counts are bounded (saturating u16); per-key table as B with the same capacity | 32 entries + shape counts (5 + 25 + 125 u16 = 155 x 2 B) + constant counts (5 ops x 16 constants u16) |

Every operation of C's predictor and of every proposed program is in the V0 subset,
so every action lowers through the existing `omega_realize_synth.c` path or is rejected
by `omega_program_realize_check` before execution (unknown operations are rejected before
execution; the harness counts those rejections).

C's variants (the finite grammar of theta beyond A and B, selected on calibration only):
C0 = C without composition (steps 1 and 3), C1 = full C. No other variants exist.

## 3. Numeric domains, capacities, budgets, seeds

- Width w = 32. Constant domain D = {1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 64, 100, 255, 256, 1000} (16 values).
- Rule length 1 to 3 steps (sealed distribution: 40% one step, 40% two, 20% three).
- Candidate programs: shapes of length 1..3 over 5 ops (5 + 25 + 125 = 155 shapes), constants from D:
  total enumeration space 5*16 + 25*256 + 125*4096 = 518,480 programs.
- Search budget per objective: at most 524,288 candidate programs evaluated on the examples
  (a candidate evaluation = the V0 semantics on the examples); the budget covers the whole
  518,480-program space so that an incumbent failure is never an artefact of the budget.
  Exceeding it is a FAILED SEARCH, counted and charged.
- Realization budget per objective: at most 4 `omega_synthesize_for_dgx_spark` calls.
- Program length of an action: at most 6 steps (compositions of two 3-step rules).
- Table capacity: 32 keys (A has none). Update frequency: after every objective.
- Streams: 240 objectives per stream, built from a frozen generator (section 4).
- Seeds (xorshift64*, frozen):
  - search/training (development) seeds: 0x5841, 0x5842, 0x5843
  - calibration seeds: 0x5851, 0x5852, 0x5853
  - sealed evaluation seeds: 0x5861, 0x5862, 0x5863, 0x5864, 0x5865
  Nothing is selected on the sealed seeds; they are run once, after the selection in
  section 7 is recorded in `SELECTION.md`.
- Fallback: an arm that exhausts its search budget or whose realization is refused
  ABSTAINS; abstention is a non-success, never a wrong answer.

## 4. Task suite (same finite stream for every arm)

A stream is 8 preface commands plus 240 objectives from one seed, drawn from a pool of
64 keys. Preface (not scored, adapter checks): 8 malformed or ambiguous commands (unknown
kind, no examples, conflicting examples, width outside the frozen domain); the adapter
must answer with a clarification/abstention and never commit. Then, in this order:
1. warmup/first exposure (objectives 1..64): each key once (SOLVE).
2. repeated objectives (65..112): 48 SOLVE on keys already seen, fresh example inputs.
3. novel bindings (113..144): 32 SOLVE on 32 new keys whose rules reuse shapes seen in
   1..112 but with constants not seen for that shape.
4. compositions (145..176): 32 COMPOSE(a, b): y = R_b(R_a(x)) for seen keys a, b; the key
   of the composition is new, the objective names a and b.
5. delayed recall after interference (177..200): 24 RECALL on keys from block 1 that were
   not touched in blocks 2..4. Interference = the 32 + 32 new keys of blocks 3 and 4
   (table capacity 32 forces eviction pressure: retention is measured, not assumed).
6. changed rules (201..224): 24 SOLVE on seen keys whose hidden rule has changed
   (examples disagree with the retained program): a retained answer must be refused.
7. correction handling (225..240): 16 CORRECT: the key's rule changed to one that agrees
   with the old rule on the 4 structured inputs, which are the only examples given, so a
   retained answer passes the applicability check and is wrong; the harness delivers one
   counterexample (an input where old and new rules differ); the arm's second answer is
   scored on the new rule. Both answers' costs are charged. When no agreeing new rule is
   found in 4096 tries the change is an ordinary one and is recorded as such (key_a=0).
   Because 5 examples underdetermine the rule, success in this block is expected to be
   low for every arm; the block measures correction mechanics (refusal of the stale
   entry, second attempt, cost), not identification.

Retention intervals (frozen): recall is tested at 112 to 136 objectives after first
exposure (block 5 positions minus block 1 positions).

## 5. Quality floors and success definitions (precommitted)

- Semantic correctness (separate from usefulness): every committed program's realization
  passes `omega_realization_verify_program` (84-input differential vs the semantic
  evaluator). Zero mismatches allowed. Mutants (section 8) must be caught with bad rows > 0.
- Task success: the committed program agrees with R_k on all 16 sealed check inputs.
  (This is a sampled differential test and is labelled SAMPLED; it is not a proof of
  functional equivalence.)
- Floor F1 (semantic): 0 commits whose native realization disagrees with the objective's
  examples, and 0 realizations committed without a passing verification. A program that
  fits all 12 examples but not the sealed inputs is an identification failure of the
  evidence, counted as `wrong` under F2, not a semantic error. (A faster incorrect
  candidate is still a failed experiment: `wrong` counts against F2 and is reported.)
- Floor F2: success rate (success / objectives) >= 0.90 per sealed stream, per arm, and
  wrong commits of a learning arm <= wrong commits of arm A on the same stream + 1
  (learning must not degrade correctness relative to the incumbent; identification
  failures are a property of the evidence, not of the mechanism, and the incumbent
  itself has them: development seed 0x5841 gave A 7 wrong of 240 before this line was
  written, which is why an absolute count of 2 was replaced before any calibration run).
- Floor F3: stale/wrong-memory control: 0 wrong commits.
- An arm below a floor is reported FAIL regardless of cost.

## 6. Cost accounting (lifecycle, per objective and per stream)

Charged to the arm: parse/adapter CPU ns, lookup, applicability checks, search candidate
evaluations, realizations (synthesis + verification), native executions on the examples,
update, persistence append bytes and CPU ns, failed searches, refusals, repair (second
attempt after correction). Counted: candidates evaluated, realizations, lookups, hits,
misses, refusals by reason, abstentions, state bytes (peak), persisted bytes.
Units: CPU ns (thread CPU time, CLOCK_THREAD_CPUTIME_ID) and wall ns; bytes.
Joules: UNMEASURED (no named per-process instrument; the UPS wall meter has 10 W
resolution and sees the whole box). Not estimated.
Communication adapter cost: bytes and CPU ns of parsing the bounded command, reported
separately. No existing language model is used, so model cost is 0 and is reported as
such; this is not open-domain conversation.

## 7. Search, calibration, sealed evaluation

- Development seeds: build and debug the harness; any number from them is development.
- Calibration seeds: run A, B, C0, C1. Select the one C variant to carry to the sealed
  set by the lowest median per-objective CPU ns among variants meeting F1..F3 on all
  calibration streams; record in `SELECTION.md` with the numbers. Fit p, Delta, r, C of
  the #246 net(H) diagnostic from calibration (section 9) and write the predicted
  break-even horizon BEFORE the sealed run (#262).
- Sealed seeds: run A, B and the selected C once each. Report every arm, every failure,
  with min/median/max over the 5 streams. Compare predicted vs measured net(H).

## 8. Scientific controls (each run on calibration and sealed seeds)

- shuffled feedback: examples/feedback of objective i are replaced by those of a random
  other objective of the stream; learning arms must not beat A and must not commit wrong
  programs (success must drop to abstention, not to wrong answers).
- disabled learning: C with U = identity; must equal A on every count (bit-identical
  decisions).
- stale/wrong memory: before blocks 2..7, 50% of retained entries are overwritten with
  a different valid program; B and C must refuse them (applicability) and re-solve; 0
  wrong commits.
- unseen composition: COMPOSE of keys never seen; C's composition rule must not fire;
  success only through search within budget.
- Mutant checks (semantic correctness): (M1) flip one immediate in a realized program's
  code bytes after synthesis; (M2) swap a retained program for one that differs in one
  constant and does not reproduce the examples; (M3) corrupt one payload word of the persisted journal. Each mutant must
  produce bad rows > 0 in the harness's own checks; a mutant with 0 bad rows means the
  check cannot fail and the check is fixed before any PASS is counted.

## 9. Persistence and recovery (through the existing Cortex contract)

Arm state is persisted only as Cortex records (`src/runtime/rx_cortex.h`): each retained
program is a CX_CLAIM / CX_K_CANDIDATE record (payload: version word, key, semantic id,
program body words), promoted with `cx_promote` after a verified reuse (CX_K_PROMOTION
with evidence = a CX_EVIDENCE / CX_K_EVIDENCE_REF record carrying the realization id).
No independent store, no second journal format. Reload = `cx_open` (replay + digest
verification) then rebuild z from the newest records whose digests verify; a candidate
without a promotion record is loaded as CANDIDATE, with one as VERIFIED (OLD-or-NEW:
a half-written candidate never becomes the active rule). Corrupt journal (cx_tamper, M3)
or a torn tail: refuse the journal and start from empty z (safe fallback), counted.
Restart check: stream is cut after block 4, process state dropped, journal reopened,
blocks 5..7 continue; counts must equal an uncut run on the same seed.

## 10. net(H) diagnostic (#246 extension)

net(H) = p * Delta * (1 - r^H) / (1 - r) - C for 0 < r < 1; H * p * Delta - C for r = 1.
- C: one-time acquisition cost per key = (first-solve cost under the learning arm) -
  (first-solve cost under A) + persistence cost, in CPU ns.
- p: applicability probability = accepted reuses / reuse opportunities (blocks 2, 5).
- Delta: per applicable use saving = median(A cost on repeated objective) -
  median(learning arm cost on accepted reuse), CPU ns.
- r: retention per opportunity, fitted from block 5 recall hit rate h over the
  interference distance d: r = h^(1/d).
- H: reuse opportunities per key.
Break-even H* = smallest H with net(H) >= 0; no-payoff region: C >= p * Delta / (1 - r).
Each evaluation is checked against a direct finite sum, and the arithmetic receipt is
produced with `aien-calc` (receipt ids recorded in the evidence README).

## 11. Not in scope / owners retained

Runtime files are not changed in this branch (rx_plan.c, rx_costmodel.c,
omega_realize_synth.c, allen_bind.c, rx_cortex.c are called, never edited). GPU variant
selection stays in #522; compiler rewrites in #249/#261. ALLEN interface ownership
unchanged: the adapter maps bounded text commands to the goal-object field layout of
`rx_plan.h` (PL_OT_GOAL: seq, kind, max steps, energy budget, facts ref) and nothing else.
