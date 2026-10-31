# Authority and reuse transitions promoted to `.om`

Date: 2026-10-23. Branch: `fix/security673-qualified`. Platform: Linux AArch64, native execution.

## Scope

The transition logic previously held only in the control artifact is now first-party seeded `.om`
under `seed/authority/`:

| Contract | Responsibility | Test result |
| --- | --- | --- |
| `action.om` | operation state ready/in-flight/unknown/performed, attempt and limit, generation, expiry, identity binding | `action checks=219 000`, exit 0 |
| `delegate.om` | child capability derivation and subset checks, including all small right subsets | `delegate checks=074 000`, exit 0 |
| `reuse.om` | exact-key reuse and whole-token prefix identity | `reuse checks=074 000`, exit 0 |
| `store.om` | 32-word durable record and generation file: temp write, fsync, atomic rename, exact read, torn refusal | `store checks=077 000`, exit 0 |
| `dispatch.om` | persist in-flight, /bin/true and static execpure effects, performed/unknown receipt, generation-change fence, duplicate/limit/stale refusal | `dispatch checks=037 000`, exit 0 |

The five `*-gate.om` files are committed source-plus-test concatenations because the current
compiler reads one file. That is a compiler capability gap (modules/imports), not a permanent
design.

## What is not claimed

- The transition contracts are pure. `store.om` adds a durable record/generation file and
  `dispatch.om` adds the state/effect/fence loop and runs a real `/bin/true` effect, but there is no
  process-tree budget, cross-process lock or consumer cutover yet.
- `store.om` uses a same-directory temp plus rename for atomic replacement; it does not implement a
  cross-process lock. A short or torn file is refused on read. Generation equality is a caller check
  (`storevalid`), not enforcement by the store.
- `dispatch.om` runs `/bin/true` for the performed path and synthetic fork children for the revocation
  and nonzero modes. It does not yet apply process-tree budgets or check a provider receipt after a
  crash. Revocation is detected after the
  effect returns; a change that lands after the read is outside this bounded test.
- The legacy C authority/reuse owners are still active. No consumer has been cut over.
- The tests run against the same seed compiler lineage; this is not an independent implementation
  proof.

## Evidence

- `action.out`, `delegate.out`, `reuse.out`: check counts.
- `exits.txt`: compile and run exits.
- `gate-authority.log`: the same three programs compiled/executed inside `gate669`.
- `identities.txt`: source, combined gate, compiler and gate hashes.

The gate result is `GATE669 PASS` with the authority programs at exit 0.
