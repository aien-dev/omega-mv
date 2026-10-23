# Bounded recursive calls: proposed additive contract v2

NOT A MASTER PLAN. Issues #625/#621/#645, validation #623, integration #624.
Status: candidate qualification contract. This does not upgrade the v1 codec,
advertise recursive C imports, authenticate receipts, or establish general
source-to-native translation equivalence. Existing v1 bytes and IDs are immutable.

## Scope and order

The first execution slice is handwritten seeded `.om` with direct recursion,
`ulong` arguments/results (unsigned 64-bit), sequential call-by-value, arguments
evaluated left to right, and only the selected conditional branch evaluated.
Addition/subtraction/multiplication wrap modulo 2^64. No division, pointer
escape, allocation, I/O, external state, mutual/dynamic/higher-order calls or
effects are admitted by this proposed recursive interchange profile. Transport
I/O and private instrumentation are outside the pure computation. Mutual calls
and effectful argument-order probes are explicitly compiler inventory controls.

The two fixtures are tail accumulation and immutable finite tree/DAG evaluation.
Tail accumulation returns accumulator+n+(n-1)+...+1 modulo 2^64. A node returns
its stored value plus its left subtree plus its right subtree; evaluate left
before right, and stop at the first exhaustion. Missing child64 returns zero.
Children refer only to smaller node indices; sharing is allowed, cycles refuse.
At most64 nodes, root64 is an empty tree. Tree data is a complete immutable input.

## Logical resources and observable failure

Fuel is the number of admitted function entries, including entry to a base case
or missing child. It is not a CPU-instruction counter. The fixture body between
entries has a statically finite amount of work. A future profile admitting loops
must account for those steps separately; this rule cannot bound arbitrary bodies.
Fuel is0..4096. Logical live frames are0..64. One activation reserves32 logical
storage bytes, with a budget0..2048. This is abstract execution storage, **not a
measurement of the native stack**. Native stack reserve and actual binary call
frames must be separately checked before production qualification.

Before entry, check in order: prior failure, remaining fuel, live-frame capacity,
logical storage capacity. On success decrement fuel once, increment admitted
entries/live frames and update peaks. On refusal admit no entry and consume no
fuel. Return unwinds every admitted frame. Failure emits no successful value.
Named statuses:80 FUEL_EXHAUSTED,81 FRAME_EXHAUSTED,82 STORAGE_EXHAUSTED;
67 LIMIT for unsupported declared budgets;66 UNKNOWN_VERSION;65 MALFORMED.
Zero fuel therefore fails even for a base case. Both depth and total work are
bounded. The endless-call fixture demonstrates partial budget-bounded execution,
not a termination theorem or equivalence to arbitrary unbounded recursion.

The observation is `(status, value-if-success, logical entries, peak logical
frames, peak logical bytes)`. Work saved by lowering or reuse must not change
this observation. The iterative baseline preserves these logical checks while
using bounded explicit storage. Tail-to-loop lowering is not permitted to waive
the declared frame budget. Actual execution cost is measured separately.

## Versioned representation and migration boundary

The existing OMSB/OMSA/OMSP v1 codec remains unchanged and excludes recursion.
Do not use language4/profile5 (OSC5) for `.om`. Proposed v2 domain names are
`omega.source.semantic.recursion.v2` and `omega.source.provenance.recursion.v2`.
Before production admission the #621 owner must freeze and implement additive
function signatures, ordered basic blocks, typed parameters/returns, explicit
call targets/continuations and branch records, including canonical validation
of malformed graphs, unknown versions and complete semantic/provenance identity.
The runtime resource policy and this contract digest belong in semantic identity;
source bytes/spans belong in provenance; lowering/compiler/native/target/verifier
identities belong in the #623 realization obligation. No new production wire
encoding is asserted by the qualification transport below.

The test-only RQ2 transport is1600 bytes: eight little-endian u64 header fields
`version=2, mode, fuel, frames, bytes, n-or-root, accumulator, node-count`, then
64 triples of `value,left,right`. Unused triples must be all zero. Modes0/1 are
recursive/iterative accumulation,2/3 recursive/iterative tree,4 endless recursion,
5 argument-order ABI probe,6 bounded mutual-recursion ABI probe. Modes outside
0..6, invalid child/root/count, non-tree node data and tree accumulator refuse.
Header budget validation precedes version validation. Mutual probe n is0..64.
Output is six u64 words: status, value (zero on failure), entries, peak frames,
peak logical bytes, final live frames (must be zero). Batch limit8192 requests;
short request/IO/session overflow returns65/77/67. Exactly8192 requests followed
by EOF succeeds; any extra byte after those8192 requests is overflow. This
transport has no authority
or source-association claim and must never be passed to the v1 codec as upper IR.

## Cache and verifier integration dependencies

#646 consumes the existing native semantic-cache migration seam. Pending recursive
evaluation is separate from an acyclic DAG of completed results. Never invoke
recursive evaluation inside a non-reentrant cache callback lock. An exact hit
must reproduce logical fuel/frame/storage failure, including the first refusal;
a scalar total-cost field alone cannot reproduce partial exhaustion traces.
Keys must bind complete typed inputs, immutable tree contents, implementation,
generation, profile/contract/environment versions. Failed/partial results are not
successful entries. The owned native migration interface remains a prerequisite;
this slice supplies no cache or concurrency/restart claim.

#647 remains independent. Existing checker2 quiet fixed-point propagation then
ordered diagnostic pass is the reference. Its native owner must expose the same
transfer/ownership/reachability facts before substituting a queue. Preserve trap
termination and first-error order. Default adoption requires zero semantic/fact/
diagnostic mismatches, all compiled mutants caught, and measured total cost below
the full scan on the predeclared large-case set with no more than5% regression
on the small-case set; otherwise retain full scan. No timing claim exists here.

## Qualification and limits

Use existing pinned seeded compiler from `tools/source-aware/pins.json`; no `.osc`
stage or foreign compiler/runtime fallback in computation. Native reference
workloads and mutants are `.om`. Node is independent test orchestration/BigInt
reference only, consistent with the existing source-aware harness. Compare values
and exhaustion before timing. Record commands/exits/source/compiler/binary hashes,
raw streams, actual observations and failed attempts. Test zero/one/exact/+1
budgets, depth, branching/sharing, arithmetic extremes, missing base case, call
ordering, and mutations of fuel, frame checks, return values and child order.

Passing this slice qualifies only the executed handwritten workload/source pairs.
General source->upper->lowering->native validation, authenticated receipt binding,
native cache integration, native verifier worklist, full Spark judge, independent
final-source review and non-AArch64 targets remain separate required gates.
