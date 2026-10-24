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
