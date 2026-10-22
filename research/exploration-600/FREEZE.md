# Omega600 finite retention experiment, L492

FROZEN before implementation and held-out execution, 2026-10-22.
Base main a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a, verified against remote main.
No deployment or promotion authority. All costs below are SYNTHETIC integer work
units, never seconds or measurements. Energy UNMEASURED; hardware/model NOT_RUN.

## Ownership and reuse

Read full issue600/584/583 bodies and comments from the parent snapshot. Snapshot
has no comments for600/584. PR591 owns584; PR590 owns583. Prior artifacts read:
584 L048 research/learning-search-584@b557e9e7 and L167 r2@ce47b8b8, both handoffs,
FREEZE.md, run-r2.sh and r2-check.sh;583 L047 research/cost-sim-583@3bd56731,
spec/offline-evidence-v1.md;249 L059 research/rewrite-elim-249@79c436c2,
docs/research/249-checked-elimination.md and tools/rw249/README.md.
584's initial C0 FAIL, B INCONCLUSIVE and r2 C0 PASS/B FAIL remain their results.
583's failed calibration-transfer/low-k findings remain unchanged. No remeasurement.

584 owns native program learning and its campaign;249 already retains worse rewrite
intermediates with checked equivalence. Do not duplicate either implementation.
This follow-on isolates a synthetic retention-policy mechanism in OSC using the
existing compiler/interpreter/native ABI, existing Cortex CANDIDATE records and
existing tools/m19r_campaign.sh hash/verdict aggregator. The host driver is test
plumbing and an independent exhaustive oracle, following existing host C tooling.
No new store/scheduler/cost model. Runtime/model/training owners keep their paths.
No shared Makefile edits. Parent L487, intake L488. Other protected owners: L489
trace607-612, L478/L479 614-618, L483/L484 bridge, L184 601. None touched.

## Grammar and fixtures

A finite tree of learning-mechanism configuration labels, not equivalent rewrites.
Root0 is the immutable qualified reference (cost100). Append choice1 or2, at most
three times, node children2*n+1/2*n+2. Exactly15 nodes,14 candidate trials.
Labels correspond to the ADD1/ADD2 chain subgrammar of584's V0, width32 wrapping;
configuration quality/costs are synthetic, not claims about arithmetic equivalence
or learned capability. No generated candidate replaces the reference.
All nodes well-defined; validity mask is a separate hard evidence/authority gate.
An invalid node may neither improve best-qualified nor generate descendants.

Development families (seeds1,2,3): valley costs[100,110,120,130,140,150,160,
170,180,190,200,210,220,230,20]; monotone cost100-10*depth; no-payoff100+depth.
Exact independent enumeration establishes reachable optimum only in this tree.

Held-out seeds6001..6005, families unseen during development:
* plateau: all100 except node(7+seed%8)=30;
* two-wells: nodes1/2=105/115, leaves7..14=20+((node*17+seed)%70), others140;
* trap: node1=60, its descendants150, node2=120,node6=130,node14=10, others140;
* invalid-shortcut: all110, root100,node2=1 invalid,node6=2,node14=3,node7=70;
* expensive: cost100+((node*13+seed)%40), root100.
Generator formulas frozen here; same author designs tests and held-out generator,
so held-out means policy-unused family, not independently authored sealed data.
No tuning after first held-out run. All outcomes including losses retained.

## Policies, floors and resource ceilings

A immediate greedy: canonical smallest-frontier-id, expand a valid candidate only
if cheaper than its parent. B canonical bounded: smallest-frontier-id, retain
all valid intermediates, capacity8. B reuses584's canonical bounded enumeration
contract as a policy control; it is NOT a rerun of584's B reuse arm or249's B.
C retain-inferior: depth-first greatest-frontier-id, expand every valid candidate,
capacity4. D matched-ceiling random: choose a frontier rank by persisted 16-bit
LCG (seed*25173+13849 modulo65536), capacity4. A capacity8.
All have equal initial task exposure, horizon and total budget ceiling160. Candidate
limit14, depth3, no repeats. Capacity refusal drops a newly proposed child, counts
abandonment; no unseen score is consulted to allocate continuation. Exactly one
candidate per tranche, then persist. Continuation until frontier empty or budget
cannot pay the entire next tranche. Stops COMPLETE, BUDGET_EXHAUSTED,
INVALID_STATE, INVALID_EVIDENCE. Exhaustion means unresolved potential.
Correctness floor: no invalid candidate in best or frontier; best<=100;
incumbent0 reference never changes. No production promotion is implemented.

Each trial, successful or failed: search2 + checking3 + checkpoint2 + storage rent
one unit per live frontier entry BEFORE evaluation. Charge before execution. The
checkpoint2 includes one append and one recovery verification/lookup allowance;
there is one reopen allowance per committed checkpoint. Budget/retained bytes are
part of state and cannot reset on restart. Initial record is setup common to all
arms, reported separately: 1 record, no trial cost. Repeated completed execution
is a read with no trial/effects/append. Reopening more than once is diagnostic host
work, reported separately, and gives no additional search allowance.
Checkpoint payload64 words plus context24 words =704 bytes; Cortex record adds
136 bytes; journal header32 bytes. Max16 records (setup+14 trials+terminal stop),
13472 journal bytes. Exact file sizes checked, journal overflow refused.
State512 bytes; landscape128 bytes; frontier represented in state. Actual host
RSS/CPU/wall, compilation/testing/recovery cost reported as host diagnostics only.
No physical cost estimate is inferred. Resource work is synthetic under583's
provenance rules; measured/fitted scalar coefficients unavailable, no ranking by
unqualified old DRAM measurements. Any OEV timing packet stays NOT_RUN.

## Gates and predeclared verdict

Development RED: dropping inferior intermediates in C must fail the valley oracle.
Independent oracle enumerates each node and its ancestor validity without calling
OSC policy, and checks all three development families' reachability and optimum.
Trace best-qualified versus cumulative work, rejected/abandoned nodes, regret
relative to finite reachable optimum and false-pruned better descendants.
Horizon1,8,32; lifecycle=spent+H*bestcost; baseline H*100. Break-even minimum
integer H with H*(100-best)>spent; no-payoff if best100. Arm comparisons report
per-family/seed raw results, paired differences and min/max (descriptive, no
population confidence claim). Overall delayed-payoff hypothesis PASS only if C
has positive net atH32 AND beats A and D on ALL held-out cases. Otherwise FAIL;
unknown physical/model repayment INCONCLUSIVE/NOT_RUN independently.

Before held-out: test zero/insufficient/exact/oversized budgets, invalid state,
invalid candidate, capacity limits; stale contract/input/model/assumptions and
policy/exposure binding; interrupted/repeated execution; every checkpoint cut
versus uncut; torn/corrupt journal and correctly rehashed invalid payload refusal.
Identity binds source/contract, fixture bytes, model-cost assumptions, seed/arm,
budget/capacity, parent root and checkpoint state. Cortex verifies journal bytes;
OSC validates structural state, host replay verifies recorded transition history.
No recovery silently starts a new free campaign. Failed evidence keeps incumbent.
Mutants: C becomes greedy; invalid accepted; failed trial free; seed/state omitted
from restart; budget ignored; context gate skipped. Each must fail a relevant test.
Tests must run interpreted and compiled AArch64 code with identical states.
Independent parent reproduction/review remains PENDING because no other agents
are authorized. Campaign PASS means host qualification checks passed only.
