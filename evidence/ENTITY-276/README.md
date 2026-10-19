# ENTITY-276: one persistent entity whose screen is a projection of its committed state

Campaign for aien-dev/aien-architecture-mv#276 (operator clarification 2026-10-19) and
aien-dev/omega-mv#584 ("Unified entity pilot"). Ledger L058. NOT A MASTER PLAN.
Mapping: docs/research/276-entity-mapping.md. Frozen rules: FREEZE.md (committed ec0c2e27,
before any run of record). Prediction: dagsim/PREDICTION.md (committed 3be76bd4, before
the run of record). Amendments: none.

## What was shown, in plain words

A small planning entity (it rearranges a tower of six blocks on the existing World runtime)
took an instruction while its screen was attached. It then did the work with no screen
calls at all. When the screen was reattached, it showed exactly the committed result and
the same history. A human correction made the entity drop its learned shortcut through the
plan cache's own rule and redo the work correctly. The program then ended. A second program
started from the journal alone, came back with the same identity, state and history, kept
the correction, and reused the learned plan for three more instructions with no new
searches. Over 3 s of no input it did no planning, no lookups and no screen work, and wrote
nothing. Each sabotage variant was caught in every run. Keeping the learned plan across
the restart did **not** make the three repeated instructions measurably cheaper overall,
and on planning CPU alone it cost more than it saved. That is a negative result over this
horizon.

The "screen" is the existing visor World view (text and JSON) plus a value-only history
view written by the harness, saved to files. That is the smallest honest display; no UI
was built.

## Run of record

| Field | Value |
|---|---|
| date | 2026-10-19, 20:56:28Z to 20:58:41Z |
| host | spark-b87b (DGX Spark, GB10) |
| head sha | 974820021325fe203e79ada919da1a6131935dd3 (harness source identical to the build of record at 3be76bd4; later commits added only measure.sh and aggregate.sh) |
| build | `quiet-build nice -n 10 make -j4 PHYSICS_DIR=~/workspace/physics-pf-6d7cf0d4 AIENOS_LOCK_REPO=~/workspace/aienos-repo OUT_DIR=~/workspace/targets/omega-entity-276/record .../tests-research/entity276`, exit 0 (the first attempt waited 60 min in the build queue and never started) |
| command | run-of-record/command.txt: `quiet-measure flock /tmp/aien-gb10.lock spark-receipt entity-276-pilot -- env ENTITY276_BIN=... PILOT_OUT=... bash tests/research/entity276/measure.sh` (5 runs of run.sh) |
| load / MemAvailable | start 27.21 / 25,145,580 KiB; end 22.29 / 23,939,688 KiB; per run in run-of-record/runs.csv; per process in each CSV header |
| instrument | CLOCK_THREAD_CPUTIME_ID, CLOCK_PROCESS_CPUTIME_ID, CLOCK_MONOTONIC, getrusage, /proc/self/io, journal file size; receipt run-of-record/receipt-entity-276-pilot.json (~/yardsticks/receipts/2026-10-19-entity-276-pilot.json) |
| result | 5 of 5 runs: run.sh RESULT fails=0 (20 of 20 lines OK each); summaries byte-identical across all 5 runs |
| energy | UNMEASURED. The receipt reads 23,574 J of wall energy, but that is the whole UPS output over a 133 s run that alternates work and 3 s idle intervals. It is not a steady run of 30 s or more, so no joule figure is claimed |

## Gate verdicts (the five #584 entity gates)

| Gate | Verdict | Numbers (all 5 runs) | claim_strength |
|---|---|---|---|
| G1 same identity and history: open, detached, reattached, restart; no shadow memory authority | **PASS** | every reattach: visor digest equals `rx_world_digest`, chain verifies, earlier history unchanged, lineage unchanged, state shown equals the World; restart: entity-state digest, lineage, chain at open and record count (191) equal process 1; mutants red every run: SHADOW 1 bad row (restart 1), VIEWWRITE 2, TAMPER restart 7 with journal refused and 0 wrong answers | proven_in_scope |
| G2 no redundant inference, search or redraw on unchanged state; IO and timers disclosed | **PASS** | over 3000 ms: lookups 0, searches 0, plan views 0, snapshot/formatter calls 0, admissions 0, journal records 0 (in the pilot and in control-idle); IDLEPOLL red (1 bad row) every run. Disclosed: process CPU over the interval 73,984 to 133,360 ns (pilot) and 77,696 to 193,968 ns (control-idle); 1 to 4 voluntary context switches, 0 involuntary; 105 to 112 B read (the harness reading /proc/self/io itself), 0 B written; one harness timer (clock_nanosleep); four World reaction threads present, blocked, not polled by the harness. No claim of zero hardware activity | measured |
| G3 corrections invalidate shortcuts; floors kept | **PASS** | before the correction, the retained template applies (probe accepted); after it, `rx_plan_applicable` refuses it on the cognitive generation (cog_gen_refused 1); the corrected objective is re-derived, commits, the goal holds and the World is legal; the template is renewed under generation 4; generation 4 survives the restart | proven_in_scope |
| G4 ablations (elimination, reuse, incremental, combined) | **NOT_RUN** | owned by #584 and #246; this pilot runs reuse only, with a no-retained-knowledge control | n/a |
| G5 retained knowledge reduces cumulative cost on the repeated objectives after restart | **INCONCLUSIVE** (frozen rule: ranges overlap) | cumulative process CPU (steps 5a + 5_obj2..4) warm vs cold per run: r1 46,340,672 vs 38,040,112; r2 51,075,216 vs 50,609,888; r3 47,138,368 vs 49,829,312; r4 55,301,408 vs 55,309,040; r5 51,608,464 vs 51,159,792 ns (calc:f06d0200, 5e0fa359, 6fe26bb4, 81e4e90a, 2a097fdb, 19ae0b03, f1df3fdf, 99b2a53a, aca65cf8, 029ef7da). Warm 46.3 to 55.3 ms overlaps cold 38.0 to 55.3 ms. Searches: warm 0, cold 1 | measured |

