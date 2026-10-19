# ENTITY-276 FREEZE (precommitted before any run of record)

Campaign: #276 entity pilot (aien-dev/aien-architecture-mv#276, aien-dev/omega-mv#584 section
"Unified entity pilot and bounded optimization policy"). Ledger L058. NOT A MASTER PLAN.
Base sha a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a. Mapping: docs/research/276-entity-mapping.md.
Harness: tests/research/entity276/entity276.c, run.sh, mk/entity276.mk. Changes to anything
below after this file is committed go to AMENDMENTS.md, never into this file.

Disclosure: the harness was developed with development runs on this machine (not of
record, unreceipted). Their numbers were seen before the gate rules below were written:
execution of the moves dominated CPU per objective, and the idle interval showed about
0.2 ms of process CPU. The gate rules were therefore not blind to those observations.

## 1. Objective (one, from the existing rx_plan test path)

The arrangement ("blocks world") goal of tests/runtime/rx_plan_reuse.c, shape "reverse a
tower" (SHAPE_REVERSE, PLA_GOAL_ARRANGE): six goal units in one tower plus four distractors
(a tower of two and two single units). Boot is that file's `env_build` with no perturbation,
seed 276, World generation 7, cognitive generation 3, max steps 32, energy budget 1000.
Changes from env_build, all in the harness: goal and facts on their own resource, the
cognitive generation on its own resource, so the human holds narrow capabilities.

Objective k (admitted as one outside publication of the facts and goal objects):
k even: units U5..U0 from the floor up; k odd: U0..U5 from the floor up. Goal seq 1000+k.
Process 1 uses k = 0, 1. Process 2 uses k = 2, 3, 4 (three repeated objectives).

## 2. Sequence (the #584 gate list) as the harness runs it

1. Boot, attach the World to a fresh journal (CX_OPEN_SYNC), observe boot state. An
   admission with a READ-only capability on the goal resource must be refused and leave no
   record. Admit objective 0 with the human's WRITE capability. Snapshot (view attached).
2. View detached: no snapshot or formatter call. The entity runs the rx_plan path (no
   template, so A* search, generalise, execute on the World's reaction threads, store a
   CANDIDATE template, journal it as a CX_K_CANDIDATE record, observe state).
3. Reattach: snapshot. Checks: visor digest string equals `rx_world_digest` now; the journal
   chain verifies; the record seen at the last snapshot is unchanged; lineage (record 1
   digest) unchanged; the history view shows every unit where the World has it; the goal
   holds; the history observation is newer than the last commit; the template table equals
   the journaled templates (bad row per mismatch).
4. Correction: admit objective 1. Probe (decide only): the retained template must apply.
   Then the human publishes cognitive generation 4 (theta's version). Snapshot. Solve: the
   template must be refused on PL_AX_COG_GEN by `rx_plan_applicable`, the objective is
   re-derived by search and commits; the template is renewed under generation 4 through
   `rx_plan_cache_store` and journaled. Reattach checks as in step 3.
5. Restart: process 1 closes the journal and exits. Process 2 (`restart`): `cx_open`
   replays and checks every record, rebuilds the template table from the journal (accepted
   only when `rx_plan_identify` reproduces the recorded id; entered as CANDIDATE), boots
   the frozen specification, attaches as session 2, publishes the newest observed state per
   role object. Checks against process 1's identity file (the checker's oracle; the restore
   does not read it): entity-state digest, lineage, chain at open, record count at open,
   the corrected cognitive generation, rebuilt template table equals the journal. Then
   steps 1 to 3 for objectives 2, 3, 4: each must reuse a template (3 hits, 0 searches).
6. Unchanged-state control: 3000 ms with no admitted event (in process 1 after step 4, and
   alone as `control-idle`). Must be 0: template lookups, search calls, plan views, snapshot
   and formatter calls, admissions, journal records. Disclosed separately: process CPU over
   the interval (all threads), voluntary and involuntary context switches, rchar/wchar of
   /proc/self/io, the harness's one timer (clock_nanosleep), the World's four reaction
   threads (blocked, not polled by the harness). No claim of zero hardware activity.
