# #249 E06 extension: checked elimination ranked by actual cost

Status: design note, written before any code (2026-10-19). Ledger L059.
Issues read: aien-dev/aien-architecture-mv#249 (E06 and its "Wolfram-method extension"),
aien-dev/aien-architecture-mv#276 (parent: prove work unnecessary before reuse or optimization),
aien-dev/omega-mv#583 ("Minimum-work accounting extension", OEV1 `removed` records).
Plan read: `docs/plans/necessary-work/AIEN_Physical_Limits_Project_Plan.md` at aien-architecture
a0a90739 (local ref of `research/necessary-work-program`; the remote branch could not be fetched
on 2026-10-19), sections 6 (E04, E06), 9 (G0 to G6), 10 (evidence fields).
Source read: omega a8ffd2b9 (`src/omega_synthesis.c`, `src/omega_program.c`, `src/runtime/rx_costmodel.h`).

E06 depends on E04 (aien-dev/aien-architecture-mv#247, OPEN, NOT_RUN on 2026-10-19). This work does not
wait for E04; it records the dependency here and in the evidence README. The contracts in this
note are not E04's library of hand-derived reductions for hot operations; they are the rule set
E06 searches with.

## What the issue asks for, mapped to files

| #249 "Deliver" item | Where |
|---|---|
| 1. Versioned rewrite contracts with domain, width, overflow, rounding, shape, aliasing, effects, and a checkable derivation | `tools/rw249/contracts/*.rw` (format `rw-contract v1`), axiom base `tools/rw249/contracts/axioms.rwa` |
| 2. Proven equivalence kept apart from probe agreement; completeness loss of probe pruning measured; counterexamples | `tools/rw249/rwcheck.c` (verdicts `proven_in_scope`, `probe_agreement`, `rejected`); `tools/rw249/rwprobe.c` and `tests/research/rw249/counterexamples/` |
| 3. Manual rules and instruction-count ranking against bounded cost-guided search with `rx_costmodel`; eligibility before ranking; unknown cost never zero | `tools/rw249/rwsearch.c` (arms M, G, B) |
| 4. Real identities tested apart from machine arithmetic; negative cases sqrt(x*x), overflow domain, floating-point reassociation | `tools/rw249/contracts/N*.rw`, domains `int` and `wrap` and `f32` in `rwcheck` |
| 5. Optional indexing only if matching dominates | profile counters in `rwsearch`; decision recorded in `evidence/REWRITE-249/README.md` |

Build and tests: `mk/rw249.mk` (targets `rw249`, `test-rw249`). Evidence: `evidence/REWRITE-249/`.

## The grammar covered

The omega V0 program body (`src/omega_program.h`): a chain of unary steps applied to one input,
innermost first, `f(x) = op_n(... op_1(x, c_1) ..., c_n)`.

- Operations: ADD, SUB, MUL, AND, OR, each with one constant operand.
- Widths: unsigned 8, 16, 32, 64 bits; input width equals output width.
- Overflow: wrapping modulo 2^w (the V0 lowering uses `OVERFLOW_WRAP`).
- Constants: must fit the width (the V0 gate refuses others).
- Rounding: none (integer only).
- Shape: one scalar in, one scalar out. Aliasing: none (one register, no memory).
  Effects: none (pure; the realization touches only scratch registers).

The search only ever produces terms in this grammar. The checker understands a larger language
so that it can reject the negative cases: unsigned division `udiv`, integer square root `isqrt`,
products of the input with itself, the domain `int` (mathematical integers) and the domain `f32`
(IEEE 754 binary32, round to nearest even, no fused multiply-add). None of these is added to omega
or to the search: they exist only so the checker can show a claimed identity is false.

## Proven equivalence versus probe agreement

omega's `omega_synth_compute_signature` runs a program on six probe inputs (0, 1, 3, 7, 13, 42)
and hashes the outputs; `omega_synth_equiv_contains_or_add` then drops a candidate whose hash was
already seen at a lower or equal instruction count. Two programs that agree on those six inputs are
treated as the same. That is observational matching. It never authorizes a substitution in this
tool.

`rwcheck` gives a contract one of three verdicts:

- `proven_in_scope`: (a) the contract has a derivation and every step is checked syntactically: it is an
  instance of one axiom from the fixed base (in either direction), applied at one position, with any
  side condition of that axiom appearing literally in the contract's `assume` list, and the axiom is
  valid in the contract's domain; and (b) the semantic check finds no counterexample: every input at
  w = 8 with every value of up to two constants (more constants are sampled from a frozen seed),
  and every input at w = 16 with a frozen sample of constants, skipping only rows the assumptions
  exclude (a check that skips every row is vacuous and fails). The declared width is covered by the
  argument in (a), because every axiom in the base holds at every width of its domain (the ring laws
  of the integers modulo 2^w and the laws of Boolean algebra on w-bit words); (b) is a second,
  independent line of evidence, not the proof.
- `probe_agreement`: no derivation, but no counterexample was found. Never used for substitution.
- `rejected`: a counterexample was found, or a derivation is present and any step fails.
  A wrong proof of a true statement is rejected: the checker judges the argument, not only the claim.

For a rewrite the search proposes on a concrete expression, the search emits a derivation whose
steps each cite one contract at one position with concrete constants. The checker accepts it only
if every cited contract is `proven_in_scope` under the same semantics version, each step's left side
matches the contract's left pattern at that position, the contract's side conditions hold for those
constants at that width (the context must satisfy the assumptions before a contract applies), and
the step's right side equals the contract's right pattern with its constant expressions evaluated at
that width. It then also compares the start and end programs directly: every input at widths 8 and 16,
a frozen sample plus edge values at 32 and 64, and the same inputs through omega's own evaluator
`omega_program_eval` as a cross-check of the checker's evaluator.

Independence: `rwcheck.c` has its own parser, term representation, evaluator and matcher. It links
omega's core only to call `omega_program_eval`. It shares no source with `rwsearch.c`. The search
implements its rules in its own code and only labels each rewrite with a contract id; if its code
and the contract file disagree, the checker rejects the rewrite and the search counts a FAILED
candidate.

## Cost: query path and the eligibility rule

Arm G ranks by emitted instruction count: the code length of the canonical realization
(`omega_program_emit_schedule`, sequential schedule) divided by four, the same quantity omega stores
in `cost.insn_count` and that `omega_synth_equiv_contains_or_add` compares.

Arm B ranks by `rx_costmodel`. That model today has one operation class (matrix-vector) and eight
arms. It has no scalar operation class. Adding one is a change to `src/runtime/`, outside this work,
so it is listed as a MISSING PREREQUISITE. Here the model's arms hold scalar step classes: one arm per
operation (ADD, SUB, MUL, AND, OR), one arm for each extra 16-bit chunk a constant needs (MOVK), one
for the fixed per-call part (call, width mask, return), one never observed. Features are M = N = 1,
quiet pressure, the core class of the measuring core. The arms are fitted from a native timing
calibration of realized code (`rwsearch calibrate`), serialized with `rx_cm_serialize`, and the blob
digest is frozen before the run of record. This use of the arms is a labelled stand-in, not a
qualified scalar cost model. Arm B is also run with emitted instruction count as its ranking, labelled
`B-insn`, so the effect of the search and the effect of the ranking can be separated.

A candidate's predicted cost is the sum of `2^mean_log2_ps` over its parts, taken from
`rx_cm_predict`. Rule: unknown cost never becomes zero. If any part's prediction has `known == 0`,
the candidate is ineligible: it is not ranked, not counted as free, and the incumbent (the input
expression) stays. Order: first eligibility (every step proven by a contract whose assumptions the
context satisfies, every cost part known), then ranking among the eligible.

## The three arms and the budget

- M (manual rules): the rules a person writes first: merge two adjacent steps of the same operation
  (ADD+ADD, SUB+SUB, MUL+MUL, AND+AND, OR+OR) and delete identity steps (add 0, sub 0, mul 1,
  and all-ones, or 0). Applied greedily, innermost position first, until none applies.
- G (greedy): every contract in the library, but only moves that strictly lower emitted instruction
  count, best first. It cannot take a step that keeps the count equal, so it cannot use distribution.
- B (bounded exploration): every contract, including those that keep or raise the count (multiply
  over add, and over or, or over and, sub to add and back). Breadth-first over whole terms of one
  equivalence class with a set of visited terms and parent links, so every result has a derivation.
  This is not a subterm-sharing e-graph (UNVERIFIED that sharing would matter for chains of at most
  six steps). Ranking after exploration, by cost model, among eligible terms.

All arms get the same frozen budget per expression (candidate evaluations, distinct terms, CPU time).
Hitting the budget ends the search with "best found under budget", never "optimal".

The derivation cache inside `rwcheck` stores a contract's schema verdict keyed by contract id, contract
file digest, assumptions and semantics version (`omega.v0.wrap.1`). A changed semantics version is a
different key, so a cached verdict cannot be reused across semantics.

## Plan review changes (Fable 5.1 review of the plan, 2026-10-19)

- The label `proven_in_scope` rests on the syntactic derivation alone. The semantic checks are a
  guard against checker bugs, and they prove nothing above w = 16.
- Mutants aim at the checker as well as at the data: an unsound axiom slipped into the base (the
  checker checks every axiom semantically at w = 8 and must refuse the base), the semantic guard
  disabled, a derivation step skipped, unknown cost treated as zero. Each must go red.
- Execution savings: a null control (the same realized program timed twice in the same run)
  gives the noise floor. A per-call saving is reported as measured only when it exceeds that floor;
  otherwise INCONCLUSIVE. Net savings use measured execution time, never the model's prediction.
  The execution count used for the net-savings sum is frozen in FREEZE.md before any search output exists.
- Cost model observations: calibration times chains made of one step class only, so each per-step
  time belongs to one arm. If the input expression itself has an unknown cost, nothing is ranked,
  the input stays, and the case is counted as `unranked`.
- Arm B is also run at a sweep of node budgets so the cost of search is seen against what it buys.
  Peak resident memory is reported per arm.
- Probe counterexamples in this grammar are easy to find (AND 0x3F and AND 0x2F agree on all six
  probes at u8). They are reported as a property of the probe set, not as a discovery.
- The held-out generator was written after the rule set, by the same author, with constants chosen
  so that rules can fire. That is a bias; it is stated, not hidden.
- Negative cases that need `isqrt`, `udiv`, signed integers or `f32` run in checker-only models that
  omega does not have; they are labelled so.
- OEV1 `removed` records come only from rewrites the checker accepted as `proven_in_scope`.
- E04 is open, so every E06 verdict here is conditional on E04's eventual library and is labelled so.

## Credit for the method

- Wolfram FullSimplify documentation (https://reference.wolfram.com/language/ref/FullSimplify.html):
  the idea that a simplification is valid only under stated assumptions (`Assumptions`) and that
  "simpler" is a pluggable cost (`ComplexityFunction`). Here: contracts carry assumptions that the
  context must satisfy, and ranking is a swappable cost (instruction count or fitted time). No Wolfram
  code, runtime or internals were read or used.
- SetReplace `AtomsIndex.cpp` at 44c868bf (https://github.com/maxitg/SetReplace): the idea of indexing
  rules by the atoms they mention so matching skips rules that cannot apply. Built here only if the
  profile shows matching dominates (see item 5); otherwise recorded as not built, with the profile.

## Unverified choices

- UNVERIFIED: the additive per-step cost model (sum of step classes) for a dependent chain.
- UNVERIFIED: rx_costmodel arms reused for scalar step classes (stand-in, see above).
- UNVERIFIED: whole-term exploration instead of an e-graph.
- UNVERIFIED: the held-out generator's constant distribution (chosen to make rewrites possible; it is
  not drawn from a real workload trace, because #244 traces are NOT_RUN).
