# L533 independent review of L519

Reviewed exact L519 head `04aab149fdf3ead511c89f1312c793b2a2101f82` on 2026-10-22.
One P2 evidence defect reproduced. No in-contract admission vulnerability or new
session/recipe-source defect demonstrated. Original L519 decoder full-coverage
claim is HOLD pending this additive correction being carried by desk. This report
does not approve integration or hardware/model/performance acceptance.

| Milestone | Exact source | Independent disposition |
|---|---|---|
| #522 | `0f45d8b4d3793261a07ad832f1e5d7cb8a2d8bdf` | HOST PARTIAL: bounded replay behavior passes; real verifier and qualification HOLD |
| #519 | `db5bbbb9` | HOST PARTIAL: session forwarding oracle passes; graph/native/timing HOLD |
| #488 | `45a54135` | HOST PARTIAL: recipe invalidation claims reproduced; compiled-artifact/integration acceptance HOLD |
| #450 | `08bb0c7f919094d4c66e10816b888dd2e1f1055a` reviewing PR499 `1e4cdbe91b444efe51af4e745f366e07d54de030` | Original full-coverage evidence HOLD (C1); corrected exact-corpus host result below; current-main/v6 integration HOLD |

## C1: P2, decoder source export silently omits four declared corpus programs

Location: `evidence/L519-DECODE-450/README.md:24-38`, especially line31's
"Actual full gate" claim; repeated in `evidence/L519-FINAL/README.md` and L519
handoff. The exact proposed test calls `load_dir("src/osh")` and
`load_dir("tests/physics0/osc")` at PR499 `tests/compiler/test_osc_ir_decode.c:596-597`.
Its `load_dir` silently returns when a directory is absent (lines97-99).
L519 exported Makefile/mk/compiler/tests/sha256/quiet helper but omitted those
corpus directories. The 37 published source hashes match PR499; this is incomplete
input materialization, not a wrong decoder SHA or fabricated execution count.

Reproduction: export exactly L519's input set, run `check-decoder-closure.sh` and
the decoder quick binary. Closure exits1; test nevertheless exits0 with55 units.
Restore exact PR499 `src/osh` and `tests/physics0/osc`, without rebuilding/changing
the decoder binary, and rerun quick:59 units,59 round trips,0 compile refusals;
requirement-identity cases increase364 to514. The four omitted files are
`osh_expand.osc`, `osh_lex.osc`, `osh_parse.osc`, `pd0_helpers.osc`. Logs and exact
restored hashes are alongside this report.

Impact: the published full-gate result misses real v1/v5 consumers, their
requirement identities and derived malformed cases. It cannot support complete
PR499-corpus coverage or a current integration verdict. No decoder acceptance
failure is demonstrated by this omission. The original requested9/10 mutants
and refusal controls still actually ran and caught failed assertions.

Bounded correction: include both directories in the pinned export; preflight a
source/corpus manifest so missing directories fail before execution; publish a
new evidence addendum with the complete run and correct the desk's coverage
claim. Preserve old raw evidence. `reproduce-decoder.sh` supplies the complete
exact-source export; the reviewer ran the equivalent staged extraction/full-gate
command in `decoder-complete-full-gate.log`. Author source/tree is unchanged.

## #522 admission and replay

Read the full C/header, synthetic tests, mutation harness, rank implementation,
PR507/515 historical packets and evidence. Actual ranking is the existing
`turing_rank_min_cost`: one exact integer observation per admitted candidate,
incumbent inserted first for ties; other ties retain original declared order.
There is no cost prediction, runtime execution, cache, locality model or new
ranking rule. 16 candidates fit the existing32-slot bucket. Costs at most2^53-1
are exactly represented; unknown/zero/overlarge cost, invalid clock nesting,
missing samples/resources and mismatched binary/12 bindings are rejected.
Timestamp subtraction is guarded against future observations.

Independent sanitizer oracle tests all1..16 candidate counts, every incumbent,
16 rotated near-limit cost sets using a separate integer minimum; duplicate IDs
at index15 are rejected before any callback. It tests UINT64 timestamp edges,
zero/future epochs, exact age/resource boundaries, oversized bytes, null verifier,
malformed API inputs, scratch0 cap and cost2^53 refusal. It also changes original
receipt storage during the callback and confirms the verifier still sees the
hashed private snapshot. Original421/0 plain+sanitizer tests pass here.

Security boundary: the verifier is an explicitly trusted in-process callback.
The malloc snapshot is not protected read-only memory; a malicious callback can
cast away const, mutate it and return an invented observation. This was executed
and admitted. It violates the header's explicit verifier contract, so it is not
an in-contract acceptance vulnerability. The routine does not independently
parse/authenticate observations, isolate callbacks, protect against invalid C
pointers, or freeze mutable caller query/candidate metadata. Those inputs must
remain frozen, as documented. Concurrent mutation of source bytes during memcpy
is not a supported synchronization protocol. Do not describe the boundary as
safe against hostile executable verifiers or arbitrary concurrent writers.

Correctness, geometry, precision, pressure, residency, model/input quality,
clean sources and amortized total-cost derivation remain the real verifier's
responsibility. Opaque matching digests plus a synthetic callback cannot qualify
those semantics. Both historical packets are explicitly historical/synthetic
OWN30B and cannot establish current35B qualification. Real verifier, two frozen
families, held-outs, independent correctness, regret/overhead/break-even/cache
invalidation and cached/CPU/GPU scorer comparison remain with canonical owners.
Existing8 mutant logs support intended refusals, but their harness accepts any
nonzero exit rather than requiring a FAIL assertion; this is weaker than the
session/decoder harnesses. No crash-only kill was used as independent approval.

