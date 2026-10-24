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
| `lock-test.om` | exclusive acquire, busy refusal, release, forked waiter and stale-pid reclaim |
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
| lock | `lock checks=5 fails=0` | 0 |

The authority gates and the standalone lock gate are run by
`seed/qualification/security673/gate669.om` as part of the security qualification; the dispatcher
also exercises the lock in its 37-check gate. A failure is a failed check count or a nonzero exit; a compile failure is
`INVALID`, not a pass.

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
Expected: `dispatch order checks=3 fails=0`, exit 0. Build/run the existing dispatch gate and
full security gate separately; this standalone regression is not yet wired into `gate669`.
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
This standalone gate is not yet wired into `gate669`; all compiled runs remain NOT_RUN.

`store-gate.om` has been regenerated as `store.om` followed by `store-test.om`. Its prior copy
also lacked the already-existing `sgerr` generation-error interface; it now tests the current
implementation rather than preserving that stale copy.

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
