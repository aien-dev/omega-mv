# Specification: Omega Program Identity v2 (`OMEGA_PROGRAM_ID_*`)

```text
Document ID:     SPEC-OMEGA-PROGRAM-ID-V2
Amends:          SPEC-OMEGA-M8 (spec/program.md), spec/omega-language-v0.md identity law 6,
                 spec/visor.md section 6a (program id finding)
Status:          IMPLEMENTED (branch fix/program-id-body)
Domain tag:      "omega.program.v2"
```

## 1. The defect this fixes

`omega_program_compute_id` (v1) hashed `"PROG"`, the program name, the contract type
tags and the low byte of each width, the two contract constraint ids, and the low byte of
`cost.insn_count` and `cost.latency_cycles`. It did **not** hash the body. Two programs
with different meaning therefore shared an id whenever they shared a name, a contract
and (by accident) a cost low byte. Reproduced in Visor: after `clear`,
`fn t(x: u64) -> u64 { x + 1 }` and `fn t(x: u64) -> u64 { x + 2 }` got the same
`program_id` (reproduced on origin/main 8e7a445: both `87a9e0e1...`). Discovery gives every
machine-code slice candidate the same name and contract, so equal-length slices would also
have shared one id. That breaks content addressing: one id named two meanings.

## 2. Identity law (v2)

```
program_id = SHA256( "omega.program.v2" || 0x00
                     || body_root_id                     (32 bytes)
                     || input_type_id                    (32 bytes)
                     || output_type_id                   (32 bytes)
                     || contract.precondition_id         (32 bytes)
                     || contract.postcondition_id )      (32 bytes)
```

Every 32-byte component is an existing canonical `SemanticId` computed by
`omega_compute_semantic_id` (spec/canonical-encoding.md). No second canonical encoder
exists: the body is lowered into a scratch `OmegaGraph` with the existing builders and
only the root object's id is taken. The 17-byte domain tag is the version boundary: a
v1 preimage began with `"PROG"` and a name, so a v1 id and a v2 id can never come from
the same preimage, and a reader that sees the tag knows which law produced the id.

### 2.1 The canonical semantic body

A program body is the ordered step list `OmegaProgramBody` (innermost step first):
`f(x) = op_n( ... op_2( op_1(x, c_1), c_2) ..., c_n)`, each step one of
`OP_ADD OP_SUB OP_MUL OP_AND OP_OR` with one constant. It is lowered as:

| object | builder | notes |
|---|---|---|
| `T`  | `omega_build_type_uint(w)` (or `_signed_int` / `_bitvector` for those tags) | `w` = contract input width; the operation type |
| `P`  | `omega_build_param(T, 0)` | the program's parameter (section 2.2) |
| `C_i` | `omega_build_val_uint(T, w, c_i)` | step constant, big-endian `ceil(w/8)` bytes |
| `O_i` | `omega_build_op_binary(op_i, OVERFLOW_WRAP, T)` | the step's operation; wrap is what the AArch64 realization does |
| `A_i` | `omega_build_apply(O_i, A_{i-1}, C_i)` with `A_0 = P` | running value is operand 0, constant operand 1 |

`body_root_id = id(A_n)` (and `id(P)` for an empty body, the identity function). These are
the same object shapes the Visor language already builds for expressions (`x + y` lowers
to `omega_build_apply(omega_build_op_binary(OP_ADD, OVERFLOW_WRAP, u64), x, y)`).

`input_type_id` / `output_type_id` are the ids of the contract's TYPE objects built the same
way from `(tag, width)`, so a width change changes the id through a canonical type object,
not through a raw byte.

### 2.2 The parameter object (new canonical shape)

The object model had no free variable. `omega_build_param(type_id, index)` builds:

- kind `KIND_VALUE`
- one attribute `omega.param` whose value is `index` as a big-endian u16 (V0: always 0)
- payload: the existing `ValuePayload` layout with `type_id` set and `byte_len = 0`

