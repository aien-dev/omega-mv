# L530 independent parent re-review of L516

Reviewed source head `63bcce021741d18aeafb4b88afc46182c85840d0` and final evidence-only head `2dee6673dc6df329ee4b4913069affd1ea307e29`, branch `issues/trace-review-fixes-20261022`. Remote ref matched, author worktree clean. Implementation was by L516; the parent personally performed this source review, fresh host replay and new adversarial probes. No review worker was spawned.

## Disposition

L512 source findings T1/T2/T3 are **resolved in this follow-on**, within tested synthetic host behavior. Original head `ed6ddfb485dd8401e770ad0750713fa007122670` remains HOLD when taken without these fixes. Desk can intake the follow-on with its original dependency; this does not close issues607-612 or qualify native callback, actual training, hardware, browser, performance or cross-project consent/transport.

- **T1:** Mandatory span metadata is now explicitly enumerated in the exact preview/package privacy surface with a workload-structure notice. Parent independently walked every emitted non-metric field including nested time fields and required its presence in the disclosure. Selected metrics retain their separate field list. This fixes hidden disclosure; it is not a grant of publication consent.
- **T2:** Synchronous session-marker acquisition precedes producer enqueue. Worker token adoption verifies the marker; durable-close success removes it, while persistence/startup failure preserves uncertainty. Reads conservatively inspect marker presence at open and close. Parent stale-marker probe gives INCONCLUSIVE, unknown-loss warning and packaging refusal; recovery into a new archive retains unknown loss and leaves the source unchanged. The full suite includes real terminated-child cuts and EIO/close-race controls, which parent actually executed.
- **T3:** Readers detect tombstoned files still present and report deletion incomplete, without silently deleting user data. Parent replayed the tombstone-before-unlink boundary, verified the residual remains visible and source byte hashes are unchanged. Explicit deletion already fsyncs its directory; the corresponding removal-of-fsync mutant is caught. Query/recovery retain read-only source semantics.

## Fresh verification

Commands from the exact source checkout, with parent-owned output directories:

```sh
OMEGA_TRACE_BUILD_DIR=/home/drakestapleton/workspace/targets/L516-parent/build \
  quiet-build timeout 120s tests/trace/run.sh --under-quiet
node /home/drakestapleton/handoffs/omega-issue-sweep-20261022/L516-parent-probe.mjs \
  "$PWD" /home/drakestapleton/workspace/targets/L516-parent/adversarial
OMEGA_TRACE_MUTANT_EVIDENCE_DIR=<fresh>/original-mutants \
  quiet-build timeout 120s node tests/trace/mutants.mjs
OMEGA_TRACE_REVIEW_MUTANTS=<fresh>/review-mutants \
  quiet-build timeout 120s node tests/trace/review-mutants.mjs
quiet-build timeout 60s strace -f -qq -e trace=network -o <fresh>/network.log \
  /home/drakestapleton/workspace/targets/L516-parent/build/network-deny \
  env OMEGA_TRACE_NETWORK_DENIED=1 node tests/trace/review-offline.mjs
```

Actual fresh `<fresh>` path: `/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L516-direct-review-20261022T195627Z`.

- Host gate exit0,50/50 tests,0 skipped/cancelled; existing encoder oracle2 records/12 assertions and socket-denial EPERM positive control; original offline8 assertions.
- Parent independent probes:26 assertions, all pass.
- Original mutation suite17/17 and new review suite9/9 caught by behavioral assertions. No timeouts/import/compiler failures counted.
- Follow-on offline audit14 assertions, exit0; parser found0 network calls in the strace log. This is one parent network audit. The author's two-audit claim remains separate author evidence.

A first parent mutation invocation refused an already-existing output directory with EEXIST before tests. That was not credited as a passing or caught-mutant run. The successful runs above used a fresh timestamped directory; no existing results were deleted or overwritten to make the gate pass.

## Remaining limits

Review covers the reported source defects and scoped synthetic tests, not a whole-program security proof. Native callback attachment remains blocked/unqualified. Real training, representative hardware overhead, browser footprint and cross-project independent consent/signing/intake remain NOT_RUN. No live model/driver/server change. The parent's separate direct #600 experiment is unrelated and remains a negative scientific result.

Raw parent results and the independent probe are in `evidence/L530/`. This report is a source-defect re-review, not authority to merge the old uncorrected head or close the broader hardware issues.