## #519 session visibility

Original oracle includes the existing fake, replaces only sync callbacks and
checks production session forwarding. Separate host/device allocations and
whole-buffer byte checks include the final byte; exact trace checks pin direction,
handle/generation, offset0, actual page-rounded size, lock depth, order and return.
Each failure is injected before the fake's copy, so unchanged memory on failure
is a fake model assumption, not proof of native failure atomicity.

Fresh host gate and all4 compiled session mutants pass with explicit failed
assertions. Independent patterned producer/consumer checks cover eight repeats
with varying bytes, full last-page contents, a wrong generation on a still-live
allocation, oversized range, refusal then recovery, and both null/empty directions.
These pass. Data allocation uses the actual uncached mode; this does not test
other cache modes. The production session object was not sanitizer-instrumented
in that extra probe, but the probe/fake was; no broader sanitizer claim is made.

All28 original events occur at one caller-held lock depth; no concurrency proof.
Callback order is checked, not graph setter/begin/end/read sequencing. The graph
region table and NOT_RUN limits are accurate. PR588 at
`f53511177def30e64f781320c9d9090906dd6642` retains graph recovery. No graph change,
real device visibility, decode parity or A/B/C timing is established here.

## #488 Physics identity recipe oracle

Exact PR546 `8a4f15a6abedd3c21c17bf2f7f033d94fbab57ea` identity and PR495
`a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3` dirty modes pass independently.
Baseline `85d8343fa04647b198e3ddda00ad8e448837f9ca` fails directory-switch count;
PR546 fails the extra dirty-content boundary, as originally disclosed.
Fixtures are fresh private Git repos, timestamps forced to2000, symlink alias
resolves identically, empty HEAD advance invalidates, and unchanged invocations
prove no unnecessary rebuild. Exact Makefile hashes match published pins.
No marker was linked as an object or confused with a driver binary.

A targeted control keeps dependencies and recipe invocation counts but changes
both compile inputs to `src/sha256.c`. The oracle still passes. Its spy records
source arguments but assertions compare only counts. This confirms the already
stated recipe-only scope; it does not establish a new PR495/546 source defect.
Do not promote it to artifact/source-content or library-link proof. The real
quiet checker does compile; synthetic markers cannot certify driver contents.
Header-consumer/dependency-file/concurrent-stamp extensions remain L532's scope.
No coverage or edits from that lane are claimed. Canonical CUDAPP_DRIVER_DIR
requires its separate wiring gate; these are legacy PHYSICS_DIR rules.

## #450 source and oracle independence

Exact PR499 source input hashes verified (37/37). Normal rcount bounds before
array use; decoder lengths use remaining-byte subtraction with a monotonic,
bounded cursor; byte fields use unsigned little-endian reads; register sign/range
checks precede validation. Block first+count is checked with uint64 widening.
Unknown opcode is refused before unknown-length interpretation. Refusal clears
all of OscUnit; independent raw-wire checks inspect the entire output, not just
the owning suite's selected fields. Every truncated prefix, UINT16/UINT32 count
maxima, block-range overflow, register sentinel/sign boundary, unknown opcode,
text length and encoded length limit passed under ASan/UBSan.

All10 decoder mutants were rebuilt and killed with explicit FAIL assertions.
All5 oracle controls run. Mutant9 can remain semantically refused by a later
check: changing the named refusal from OVERSIZED to INVALID is a valid kill for
this refusal-class contract. Mutant10 likewise changes the first refusal; it
does not prove unchecked execution of unknown instructions. The oracle's
validity check calls the same validator as decoder admission; it is independent
of parsing, not an independent semantic validator. Byte identity shares
encode_unit with the canonical backstop, accurately disclosed in the corrected
comments. Hand-written wire cases and reviewer's raw-wire probe provide distinct
byte construction. No later v6 behavior is inferred.

## Provenance and execution boundaries

Own tree/branch verified; no source fixes or author-tree writes, extra agents,
remote PR/issue writes, merges, hardware/model/service/driver changes. App-server
SQLite records plus worktree heads reconcile L519 completed and L531/L532 active.
Only L533 ledger notes were written. The19:20:36Z issue/PR snapshots remain
historical; all four assigned issue bodies/discussions were read. Guarded live
repo/PR499 reads later exited0 normally; no RATE guard bypass. Live PR499 still
OPEN at the exact reviewed head. Remote main and L519 pins are in remote-before.txt.
The live-read stderr contains unrelated watchdog alerts and remains local under
build/L533, not in public evidence. No current claim is inferred from the older
snapshots. L519/L511 original FAIL/INCONCLUSIVE results remain intact.

## Corrected complete decoder result

Complete exact-corpus full gate exited0 here:59 corpus units,59 round trips,
0 compile refusals,22/22 refusal cases,10/11 refusal classes (NONCANONICAL remains
the canonical backstop), all5 oracle controls and all10 decoder mutants caught.
Plain generated10,000 valid objects and202,675 malformed cases; sanitizer
counts and detailed refusal distribution are retained verbatim in
`decoder-complete-asan.log`. Both full-count legs passed. These are actual host
observations; there are no timing/throughput estimates. This reviewer-supplied
addendum supplies the missing host coverage without rewriting historical logs.
C1 concerns the old coverage claim; desk can carry this corrected packet as its
bounded remedy. Integration/current-main/native qualification remains open.
