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
| `*-test.om` | transition checks; no store or provider simulation |
| `*-gate.om` | source + test concatenation for the current single-file compiler |

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

The same three programs are run by `seed/qualification/security673/gate669.om` as part of the
security qualification. A failure is a failed check count or a nonzero exit; a compile failure is
`INVALID`, not a pass.

## Cutover rule

The C authority/reuse sources are deleted only after the `.om` owner has a durable store, dispatch,
revocation and budget behaviour, and every consumer uses it. A parallel production authority path is
not an end state.