A literal value always has `byte_len >= 1` and no attributes, so a parameter can never
equal a literal. The parameter carries its **position**, never its source name, so
`fn f(x)` and `fn f(n)` with the same body are the same program (alpha-equivalence).

### 2.3 Programs without a body

`has_body = false` means the program's meaning is unknown to Omega (for example a raw
machine-code slice nothing could lift). Such a program has **no identity**:
`omega_program_compute_id` returns -1 and sets `program_id` to all zero bytes, and
`omega_library_insert` refuses an all-zero id. Two unknown programs never share an id.

### 2.4 The composed postcondition (semantic rule: composition is associative)

Composition is sequencing, and sequencing is associative, so the contract of a composition
must not depend on how the chain was bracketed. `omega_program_compose(a, b)` therefore does
**not** derive a postcondition text (the old `"(B)o(A)"` text was non-associative:
`(a;b);c` and `a;(b;c)` got different postcondition ids and so different program ids).
Instead:

- Every contract has a **leaf list** (`OmegaContract.post_leaves`, `post_leaf_count`). A
  program whose postcondition was stated directly (builder, Visor `ensures`, any explicit
  replacement) is one leaf: its own `postcondition_id`.
- `compose(a, b)` takes the flattened list `leaves(a) ++ leaves(b)` (application order; nested
  compositions are flattened, never nested), at most 64 entries.
- The composed `postcondition_id` is the canonical sequence constraint over that list,
  `omega_contract_post_seq_id`: the right fold `Seq(l1, Seq(l2, ... ln))`, each `Seq` a
  `KIND_CONSTRAINT` object with attribute `omega.compose = "seq"` and one
  `CONST_POSTCONDITION` constraint whose payload is `head_id || tail_id`, id by
  `omega_compute_semantic_id`. A `Seq` can never equal a text leaf (it carries the attribute).
- The precondition of a composition is `a`'s (already associative).
- A leaf list that no longer folds to the stored `postcondition_id` (the postcondition was
  replaced without clearing it) is ignored and the contract counts as one leaf.
- The postcondition **text** of a composition (`"A ; B"`, truncated) is display only.

Hence `(a;b);c`, `a;(b;c)` and every other bracketing of one chain have one program id,
while `a;b` and `b;a` differ whenever the bodies differ (different body, different leaf order).

## 3. Field classification

| field | class | in the id? | reason |
|---|---|---|---|
| `body` (step list) | **semantic** | yes, as `body_root_id` | what the program computes |
| `contract.input_type`, `input_width` | **semantic** | yes, as `input_type_id` | the domain |
| `contract.output_type`, `output_width` | **semantic** | yes, as `output_type_id` | the codomain |
| `contract.precondition_id`, `postcondition_id` | **semantic** | yes | the promise the program makes; the text strings are the source of these ids and enter only through them |
| `name` | metadata | **no (removed in v2)** | a human label; renaming does not change meaning. Two names for one body now share one id (dedupe), exactly as `let a = 7` / `let b = 7` share one VALUE id |
| `cost.insn_count`, `reg_pressure`, `memory_bytes` | metadata | **no (removed in v2)** | properties of one realization (the same body realized differently has a different count) |
| `cost.latency_cycles` | metadata | **no (removed in v2)** | a model or measurement of one realization on one machine |
| `contract.post_leaves` (compositions) | **semantic** | yes, through `postcondition_id` (section 2.4) | the ordered component postconditions |
| `contract.postcondition` text of a composition | metadata | no | display only; the id comes from the leaf list |
| `realization` (code bytes, realization id, target) | metadata | no | a realization of the meaning, identified separately by `realization_id` and the (program, machine, realization) triple id |
| `graph` pointer | metadata | no | not used for identity; pointer values never enter an id |
| `is_realized`, `is_verified` | state | no | lifecycle, not meaning |
| source whitespace, comments, literal spelling, parameter name, commutative operand side | not stored | no | normalized away by the parser/lowering before the body exists |
| parser node allocation order, builder call order | not stored | no | the body is an ordered step list; the scratch graph is rebuilt from it in one fixed order |

