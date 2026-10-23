# AI OS source review, 2026-10-23

This review inspected public source and documentation. It did not boot or
qualify the six operating systems. Founder background, team size, line counts
and use of AI in development are not evidence of correctness or performance.

The most transferable implementation ideas are explicit uncertain outcomes,
exact request binding, attenuated authority and fully identified reusable
artifacts. The accompanying Om code implements bounded contracts for those
ideas. System integration and hardware qualification remain separate gates.

## OctantOS

The canonical public repository found was
[MatrixForgeLabs/OctantOS-Public-Docs](https://github.com/MatrixForgeLabs/OctantOS-Public-Docs/tree/35f90f4dd4f03ce77f23733af2b8a39e9a459d9b).
The inspected tree contains documentation and diagrams, not kernel source.
Its [capability manifest design](https://github.com/MatrixForgeLabs/OctantOS-Public-Docs/blob/35f90f4dd4f03ce77f23733af2b8a39e9a459d9b/docs/capability-manifests.md)
describes signed manifests, parent-child permission intersection, bounded
resources, delegation depth and lifetime. Enforcement is described through
Linux namespaces, seccomp and cgroups. That public material does not let us
verify a custom Rust kernel or the claimed security guarantees.

Useful: authority should diminish through delegation; resource bounds and
expiry belong in the same contract. `src/delegate.om` implements the bounded
predicate. The existing authority owner still verifies credentials, reserves
budgets and issues grants. Documentation about a security mechanism is not an
independent test of its enforcement.

## khy

The [khy-os 1.1.7 PyPI release](https://pypi.org/project/khy-os/1.1.7/)
provides a source archive. Its linked GitHub repository, `kodehu03/khy-os`, was
unavailable during review. The downloaded source archive's SHA-256 was
`9c8fe8263e6cddd04af0d668a841aeb8d3899d4fbcc92fd81498995ce375391d`.

Unlike a documentation-only project, the archive contains C kernel source,
boot assembly, a scheduler, memory management, loaders, capabilities and
agent request/response code. `kernel/src/agentask.c` uses a bounded waiter
table, kernel-selected sequence IDs, deadlines and explicit response/timeout
states. Its interrupt-masking concurrency model explicitly assumes one CPU;
it is not a portable multicore synchronization design.

The kernel README calls the kernel educational/experimental and separates it
from the host-side terminal and web control planes. Installing a host CLI is
not proof that the complete agent stack runs inside that kernel. The archive's
license restricts copying, modification, distribution and commercial use.
No source was incorporated. Bounded request correlation is a useful general
pattern, but the existing AIEN effect/transport owners already own that role.

## Delentia OS

[Delentia-OS](https://github.com/delentia-labs/Delentia-OS/tree/5602c9bb57106543ed799719be8d6dc79773c860)
is primarily an SDK/control plane in the inspected code. It offers substantial
agent-governance machinery; the name does not make it a bare-metal kernel.

The strongest material is
[episode journaling](https://github.com/delentia-labs/Delentia-OS/blob/5602c9bb57106543ed799719be8d6dc79773c860/rct_control_plane/episode_journal.py),
[idempotency](https://github.com/delentia-labs/Delentia-OS/blob/5602c9bb57106543ed799719be8d6dc79773c860/rct_control_plane/idempotency.py)
and [approval execution](https://github.com/delentia-labs/Delentia-OS/blob/5602c9bb57106543ed799719be8d6dc79773c860/rct_control_plane/approvals.py).
Intent without a result remains uncertain. Reconciliation does not silently
repeat the action. Approval binds an exact payload, and execution claims have
durable state. Provider-supported idempotency is distinct from local tracking.

The tests also contain useful negative controls: the same attempted write
is driven through multiple entry points, and removing the authorization gate
makes the adverse effect observable. Adopt that test principle when wiring
Om into actual dispatch, not just a test that mocks the gate's answer.

The project's own
[readiness snapshot](https://github.com/delentia-labs/Delentia-OS/blob/5602c9bb57106543ed799719be8d6dc79773c860/docs/release/READINESS_SNAPSHOT.md)
leaves model, host, pilot and review evidence incomplete. Its test counts are
self-reported, not rerun here. They do not establish the broad
"production-stable AI OS" description. Some model/LoRA paths permit mock
behavior, so their timing must not be presented as native training performance.

`src/action.om` adopts the recovery and exact-binding contracts independently.
Actual durable journaling, provider receipt verification and entry-point
coverage remain with #649's existing owners.

## KIRA OS

The matching source was the
[author's launch discussion](https://news.ycombinator.com/item?id=44584958).
In the same thread, the author clarified that the demonstrable AI engine ran
on existing operating systems, while the OS itself remained in development.
No matching public source repository was located. Several unrelated projects
use the KIRA name, so they were not substituted as evidence.

No kernel architecture or local-AGI claim from this project is an implementation
input. Revisit when a canonical source tree, reproducible build and concrete
workload measurements are available.

## AitherOS

The public `Aitherium/aither` URL redirects to
[Aitherium/awdk](https://github.com/Aitherium/awdk/tree/eca903dc7c84f98db7df72ae48d7e396e06bffc6),
an SDK in a larger Linux-based ecosystem. The inspected code does not establish
a newly implemented hardware kernel.

[KV artifacts](https://github.com/Aitherium/awdk/blob/eca903dc7c84f98db7df72ae48d7e396e06bffc6/adk/kvartifact.py)
bind reusable state to model/runtime configuration and verify payload digests.
[KV slots](https://github.com/Aitherium/awdk/blob/eca903dc7c84f98db7df72ae48d7e396e06bffc6/adk/kvslot.py)
also illustrate the importance of tokenizing the complete prompt before
choosing a reusable prefix. Different tokenization at a concatenation boundary
can invalidate a cache key even when the visible strings appear compatible.

Some binding fields can be left blank by a caller. Our bounded Om contract
instead refuses missing identity groups and names the mismatched dimension.
AIEN already has model/runtime-bound persistent KV machinery in
[`shared_kv.rs`](https://github.com/aien-dev/aien-sovereign-core-mv/blob/11842d678c72e19d78a311de2b11710c98896027/crates/aien-runtime/src/shared_kv.rs).
This is an integration/test improvement for its Om successor, not a reason to
create a second cache.

The receipt code also states an important limitation honestly: a local signed
hash chain cannot by itself prove that a deleted tail or a replaced local
anchor never existed. Freshness needs an independently protected anchor.
Preserve AIEN's existing separation of integrity, authentication and rollback
freshness. No new cryptographic subsystem is introduced here.

## SomaOS

The current
[avsribhas/SomaOS tree](https://github.com/avsribhas/SomaOS/tree/ef3a42afe2cca50f0047294e4a42c6bb31fd7e9d)
uses a Linux/Buildroot image and Rust components. That differs from the
bare-metal Rust characterization.

The valuable pattern is a shared application state and typed action interface:
the agent's sheet capability sends structured commands, and compositor events
update the same app-state cache used by observation. This is relevant to
Omega #648's existing frontend/backend contract. It is more concrete than
having an agent infer every application action from screenshots.

The implementation also demonstrates pitfalls to avoid. State reflection can
substitute dummy disk values when telemetry is unavailable. Action-tier logic
can advance authority based on recent behavioral scores. Missing measurements
should remain explicitly unknown, and successful behavior must not itself mint
permissions. The Om delegation predicate includes no such escalation input.

## What the review does not establish

None of the inspected material supplies evidence of a faster CUDA-free GB10
execution path or a qualified replacement for the existing CUDApp backend.
No performance, training-quality or recursive-self-improvement gain is claimed
for these Om contracts. Their benefit is precise, testable refusal and recovery
behavior at existing integration boundaries.
