# #276 entity contract mapped onto existing omega code

Status: research note for aien-dev/aien-architecture-mv#276 and aien-dev/omega-mv#584
("Unified entity pilot"). NOT A MASTER PLAN. Ledger L058.
Base sha: a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a (every file:line below is at this sha).
Issue bodies read 2026-10-19 (276 operator clarification 2026-10-19; 584 section
"Unified entity pilot and bounded optimization policy"; architecture 246 section
"Minimum-work policy integration").

## What the contract says

s = persistent entity state, e = an admitted event, theta = versioned learned
mechanisms, s_next = F_theta(s, e), v = R(s, h) with h the view context. These are
specifications to be mapped onto existing mechanisms, not a new event loop or store.
A view may not mutate authoritative state outside the existing command/commit path.

## Key facts found in the code (they shape every row below)

1. The World is in memory only. `RxPersist` (src/runtime/rx_world.h:71) is a class
   label stored on each object (src/runtime/rx_world.c:2194); nothing writes a World
   to disk or rebuilds one from disk. The durable part of the entity is the Cortex
   journal.
2. Cortex records World execution by itself once attached: every causal crumb becomes
   one record (src/runtime/rx_cortex_record.h:7 and following, attach at :42). An
   outside publication becomes CX_K_WORK_ACCEPTED, a commit CX_K_EXEC_COMMIT with the
   object's field values after the commit.
3. The object digest covers type and field values only (src/runtime/rx_world.c:62); the
   World digest covers every live object's (slot, generation, object digest)
   (src/runtime/rx_world.c:2858). So the World digest also covers runtime-internal
   objects such as the action-graph run and cell objects that `rx_plan_execute` lowers.
4. The plan cache is host-side memory (src/runtime/rx_plan.h:33). `rx_plan_encode` /
   `rx_plan_decode` (rx_plan.h:164, :167) carry the semantic fields but not the action
   graph body ("carried by its digest"). There is no canonical encoding of an `AgGraph`.
5. No `VisorCortexView` or `visor_cortex_snapshot` exists. It is only a declared
   extension point (src/visor/visor_world.h:39). Same for `VisorGenerationView` (:41).
6. `allen_goal_mutations` (src/allen/allen_bind.c:73) maps only latency intents
   (`CS_INTENT_GOAL_LATENCY`, :77) to goal fields 0..3. The one existing mapping of
   bounded text commands to the PL_OT_GOAL layout is the LS584 research adapter
   (omega-research-584 FREEZE.md section 11), which is not merged.

## Mapping table

