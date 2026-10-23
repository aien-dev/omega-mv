# Accepted bounded macOS ARM64 target milestone — L615

## Verdict

The scoped L615 milestone is ACCEPTED. This is not full macOS toolchain support.
Issue634 remains OPEN for platform install, project workflow, broader language,
ABI, runtime attachment and package gates. Integration remains with Renda/L624.

The handwritten Omega adapter and original full source review are pinned to
f43e6755d95a9783040b5ec2147ae519e9b01a11. A parent host-wrapper correction at
80d88a6af1425d28881daf8830eb966dc0ae9f3c fixes directory destinations without
changing adapter source, producer binary or any of15target artifacts.
Original independent review SHA256:
8df2113552666e3ec034100264d2cd7b8fdb166e0a1f5747070033e6c7cf1fe1.
Final focused correction review SHA256:
a6e0f82b8a550804f7e192fb1d6bf023ef258329ec12aed547e966f272cf7729.
The final correction review has no findings and confirms the prior Minor fixed.
Both original reports are preserved, not replaced by parent summaries.

## Executed evidence

- Source-built Spark combined gate67checks, including6output-path regressions.
- Retained four real pre-fix RED directory/symlink-directory failures and GREEN.
- Original10Linux native fixtures,5compiled controls,172hello-byte mutations.
- Parent physical ARM64 Mac, macOS26.6.2:12native behavior cases passed, with
  actual ad-hoc signing/strict verification, original artifacts and raw receipts.
- All15unsigned Mach-O artifacts plus adapter source and seed-built producer
  match original Mac replay inputs exactly:17identities checked after correction.
- Apple dyld/libSystem/signing and Linux GNU/shell/Node host orchestration are
  disclosed dependencies. No C/Rust/LLVM production compiler/runtime fallback.

See evidence/parent-native/, evidence/review-fix/ and evidence/final-review/.
The Spark combined gate's local macOS NOT_RUN label is deliberately retained:
it did not execute macOS programs. The separate physical-Mac receipt supplies
that evidence. No new native run is invented for a wrapper-only correction.

## Canonical integration shape

Public-main base: a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a.
Candidate branch: dev/macos-target-public-L615 in aien-dev/omega-mv.
This carries only ports/macos-arm64/ into public main, with exact reviewed module
bytes and evidence, avoiding294unrelated ancestor seed/compiler files from the
original57f5f425-based worker branch. L408 compiler ownership is untouched.

Supersedes dev/macos-arm64-L615 and verify/macos-native-L615 as merge candidate;
retain their immutable source and receipt pins as provenance, do not merge those
whole history branches independently. No PR, merge, release or deployment was
performed by this coordinator. Desk L624 retains all normal acceptance policy.

This accepts only validated bounded seed-ELF to ARM64 Mach-O integer/control
flow and one exact hello, with explicit refusal grammar in CONTRACT.md. It is
not an installable native Mac compiler, general Apple ABI or universal platform
support. Minimum OS claims beyond the actual tested26.6.2host remain unqualified.
