# Authority and reuse transitions in seeded `.om`

These are the `.om` replacements for the transition logic currently held by the legacy C authority
and reuse owners (`src/runtime/rx_capq`, `rx_caproot`, `rx_semantic`). They are pure transition
functions: they validate a record, bind identities, check authority, and advance a state. They do
not mint authority, perform I/O, persist a transition or dispatch an effect. The durable store and
dispatcher are the next phase; the C owner remains active until every consumer is cut over.

| File | Role |
| --- | --- |
| `action.om` | operation lifecycle: ready/in-flight/unknown/performed, attempts, limit, generation, expiry, identity binding |
| `delegate.om` | child capability derivation and subset checks |
| `reuse.om` | exact-key reuse checks, including whole-token prefix identity |
| `store.om` | durable 32-word record and generation file: temp write, fsync, atomic rename, exact 256-byte read; torn/short files refused |
| `dispatch.om` | persists in-flight, runs /bin/true and a static execpure effect, records performed/unknown, and marks unknown if the generation changes during the effect |
| `lock.om` | cross-process O_EXCL pid lock with bounded retry and stale-pid reclaim; serializes read-check-persist, not the effect |
| `lock-test.om` | exclusive acquire, busy refusal, release, forked waiter, stale-pid reclaim and failed-release reporting |
| `*-test.om` | transition and store checks; no provider simulation |
| `*-gate.om` | source + test concatenation for the current single-file compiler |

`dispatch.om` holds `lock.om` across read, generation check, start and the in-flight write, releases
before the effect, and reacquires before recording the outcome. The dispatch gate therefore exercises
the locked path (37 checks).

`action-gate.om`, `delegate-gate.om` and `reuse-gate.om` are committed concatenations because the
seeded compiler currently reads one source file. Module/import support is a compiler capability
dependency; it is not a reason to keep a legacy implementation.

## Run

```
seed/out/omc < seed/authority/action-gate.om > build/action-gate
chmod +x build/action-gate
./build/action-gate
```

Expected output and exit:

| Gate | Output | Exit |
| --- | --- | --- |
| action | `action checks=219 000` | 0 |
| delegate | `delegate checks=074 000` | 0 |
| reuse | `reuse checks=074 000` | 0 |
| store | `store checks=077 000` | 0 |
| dispatch | `dispatch checks=037 000` | 0 |
| lock | `lock checks=8 fails=0` | 0 |
| dispatch-order | `dispatch order checks=3 fails=0` | 0 |
| store-exact | `store exact checks=4 fails=0` | 0 |
| store-cleanup | `store cleanup cases=18 fails=0` | 0 |

The authority gates and the standalone lock gate are run by
`seed/qualification/security673/gate669.om` as part of the security qualification; the dispatcher
also exercises the lock in its 37-check gate. A failure is a failed check count or a nonzero exit; a compile failure is
`INVALID`, not a pass. The three newer regressions run after the existing authority gates,
each with its own fresh child working directory beneath `build/security673-native`. Their
relative `build/authority-store` fixtures cannot overwrite each other or the earlier gates.
This filesystem separation is not a sandbox. See the coordinator README for retained artifacts
and native verification limits.

## Cutover rule

The lock is integrated in the dispatcher, but no production consumer uses this store yet. The C authority/reuse
sources are deleted only after the `.om` owner has a durable store, dispatch, revocation and budget
behaviour, and every consumer uses it. A parallel production authority path is not an end state.

## Dispatcher acquisition-order regression

`dispatch-order-gate.om` links the production action, store and dispatch sources with a
**test-only lock-acquisition schedule** from `dispatch-order-test.om`, rather than `lock.om`.
The acquisition double writes a performed record before granting the lock. A dispatcher that
reads before acquiring sees stale ready state and misses the required duplicate refusal (22).
The other two named assertions require release on a failed protected read (70), and require an
acquisition error (80) to return without a read or release. No timing or scheduler luck is used.
This checks dispatcher ordering; it does not qualify pid-lock exclusion, stale-owner recovery,
revocation fencing, or production concurrent execution.

In a fresh disposable Linux AArch64 checkout, after rebuilding the existing approved compiler:

