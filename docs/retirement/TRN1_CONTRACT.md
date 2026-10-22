# L496 frozen first artifact: aien-replay TRN1 schema-1 verifier

Frozen before implementation, 2026-10-22. Owner L496. Sovereign-core origin/main
754c07b118f41ad899607b60329bd851b1ccb13e, entire
`crates/aien-replay/src/lib.rs`, introduced by ae89bbd7. This is the complete
Rust-built transcript verification utility, currently packaged as a library,
not an existing standalone CLI. No claim of a drop-in command or library ABI
replacement is made. The new native command replaces its verification and
comparison work; existing Rust callers remain legacy until migrated.

All public behavior is in scope: schema-1 verification, exact first-refusal
order/code/event, accepted record count/run id/final digest/per-record compared
digests/subsystem ids, subsystem registry, verdict_line and compare_line.
The unrelated KV ownership property tests in this crate exercise a different
crate and are not replaced. No runtime or GPU test is needed for this utility.

The Rust reader supports schema 1 only. Current protocols main also has schema
3; preserving the Rust contract requires refusing schema 3 with VERSION.
The original schema-1 corpus is protocols 4600677 (before schema-3 extension).
Run the entire original corpus and Rust conformance test, plus compare the
current corpus against the actual Rust reader and report the known schema gap.
The C schema-1 reference is an independent test oracle only. No C translation,
no edits to the L418 C work, and no compiler changes.

## Native executable interface

Linux AArch64, standard input/output, no files, environment, arguments or child
processes. Sequence of framed requests, reusable in one process:

- byte V, little-endian u64 byte length, transcript: print verdict_line + LF.
- byte D, little-endian u64 byte length, transcript: print verdict_line + LF,
  then for every accepted record `digest <64 lowercase hex> subsystem=<id>` + LF.
- byte C, length+expected transcript, length+actual transcript: print compare_line + LF.
- byte S, little-endian u16: print registry name or `unknown`, then LF.

EOF at a request boundary exits 0. Unknown opcode, truncated framing or failed
I/O prints `TRN1 transport error` on stderr and exits 2. Semantic refusals are
results, so they do not end the stream. Bytes after END belong to the framed
transcript and receive the original LENGTH refusal, never a new request.
Arguments are ignored. This framing is new; a test-only Rust adapter exposes
the original API with exactly the same framing. It is not shipped.

Fixed workspace: one maximum record (131128 bytes), two sets of 4096 compared
digests and subsystem ids, SHA-256 state, small output/transport buffers.
Streaming input preserves all Rust length/refusal behavior without an arbitrary
whole-transcript allocation limit. The original 4096-record, 65536 identity and
annotation limits remain. Rejected remaining bytes are drained before reuse.
No source-built C/Rust dependency may enter the native executable.

## Gates and cutover

RED absent native executable; GREEN full original corpus + Rust original test,
current corpus differential, generated valid/malformed/overflow/capacity/state
cases, fragmented pipe writes, fresh/repeated invocations, independent C oracle,
compiled wrong-result mutants. Record compiler/source/executable hashes and ELF
closure. Rebuild the pinned seed using existing seed/omega-from-seed.sh in an
isolated extracted source tree; verify the qualified fingerprint and seed suite.

Deploy only inside build/retirement, with a hash-verified atomic symlink swap,
smoke and rollback. Existing source, installed commands and services stay intact.
FR-1/FR-13 block file/argv callers; FR-7 blocks shared-library substitution.
Production caller replacement and source archiving require those integration
gates and review. L495 stays open; system status remains partial.