## 4. Where bodies come from

- `omega_program_build_unary_op` records one step. It now refuses a constant above
  `0xFFFFFFFF` (fail closed): it loads immediates with MOVZ + one MOVK, so a wider
  constant used to be silently truncated in the code while the body claimed the full value.
  (V0 realization, spec/program-realization.md, now loads full 64-bit constants; the builder
  and Language V0 keep the 32-bit limit, which the identity gates lock.)
- `omega_program_compose(a, b)` concatenates `a`'s steps then `b`'s (capacity 64 steps;
  refuses beyond). Composition therefore gives the same body as the equivalent directly
  written chain. Its realization is compiled from that body (`omega_program_realize`,
  spec/program-realization.md), no longer spliced from the parts' code.
- Visor Language V0 lowering (`omega_language_lower_program`) builds through the two above.
- `omega_discover_abstractions` builds an abstraction from a machine-code slice. It now
  **lifts** the slice back to a body (`omega_program_lift_body`: decode the rigid
  `movz x1[, movk x1 lsl 16|32|48 ...]; op x0, x0, x1` template, then re-emit the steps and require
  byte equality). A slice that does not lift has no body, no id, and is not verified, so it
  cannot be selected or admitted.
- `omega_refactor_program` copies the original's body: a refactoring that preserves meaning
  keeps the original's identity (its cost differs; cost is metadata).
- Programs assembled by hand (tests) must set a body or they have no identity.

## 5. Consequences for consumers

- **Library** (`omega_library_insert` duplicate refusal, `find_by_id`, dependency DAG): keyed
  by the v2 id. Two programs with one body and one contract but different names are now one
  program: the second insert is refused as a duplicate. `find_by_name` is unchanged.
  An all-zero (no body) id is refused.
- **Synthesis**: candidate ids now separate different bodies; the equivalence pruning uses
  behaviour signatures and is unchanged.
- **Realization synthesis**: `(program_id, machine_id, realization)` triple ids change
  because the program id changed; the law is unchanged.
- **Discovery**: candidates with different slices no longer share an id.
- **Crumbline** (`cl_steps_build_program`): ids change; a step constant above 32 bits now
  fails the build instead of producing a program whose code disagrees with its steps.
- **MetaSkills / workflow fusion, plan cache, action graph**: they identify by action-graph
  digests (`rx_graph_identify`, `rx_fusion`), never by `program_id`. Unaffected.
- **Visor**: `fn` redefinition with a different body now yields a distinct id and simply
  rebinds the name (section 6). The session's collision guard stays as an internal invariant
  check (same id but different realization code means a builder bug), with a new message.

## 6. Visor behaviour change

Before: `fn t(x: u64) -> u64 { x + 1 }` then `fn t(x: u64) -> u64 { x + 2 }` failed with
"identity collision ... the program id does not cover the body".
Now: both succeed, the ids differ, the second definition is added to the program table and
`t` is rebound to it. `fn other` with `transform`'s exact body and contract returns
`transform`'s id and reuses its table entry (the entry keeps its first name).

## 7. Changed golden ids (old v1 -> new v2)

Every program id changes (new preimage). The ids pinned in tests, and why:

| where | program | old (v1) | new (v2) |
|---|---|---|---|
| `tests/language/golden/v0.txt` P rows 1-3, `tests/visor/sessions/e2e.expected` (2 lines) | `fn transform(x: u64) -> u64 requires true ensures result >= 1 { x * 2 + 1 }` (and its two respellings) | `beedf7f6fc3853c0aed82e0b35e165b14878a82b0ebac64aa5bd9430800a8c4f` | `83bd39597742d3bb83e5140c662532c0a2ecb59e5f868e38be16276981644543` |
| `tests/language/golden/v0.txt` P row 4 | `fn mask(x: u64) -> u64 { x & 0xff }` | `7f6104b6cbf7b1e87055458fa9de9794067893186a84991b35b2a435c06ee244` | `e180baf4807d890dc1ba22472e847da76e877ee04d59b0762f6dfc26b2af62ff` |