7. Mutants (each must go red: exit 1 and bad_rows > 0):
   - SHADOW: after the correction, the cached template's verified cognitive generation is
     written directly in memory (not through the journal/commit path). Detected by the
     template-table check; its restart must also go red (the shadow write is not memory).
   - TAMPER: on a copy of the pilot journal, `cx_tamper` one payload word in memory (must
     break `cx_verify_chain`), and flip one bit of the newest template record in the file.
     The restart must refuse the journal, start from empty knowledge in a fallback journal,
     count the refusal, and answer all three objectives correctly (0 wrong answers).
   - VIEWWRITE: edit the visor snapshot copy (digest text, an object id, a shown position).
     The runtime digest must not change; the edited view must fail the projection checks.
   - IDLEPOLL: the idle interval with a snapshot every 100 ms; the idle counters must flag it.

## 3. Controls and determinism

- `restart-cold`: the same restart on a copy of the pilot journal with the template records
  ignored (no retained knowledge across the restart). It may learn inside its own session
  through the same rx_plan path.
- Determinism: run.sh runs pilot and restart twice; summary-pilot.json and
  summary-restart.json (counts and the entity-state digest, no time) must be byte-identical.
  Lineage, chain and the World digest carry World timestamps (CX_WREC_T_START_NS) and are
  per-run identity values, not compared across runs.

## 4. Numbers frozen

| Item | Value |
|---|---|
| idle interval | 3000 ms (at least 2 s) |
| idle counters that must be 0 | lookups, arrange_calls, plan_views, snapshot_calls, admits, records |
| repeated objectives after restart | 3 |
| IDLEPOLL period | 100 ms |
| seed | 276 (only seed) |
| runs of record | 5 runs of run.sh |
| journal mode | CX_OPEN_SYNC (fdatasync per append); journal_bytes = bytes persisted |

Per-step cost columns (steps-<mode>.csv): cpu_ns (main thread CPU), proc_cpu_ns (all
threads), cog_cpu_ns (main thread CPU in view, goal read, lookup, bind, applicability,
search, generalise), wall_ns, journal_bytes, records, peak_rss_kib, plus the counters.
Each CSV header carries load and MemAvailable at start and end of that process.

## 5. Gate rules (decided now)

| Gate | PASS | FAIL | otherwise |
|---|---|---|---|
| G1 same identity (open, detached, reattached, restart); no shadow authority | every run: pilot and restart exit 0, and SHADOW, TAMPER, VIEWWRITE red | any identity check fails in a pilot or restart, or a mutant is not red | n/a |
| G2 no redundant work on unchanged state | every run: all idle counters 0 in the pilot and control-idle intervals, and IDLEPOLL red | any counter nonzero | n/a |
| G3 corrections invalidate shortcuts, floors kept | every run: probe accepted before, COG_GEN refusal after, corrected objective commits (goal holds, legal), renewed under generation 4, generation 4 present after restart | any of those missing | n/a |
| G4 ablations | NOT_RUN here: owned by #584 and #246 | | |
| G5 retained knowledge reduces cumulative cost after restart | warm max < cold min, on cumulative proc_cpu_ns over steps 5a + 5_obj2..4 per run (5 runs), with 0 searches warm | warm min > cold max | INCONCLUSIVE |

G5 secondary columns (reported, not the gate): cumulative cog_cpu_ns plus rebuild CPU,
search counts, journal bytes written in session 2. Arithmetic in verdicts goes through
aien-calc.

## 6. Prediction before measuring

The resource-DAG prediction (OEV1, evidence/ENTITY-276/dagsim/) is committed before the
first run of record. Energy: UNMEASURED unless a receipt's wall joules cover a steady run of
30 s or more; run.sh alternates work and idle intervals, so it is not a steady run.
