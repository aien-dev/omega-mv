# L519 #522 host admission/replay boundary

HOST PARTIAL. Synthetic tests only. No real GPU receipt parser/verifier is wired,
no performance measured, no current35B qualification, no runtime promotion.

Base: 85d8343fa04647b198e3ddda00ad8e448837f9ca. Additive C host/oracle files only.
The external verifier is mandatory and its trust contract is in
`tools/turing_gpu_replay.h`. It must verify the independent qualification of the
exact hashed byte snapshot and decode the observation. A digest or the text
VERIFIED is insufficient. NULL verifier refuses every candidate, including the
incumbent. The synthetic test verifier recognizes exactly three two-byte fixture
IDs and supplies synthetic observations from an explicit oracle table. It does
not parse or qualify any real receipt.

The boundary checks all twelve identity bindings and expected reuse exactly,
per-candidate binary identity, measurement freshness including future time,
resource bounds, sample count, positive known end-to-end cost, and clock nesting.
It snapshots then hashes receipt bytes before invoking the verifier. It requires
a qualified incumbent and preserves it on an exact tie. Otherwise it calls
`turing_rank_min_cost`, the existing control, with admitted observations only.
No prediction, locality model, DAG composition (#583), GPU launch or selection
cache is introduced. Input candidate order and caller-owned query must be frozen
for replay. Normalized total_ns is the measured per-call mean of the declared
complete end-to-end workload, including preparation/transfer/validation costs
amortized ONLY at the identical declared reuse. The verifier owns that derivation;
the boundary never estimates or sums component times. submit_host_ns includes
the device span, total_ns includes submit_host_ns. Unknown costs never enter the
ranking bucket. Integer means are limited to 2^53-1 ns so the existing control
represents them exactly; no ns-to-ps multiplication is performed. The control
compares numeric values without unit-specific transformations.

## Executed checks

Commands, all host only through quiet-build:

- `make OUT_DIR=build/L519 test-turing-gpu-replay`: initial deliberately ungated
  implementation RED, exit2 with assertion failures and a NULL-input fault;
  `red.log` retained. This initial implementation is not committed.
- Same command after implementation: GREEN exit0, 421 checks / 0 failures.
- `make OUT_DIR=build/L519 test-turing-gpu-replay-sanitize`: exit0, 421/0,
  ASan/UBSan with no recovery.
- `sh tests/turing/gpu_replay_mutants.sh build/L519/mutants-v2`: see mutants.log.
  Each mutation must compile before the executable is required to fail.
  Initial `zero-cost` mutation only removed `!total_ns`; it survived because
  positive submit_host_ns > zero total_ns independently rejects zero. This is a
  redundant/equivalent guard removal, retained in mutants-initial.log. The final
  cost mutant removes the full cost gate, testing the actual invalid-cost policy.

The independent min oracle enumerates all 125 triples of costs in [1,5].
Tests also cover each identity binding, exact incumbent and nonincumbent ties,
repeated replay, changed byte/digest, failed verifier, no verifier, no qualified
incumbent, unknown costs, stale/future evidence, resource caps, exact boundary
admission, duplicate candidate IDs and malformed queries.

## Existing real evidence inspected, not qualified

`pr507-historical.json` is copied byte-for-byte from PR507 head
00fca453c629af2cafa50e60d532385b82db8185,
`tests/dgraph_receipt/fixtures/good-historical-710d438c-main.json`.
SHA256 0004142d5de0fb1389d94c23439d8e17262b8edc07aef9303475985b7e756078.
Status HISTORICAL; random-weight 30B-shaped synthetic step, not current35B.
It pins source, Physics, binary, driver and partial geometry, but its structural
checker does not qualify a contract/quality envelope, registers, resident/scratch
bounds, pressure, complete inputs or amortization. `chip_ns` is host-clock time;
only stamped span/stages are device time. No relabeling as T/Ts/TJ is allowed.

`pr515-historical.json` is copied byte-for-byte from PR515 head
acf9a7219349ea7010bfb9cad7fa4af068bf77e2,
`evidence/OWN30B-FP8-GEMV-625ff9f5-20261013T1822Z/receipt-grouped-old.json`.
SHA256 7d878d359971b8d5c537c48125769e44ac95ece9a6340d4e8b30a14a1bf8dfa3.
The row has correctness and host-clock timing values but no full independent
immutable machine/build/contract qualification in that receipt alone. It is
historical own30B work, not current35B. Neither reference is silently accepted.

## Exact external gate packet

Desk/canonical shared-train owner must supply two frozen meaningful variant
families with: canonical contract/input-quality digest; Omega/Physics commits;
per-candidate binary and receipt SHA256; machine+driver+toolchain/config identity;
exact shape/batch/sequence and precision; residency/pressure assumptions; register,
resident and scratch limits; independent correctness oracle PASS; measurement
protocol/quiet receipts; complete per-call host mean and separately identified
device span with sample count and measurement epoch; expected reuse including
preparation/transfer/validation accounting; and fixed train/held-out shape sets.
An independently reviewed verifier/decoder must bind all normalized fields to
those exact bytes. Only then run incumbent/min-cost/locality diagnostic comparison,
held-out regret, cold-start/overhead/break-even and cached/CPU/GPU scoring where
feasible. Locality remains diagnostic until independently justified; #583 cost
composition and L047 ownership retained. Hardware acceptance and independent
review NOT_RUN. No chip work queued or owner process changed by this milestone.

Final mutation result: 8/8 compiled mutants killed (exit0). An intermediate
mutation-harness quoting error generated invalid C and exited2 before completing;
`mutants-invalid-harness.log` retains that failed attempt. The harness was fixed
by escaping sed replacement ampersands; neither implementation nor assertions
were weakened. Final directory was build/L519/mutants-v3.

Existing TURING regression gate also executed: `quiet-build make OUT_DIR=build/L519
test-turing`. See existing-turing.log for exact result. This checks algebra Field
and history controls on existing fixtures; those historical metrics are not new
GPU performance observations.