```sh
mkdir -p build
seed/out/omc < seed/authority/dispatch-order-gate.om > build/dispatch-order-gate
readelf -h build/dispatch-order-gate
chmod +x build/dispatch-order-gate
build/dispatch-order-gate
```

Run each command only after its predecessor succeeds; never execute failed compiler output.
Expected: `dispatch order checks=3 fails=0`, exit 0. This regression is also wired into
`gate669`; the existing dispatch and lock gates remain in that campaign.
The negative control is the same compilation unit with only `dispatch.om` restored to the base
revision's pre-acquisition read. It must print `FAIL dispatch acquired-record refusal` and exit 1;
a compiler failure, signal, or timeout is INVALID, not a passing control.

Committed concatenations are the byte-for-byte sequences below (no extra separators):

- `dispatch-gate.om`: action, store, lock, dispatch, dispatch-test.
- `dispatch-order-gate.om`: action, store, dispatch, dispatch-order-test.

The acquisition-order patch and regression were source-reviewed only on the editing host;
compiled positive/negative runs remain NOT_RUN until the AArch64 qualification above is performed.

## Exact store-input consumption regression

`readrec` accepts exactly 256 bytes and `readgen` accepts exactly 8 bytes. Both now probe for
EOF before decoding. A trailing byte or an error in that probe is refused through the existing
record error (22) or generation error (`sgerr=32`, returned value 0). This is exact stream
consumption, not authentication or a defense against concurrent file replacement/writes.

`store-exact-gate.om` is exactly `store.om` followed by `store-exact-test.om`. It uses real
store writes/reads and appends a synthetic byte with Linux `O_WRONLY | O_APPEND` (1025).
It checks the exact-length positives and both trailing-byte refusals. In a fresh disposable
Linux AArch64 checkout, using the reconstructed and approved existing compiler:

```sh
mkdir -p build
seed/out/omc < seed/authority/store-exact-gate.om > build/store-exact-gate
readelf -h build/store-exact-gate
chmod +x build/store-exact-gate
build/store-exact-gate
```

Run each command only after its predecessor succeeds. Expected output is
`store exact checks=4 fails=0`, exit 0. The negative control concatenates the unmodified
`store.om` from `a35697e94b5eeb72efadfee6edab7f14344aea43` with this test; it must report
`FAIL store trailing-record refusal` and `FAIL store trailing-generation refusal`, exit 1.
The test deliberately has no dependency on the new EOF-probe scratch buffer, so the old-source
control remains a valid compilation unit. Failed compilation, signals and timeouts are INVALID.
Run the existing store, dispatch, dispatcher-order and complete security gates as regressions.
This gate is also wired into `gate669`; all compiled runs remain NOT_RUN.

`store-gate.om` has been regenerated as `store.om` followed by `store-test.om`. Its prior copy
also lacked the already-existing `sgerr` generation-error interface; it now tests the current
implementation rather than preserving that stale copy.
## Lock release errors

`lrm`/`lrel` return 0 only when unlink succeeds, and 41 on unlink failure (including
an absent path). The lock gate checks ordinary release, directory refusal and
missing-path refusal. Its directory fixture is removed with `rmdir`; run gates
only in disposable test working directories, never against an active store.

The pre-effect dispatcher release check returns 78 on this failure. Other dispatcher
cleanup and final-release sites still ignore the release result; this change does not
claim those paths report it, nor does it redesign stale-lock reclamation.

## Fixture source correction (2026-10-24)

The dispatcher-order fixture now uses global/text names within the existing
compiler's eight-character limit and copies its scheduled record into a local
array before calling `writerec`; global arrays cannot be function arguments.
The exact-store fixture likewise uses global/text names within that limit.
These correct known source compilation blockers without changing the test
scenarios or production sources. Declared concatenated gates are regenerated
from this revision's own component sources. Source review is not compiler
acceptance: Omega compilation and native positive/negative runs remain NOT_RUN.
Historical evidence is unchanged.

## Store write failure cleanup

`writerec` and `writegen` close their opened descriptor once on failed/short
write and failed fsync, preserving the primary errors (11/12 and 41/42) even
if cleanup also fails. Close is never retried. Successful-path close errors
remain 13/43, and record rename is skipped after any earlier error.

