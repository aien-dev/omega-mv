# Pull requests, Spark qualification and Alpha verification

This runbook follows `.github/workflows/aien-verification.yml` and `AGENTS.md`.
The retired signed-bundle-before-every-merge process is not the current contract.
Source snapshot: `ec07d25d5506d33567adbd4d9b518177b7933bad` (2026-10-26).

## Pull request checks

`AIEN Checks` runs on pull requests and pushes to `main`. It verifies crumbs and,
for repositories enrolled by `.aien/verify.pub` at the base ref, runs the portable
smoke set selected from the base ref. It does not fetch a signed Spark bundle for
every pull request. Hosted checks are advisory, not Alpha qualification.

Verifier source revisions are pinned in the workflow. `actions/checkout@v4`,
`actions/cache@v4` and `dtolnay/rust-toolchain@stable` remain upstream-tracking
references. The toolchain policy is stable Rust, not an immutable compiler or a
fully pinned CI execution environment. A verifier source pin must not be described
as end-to-end immutable supply-chain reproducibility.

## Review and merge

Open a draft pull request with the exact local commands, observed results and
limits. External review is expected before merge. Do not add `ready-to-merge`
while review is pending or without authority. A pull request, a merge, an Alpha
qualification and a deployed binary are separate states.

The checked-in workflow records Drake's 2026-10-24 policy that no GitHub status
check gates a merge. Historical references to required `AIEN Verification` and
`evidence-immutable` checks describe the retired contract, not current branch
protection. Inspect live repository rules before making an administrative claim;
this runbook neither configures nor overrides those rules.

## Spark and Alpha

The Spark runs the selected gates plus `.aien/always` and signs its evidence.
The local pipeline publishes bundles under
<https://github.com/aien-dev/aien-receipts/tree/main/omega> and pushes annotated
`alpha/YYYYMMDD.N` tags when authorized. `AIEN Alpha` runs only for `alpha/*` tags;
it requires the tagged commit to be on `main` and verifies the signed Spark bundle
with `--require-full` against the trust anchors on `origin/main`.

Host and emulator checks do not establish GPU qualification. Missing execution is
`NOT_RUN`, never PASS; hardware gates require the operator's current authorization.

## Changes to gates or trust infrastructure

A head changing `.aien/`, `tests/gates/`, `.github/workflows/`, `.forgejo/` or
`tools/aien-test/` is `BLOCKED_OPERATOR`: the worker does not run it autonomously.
An authorized operator run on the Spark (`aien-local-ci once omega <sha> --operator`)
is required for its signed bundle. An offline local regression test does not grant
that authority or remove the operator gate.

The zero-disk-secrets gate scans all tracked paths, including generated directories,
and untracked paths outside root `.git/` and `build/`. It never reuses another
commit's absence-of-secrets result. The seed host qualification requires all three
independent IR oracles and its fixed original-fixture/status manifest; an empty
selection or missing oracle cannot produce PASS.

## Operational evidence

- Spark state: `aien-local-ci status`.
- Worker journal: `~/.local/state/aien-local-ci/journal.jsonl`.
- Pipeline journal: `~/.local/state/aien-local-ci/pipeline.jsonl`.
- Published bundles: the `aien-receipts` link above, indexed by exact commit.