| Contract item | Existing mechanism (file:line at base sha) | Status |
|---|---|---|
| s: entity objects | RxWorld objects created RX_PERSIST_RESIDENT exactly as `env_build` in tests/runtime/rx_plan_reuse.c:187 does: arrangement units (PL_OT_UNIT), the goal (PL_OT_GOAL, rx_plan.h:53), goal facts, World generation, cognitive generation, station, decision, index | reuse |
| s: committed history | Cortex journal opened with `cx_open` (rx_cortex.h:265), World attached with `rx_cortex_attach` (rx_cortex_record.h:42) so every crumb is recorded by the one World writer | reuse |
| s: retained knowledge (theta) | Plan templates (PlanTemplate, rx_plan.h) persisted as Cortex records through the attached writer: CX_CLAIM / CX_K_CANDIDATE (rx_cortex.h:101) with payload = `rx_plan_encode` bytes plus the graph body, promoted with `rx_cortex_promote` (rx_cortex_record.h:81) on a CX_K_EVIDENCE_REF (rx_cortex.h:102) after a verified reuse. Same pattern as LS584 FREEZE section 9 | extend (harness writes records through the existing writer; no new store) |
| s: generation store | `rx_generation.*` versions realization generations (rx_gen_propose rx_generation.h:178, rx_gen_promote :207). The arrangement path does not use it: theta's version is the cognitive generation object (PL_OT_COGNITION field 0) checked by `rx_plan_applicable` (rx_plan.c:299) | not used by this pilot (stated, not built) |
| Identity within one process | World digest `rx_world_digest` (rx_world.h:862) taken by the visor snapshot, compared to a fresh runtime read; plus the Cortex running chain re-verified with `cx_verify_chain` (rx_cortex.h:279); plus lineage = digest of Cortex record 1, the rule `allen_lineage` uses (allen_bind.c:33) | reuse |
| Identity across restart | Lineage (record 1 digest) and chain must match; the World digest cannot (fact 1 and 3: the World is rebuilt, its lowering objects are session state). The pilot defines an entity-state digest = SHA-256 over (role, type, field values) of the entity objects, using the object-digest rule of rx_world.c:62, computed from the live World and from the journal (`rx_cortex_recall_result`, rx_cortex_record.h:71); both must match the pre-restart value | extend (definition is the harness's; see MISSING PREREQUISITE A) |
| e: admitted event | An outside publication `rx_world_publish_external` (rx_world.h:838) of goal-object fields under a capability for (external subject, goal resource, WRITE); the field layout is PL_OT_GOAL (rx_plan.h:53). Recorded as CX_K_WORK_ACCEPTED | reuse (publication path); extend (the arrangement intent -> mutations step is done in the harness, see MISSING PREREQUISITE C) |
| "admitted" means | The World checks the capability: a publication without WRITE on the goal resource for the external subject is refused (negative return) and nothing is recorded as accepted | reuse |
| F_theta(s, e) | `rx_plan_view` -> `pla_goal_read` (rx_plan_arrange.h:55) -> `rx_plan_cache_retrieve` (rx_plan.h:235) -> `pla_bind` + `rx_plan_applicable` (rx_plan.h:214) -> either reuse or `pla_plan` search (rx_plan_arrange.h:90) + `pla_generalize` (:101) -> `rx_plan_execute` (rx_plan.h:270), which lowers the action graph onto resident reactions that commit through the World -> `rx_plan_record_use` (:275) / `rx_plan_cache_store` (:231) -> `rx_plan_decide` decision crumb (:283). The same sequence as `solve` in tests/runtime/rx_plan_reuse.c:447 | reuse |
| theta | The template table (PlCache) with status CANDIDATE / VERIFIED / STALE (rx_plan.h:105) and the verified World / cognitive generations | reuse |
| R(s, h) | `visor_world_snapshot` (visor_world.h:85) -> `visor_world_format_text` / `_json` (:90). It takes only the World pointer: h has no parameter in the current interface, so h = (which formatter, when the snapshot is taken) | reuse |
| R history part | `VisorCortexView` is missing (fact 5). The pilot's history view is a value-only formatter in the harness over `cx_recall` (rx_cortex.h:324) reads, with the same rules (bounded, deterministic, copies values, holds no authority) | missing -> harness formatter (MISSING PREREQUISITE B) |
| Committed outcome visible | The World view shows slots, kinds and the digest, not field values. The outcome (where each unit rests) is shown by the history formatter from the newest CX_K_EXEC_COMMIT per unit | extend |
| View detached | No snapshot or formatter call for an interval. The runtime keeps executing admitted work: `rx_plan_execute` runs on the World's reaction threads | reuse |
| View reattached | A new snapshot. Check: its digest equals `rx_world_digest` read directly, the journal chain verifies, the records seen at the earlier snapshot are still the same prefix (ids and digests), and the history view shows the committed outcome | reuse |
| Correction | A second outside publication through the same path, here advancing the cognitive generation (theta's version) and re-admitting the objective. The retained template is then refused by `rx_plan_applicable` on PL_AX_COG_GEN (rx_plan.c:299). Nothing writes into the cache from outside: re-derivation renews the same semantic id through `rx_plan_cache_store` (rx_plan.c:353). STALE is set only by `rx_plan_record_use` on a failed reuse (rx_plan.c:392) | reuse |
| Restart | Close the journal, end the process, start a second process: `cx_open` replays and checks every record digest (rx_cortex.h:265); the World is rebuilt (see A) and the template table is rebuilt from the journal's candidate and promotion records by decoding, recomputing the semantic id with `rx_plan_identify` and storing through `rx_plan_cache_store`. Survives: the journal. Rebuilt: World, template table, caches | extend |
| Tampered journal | `cx_open` refuses a record whose digest does not match (rx_cortex.h:258 context). The pilot then starts from empty retained knowledge and counts it | reuse |

## MISSING PREREQUISITES (described, not built)

A. World restore from the journal. Nothing in src/runtime rebuilds a World from Cortex.
The smallest change: a function in src/runtime/rx_cortex_record.c (owner: runtime/World)
that, given a journal and a freshly booted World with the same object layout, applies
the newest committed field values per subject through the outside publication path and
reports any subject it could not restore. Until it exists, the pilot's second process
boots the World from the frozen objective specification and applies the journal's
committed unit values through `rx_world_publish_external` in the harness. This is
disclosed as harness code, and identity across restart is checked on the entity-state
digest, not the whole World digest.

B. VisorCortexView. Declared at src/visor/visor_world.h:39, not implemented. Smallest
change: `src/visor/rx/visor_cortex_rx.c` with a value-only `VisorCortexView` filled by
`cx_range` / `cx_recall`, plus text/json formatters in `src/visor/visor_world.c`'s style.
The pilot's harness formatter is a stand-in, not a visor component.

C. ALLEN arrangement intents. `allen_goal_mutations` (src/allen/allen_bind.c:73) maps
latency intents only. Smallest change: an intent kind for arrangement goals whose
mutations fill PL_OT_GOAL fields 0..4. The pilot builds the same five mutations in the
harness and publishes them through the same World path ALLEN uses.

D. Canonical action-graph encoding. `rx_plan_encode` omits the graph body (rx_plan.h:164).
The pilot stores the `AgGraph` struct bytes in the candidate record and accepts them on
reload only when `rx_plan_identify` reproduces the recorded semantic id (which hashes the
graph digest). This ties the record to one build's struct layout. Smallest change: an
`rx_graph_encode` / `rx_graph_decode` pair in src/runtime/rx_graph.c.

## Not covered by this pilot
No UI, no GPU, no model inference, no new store or loop. The "screen" is the visor text
and JSON projection written to a file, plus the harness history formatter. Ablations
(elimination only / reuse only / incremental / combined) belong to #584 and #246.

## Addendum (found by the pilot, before any run of record)

E. Cortex World records carry one object per crumb. `record_link` in
src/runtime/rx_cortex_record.c:70 takes the first output (else first input) and stores only
that object's field values. A move committed by a lowered action graph names its graph cell
first, so the arrangement units' new positions never reach the journal as World records:
in a development run, every CX_K_EXEC_COMMIT record named a cell object and no record named
a unit after boot. A history view or a restart that reads World records alone would show the
boot arrangement. Smallest change (owner: runtime Cortex record): record the field values of
every output of a crumb (one record per output, or one payload with per-output slot,
generation, version and fields). The pilot's stand-in: after each commit the entity appends
a state observation (class CX_OBSERVATION, writer kind 0x2760 below CX_K_BASE, payload = slot,
generation and fields of each role object) through `rx_cortex_append`, the World's attached
writer. The history view and the restart read the newest observation, and every reattach
checks that the observation is newer than the last commit record.
