# Public record admission preparation

L776, action `L776-654-public-admission-01`, implements one bounded component of
Omega #654. Its only dependency is the existing L775 benchmark component at
`84d03c5523f39ca1045d1c313c5f62597779462b`, source
`src/learning/benchmark/core.om`, SHA-256
`a5a2e74c815f9ca6a90dbdfbfc7ecc343f0362c824064f2058e9d1d7c119c40c`.
Use that exact source as the first part of a translation unit, followed by
`admit.om` and the caller. The benchmark implementation remains owner supplied;
combined source snapshots under evidence preserve failed and successful builds.
Compiler and target are the same pinned seed compiler191b776e and static
AArch64 Linux target used by the public fixtures.

`cbegin(n,budget)` calls existing `lbegin` with one attempt per record, maximum
64 records. `csubmit(task,status,exit,output_length)` checks a supplied public
metadata packet, then uses `lobserve` for exactly three independently specified
u64 observations, encoded as24bytes. Costs are always charged by existing
`lattempt`, including refused metadata and repeated submissions. `cfinish` uses
`lfinish`; accepted indices may be consumed only after it returns zero and
`CState[2]` equals one. This is in-memory processing, without generation, repair,
file loading, subprocess execution, persistence or durable resume.

`CMeta[0..7]` are schema1, source-profile1, license1(AGPL-3.0-or-later),
provenance1(disclosed handwritten public fixture), independent oracle ID,
generator/template ID, evidence1(bounded testing), independent sample count3.
Oracle/template must match the frozen task record and differ from each other.
`CMeta[8..11]` is observed candidate source SHA-256 in four big-endian u64 words,
matching `LT[task*32+12..15]`; reserved words12..15 must be zero. The underlying
manifest and compiler/ELF checks preserve L775's public-only partition boundary.
`cbegin` snapshots the admitted task records into `CFreeze`; changing any word
of a task before submission refuses it with47. Accepted indices refer to that
frozen task snapshot, not a caller's later edited `LT` array.
This metadata equality does not authenticate an untrusted caller or measure
source bytes. Existing executor/evidence/custody owners must supply those facts.

Detailed refusals40..47 mean schema/profile/reserved, license, provenance,
oracle/template, evidence class, sample/output bounds, and source identity.
Code47 is a changed task-manifest record.
Observation diagnostics and accounting errors retain L775 values. `CDiag`
preserves each record's reason; `CGood` and `CBad` separate accepted and
quarantined indices with counts in `CState[0..1]`. A repeat submission charges
cost and invalidates the run without overwriting the first stored record.
An all-rejected set is a valid processed set with zero accepted targets.
A failed new `cbegin` clears the ready flag from the previous completed run.

The same `admit-test.om` is compiled three times: with the production module,
with `negative/provenance-bypass.om`, and with
`negative/source-identity-bypass.om`. The latter two replace the module and
deliberately remove one guard each. Production exits0 and emits the pass marker;
the controls compile successfully but fail checks33 and43, process exits83 and93.
These controls establish that the tests detect broken metadata checks. They do
not establish trusted custody or source-byte authentication.

Tests use synthetic public metadata and predeclared independent affine inputs
0,4,7 with expected values2,14,23. A hardcoded two-example answer fails the third
observation. These are test observations injected into the consumer; no candidate
execution or genuine held-out benchmark campaign is claimed. Full #653 benchmark,
#654 generated corpus/repair/durable resume, source/receipt authentication,
protected custody and model training remain NOT_RUN. No existing runtime or
dependency is removed, and no production consumer is switched by this slice.
