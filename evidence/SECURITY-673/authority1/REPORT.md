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

The three `*-gate.om` files are committed source-plus-test concatenations because the current
compiler reads one file. That is a compiler capability gap (modules/imports), not a permanent
design.

## What is not claimed

- These are pure transitions. They do not mint authority, read or write a durable store, dispatch an
  effect, enforce a budget or revoke a grant.
- The legacy C authority/reuse sources remain active until the `.om` owner has a durable store,
  dispatch and revocation, and every consumer is cut over.
- The tests run against the same seed compiler lineage; this is not an independent implementation
  proof.

## Evidence

- `action.out`, `delegate.out`, `reuse.out`: check counts.
- `exits.txt`: compile and run exits.
- `gate-authority.log`: the same three programs compiled/executed inside `gate669`.
- `identities.txt`: source, combined gate, compiler and gate hashes.

The gate result is `GATE669 PASS` with the authority programs at exit 0.