G5 secondary (reported, not the gate): planning CPU only (lookups plus the template rebuild
after restart) warm 495,264 to 789,360 ns vs cold (search plus lookups) 104,288 to 187,280
ns. Warm min is above cold max in every run: **over three repeated objectives, rebuilding
the retained plan from the journal (455,840 to 754,368 ns) costs more than the one search
it saves (cold search median 139,664 ns).** The retained plan also saves one template record in the
journal: objective 2 writes 55,160 B cold vs 28,504 B warm, 26,656 B less (calc:f8896dc4). Wall time
per objective (251 to 342 ms) is dominated by execution plus one synchronous disk flush per
journal append, not by planning.

## Prediction (D3) against measurement

Prediction provenance: `synthetic` (one unreceipted development run, devB, load 25.7),
dagsim at research/cost-sim-583 9f8a9cea. Measured: medians of 5 runs of record.

| Op class | Predicted ns | Measured median ns | measured / predicted |
|---|---|---|---|
| journal replay + boot + template rebuild (5a) | 3,119,737 | 3,633,267 | 1.1646 (calc:2f6474f6) |
| World restore and identity (5b) | 147,642,942 | 152,887,443 | 1.0355 (calc:f4144276) |
| plan lookup (objective 2, warm) | 27,344 | 17,808 | 0.6512 (calc:8ad45618) |
| lookup + execute + journal (objective 2, warm, step wall) | 251,154,204 | 274,008,182 | 1.0909 (calc:bc28a8b7) |
| objectives 3 and 4 (warm, step wall) | 534,512,695 | 536,610,672 | 1.0039 (calc:94c51c32) |
| search (objective 2, cold) | 100,880 | 139,664 | 1.3844 (calc:664c9777) |
| whole restart, warm (sum of step medians vs makespan) | 936,429,578 | 967,139,564 (calc:529aee6b) | 1.0327 (calc:fc3856c9) |
| whole restart, cold (sum of step medians vs makespan) | 969,313,876 | 972,219,448 (calc:4c3431af) | 1.0029 (calc:98a60ed7) |

The prediction's qualitative call held: retained knowledge does not pay back within three
objectives here, and G5 is INCONCLUSIVE. The simulator added no contention insight: the
pilot is one serial chain. The honest fixture with `unknown` costs was refused
(REFUSED_UNKNOWN), as it must be.

## MISSING PREREQUISITES (described in the mapping, not built)

- A. No World restore from the journal (`RxPersist` is a label; the World is memory only).
  The pilot's second process boots the frozen specification and publishes the journal's
  newest observed values through the outside-publication path. Across restart, identity is
  checked on lineage, chain, record count and an entity-state digest, not the whole World
  digest (which covers session objects such as lowered graph cells).
- B. No VisorCortexView. The history view is a harness formatter over `cx_recall`.
- C. `allen_goal_mutations` maps only latency intents. The harness builds the PL_OT_GOAL
  mutations and publishes them through the same World path.
- D. No canonical action-graph encoding. The template record carries AgGraph struct bytes,
  accepted on reload only when `rx_plan_identify` reproduces the recorded id.
- E. (found by this pilot) Cortex World records carry the field values of only the first
  object a commit writes (src/runtime/rx_cortex_record.c:70). A move commit names its graph
  cell first, so unit positions never reach the journal. The pilot appends a state
  observation through the World's attached writer after each commit, and every reattach
  checks that it is newer than the last commit record.

## UNVERIFIED (choices the contract did not pin)

Correction = advance of the cognitive generation plus re-admission. Rebuilt templates
re-enter as CANDIDATE. AgGraph stored as struct bytes. World rebuilt by boot plus
publication of observed state. Identity across restart as above. Screen = visor text/JSON
plus history text. Single seed (276), one objective shape, three repeated objectives: no
claim beyond this scope.

## Files

- run-of-record/: command.txt, runs.csv (load and memory per run), aggregate.csv
  (min/median/max per step and column), the receipt, r1..r5/run-verdict.txt, and for r1
  every CSV and JSON (pilot, restart, cold, idle, each mutant) plus the screens
  (run1/screen-*.txt).
- dagsim/: OEV1 fixtures, evaluator outputs, PREDICTION.md (coverage gaps for #583).