Reason for all: v2 hashes the domain tag, the canonical body root and the contract type
object ids, and no longer hashes the name or cost. The realization code bytes in the same
rows (and every `E` row) are unchanged. Behaviour changes in tests:

- `tests/language/test_language_v0.c`: "different fn name -> different id" became "same id,
  table entry reused"; the table count after the name/ensures cases is 2 (was 3); the
  "different body is refused as a collision" case became "distinct id, name rebound"; the
  table-full probe uses `x + 1000` (the old probe `x + 1` now equals `fill1` and is reused).
- `tests/visor/qualification/run_qualification.sh`: `program-id-collision-refused` became
  `program-id[body-bound: x+1 != x+2 after clear]` and `program-id-redefine-distinct`.
- Discovery demo (`make test-m11`): the selected abstraction is unchanged (mul2;add1,
  f(50) = 101); its id changed `c1971e4a...` -> `52b6059b...`.

The omegatool M4-M14 gate suites needed no change.

**Composition postcondition rule (section 2.4) changes every composed program's id** again
(its postcondition id is now the sequence constraint over the flattened leaf list, not the
hash of the `"(B)o(A)"` text). No pinned golden changes: Visor `fn` programs carry their own
`ensures` leaf, so the language golden P rows and `e2e.expected` keep the v2 ids above.
Examples (M11 benchmark corpus, `make test-m11` demo, first 16 hex; text-derived -> leaf-list):

| program | before 2.4 | after 2.4 |
|---|---|---|
| mul2;add1;add5 | `bdca70b9e99a31cb` | `abdda5bc4c0d1300` |
| mul2;add1;mul3 | `e678c0ba6f3e1236` | `db28207b80a5f6ed` |
| mul2;add1;sub4 | `451f56e9b4c8564d` | `f9d39c98a66da1ec` |
| mul2;add1;add10 | `c1a07f6c9dfa754c` | `b1ccbc58e3eb04c3` |

The discovered abstraction (lifted, not composed) keeps `52b6059b...`.

## 8. Non-claims and deferred defects

- `SEMANTIC_ID` / `program_id` is **intensional**: it names the canonical body, not the
  mathematical function. `x + 1 + 1` and `x + 2` compute the same function and have different
  program ids, and that stays so. The extensional identity is a separate, versioned id,
  `FUNCTION_ID` (section 9): canonical inside the polynomial fragment only. Equality of
  arbitrary programs is undecidable, so no id is extensional for every program.
- Contract clauses are identified by their canonical **text** (`omega_build_constraint_id`);
  two logically equivalent clauses with different text differ.
- Historical receipts under `evidence/` carry v1 ids and are append-only; they are not
  rewritten. The `omega.program.v2` tag is what keeps them from being confused with v2 ids.
- The body model is the V0 unary step chain (u64 -> u64, five ops, one constant each). Richer
  bodies need a richer `OmegaProgramBody`; the identity law (root of the canonical body
  graph + contract) does not change.
- Payload bytes are the existing host-struct canonical payloads (spec/omega-language-v0.md),
  so ids are pinned to the LP64 / 4-byte-enum ABI, as before.

## 9. FUNCTION_ID (`omega.function.v1`)

```text
Status:      IMPLEMENTED and EXERCISED on the host (om/meaning/meaning.om, om/meaning/test/run.sh);
             not wired into the C library, Visor or any receipt; program ids are unchanged
Domain tag:  "omega.function.v1"
```

`FUNCTION_ID` names the function a program computes, where that can be decided cheaply. It
lives beside `omega.program.v2`; it never replaces a program id, never enters a program id
preimage and never rewrites a receipt.

### 9.1 Programs and the polynomial fragment

