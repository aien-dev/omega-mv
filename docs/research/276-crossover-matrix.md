# Symposium crossover matrix for aien-architecture#276

Written 2026-10-19 by the research/cost-sim-583 agent (ledger L050). Answers the
"Symposium crossover: unresolved" section of aien-dev/aien-architecture#276.

## What "Symposium" is

The tracking issue could not find Symposium because it is not an aien-dev repository.
It is the outside project **symposium-dev/symposium** ("cargo agents", licence MIT OR
Apache-2.0): a Cargo subcommand that scans a Rust workspace, matches plugins against
the dependency graph, and installs crate-matched skills, hooks and MCP servers into
whichever coding agent is in use, from one agent-agnostic manifest.

Pinned source read for this check (read-only clone at `~/workspace/_ext/symposium`):

- symposium-dev/symposium @ `b420d5dc4c71abde973cea8cb4f30ea4c8e76ae0` (main, 2026-10-19,
  "Merge pull request #322 from Fluzko/reference-docs"), 561 tracked files.
  Modules: `src/{agents,hook.rs,hook_schema,skills.rs,sync.rs,plugins.rs,predicate.rs,
  crate_sources,pm,telemetry,...}`. No cost model, scheduler, DAG, bandwidth, energy
  or hardware model exists in the tree: `git grep -il "cost model|simulat|resource.dag|
  bandwidth|joule|energy|roofline|critical path|learning mechanism|retention|payback"`
  matches only mdbook/RFD prose about telemetry and the blog post, never `src/`.
  The three `cost` hits in `src/` are comments about network cost of a registry fetch
  (`src/hook.rs:275`, `src/hook.rs:480`, `src/skills.rs:194`).
- Open PRs at the time of the check (30): #320 to #361. Every one is about agent
  plugin installation (Claude Code, Codex, Copilot, Goose, Antigravity), skill/hook sync,
  or opt-in local telemetry recording (#326 to #358). PR #339 "RFD: Evaluating agent
  tasks with and without Symposium" is an A/B protocol for coding-agent tasks, not a
  hardware or learning-cost experiment.

What AIEN already took from Symposium today (another Hermes session, same day):

- Audit and design: `~/workspace/agents-in-tree/BRIEF.md` (on the Spark).
- ADR 0038 "Agent guidance travels with the tree", aien-architecture branch
  `agents/in-tree-guidance` @ `600a7584c277fa02ff3fbea48088e1d0ac07f149`,
  `docs/adr/0038-agent-guidance-travels-with-the-tree.md`.
- Seven branch heads in `~/handoffs/2026-10-19-agents-in-tree.md`; the omega one is
  `agents/in-tree-guidance` @ `984b7eed9b86e1a25b79251ad5a30529f597faab` and touches only
  `.crumb`, `agents.toml`, `skills/omega-basics/SKILL.md`, `tools/.crumb`,
  `tools/agent-hooks/*` (10 files, 640 insertions).
- Borrowed-ideas plan: `~/handoffs/2026-10-19-borrowed-ideas/PLAN.md` (Symposium is not one
  of its 20 items; its sources are TensorFold, llama.cpp, vLLM, SGLang, seL4, Linux, etc.).

## Matrix

Overlap is judged on three tests the issue asks for: same mechanism, same file, same
contract. "None" means all three fail with the evidence named.

| Deliverable | Symposium (b420d5dc, PRs #320 to #361) | agents-in-tree work (ADR 0038, 7 branches) | Verdict |
|---|---|---|---|
| omega-mv#583 offline resource-DAG cost simulator (schema, evaluator over `src/runtime/rx_costmodel.c` estimates, bounds, energy accounting, attribution, held-out gates) | Mechanism: none (no cost, DAG, scheduling, bandwidth, energy or hardware code in `src/`). Files: none (Rust CLI for agent config; omega is C). Contract: none (its only versioned data contract is the opt-in telemetry JSONL in `md/rfds/telemetry-recording/contract/recorded-data.md`, about agent command counts and durations, not machine resources). | Mechanism: none (hooks that block `--admin`, force push, kill; skill lookup). Files: none. The omega branch touches no path under `src/`, `tests/`, `docs/research/`, `evidence/`. Contract: none. | **None.** |
| omega-mv#584 bounded learning-mechanism search, native pilot, retention and payoff | Mechanism: none (plugin matching is a static predicate over Cargo metadata, `src/predicate.rs`; no search, no update rule, no retention measurement). Files: none. Contract: none. PR #339 is an evaluation RFD for coding-agent productivity with and without Symposium: a methodological neighbour (A/B arms, held-out tasks) but a different subject, no shared artefact. | Mechanism: none. Files: none. Contract: none. | **None.** |
| aien-architecture#246 E03 plan-reuse campaign on `src/runtime/rx_plan.c` with lifecycle payback and retention accounting | Mechanism: partial idea only. Symposium caches resolved plugin predicates (`src/predicate_cache.rs`, `src/workspace_state.rs`) and re-syncs on each hook when dependencies change, which is "reuse a cached result while its inputs are unchanged, invalidate when they change". That is the same shape as plan reuse with precondition invalidation, but it is a build-tool cache keyed on `cargo metadata`, not a verified-plan store, and no code, file or contract is shared with `rx_plan`. | Mechanism: none for the campaign itself. The in-tree skills tell agents how to run tests and what not to touch; they do not implement or measure reuse. Files: none under `src/runtime/` or `tests/runtime/`. | **None at mechanism/file/contract level; one design idea in common (cache + invalidate on changed inputs), already present in rx_plan.** |

## Reuse / extend / missing, per #276's request

- Reuse from Symposium: nothing for #583/#584/#246. The parts worth reusing (one manifest,
  in-tree skills and hooks, translated per agent) were already taken by ADR 0038 and
  belong to agent guidance, not to cost simulation or learning search.
- Extend: nothing. No Symposium component can be extended into a cost model, DAG
  evaluator or learning-search harness without becoming a new project.
- Missing: nothing is missing because of Symposium. #583, #584 and #246 proceed on
  existing omega code (`rx_costmodel`, `rx_plan`, `omega_realize_synth`) as their issues
  already state.

## Consequence for sequencing

The "overlapping runtime changes wait for this check" gate in #276 is cleared: there is no
overlap, so #583 (additive offline harness) and #584 (bounded search) need no coordination
with Symposium or with the `agents/in-tree-guidance` branches beyond the ordinary rule of
not editing `agents.toml`, `skills/`, `tools/agent-hooks/` in their worktrees.

Status: PASS for the crossover check (the check ran and found no overlap). This is a
source observation at the pinned commits above, not a qualification run.
