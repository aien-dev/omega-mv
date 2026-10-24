# E03 extension: lifecycle payback of the existing rx_plan cache. Frozen protocol.

Status: FROZEN before any evaluation run. Written 2026-10-19, branch research/e03-payback-246 off a8ffd2b9.
Issue: aien-dev/aien-architecture#246, section "Hardware-first learning extension (2026-10-19)". Parent #276. Ledger L049.
Campaign folder: evidence/PLAN_REUSE (existing, append-only). This extension appends under evidence/PLAN_REUSE/e03-payback/.
Existing gates G1..G6 in tests/runtime/rx_plan_reuse.c and spec/plan-reuse.md are not changed by this work.

## What is measured, and with what

Code: tests/runtime/rx_plan_payback.c (new, host CPU). It drives only existing paths: rx_plan_cache_retrieve,
pla_bind, rx_plan_applicable, rx_plan_execute, rx_plan_record_use, rx_plan_cache_store, rx_plan_encode/decode,
rx_plan_identify, rx_plan_decide, rx_plan_publish_index, and the A* planner pla_plan / pla_generalize. No second cache.

One task = one fresh World instance built from a Spec (shape, seed, perturbation, generations), solved once.
Two arms on the same task stream (same seeds, same order):
- Arm A, no reuse: plan with A* every time, execute, verify. Never touches the cache. Pays no identity hash, store or index.
- Arm B, existing reuse: retrieve, bind, check applicability, execute (CANDIDATE runs the reference first), verify,
  record use, and on a miss plan with A*, generalise, identify, store, publish index.
