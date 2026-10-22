# Omega600: inferior-intermediate retention, L492

Additive follow-on to584. This finite synthetic experiment compares immediate
greedy A, canonical bounded B, inferior-retaining C and random D. It implements
policy transitions in OSC, tested through the existing interpreter and compiled
AArch64 ABI. It preserves reference0 and never requests production promotion.

Read FREEZE.md and AMENDMENTS.md first. Reproduce in a new packet directory:

```
bash research/exploration-600/run.sh checks /absolute/new/packet
bash research/exploration-600/run.sh heldout /absolute/new/packet
bash research/exploration-600/run.sh aggregate /absolute/new/packet
```

Every build/test runs under quiet-build with a60-second foreground bound. All
outputs belong to this lane. Existing tools/m19r_campaign.sh checks run metadata,
mandatory host gate PASS and hashes. Its PASS says only the host experiment packet
qualified. Scientific PASS/FAIL is in heldout/experiment.json and arithmetic.txt.
No M19R, GPU, model, physical-speed or energy qualification is inferred.

The C driver is existing-toolchain test plumbing, fixture generation and the
independent finite oracle. It contains no candidate selection/retention algorithm.
The OSC policy owns search, checking gates, queue, budget, interruption accounting
and best-qualified state. The fixture oracle independently inspects every node's
valid prefixes; report.awk independently reconstructs reachability and checks all
published arithmetic. No new language/runtime/store/scheduler is introduced.

Cortex checkpoints use CX_CLAIM/CX_K_CANDIDATE, isolated branch1 subject0,
generation6001. Headers link the prior checkpoint. Context24 words bind the
source/native/host binary artifact, frozen contract+amendments, fixture bytes,
synthetic cost model, assumptions and arm/seed/budget/capacity. The64-word OSC state
is opaque payload. Reserve before evaluation, append result after evaluation.
An interrupted reservation stays charged and must be abandoned before retry.
Only fully replayed history can resume. No promotion/effect records are emitted.
All completed-boundary cuts reproduce exactly. Repeated completed execution does
not append. Partial journal/digest corruption and stale identities are refused.
A deliberately rolled-back valid whole-record prefix cannot be detected without
an existing J-Space/external anchor; no rollback protection is claimed here.

State word layout:

| Words | Meaning |
|---|---|
|0..3|version6001, arm0..3, current RNG state, budget ceiling|
|4..9|spent, completed trials, best node/cost, frontier mask, exposure mask|
|10..13|stop0 active/1 complete/2 exhausted/3 invalid, refused, abandoned children, peak frontier|
|14..15|checkpoint record bytes excluding setup, checkpoint record count excluding setup|
|16..19|reserved0, branch capacity, reserved0, immutable reference cost100|
|20..25|storage/check/search/checkpoint work, last evaluated node, last validity|
|26..28|interrupted attempts, pending reservation, total attempts|
|29..63|reserved0|

Search states are configuration labels in the ADD1/ADD2 subgrammar, not checked
rewrites. The tree exposes synthetic outcome costs. A finite optimum says nothing
outside that tree. D changes its LCG state before each choice and persists it.
C uses depth-first order and capacity4; B uses canonical order and capacity8.
These are different policies at the same lifecycle ceiling, not a claim to isolate
retention independently of ordering/capacity. The greedy-C mutant isolates the
retention condition on the development valley while keeping C's other settings.

Costs are synthetic work units. H*best+spent and baseline H*100 use horizon1/8/32.
Break-even is the first integer horizon with strictly positive net.0 means no
finite payoff. Tests/compilation/replay CPU/wall and whole-process peak RSS are
recorded separately as shared-host diagnostics. They include oracle/compiler and
both execution modes, so they are not per-arm latency or speedup measurements.
Cortex file sizes are actual bytes. Each attempt emits2 records of840 bytes,
plus the common setup840 and header32. Budgeted peak frontier is not process RSS.

Independent parent review remains required. Review/reproduction checklist:

1. Verify all input and packet hashes and exact pushed head.
2. Reproduce development checks and six red behavioral mutants from the commands.
3. Check reservations survive both deterministic process cuts and retries pay anew.
4. Independently inspect all15-node reachability/optima and held-out raw traces.
5. Distinguish host gate PASS, scientific hypothesis verdict and NOT_RUN hardware.
6. Preserve584/r2/583 earlier negative findings and249's rewrite ownership.
