# Store exact-consumption correction: source-only candidate

Upstream source: `a35697e94b5eeb72efadfee6edab7f14344aea43` (`fix/security673-qualified`).
Stack parent: `708f5378847deeed6c4854b259c7f4f4b3f9ece2` (dispatcher protected read).
Issue scope: #671 ingress qualification, #649/#670 authority store, #673 tracker.
Review date: 2026-10-24.

## Finding and correction

`readrec` read 256 bytes and `readgen` read 8, then closed the descriptor. Neither checked
for remaining bytes. Thus a 257-byte record or 9-byte generation file with a valid prefix
was accepted despite the store's exact-length contract. This is a source-level finding;
no runtime reproduction or exploit claim is made.

Both parsers now require an EOF result from a one-byte probe before decoding. Extra data
and read errors fail closed through existing length-error codes. The descriptor is closed
on both paths. A new seeded `.om` regression checks exact lengths and a trailing byte for
each parser, using real file operations. Its old-source negative control does not depend
on the new implementation's extra scratch buffer.

Refresh source concatenations for store, dispatch and dispatch-order gates. Regenerating
store-gate also fixes a previously stale embedded `readgen` implementation: the component
already used `sgerr`, while the committed gate still returned error codes as generations.
No shared compiler, legacy runtime, production consumer, or historical evidence is modified.

## Verification and limits

- PASS: `git diff --check` (text/whitespace only).
- PASS: all four affected gate files equal their documented component concatenations.
- NOT_RUN: compiler reconstruction, compilation, positive and negative-control execution,
  existing store/dispatch/order gates, full security gate and independent final replay.
- Host: x86_64. Bundled seeds: Linux AArch64 ELF. No bundled executable ran and no toolchain
  was installed. These source checks are not functional qualification or native PASS.

No claim of concurrent-writer snapshot isolation, authenticated/canonical records, symlink
confinement, durability of directory entries, robust pid locking, or production cutover.
Missing review and required gates continue to block merging/release and issue closure.