A program is a width `w` (1..64) and a postfix list over `x`, constants below `2^w`, `add`,
`sub`, `mul`, `and`, `or`, with arithmetic modulo `2^w`. The V0 body chain of section 2
(`x c1 op1 c2 op2 ...`) is one such list. The **polynomial fragment** is decided by an abstract
evaluation that gives each stack entry the class constant, polynomial or outside:

- `add`, `sub`, `mul` of constants and polynomials are polynomials; `mul` by the constant 0 is the
  constant 0 (even if the other side is outside).
- `and`/`or` with a constant side use only exact identities modulo `2^w`: `and` all-ones = the
  other side, `and 0` = 0, `or 0` = the other side, `or` all-ones = all-ones.
- Everything else is outside. (So `x and 0xff` at w 64 is outside; `x and 0xff` at w 8 is `x`.)

A program in the fragment computes a polynomial function `Z/2^w -> Z/2^w`.

### 9.2 Canonical form

Every polynomial function `f` modulo `2^w` has exactly one representation

`f(x) = sum_{k<K} a_k x(x-1)...(x-k+1)  (mod 2^w)`, with `0 <= a_k < 2^(w - v2(k!))`,

where `v2` is the 2-adic valuation and `K = K(w)` is the least `k` with `v2(k!) >= w` (the
falling factorial of degree `>= K` is divisible by `K!`, hence zero modulo `2^w`). This is the
falling-factorial normal form of Keller and Olson (Counting polynomial functions (mod p^n),
Duke Math. J. 35, 1968) and Singmaster (On polynomial functions (mod m), J. Number Theory 6,
1974, 345-352). The coefficients come from the values `f(0..K-1)` alone: the forward
differences are `d_k = Delta^k f(0) = k! a_k (mod 2^w)`; with `k! = 2^v u`, `u` odd,
`a_k = (d_k / 2^v) u^-1 mod 2^(w-v)` (Newton inverse modulo `2^64`, five steps from `u`).
`meaning.om` traps (code 9) if a `d_k` is not divisible by `2^v`, which the fragment check makes
unreachable.

Wolfram evaluations behind the constants (WolframLanguageEvaluator):

- `K(w)` for `w = 1..64` = `{2,4,4,6,8,8,8,10,12,12,14,16,16,16,16,18,20,20,22,24,24,24,26,28,28,30,32,32,32,32,32,34,36,36,38,40,40,40,42,44,44,46,48,48,48,48,50,52,52,54,56,56,56,58,60,60,62,64,64,64,64,64,64,66}`
  (least `k` with `IntegerExponent[k!, 2] >= w`; `K(8) = 10`, `K(64) = 66`). The suite checks
  all 64 values against `mg_kof` (`W` records).
- The number of polynomial functions is `prod_{k<K} 2^(w - v2(k!))`: `log2` = 50 (w 8), 170
  (w 16), 2274 (w 64). At w 3, a brute force over all coefficient tuples gives 1024 distinct
  functions, equal to the product of the bounds, and every canonical tuple round-trips.
- `canon[x(x+1), 64] = {0, 2, 1, 0, ...}`, `canon[x^2, 64] = {0, 1, 1, 0, ...}`,
  `canon[3x^3 + 5x + 7, 8] = {7, 8, 9, 3, 0, 0, 0, 0, 0, 0}`,
  `canon[7x^5 + 200x^2 + 13, 16] = {13, 207, 305, 175, 70, 7, 0, ...}`,
  `canon[255x^2 + 254, 8] = {254, 255, 127, 0, ...}`; `2^63 x(x-1) = 0 (mod 2^64)` for
  `x = 0..999`. These are the `K` records of `om/meaning/test/vectors`.

The falling-factorial form is not the only canonical choice: any injective function of
`f(0..K-1)` would serve. It is used because it is readable and because it gives an independent
check: the suite rebuilds `sum a_k x^(k)` and compares it with `f` on 4096 inputs.

### 9.3 Preimage

