# Store write descriptor cleanup: source-only candidate

Publication base: `95529421e9a0e6d6d63ca5d8b8cdfaf443e0ef78` (#687).
Historical preparation base: `19948824fea22e66499af02f27feef3ce9efa5d9`.
Review date: 2026-10-24. Scope: authority store (#649/#670), #673 tracker.

## Defect and minimal correction

After a successful open, `writerec` returned 11 (failed/short write) or 12 (failed
fsync), and `writegen` returned 41 or 42, without closing the descriptor. Each
of those four paths now makes exactly one best-effort `fclose` call before
returning its original error. A secondary close error must not mask the primary
write/fsync error. No close retry is introduced: a failed close may already have
released the descriptor. Existing successful-write close errors remain 13/43;
record rename remains unreachable after any earlier failure.

No compiler, runtime implementation, ABI, store format, lock protocol, durability
claim, temporary-file removal, or historical receipt is changed. This does not
repair partial generation writes or guarantee cleanup if the process is killed.

## Deterministic source-level regression

`store-cleanup-test.om` is handwritten Omega. `store-cleanup-source.sh` composes
it with the real store source after mechanically renaming only five write-side
The composer uses portable awk token matching, rejects unexpected write-side
effect argument prefixes, and buffers output until source transformation and
harness reading both succeed. It does not rely on GNU sed word boundaries or
a pipeline that could mask a source-read failure.
primitive call prefixes to its effect doubles. Global buffer arguments are omitted because ordinary Omega functions cannot accept
global arrays; buffer contents are not under test. There are no production test hooks or
copied writer implementations. `read` remains real but the read functions are
never called. The fixture opens no real files; its final success message uses
real stdout. This is a control-flow/error-precedence test, NOT a real syscall,
fd-leak, persistence, concurrency, Linux, or hardware qualification.

Eighteen cases cover both writers and nine modes: open failure, negative write,
short write, fsync failure, close failure, success, write plus close failure,
fsync plus close failure, rename failure (the generation writer does not rename).
Each asserts the return code, exact ordered effect trace, and that every
write/sync/close receives the successfully opened fake fd 7. Traces distinguish
zero/one/multiple closes, skipped effects and unwanted rename. Open failure has
no close. Primary failures survive a simultaneous close failure.

In a disposable Linux AArch64 checkout, after approval of the reconstructed
compiler and its prerequisites:

```sh
mkdir -p build
sh seed/authority/store-cleanup-source.sh > build/store-cleanup.om
cmp build/store-cleanup.om seed/authority/store-cleanup-gate.om
seed/out/omc < build/store-cleanup.om > build/store-cleanup
readelf -h build/store-cleanup
chmod +x build/store-cleanup
build/store-cleanup
```

Run each command only after its predecessor succeeds; never run failed compiler
output. Expected: `store cleanup cases=18 fails=0`, exit 0. This standalone gate
is not wired into `gate669`; existing real store, exact-store, dispatch,
dispatch-order and full security gates remain separately required.

Negative control: extract `seed/authority/store.om` from the publication base above,
pass that path as the script's optional first argument, then compile/run the
result identically. It must exit 1 without the success message: ten cases (five
per writer) lack their required close. A compiler error, signal or timeout is
INVALID, not a passing negative control. Both fixed and negative native outcomes
are expectations, NOT observed results.

## Inherited fixture corrections preserved

The publication base already contains the #685/#686 identifier and global-array
argument corrections. This candidate preserves those test files and the README
fixture-correction notes byte-for-byte; it does not introduce or re-publish those
repairs. They were reviewed alongside the new cleanup fixture during preparation.

## Verification status

- PASS: source diff whitespace check and shell syntax check.
- PASS: source-only composer negative checks: separately changed the reviewed
  fopen/write/fsync/fclose/renames argument prefix in temporary source copies;
  all five returned nonzero with zero stdout. Missing source also returned
  nonzero with zero stdout. No Omega source was compiled or executed.
- PASS: independent source review of all three updated fixtures, scalar doubles,
  effect traces, identifier limits, array arguments and negative-control structure.
  This is source review, not compiler acceptance.
- PASS: global/text names <=8 and function names <=63 across all authority
  gate declarations; inherited global-array call replaced with a local copy.
- PASS: all four existing affected gate concatenations match their component
  sources exactly; new gate matches the source-composition script.
- PASS: the pre-fix control composition differs only by the four missing cleanup
  calls. No new production helper is needed by the old-source control.
- PASS: pinned crumb structural compile/verify (source metadata only).
- NOT_RUN: Omega compilation; fixed/negative execution; real syscall fd-leak
  regression; full security gates; independent Linux AArch64/hardware replay.
  No bundled native seed executable ran and no toolchain was installed.

Keep draft. Source review and metadata checks are not functional qualification.
