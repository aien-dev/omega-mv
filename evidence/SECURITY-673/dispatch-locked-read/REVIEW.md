# Dispatcher protected-read correction: source-only candidate

Base: `a35697e94b5eeb72efadfee6edab7f14344aea43`, branch `fix/security673-qualified`.
Issue scope: #649 / #670, #673 tracker. Review date: 2026-10-24.

## Finding

`seed/authority/dispatch.om` loaded the record before acquiring `lacq`. Two callers can load
ready state, then acquire the lock in turn and independently pass `actstart` using stale copies.
Even if the lock implementation supplies mutual exclusion, serializing only check/write does
not serialize the read-check-write transaction. README and store comments promised the latter.
This is a source-level schedule argument, not a runtime reproduction or exploitation claim.

## Change

Acquire first, then read. On read failure, call the existing release operation and retain the
existing read-failure result 70. No compiler/runtime ABI, lock protocol, release owner, or
production consumer is replaced. Refresh the existing committed dispatch concatenation.

A new seeded `.om` test uses the actual store/action/dispatch code and a lock-acquisition double
that writes a performed record before acquisition completes. Three named checks cover the
stale-read refusal, read-error release, and acquisition-error refusal. The double is test-only;
it is not a second authority or production lock. The gate is a committed source concatenation.
See `seed/authority/README.md` for qualification and negative-control commands/expectations.

## Verification and limits

- PASS: `git diff --check` (text/whitespace check only).
- PASS: committed gate sources equal the declared concatenation of component source bytes.
- NOT_RUN: compiler reconstruction, compilation of either gate, regression execution,
  pre-fix negative control, existing dispatch gate, full security gate, and independent replay.
- The editing host is x86_64; bundled compiler programs identify as Linux AArch64 ELF.
  No bundled compiler or executable was run, and no toolchain was installed.
- NOT_RUN: native concurrent-process reproduction and physical qualification.

Residual findings are not silently fixed: stale-pid reclaim uses a read/check/unlink sequence
that can unlink a replacement lock; outcome reconciliation reads generation before reacquisition;
lock release currently suppresses unlink errors; store readers do not check for trailing bytes.
The patch makes no general atomic-dispatch, robust-lock, crash durability, or sandbox claim.
Existing required review, repository judges, compiler integration and release holds remain.