- Inside the fragment (class 1): `SHA256("omega.function.v1" 0x00 || 0x01 || w u16 || K u16 ||
  a_0 .. a_{K-1} u64)`, all big-endian.
- Outside (class 2, intensional fallback): `SHA256("omega.function.v1" 0x00 || 0x02 || w u16 ||
  body SEMANTIC_ID)`.

The class byte is in the preimage, so a fallback id never equals a canonical id except by a
SHA-256 collision, and the id records which kind it is.

### 9.4 What is claimed

- Two programs of the same width in the fragment get the same `FUNCTION_ID` **if and only if**
  they compute the same function (up to SHA-256 collisions). Programs of different widths
  always differ.
- Outside the fragment, equal ids imply equal functions (same body), **not** conversely:
  `x and 0x7fffffffffffffff` written two ways gets two ids.
- Not claimed: anything about contracts (a program's contract does not enter its
  `FUNCTION_ID`), division, shifts, comparisons, multi-parameter or effectful programs.

## 10. The graph of meaning (`omega.meaning.graph.v1`)

```text
Status:      IMPLEMENTED and EXERCISED on the host (om/meaning/meaning.om); in-memory library
             plus a canonical byte format; no service, no persistence path, no consumer yet
Domain tags: "omega.meaning.graph.v1", "omega.meaning.receipt.v1"
```

A graph whose nodes are content-addressed meanings and whose edges are typed relations, each
equivalence backed by a receipt that a reader can recheck without trusting the writer.

### 10.1 Nodes and edges

| node kind | id | content |
|---|---|---|
| 1 PROGRAM | program_id v2 (section 2) | body id, w u16, pre length u16, pre, post length u16, post |
| 2 BODY | body `SEMANTIC_ID` | w u16, token count u16, tokens (op u8; for a constant also u64) |
| 3 FUNCTION | `FUNCTION_ID` | the section 9.3 preimage |

Edges use the `RelationKind` codes of `src/omega_types.h`:

| rel | edge | receipt |
|---|---|---|
| 4 CONTAINS | PROGRAM -> BODY | none (recomputed) |
| 7 DERIVED_FROM | FUNCTION -> BODY | none (recomputed) |
| 9 EQUIVALENT_TO | BODY lo -> BODY hi | kind 1 CANON or kind 2 EXHAUSTIVE |
| 2 NOT_EQUAL | BODY lo -> BODY hi | kind 3 WITNESS |

`lo`/`hi` order the endpoints by id. Adding a program adds its PROGRAM, BODY and FUNCTION nodes
(deduplicated by id) and, for a canonical FUNCTION, a CANON equivalence to every other body of
that FUNCTION.

### 10.2 Receipts

Receipt id = `SHA256("omega.meaning.receipt.v1" 0x00 || content)`.

- kind 1 CANON (99 bytes): `01 || lo || hi || w u16 || FUNCTION_ID`. Checked by recomputing both
  bodies' `FUNCTION_ID`s from the stored body content: both class 1 and both equal to the one
  recorded.
- kind 2 EXHAUSTIVE (75 bytes, w <= 32): `02 || lo || hi || w u16 || 2^w u64`. Checked by
  evaluating both bodies on all `2^w` inputs.
- kind 3 WITNESS (91 bytes): `03 || lo || hi || w u16 || x u64 || f_lo(x) u64 || f_hi(x) u64`.
  Checked by evaluating both bodies at `x`.

`mg_verify` recomputes every node id from its content, every receipt id, and every edge's
receipt; a witness search that finds nothing adds nothing (no witness is not a proof).

### 10.3 Storage

`"omega.meaning.graph.v1" 0x00 || u32 N || N x (id, kind u8, u32 len, content) || u32 R ||
R x (id, u32 len, content) || u32 E || E x (src id, rel u16, dst id, receipt id or 32 zero
bytes)`, big-endian, each list strictly increasing (nodes and receipts by id, edges by
src, rel, dst, receipt). The bytes depend only on the set of nodes, receipts and edges, not on
insertion order; the graph digest is SHA-256 of these bytes. `mg_load` refuses truncated or
trailing bytes, a wrong tag, non-increasing order (so duplicates), malformed bodies, dangling
edges and unknown node kinds.

