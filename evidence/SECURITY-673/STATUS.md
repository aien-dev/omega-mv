# Omega #673 security status matrix

Date: 2026-10-23. Branch: `fix/security673-qualified`, tip `c60e5e4`.
This file records what is qualified, what is not run, and what is blocked. A source-only result is
never a PASS for an integration, hardware or release claim.

## Qualified slices

| Area | Status | Evidence |
| --- | --- | --- |
| #669 signed branch limits, forward/backward extrema, refusal 24 | PASS, bounded native AArch64 | `correction1`, `independent669`, `branchforms1` |
| #669 branch-form semantics (B.cond, CBZ, loops, else, function boundary, trap sites) | PASS for 15 compiled/executed cases | `branchforms1` |
| #670 inherited-descriptor closure before exec | PASS, old compiler fails the same assertion | `exec1`, `independent670` |
| #670 strict pinned-executable profile (Landlock, seccomp, rlimits, one pinned fd) | PASS for hostile child and four raw fixtures | `pure2` |
| #670 bypass/fail-closed controls | PASS: seccomp bypass 71, Landlock bypass 77, close_range unavailable 38, CPU 152, file limit 1 MiB | `pure3`, author run `pure1` |
| #671 compiler source families | PASS, 18 deterministic cases | `hostile1`, `independent671` |
| #671 `execprog` packed-list ingress | PASS, 9 cases | `packet1` |
| #671 `renames` packed-list ingress | PASS, 7 cases | `renames1` |
| #671 native path and `argread` boundaries | PASS, 7 cases | `paths1` |
| Reproducibility of the seed compiler | PASS: `om4 -> stage1 -> stage2` fixed point | `pure2`, gate logs |

## Not run, with the exact reason

| Area | Status | Reason / prerequisite |
| --- | --- | --- |
| #669 long backward source reachability | NOT_RUN | The current backward B.LO caller is a short array-fill loop; direct encoder extrema do not prove a source-level long backward branch is reachable. |
| #669 release integration and consumer audit | BLOCKED | Needs the compiler/release owners' integrated review and a real consumer outside the qualification corpus. |
| #670 grant identity, generation, revocation, durable budgets, atomic check/dispatch | BLOCKED | Seeded `.om` action/store/dispatch and a cross-process pid lock exist, but the dispatcher does not yet hold the lock across check/persist and no production consumer is cut over. |
| #671 manifests, receipts, RPC/tool arguments, model metadata, trace bundles, device descriptions | BLOCKED | No seeded `.om` implementation exists; these are migration dependencies, not PASS. |
| #649/#659 control lifecycle and authority integration | BLOCKED | The control contracts exist as `.om` artifacts (`control659`) but are not wired to a durable authoritative store and real dispatch. |
| #651 capability-aware reuse | BLOCKED | Needs the `.om` successor of the existing semantic reuse/store owner; no second cache is authorized. |
| #607 private-data confinement and exact-byte export consent | BLOCKED | A seeded `.om` SHA-256 passes five standard vectors, but the Open Humanity receiver contract is absent; no second wire contract is invented. |
| spark-rsi #67 poisoned-benchmark defenses | BLOCKED | Needs the `.om` successor of the existing promotion/evaluator gate; the current owner is Rust. |
| #672 device/DMA isolation and reset | NOT_RUN | Requires reserved physical hardware and driver work. Emulation is not hardware qualification. |
| #650 diverse double compilation | NOT_RUN | The C reference path cannot express `omc.om`; no sufficiently independent route exists for the final compiler. Same-lineage fixpoint is reproducibility, not DDC. |
| #623/#625 semantic validation and conformance | NOT_RUN | Requires the semantic checker owners and a broader consumer corpus. |
| #639 cryptographic replay/rollback/nonce campaign | NOT_RUN | Cross-repository adapter work with the Open Humanity, Aienux and Sovereign Core owners. |
| #624 aggregate conformance and teaching evidence | PARTIAL | This matrix aggregates the current evidence; independent negative controls and the teaching example are still incomplete. |

## Assurance-contract checklist

| Requirement | Status |
| --- | --- |
| Trusted-component and deployment-profile inventory | PARTIAL: seed compiler, strict launcher, host tools identified; devices/updater/boot not qualified |
| Per-property threat, asset, enforcing path, evidence | PARTIAL: recorded for #669/#670/#671 slices in the evidence reports |
| Separate compromised-agent, hostile-document, dependency, compiler, revocation, crash, device models | PARTIAL: agent/document and compiler-input models covered; revocation/device models NOT_RUN |
| Finite versioned supported-scope matrix | THIS DOCUMENT |
| Automatic claim invalidation on source/dependency change | NOT_RUN |
| Release/recovery exercise, old-artifact rollback, key compromise | NOT_RUN |
| Security response/requalification procedure | NOT_RUN |
| Independent final-source review and clean-checkout replay | PARTIAL: independent reviews exist for 669/670/671; final assembled-system review NOT_RUN |
| Residual risk and actionable blockers recorded | THIS DOCUMENT |

## Release position

The branch is not merged to `main`; `main` has no `seed/` tree. No deployment, hardware
qualification, key-compromise exercise or issue closure follows from these source and native-host
results. Missing hardware, independent compiler route or authority integration is `NOT_RUN` or
`BLOCKED`, never `PASS`.
