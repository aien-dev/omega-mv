# Lock release error reporting: source-only candidate

Source base: `a35697e94b5eeb72efadfee6edab7f14344aea43` (`fix/security673-qualified`).
Stack parent: `666728518ec0ce7580328dc926b5db8409ef664a` (#686).
Review date: 2026-10-24. Scope: #649/#670 authority store, #673 tracker.
Remote base was rechecked before editing; open PRs #685/#686 overlap the generated
`dispatch-gate.om` and are retained in this stacked correction.

## Defect and minimal correction

`lock.om::lrm` returned zero on both negative and successful `unlink` results. Consequently
`lrel` always claimed success and the existing dispatcher pre-effect error check could
never observe a failed release. Return 41 for a negative result; retain zero on success.
This is source-confirmed, not a native runtime reproduction or exploit demonstration.

Caller review:
- `lrel` directly forwards the result.
- `ltry` already maps a nonzero stale-lock removal result to 41; this branch is now reachable.
- `dispatch` checks the release before starting its effect and returns 78 on failure.
- Other dispatcher error cleanup and final-release sites ignore `lrel`; unchanged here.
- Existing lock-test initial best-effort cleanup ignores a missing-path error intentionally.

This does not repair automatic stale-reclaim races, PID reuse, lock-file ownership, or
ignored cleanup/final-release failures. No production consumer cutover, compiler, ABI,
locking protocol redesign, or historical receipt change is included.

## Regression and expected negative control

The existing lock test retains all five checks, adds an explicit successful final release
check, creates a directory at the now-empty lock path, checks release returns 41, removes
that directory with `rmdir`, then checks missing-path release also returns 41. Directory
unlink is refused independently of permission/root status. Fixture creation/removal errors
exit 101/102. Use a disposable working directory with no active authority store, as with
the existing file/fork fixture; the suite is not safe against a live store.

Expected fixed result: `lock checks=8 fails=0`, exit 0. Expected old-source control:
concatenate the base's `lock.om` with the new `lock-test.om`; it remains compilable without
new implementation symbols, but the two failed-unlink checks increment fails and main
returns 1 without a pass message. These are expectations, NOT observed runtime results.

## Verification status

- PASS: `git diff --check`.
- Qualification coordinator already compiles/runs the lock gate and checks child exit;
  it has no hardcoded lock stdout count to change.
- PASS: lock gate equals lock + lock-test; dispatch gate equals action + store + lock +
  dispatch + dispatch-test. No other authoritative source embedding of lrm exists.
- Source review: eight check calls; ordinary release0, failed release41; rmdir primitive
  already used by seeded host directory tests; qualification coordinator observes exit.
- NOT_RUN: native compilation, fixed/old-source execution, existing dispatch and full
  security gates, independent AArch64 replay. Editing host is x86_64; no bundled seed
  executable ran and no toolchain was installed.

Source checks are not functional qualification. Keep draft and preserve all required gates.
