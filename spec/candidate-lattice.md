# Candidate lattice: meaning × method × realization (SI LATTICE L1, #698)

Code: `src/runtime/rx_lattice.{c,h}`. Tests: `tests/runtime/rx_lattice_unit.c`
(`make test-lattice`, AArch64) and `tests/runtime/rx_lattice_bench.c`
(`make bench-lattice`, AArch64 host CPUs, no GPU). Shared fixture:
`tests/runtime/rx_lattice_fixture.h`. Build fragment: `mk/lattice.mk`.
Evidence: `evidence/LATTICE-698/`.

## Rule

For one bounded, typed objective graph, the lattice enumerates the sparse set
`C[i,j,k]`:

- `i` is an obligation, numbered like `rx_graph.h` (`AgKind`: CONST,
  PHYSICAL, JOIN, EFFECT_PERFORM);
- `j` is a qualified method (for MATVEC, an `rx_costmodel` arm);
- `k` is a reported realization: a core class with a fixed lane count, or a
  FORGE realization with its descriptor digest and verified flag.

Only declared supported `(j, k)` pairs are enumerated. Each coordinate has a
SHA-256 identity over the obligation (shape, weights, scope, precision,
generation, dependencies), the method (arm, revision, precision, rights, seat,
source digest) and the realization (core, lanes, descriptor).

**A coordinate is a proposal, not permission.** Admission records the first
refusal reason in this order: precision, stale revision, privilege, scope,
realization (unverified, or the live descriptor has changed), model seat,
unverified method, changed cost evidence, unknown cost, failed arm, budget.
The caller still runs work only through its own verified path. A node is
committed only from a receipt that binds the dispatched candidate and reports
a passing independent verification.

## Cost

There is no second cost model. Mean and spread come from `rx_cm_predict`
(features: the method's arm, the realization's core class, cache and
pressure). The lattice pins `rx_cm_digest` at admission; if the evidence later
differs, every non-incumbent MATVEC coordinate is refused. An operation that
`rx_costmodel` does not cover (SOURCE, JOIN, EFFECT here) has no prediction,
so only its incumbent may run, charged at the obligation's work cap. If there
is no cap, the incumbent is refused too. Unknown cost is never zero. An arm
with a recorded wrong result is excluded, as in `rx_cm_decide`.

## Graph checks

`rx_lat_validate` reports `AG_E_TYPE` for:

- a dependency out of range, duplicated, or on an effect;
- wrong arity, or a kind/op mismatch;
- two shapes for one weight id.

It reports `AG_E_CYCLE` (a self-dependency counts) and `AG_E_EFFECT_ORDER`
for an effect without the EFFECT right or without a transaction id. It
reports `RX_LAT_E_SCOPE` when data flows from a tenant-private node into
another scope. Public data may flow into any scope.

## Policies

All three see the same admitted set and the same lanes. A lane runs one
coordinate at a time. A single-owner device has one lane, so concurrency
never exceeds what a realization declares.

| policy | node order | candidate |
|---|---|---|
| `serial` | one at a time, topological | incumbent method, first admitted realization |
| `lcf` | lowest predicted cost first, dependency-driven | lowest predicted mean with a free lane now |
| `q3d` | longest remaining predicted path first | minimum predicted finish = lane availability + upper confidence cost (z = 1). It waits for a busy lane when that is predicted to finish first. Ties go to the incumbent, then to the lane already holding the weights, then to the lowest index. |

Effects are committed by the single dispatcher. This happens only after every
input is DONE, and at most once per transaction id (`RX_LAT_E_REPLAY`
otherwise). Pure outputs may be reused through `RxLatCache`. The key covers
the node's semantics and its input digests, not the method or realization. A
hit needs the same scope, the same generation and an intact binding. Anything
else is refused and recomputed. Effects have no cache key.

The lattice is a calculator, as `rx_costmodel` is. `mk/lattice.mk` builds
`rx_lattice.o` alone and fails if it references anything that maps, forks,
executes or opens anything, or any authority or generation operation.

## Fixture

The fixture is a 17-node graph:

- fork/join, mixed privacy scopes (public, tenant A, tenant B), shared
  weights, and one trusted effect for tenant A;
- methods: the reference (incumbent), two emitted arms (M12 scalar, R10 quad4),
  a planted wrong arm and a declared model-call method;
- realizations: two Cortex-X925 lanes, two Cortex-A725 lanes, and one
  simulated single-owner accelerator lane leased to tenant A.