- Arm C (the #584 learning arm): included only if ~/workspace/omega-research-584 has runnable code when the held-out
  run starts; otherwise two arms, and the receipt says so.

Lifecycle phases per task (all in nanoseconds; thread CPU via CLOCK_THREAD_CPUTIME_ID for cognition phases,
wall via CLOCK_MONOTONIC for execution phases because execution runs on World reaction threads):
  goal_read, lookup (retrieve), applicability (bind + axes), search (A*), generalize, identify (hash), store,
  decision_record, index_publish, compile, reference (CANDIDATE revalidation), run, verify, repair (adaptation),
  serialize (encode + write), reload (read + decode + re-identify + store), e2e wall.
Every task writes one trace row (CSV). The ledger is the per-phase sum over rows. Reconciliation: for every phase,
ledger total == sum of trace rows (exact), and per task e2e_wall >= sum of its wall-attributed phases; the
remainder is reported as unattributed_ns, never hidden.

Seconds: wall e2e per task is the primary cost figure (seconds per accepted objective). Cognition CPU is reported
beside it as the decomposition. Load average is recorded at start and end of every run.
Joules: UNMEASURED. The on-device package counter (aien_spbm hwmon) that the 2026-09-28 receipt used is not
present on this kernel (hwmon0..2 are acpitz, nvme, mt7925_phy0). The UPS (upsc spark ups.load, 10 W steps,
about 2 s, whole-UPS output) cannot resolve one host core for a few milliseconds per task on a box at load 30.
Named physical limit: expected per-task delta is below 1 mJ; the instrument step is 10 W x 2 s = 20 J.

## Task streams (all arrangement domain = blocks-world lifted onto World objects; no general-agent claim)
Seeds are disjoint from the baseline test (which uses 0..139, 5000.., 7000.., 8000.., 9000..).
Development partition: seeds 200..211 (12 per shape). Held-out partition: seeds 300..339 (40 per shape).
Shapes: REVERSE (reverse a tower of 6), MERGE (merge two towers of 3), OTHER (reverse a tower of 5).

S1 repeated: H tasks of one shape, held-out seeds (new object identities and creation order each task).
S2 held-out bindings and compositions: S1 for REVERSE and MERGE with the cache warmed on development seeds
   only; then OTHER, a shape never seen in development (expected: miss, acquisition, then reuse).
S3 interference: a mixed stream REVERSE, MERGE, OTHER, REVERSE+state-swap, REVERSE+outsider, cycled, H tasks;
   same-shape templates with different required states accumulate and every lookup checks all of them.
S4 changed preconditions: after the base template exists, H tasks alternating clean and state-perturbed
   (swap, outsider) instances; expected refusals on required_state, then search or adaptation, then store.
S5 stale generations: world generation bumps every 4 tasks; expected refusal on world_generation then
   re-derivation into the existing record (deduplicated).
S6 restart: after the development stream, every template is serialised with rx_plan_encode and the whole
   record (including the graph body, which rx_plan_decode does not carry) is snapshotted; the cache is freed
   and reloaded; the next reuse must revalidate (CANDIDATE: reference first) through existing rules.
Controls:
  P+ positive payoff: S1 MERGE, H=40 (expected sign of cumulative net: positive).
  P0 no payoff: world generation bumps EVERY task (p = 0 by rule: every retained template is refused and
     re-derived; expected sign: negative, equal to minus the lifecycle overhead).
  Stale: a template marked STALE by a failed reuse must be refused on the stale axis and never run.
  Corrupt: one byte of a serialised record is flipped; reload must refuse it (decode error or semantic id
     mismatch on re-identify) and never store it.
Mutants (each must produce bad rows > 0, otherwise the check cannot fail and no PASS counts):
  M1 ledger drops the identify+store phase (reconciliation must fail).
  M2 reload skips the semantic-id re-check (the corrupt control must fail).
  M3 the sign detector reads arm A as arm B (P0 must fail).

## Horizons and quality floors (frozen)
H_dev = 12 tasks per shape (development). H = 40 tasks per stream (held-out). Break-even H* is the smallest H
at which cumulative net (arm A cost minus arm B cost, both arms over the same first H tasks) is >= 0.
Quality floors, checked per task; any violation fails the arm on that stream regardless of cost:
  F1 the goal holds after execution (goal_holds) in both arms.
  F2 arm B plan length equals arm A plan length on the same instance (no negative transfer on length).
  F3 evidence_present == evidence_required and crumbs verify.
  F4 false applicability = 0 (an accepted reuse that does not verify).
  F5 no STALE template is accepted; no corrupt record is stored.

## Diagnostic model (units: seconds per task; opportunities k = 1..H after acquisition)
  net(H) = p * Delta * (1 - r^H) / (1 - r) - C - m * H          for 0 < r < 1
  net(H) = H * p * Delta - C - m * H                              for r = 1
  net(H) = p * Delta - C - m * H                                  for r = 0 (0^0 = 1: one opportunity, then gone)
  C     one-time acquisition cost beyond arm A on the acquiring task: identify + store + decision + index (s)
  p     applicability probability per opportunity: accepted reuses / opportunities (dev estimate, Wilson 95%)
  Delta net saving per applicable use: e2e_A - e2e_B on the same instance (s), dev median and IQR
  r     per-opportunity retention: 1 - (forget events / opportunities); a forget event is a template that was
        usable at k-1 and is refused at k for stale/binding reasons without a World change by the stream
  m     per-opportunity maintenance in arm B even with no hit: lookup + refusals + index publish (s)
Derivation: expected saving at opportunity k is p*Delta*r^(k-1); sum_{k=1..H} r^(k-1) = (1-r^H)/(1-r).
r -> 1 limit is H. No finite positive payoff when C >= p*Delta/(1-r) (sup over H of the geometric part).
Break-even (m = 0, 0<r<1): H* = ceil( ln(1 - C(1-r)/(p Delta)) / ln r ) when the argument is in (0,1];
r = 1: H* = ceil(C / (p Delta - m)). The closed form is checked against the direct finite sum with aien-calc
receipts exported next to the evidence. The model is a diagnostic, not a law; model failure is reported
when the measured cumulative curve leaves the predicted band.

## Prediction rule (#262)
p, r, Delta, C, m are estimated from the development partition only, recorded in 01-prediction.json with
their uncertainty and committed BEFORE the held-out run. Prediction: cumulative net at H = 40 and H* for S1
REVERSE, S1 MERGE and P0, with a band from the dev IQR of Delta and the Wilson interval of p. Then the
held-out run measures them; the receipt reports predicted vs measured and whether the measured value is in
the band.

## Reinvestment ledger (no benefit counted twice)
  saved_gross      = sum over accepted reuses of (e2e_A - e2e_B) on the same instance
  spent_learning   = acquisitions (identify+store+decision+index) + failed reuse + repair + serialize + reload
                     + maintenance (lookup + refusals on misses)
  net              = saved_gross - spent_learning
  reinvested       = the part of spent_learning that occurred after cumulative saved_gross first exceeded it
                     (learning paid for by earlier savings); unfunded = spent_learning - reinvested.
  Also reported: forgetting (STALE markings, re-derivations), negative transfer (F2 violations, failed reuses),
  memory growth (templates stored x sizeof(PlanTemplate), lookup cost vs cache size).

## Stopping rule
One held-out run per stream at the frozen H under quiet-measure. If the quiet lock is not granted within 20 min
the run proceeds at nice 19 and records load. No re-runs to improve a number; a re-run for a code fix is a new
receipt appended beside the old one.