`store-cleanup-test.om` provides 18 handwritten deterministic fake-effect cases.
The committed `store-cleanup-gate.om` contains a test-only derivation of `store.om`
followed by this test verbatim. It checks effect order, exactly-one close,
descriptor identity and primary error precedence. It does not test real syscalls,
fd ownership, durability or hardware. Production code has no test hooks.

### Derivation and manual source-correspondence review

New first-party runners/automation must be seeded `.om`. The temporary shell
composer used during preparation is removed from active source; it was disposable
authoring validation, not a supported runner. No replacement compiler or runner
is introduced. Keep the handwritten test and committed `.om` gate reviewable.

To derive the store prefix, preserve every line and scalar argument of `store.om`
except these exact effect-call prefixes (ignore `note` comments):

| Production prefix | Test-only prefix |
| --- | --- |
| `fopen spath` | `cfopen` |
| `write int fd sb` | `cwrite int fd` |
| `fsync int fd` | `cfsync int fd` |
| `fclose int fd` | `cfclose int fd` |
| `renames spkt` | `crenames` |

The omitted global-buffer arguments cannot be passed to ordinary Omega functions;
buffer contents are outside this test. Append `store-cleanup-test.om` unchanged.
Before any approved compilation, manually compare the gate prefix against
`store.om`: only the five substitutions above may differ. Compare its remaining
suffix byte-for-byte against the test. Check that no executable `fopen`, `write`,
`fsync`, `fclose` or `renames` token remains in the derived store prefix; reject
any unrecognized call shape rather than leave a real write-side effect behind.
The store read functions retain real `read` primitives but are never called by
this fixture. The harness alone uses real stdout for its success message.

For approved Linux AArch64 replay, compile the committed `store-cleanup-gate.om`
with the existing reconstructed compiler, inspect the output, and execute only
a successfully compiled artifact. Expected output: `store cleanup cases=18
fails=0`, exit 0. For the negative control, apply the same reviewed derivation
to `store.om` from `95529421e9a0e6d6d63ca5d8b8cdfaf443e0ef78`, followed by the
current test unchanged. That prefix must differ only by four missing cleanup
calls. Expected: exit 1 without the success message. Compilation failure,
signal or timeout is INVALID, not a passing control.

The historical preparation checks in
[`store-write-cleanup/REVIEW.md`](../../evidence/SECURITY-673/store-write-cleanup/REVIEW.md)
remain unchanged as evidence of checks observed then; its removed shell-helper
commands are not current replay instructions. The manual derivation above and
committed gate supersede those commands. Current source correspondence, absence
of unmatched write-side effects, and metadata checks pass; all Omega compilation,
native positive/negative runs and security qualification remain NOT_RUN. This
gate is wired into the existing security coordinator with its own fresh child
working directory; the standalone replay and negative control remain available.

## Native replay and compiler register bound (2026-10-25)

The original cleanup gate at `08bd7aa7d7b73864e56163772ccceaa9cc91de0b`
failed compilation with error 12 at line 274. Moving its unchanged three-part
assertion into `cbadcase` reduces live register pressure in `main`. The committed
concatenation, ending with the handwritten test verbatim, now compiles. No
production store behavior or expected outcome changes.

Linux AArch64 native replay with the reconstructed compiler
`66b4cf34e5cd2c8ff003bd142e7309f52178b66c2380f78ac313b6eb8b67471b`
passed dispatch-order (3), store-exact (4), lock (8), and cleanup (18).
Compiled controls restoring read-before-lock, accepting a trailing record byte,
masking unlink failure, and omitting the four error-path closes each compiled
successfully and exited 1 from their fixture assertions. The cleanup control
fails `cbadcase`; the lock control fails its accumulated release-result checks.
These are not crashes or failed-build controls. The exact-input control covers
the record probe only; it does not claim the generation-probe mutant was run.

Raw bindings and output are in `../qualification/security673/repairs/native.json`.
The `.om` mutation producer in that directory requires exactly the declared
number of source matches before writing a mutant. Historical NOT_RUN statements
above describe their original preparation; this replay supersedes them only for
these four positive fixtures and these four controls. The full coordinator,
production revocation fencing, stale-pid ABA exclusion, actual concurrent
effects, power loss and durable aggregate accounting remain unqualified here.
