SC1-R / SC1-E pure bounded qualification

Scope
New Apache-2.0 implementation under the repository license. The old AGPL
contracts were read for their trusted-caller/unknown/replay semantics, not copied.
Two fixed owners, eight stocks, serial pure transitions. No daemon, store, lock,
scheduler, syscall-effect substitute, compiler extension or authentication.

Stock indices
0 processes/descendants; 1 descriptors; 2 resident memory; 3 queued bytes;
4 persistent bytes; 5 temporary snapshot bytes; 6 retained history/logs;
7 cleanup reserve. These are separately budgeted stocks. A physical parent must
be charged exactly once by the accounting policy, not once per alias/name.
Fuel is a separate monotonically spent scalar. There is no replenishment API.

Reservation API (seed/authority/reserve.om)
All operations return ulong: 0 success, 1 invalid input/state, 2 binding/replay,
3 capacity refusal, 4 unknown/retained. rv_uncertain returns4 after marking the
reservation uncertain. Other refusing public transitions leave the state unchanged.
All arrays within an operation must be nonaliasing. These are trusted implementation
functions, not sealed capabilities. Do not expose raw state, rv_clear, rv_reclaimed,
rv_room or integer flags as client-authorized operations.

State: edit/view ulong64. Layout is documented beside the source. Token: ulong8,
slot,id,request,owner,grant,object,version,epoch. Quantity vectors: ulong8.
Identity words are exact trusted adapter handles, not truncated hashes. The owner
must map full authenticated identities to collision-free, nonreused handles and
persist that mapping in its existing snapshot. This helper does not authenticate
handles, compare full signed requests or implement upstream AUM no-op semantics.

rv_init(state,capacities,owners2,fuelbound): genesis only, all-zero destination.
rv_valid(state): validate serialization including aggregate subtraction-based bounds.
rv_reserve(state,token,q,fuel): all-or-nothing eight-dimensional reservation. Token
id must equal state.next-id, and request must exceed the fixed owner's highwater.
The maximum id refuses before increment; no wrap or rotation. An owner has one
slot, retained until committed stock is actually reclaimed. Copying token/state
never supplies a second authoritative writer. Two independent genesis states are
not a shared budget. Requests are monotonic per owner; retries need owner dedup
before this API, not a new reserve or a forged larger request id.
rv_uncertain(state,token): cancellation, lost acknowledgement, death and timeout
retain full stock. Already-committed stock remains charged.
rv_commit(state,token,actual,quiet,public): validates actual<=reserved in every
dimension, then moves actual to used and frees slack. Requires authoritative
quiescence and observation release permission, even for zero actual. No fuel refund.
rv_reclaim(state,token,gone,durable,public): subtract only the named slot's used
stock, only after actual reclamation plus required durability/public release.
Zero remaining stock retires the slot but keeps highwater and next-id.
rv_recover(state,actual): complete authoritative physical census raises used to
max(ledger,actual), retains all unsettled reservations, refuses capacity mismatch
without mutation. Smaller observations never refund. Unattributed recovered stock
has no reclamation shortcut here. The owner must reconcile attribution before any
future accounting design permits its release.

There is no delegation API: copying a parent charge is not delegation. A future
owner's split must atomically subtract from the parent and allocate the child in
the same writer transaction; this bounded two-owner profile makes no such claim.
QuantityLane supplies nominal checked quantity operations; this module independently
recomputes capacity from original operands with guarded subtraction. Raw wrapped
sums and client-supplied derived availability are never accepted as evidence.

Envelope API (seed/authority/envelope.om)
Record ulong8: format,owner,reservation-id,public-release-boundary,active,capacity,
partition,zero. ep_open(record,owner,id,boundary,capacity,partition) initializes
an all-zero record once, before secrets. ep_event(record,event1..4) represents
completion/cancel/error/supervisor-work and never changes public availability.
ep_available reports capacity minus the fixed active partition. ep_release requires
exact public boundary identity and authoritative quiescence. Unknown returns4 and
retains charge. A released record retains its identity and cannot be reopened.
Restart validates/restores the whole record. This is one public stock projection;
the scheduler must reserve all eight real stocks with the same owner/id and an
appropriate fixed public partition. A scalar projection is not proof that actual
supervisor, descendant or physical usage is contained.

Integration BLOCKED: WORM / existing omnet snapshot writer
Current service snapshot has no resource reservation ledger, request id or explicit
epoch. Integrate these fields and identity mappings in that owner's existing
snapshot schema and transaction, never a second file ledger. Initialize only at
explicit genesis; missing/corrupt restored accounting is not empty capacity.
Inside the same serialized dsrequest/dssave interval: authenticate principal/action,
validate current policy epoch and object/version, resolve exact request-byte dedup,
reserve complete stock+fuel, create the inactive candidate and bounded response,
write/fsync/validate temporary snapshot, rename, fsync pinned directory, publish,
ack. Resource/dedup/authority fields must commit together. A lost ack must return
stored disposition without duplicate reservation or an unbounded snapshot rewrite.
Before rename, no physical refund until temporary cleanup is proved. After uncertain
rename/sync, fail-stop; no new admission or rollback. Recovery must reconcile actual
retained+temporary bytes, validate bindings and re-establish durability before
admission. Client flags cannot stand in for these observations. Existing registered
NodeID/NodeKey adapter remains owner; this module does not replace it.