Every emitted arm passes a differential test in a forked child before
admission. Every node output is checked independently before its receipt is
sealed: four matvec rows are recomputed plus guard words, and SOURCE and JOIN
are recomputed in full. A reference-only oracle that does not use the lattice
recomputes the whole graph.

## Results

### Unit test (`make test-lattice`, 237 checks, 0 failures)

Positive results:

- 109 candidates, against 408 in the dense cube. Identities are distinct and
  build plus admission is deterministic.
- All three policies complete all 17 nodes. Every output equals the oracle and
  serial, and the effect is committed once with an identical value.
- No realization ever exceeds its declared lanes.
- The planted arm and the model-call arm never run.

Mutants, each on a fresh fixture:

- a cycle and a self-dependency;
- wrong dependencies: effect consumed, out of range, arity, duplicate, weight
  shape, kind, cross-tenant flow;
- a stale revision and a stale precision (method and realization);
- illegitimate cache entries: tampered output, another tenant, stale
  generation;
- privilege escalation (a method asks for EFFECT), an effect without the
  right, a compute node committing an effect, and an effect before its inputs;
- budget starvation: the node and its dependents are refused, no effect, and
  independent work still completes;
- the model worker down (refused, math path used), then the seat up
  (admissible, not run here);
- a self-modified receipt, a receipt for another candidate, and a replayed
  receipt;
- changed cost evidence after pinning;
- an unverified FORGE realization, and a changed live descriptor;
- transaction replay;
- unknown cost with and without caps;
- an arm with a recorded wrong result.

The unit test's makespans are MODELED from a synthetic truth table. They are
not measurements: serial 658.3 µs, lcf 154.2 µs, q3d 118.6 µs.

### Host benchmark (`make bench-lattice`, measured)

Each run is 15 or 31 paired rounds after one warm-up, in rotating policy
order. The cost model is fitted from 240 timed observations on a training
grid that does not contain the graph's shapes. `gpu.sim` is refused
(unverified: no FORGE lease), so no GPU work happens. Typical run on the Spark
host CPUs (loadavg ≈ 5), median wall time:

| policy | wall | busy sum | busy / wall | realized critical path |
|---|---|---|---|---|
| serial | 674–688 µs | 484–494 µs | 0.70–0.73 | 187–194 µs |
| lcf | 252–281 µs | 427–435 µs | 1.52–1.72 | 156–160 µs |
| q3d | 313–345 µs | 369–384 µs | 1.08–1.21 | 147–152 µs |

- **q3d loses to lcf:** a median wall ratio of 1.21–1.24. q3d was faster in
  4/31, 5/31 and 0/15 paired rounds. One-sided sign test, P(≤ 4 of 31) =
  1.7 × 10⁻⁵.
- Both beat serial: lcf/serial 0.37–0.41 and q3d/serial 0.46–0.50, in 30–31
  of 31 rounds.
- q3d does the least total work (all quad4 on X925, lowest busy sum and
  shortest realized critical path). It keeps the A725 lanes idle because it
  predicts that waiting for an X925 lane finishes sooner.
- Serial's wall exceeds its busy sum by about 10.6 µs per node, which is
  thread hand-off and verification. `rx_costmodel` has no feature for that
  overhead. With nodes of 10–40 µs, waiting costs more than predicted.
- Selection overhead (inside pick and finish) is 22–27 µs per objective.

Not measured: energy, memory bytes, GPU utilization. None is claimed.

## Status and limits

HOST_TESTED for the calculator, the mutants and the CPU-lane benchmark.

**Migration debt.** Under the seeded-`.om` rule this is a C implementation.
It consumes `rx_costmodel`, which is C with IEEE doubles. The seeded `omc`
language has no floating point or fixed-point `log2`/`exp2`, and no C ABI or
import. So an `.om` lattice can neither call nor faithfully port the cost
model's predictive arithmetic. Both are tracked under the compiler owner in
omega-mv #636 (FR4 IEEE floats, FR7 ABI/imports). The `.om` successor of
`rx_costmodel` and of this lattice depends on that issue.

Not done, and not claimed:

- signed proposal and outcome links into Cortex: the receipt binding is
  SHA-256, not a signature;
- the real accelerator realization, FORGE leases (physics-mv #77), the model
  seat (Core #735) and batching (#695);
- a cost feature for dispatch overhead;
- any real-chip or Spark GPU claim.