### 10.4 Queries (OM-2 words)

`mg_nbr n rel` (neighbours over one relation, or any with rel 0), `mg_class n` (closure over
EQUIVALENT_TO, n first), `mg_prov n` (every edge touching n; its receipts are the provenance).

## 11. Qualification

`make test-program-id` (tests/program/test_program_id.c, physics-free, runs in the CI AArch64
job) prints five gates:

| gate | what it proves |
|---|---|
| `OMEGA_PROGRAM_ID_BODY_BOUND_PASS` | `x + 1` vs `x + 2` differ through `omega_program_build_unary_op`, through Visor lowering, and under an identical name + contract + cost (the v1 collision); composition order separates; a body-less program gets no id; a 33-bit constant is refused; the v2 id differs from the v1 recipe |
| `OMEGA_PROGRAM_ID_REPRESENTATION_INVARIANT_PASS` | name, cost, realization bytes and pointer values do not move the id; six Visor spellings (whitespace, comments, parameter name, operand side, literal spelling, fn name) share one id; library composition equals Visor lowering when both state the same contract; 400 seeded chains (3-8 parts, some with user-stated leaf postconditions) x 4 random bracketings all equal the left fold, with the leaf list flattened; a;b != b;a for 400 pairs with different bodies; a stale leaf list is ignored; a hand-built body graph in 8 scrambled allocation orders has the same root; lift(realization) == body |
| `OMEGA_PROGRAM_ID_MUTATION_SEPARATION_PASS` | seeded differential over 3000 random programs (every pair, ~4.5M comparisons): id equal iff a hash-free structural oracle says body + contract equal; 2000 programs x 8 single mutations (op, constant, width, output width, pre, post, step added, step removed) always separate |
| `OMEGA_PROGRAM_ID_LIBRARY_REGRESSION_PASS` | library duplicate / admission / no-body refusal / DAG, discovery lift to mul2;add1 and admission, refactor keeps identity, realization triple id follows the program id, 64-step capacity |
| `OMEGA_PROGRAM_ID_VISOR_REGRESSION_PASS` | Visor session: distinct ids after `clear`, redefinition rebinds, aliases and respellings reuse the entry, stored realization == emit(body) |

`tools/program_id_receipt.sh` (shell only) runs these plus the Visor suites, the Visor V1
qualification campaign, the omegatool M8-M14 and Crumbline consumers and the non-energy host
regression suites on the committed clean tree, and writes a content-addressed receipt under
`evidence/PROGRAM_ID/`.

`sh om/meaning/test/run.sh seed/out/omc` (host only, no C) builds `om/meaning/meaning.om` with
the pinned compiler and checks sections 9 and 10: must-collapse pairs (`x + 1 + 1` / `x + 2` /
`2 + x`, `x(x+1)` / `x*x + x` / `x(x+1) + 2^63 x(x-1)`, the 66-factor falling factorial / 0,
reassociation, commutation, distribution, `x and 255` / `x` at w 8), must-not-collapse pairs
(constants one bit apart, the 65-factor product / 0 at w 64, the 9-factor product / 0 at w 8),
Wolfram coefficient vectors, `FUNCTION_ID` goldens that `run.sh` recomputes with `sha256sum`
from the 9.3 preimage, the two program_id goldens of `tests/language/golden/v0.txt`, the `K(w)`
table, refused programs, exhaustive and witness proofs, queries, a randomized differential at
w 1, 3, 8, 16, 64 (ids equal iff value signatures equal for fragment pairs; equal ids imply
equal signatures otherwise; reconstruction of every canonical form), graph verification,
store/load/store byte identity, reverse-insertion digest equality and tamper detection, then
source and expectation mutants that must each go red. Final line: `MEANING PASS checks=N
mutants=M/M`.
