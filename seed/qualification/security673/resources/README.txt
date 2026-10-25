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
For NAME in demo, exhaust, negative, measure, run the following existing-tool
composition (replace NAME literally; these commands are not a shipped runner):
  cat seed/authority/reserve.om seed/authority/envelope.om seed/qualification/security673/resources/checks.om seed/qualification/security673/resources/NAME.om | nice -n 19 seed/out/omc > /tmp/sc1-NAME
  chmod +x /tmp/sc1-NAME
  nice -n 19 /tmp/sc1-NAME
negative takes one argument1..5: completion,cancel,error,supervisor-work,partial-vector.
Expected normal exits21,22,23,24,25 with its named FAIL assertion; never count traps.
  nice -n 19 seed/out/omc < seed/qualification/security673/resources/oracle.om > /tmp/sc1-oracle
  chmod +x /tmp/sc1-oracle
  nice -n 19 /tmp/sc1-oracle
  /usr/bin/time -v nice -n 19 /tmp/sc1-measure

Evidence categories
Formal proof: NOT_RUN. R3: independent finite .om enumeration18 states78 edges,
whole-service effectfree crash including selfloop; not uncertain storage recovery.
Native differential: 448 small reservation cases plus helper-driven binding,
conservation, overflow, atomic rollback, recovery/fuel and paired-envelope fixtures.
Native controls: five deliberately bad adapters compile and reach named assertions.
Native measurement: 63 paired samples, 100 reserve/commit/reclaim cycles per sample;
p50/p95/p99 are nearest-rank indices31/59/62. Baseline is matched unchecked vector
mutation, not a safe service. Runtime includes token updates and function calls.
Raw measurements retain both the initial run and the resource-measured repeat.
These are helper overhead, not service throughput, timing noninterference or a
scheduler benchmark. Process crash, durable store integration, real simultaneous
clients, physical power loss and live service integration: BLOCKED/NOT_RUN.
Main integration owner supplies independent review, root contributor entry and
whole-repository checks; this lane edits only the assigned paths.