Integration BLOCKED: scheduler/public observation owner
Reserve fixed public partition and public release identity before secret work;
charge descendants, cleanup and supervisor work inside it. Persist envelope owner,
id, boundary, partition and active bit with the corresponding reservation. Complete,
cancel, error and connection loss cannot refund or alter public readiness. At the
public boundary release only with authoritative quiescence. Unknown retains; a
secret-dependent public delay is NOT certified noninterference. Fixed partitions
and occupied committed slots lose utilization and can starve later work. There is
no unlimited-demand availability or actual concurrency claim.

Native replay (AArch64 Linux, no GPU, from repository root)
  nice -n 19 sh seed/omega-from-seed.sh
  sha256sum seed/out/omc
Required compiler: 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
For NAME in demo, exhaust, measure, run the following existing-tool
composition (replace NAME literally; these commands are not a shipped runner):
  cat seed/authority/reserve.om seed/authority/envelope.om seed/qualification/security673/resources/checks.om seed/qualification/security673/resources/NAME.om | nice -n 19 seed/out/omc > /tmp/sc1-NAME
  chmod +x /tmp/sc1-NAME
  nice -n 19 /tmp/sc1-NAME
negative.om is compiled ALONE as an exact-source mutator. Modes1..4 mutate actual
ep_event; mode5 mutates actual rv_reserve. Link each generated module with the
unchanged checks.om and demo.om. Require normal exit7 with the E failure marker,
or exit4 with the R2 marker. Exact commands are in review-addendum.txt.
  nice -n 19 seed/out/omc < seed/qualification/security673/resources/oracle.om > /tmp/sc1-oracle
  chmod +x /tmp/sc1-oracle
  nice -n 19 /tmp/sc1-oracle
  /usr/bin/time -v nice -n 19 /tmp/sc1-measure

Evidence categories
Formal proof: NOT_RUN. R3: independent finite enumeration18 states78 edges,
whole-service effectfree crash including selfloop; not uncertain storage recovery
or correspondence to the different attributable-slot resource helper.
Native differential:448 small reservation cases plus helper binding, exact status,
stale/cleared/reused token, attributable reclaim, conservation, overflow, atomic
rollback, recovery/fuel and actual paired occupancy/admission fixtures.
Native controls: five separately mutated production-source modules compile and
reach the SAME demo/checker assertions; no duplicated negative assertions.
Native measurement:63 raw paired samples,100 cycles/sample; nearest-rank
p50/p95/p99 indices31/59/62 (p99 is the maximum, not a tail-confidence claim).
The unchecked baseline is deliberately unsafe. Trusted caller fuel charge is0 in
this benchmark, so it establishes no work-pricing policy. Full raw outputs and
actual source/compiler/executable/argv/cwd identities are in native-review-rev2.
The original native-20261025 receipt is historical and does NOT qualify final SC1.
Independent ResourceReview found no remaining actionable findings after repair.
Actual writer/store/scheduler integration, authoritative census attribution,
current policy/object revalidation, durable fuel/envelope restoration and physical
power loss remain BLOCKED/NOT_RUN. Same-helper checks do not discharge them.

Independent complete-relation replay (SC1 revision2)
The oracle now accepts --edges and writes the entire36x36 byte adjacency matrix,
row-major state code9*u+3*r1+r2. Its1296 bytes match an external tuple-set BFS
byte-for-byte, not just counts. Matrix SHA-256:
19e4706d53b940f5f99d9337a0277e00844a9355d96d60ea4c19af8288e8de03
r3-edges.json records every state and ordered edge plus exact source/compiler/
binary identities and the matrix encoding. The four no-reservation self-loops
and four simultaneous-two-reservation crash edges distinguish the78-edge model.
This does not qualify uncertain-effect recovery or actual resource accounting.

Clean checkout replay
clean-replay.json records an independent native replay at495a53ba6: bootstrap
fixed point734f0a39..., crumb verification, demo and448-case differential exit0,
all five rebuilt source mutants normal-fail7/7/7/7/4 at their named assertions,
and the complete oracle matrix exactly matches the independently reviewed bytes.
The matched100-cycle sample p50 was2448ns unchecked versus88401ns checked;
raw63-pair data and a separate1480KiB peak-RSS measurement are retained. This is
helper overhead, not service throughput, durable integration or a secrecy proof.
