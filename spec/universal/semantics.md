# Omega universal observation and conversion contract v1

Document: `omega.universal.semantics.v1`  
Scope: Omega #625, architecture #295; additive specification, not a format migration.

## 1. Authority and scope

Omega owns computational meaning. A frontend preserves source distinctions in a source-aware semantic layer until a checked transformation discharges them. Existing OSC typed IR is the lower execution representation for the supported legacy slice; it is not Python's object model, Rust's ownership model, or all of C. Seeded Omega and the separate Omega Bridge export work remain separate, existing implementations; this contract neither replaces them nor claims an importer is implemented.

This change supplies a semantic contract and development-oracle fixtures. No general C, Rust or Python importer/exporter, new runtime, proof checker or target backend is implemented here. A profile specified below is a specification, not a support claim. Reference C/Rust programs and CPython are independent test oracles, never production construction dependencies introduced by this change.

## 2. Observable behavior

For program P, source profile L, admissible input i, and environment E, let B(P,L,i,E) be the set of permitted observation traces. Each trace records in order:

- typed input and return values, including exact widths, signedness and representation;
- normal termination, divergence, a named trap, a language exception, or an explicit resource failure;
- externally visible reads/writes, I/O and foreign calls, including order and permitted nondeterminism;
- allocation/free, ownership transfer, borrowing/lifetime and externally visible mutation;
- synchronization, atomic order and concurrency events when the selected profile defines them;
- runtime/ABI interactions and their results, including capability requests and denials.

A pure deterministic profile restricts this to one return/trap trace with no externally visible effects. It does not establish correctness for effectful contexts. Observable resource exhaustion must be preserved or constrained by explicit environment bounds. Execution time, energy and code size are realization evidence, not semantic equality, unless a separate profile explicitly defines an observable deadline. A test timeout is evidence status TIMEOUT, never a program trap or proof of divergence.

The environment must fix allowed inputs, memory and runtime model, available capabilities, foreign ABI, dependency versions, resource limits, and any assumptions excluding undefined behavior. Undefined behavior is not a useful wildcard for accepting a source translation: unsupported or UB-possible input outside a discharged precondition refuses. An assumption is part of the claim, never an implicit optimization license.

## 3. Relations and evidence classes

An observation relation R explicitly maps source and destination observations, including trap/error representation. For the stated admissible input domain and environment:

- Equivalence means the related sets of observations are equal in both directions.
- Target refinement means every target trace relates to an allowed source trace; target nonemptiness/termination obligations must also be discharged where required. Dropping source behaviors is not equivalence. Refusing every input is not an acceptable implementation.
- Approximation requires a declared relation (for example a numerical absolute/relative error bound), input domain and permitted exceptional states. It is never exact equivalence.

Claims carry exactly one fidelity category, plus separate verdict, assumptions, scope, evidence and execution conditions:

| Category | Required interpretation |
|---|---|
| EXACT_SOURCE_REPLAY | Return the preserved original bytes, including encoding, line endings, comments and invalid byte sequences. No inference from generated source or an IR hash. No behavioral claim. |
| PROVEN_EQUIVALENCE | A checked argument for the defined bidirectional observation relation over its stated scope. Name proof assumptions, checker identity and proof object. |
| PROVEN_REFINEMENT | A checked directed relation with explicit source/target direction and nonvacuity/termination obligations. |
| BOUNDED_VALIDATION | Exhaustive checking or translation validation within a declared finite bound, with limitations and exact checked inputs/bounds. Not unbounded proof. |
| DIFFERENTIAL_COMPATIBILITY | Independently executed fixtures agree on a declared corpus. Record actual counts, tool versions and counterexamples; no proof inferred. |
| RUNTIME_INTEROPERABILITY | A typed/versioned component boundary executes correctly under its ABI/ownership/effect contract. Not source-conversion equivalence. |
| EXPLICIT_APPROXIMATION | Only the explicitly declared approximation is supported; evidence strength must be stated separately. |
| UNSUPPORTED | No qualified conversion for the construct/profile; useful named diagnostic, no partial success artifact. |
| UNKNOWN | An obligation is unresolved; no equivalence or safety conclusion. |

Verdicts FAIL, UNKNOWN, TIMEOUT, UNSUPPORTED, UB, MISSING_RUNTIME and NOT_RUN are never successful verification. PASS for DIFFERENTIAL_COMPATIBILITY only means those tests passed. Execution labels HOST_NATIVE, EMULATED and PHYSICAL_DEVICE are independent of fidelity: a proof does not show a GPU ran, and QEMU is not physical silicon. No automatic composition upgrades an evidence tier. Composition must check interface, profile, assumptions and relation compatibility, and explicitly preserve every unresolved obligation.

## 4. Initial source profiles

### C17 unsigned scalar arithmetic

`c17-uint64-pure-v1`: uint64_t exists, exact 64-bit unsigned values, sequential pure functions, explicit parameters/return, no pointers, aliasing, allocation, volatile, atomics, callbacks, preprocessor expansion or I/O in translated functions. The standalone fixture's scanf/printf are observation transport outside the pure function, not imported C features.

Addition/subtraction/multiplication on uint64_t wrap modulo 2^64. Division requires nonzero divisor and truncates as unsigned integer division. Shifts require a separately checked amount less than width. Narrow uint8_t/uint16_t C expressions undergo integer promotions: `uint8_t(255) + uint8_t(1)` is ordinarily an int value 256, NOT immediately an 8-bit zero. Only explicit conversion back to uint8_t gives zero. An adapter must preserve promoted intermediate types, then the explicit conversion. Usual arithmetic conversions and width rules must be pinned before mixed signed/unsigned expressions are supported.

C signed overflow is UB, not OSC_TRAP_OVERFLOW and not wrap. Signed overflow, pointer/integer casts, pointer provenance/aliasing, floating point (including NaN/rounding), and implementation-defined signed shifts initially refuse unless a more specific reviewed profile supplies and discharges their obligations. Do not execute an undefined expression to manufacture an oracle result.

### Restricted Rust wrapping scalars

`rust-u64-wrapping-pure-v1`: explicit u64 wrapping_add/wrapping_sub/wrapping_mul and nonzero unsigned division; no ownership-bearing resources, unsafe, references, macros in imported functions, panic propagation, traits, async, atomics or foreign calls. Fixture println! is external observation transport. Ordinary `+` is not interchangeable with wrapping_add: overflow behavior depends on Rust overflow-check configuration. Record edition, rustc version and flags. A future checked arithmetic profile must preserve panic/error behavior rather than assuming it equals an OSC trap. Borrows and lifetimes remain source-aware or refuse; none are erased to pointers here.

### Restricted Python integer scalars

`cpython-int-pure-v1`: builtin int arithmetic has arbitrary precision, subject to runtime resource limits; it is not an implicit u64. Negative integers are valid Python integers; no silent narrowing is allowed. User objects with overloaded operators, reflection, imports, exceptions, finalizers, generators and async are outside this profile. This profile describes arithmetic, not an implemented adapter. Bounded runtime exhaustion and exceptions remain explicit source obligations.

`python-explicit-u64-pure-v1`: inputs are explicitly restricted to 0..2^64-1 and each add/sub/mul result is explicitly masked with 2^64-1; division requires b>0. The masks are source semantics, not an importer-added change. Negative Python division uses floor: -7//3 is -3, unlike C/Rust signed truncation -2; only the declared nonnegative domain shares the unsigned quotient rule. Python bool is a distinct source type despite being an int subclass; no automatic bool/int conflation.

### Existing OSC lower profile

`osc-v5-u64-pure-v1` references `src/compiler/osc_ir.h`, `osc_interp.c`, `docs/osc/OSC-1-DESIGN.md`, and `spec/one-layer-format.md`. Unsigned arithmetic wraps at its declared width; signed arithmetic is checked; casts are checked; trap codes and bounded-loop/refusal behavior are observable. Existing enum values and binary versions are unchanged. Reference/native execution tests are not a proof that every source-language operation may lower to OSC.

## 5. Concrete counterexamples and initial fixture

The fixtures exercise the same handwritten u64 add/sub/mul/nonzero-div functions under C, Rust explicit wrapping, Python explicit masking and existing OSC interpreter/AArch64 native execution. CPython unmasked bigint results intentionally differ:

| Input/operation | C uint64 / Rust wrapping / explicit masked Python / OSC u64 | Python builtin int |
|---|---|---|
| (2^64-1)+1 | 0 | 2^64 |
| 0-1 | 2^64-1 | -1 |
| (2^64-1)*2 | 2^64-2 | 2^65-2 |

These are independent programs, not imported/exported artifacts. Their agreement is DIFFERENTIAL_COMPATIBILITY only. Deterministic boundary products and seeded random vectors test all four operations; compiled XOR-for-add and saturating-for-wrapping mutants must produce observed value mismatches, not merely fail compilation. The u64 corpus intentionally excludes zero divisors; it does not qualify division-by-zero mapping or signed arithmetic. Existing OSC suites cover their own trap contracts separately.

Run on AArch64 Linux with GCC/compatible C compiler, rustc and CPython:

```
make OUT_DIR=build/universal-625 test-universal-semantics
```

The runner fails if a required oracle/runtime/native mapping is unavailable. It writes unsigned development evidence to `build/universal-625/universal-semantics/receipt.json`, with tool versions, exact input hashes, binary hashes, actual counts and negative results. This is NOT a signed checker2 receipt and authorizes no execution. User-provided output locations are test scratch and must not name existing user data. x86-64/RV64GC/GPU are NOT_RUN here.

## 6. Memory, effects, external interfaces and diagnostics

Until separately specified, reject pointer arithmetic/provenance, aliasing, ownership/lifetime transfer, dynamic memory, floating point, exceptions, concurrency/atomics, nondeterministic behavior and foreign effects at import. Upper layers may retain them typed with unresolved obligations; they may not silently flatten them into OSC scalars. A runtime dependency is admitted only with an explicit ABI, version, layout, endian/alignment, ownership, authority and failure contract. A checker proves no additional capability authority: AIENOS/Sovereign Core owns authorization.

Future adapters must distinguish at least `UNSUPPORTED_CONSTRUCT`, `UNSUPPORTED_ARITHMETIC`, `UNSUPPORTED_EFFECT`, `UNSUPPORTED_MEMORY_MODEL`, `MISSING_RUNTIME`, `ABI_MISMATCH`, `PROVENANCE_MISSING`, `PROFILE_MISMATCH`, `UNKNOWN_VERSION` and `VALIDATION_UNKNOWN`. These are contract requirements for future adapters, not diagnostics implemented by this specification-only slice. Failure must not publish a success artifact or overwrite an earlier destination.

## 7. Identity, provenance and trust compatibility

No existing identity domain, encoder, OSC reader, OMG0 object, trap enum, checker allow-list, policy epoch or signed receipt layout is changed. `spec/program-identity.md` remains authoritative for its existing IDs. Hashes identify exact content under a versioned domain; equality of content IDs is not extensional equivalence of arbitrary programs. Equivalent functions may have different intensional IDs.

The SHA-256 of the exact UTF-8 bytes of this file is stored in `profiles.json` with the document version; it pins contract bytes, not a behavioral proof. Any byte change requires an updated digest and review. This JSON is a development contract manifest, not a new canonical semantic IR or signed trust anchor. Future #621 sidecars must bind original source bytes/spans and the full profile separately from the target-independent semantic identity; #623 must bind transformation versions, assumptions, artifact identity and obligations into the existing checker2 receipt path through reviewed versioning. Do not invent competing serialization or checker systems here.

Source replay requires byte storage and verified source association; without it exact export returns PROVENANCE_MISSING even when generated code or behavior agrees. Source text may contain private information: preserve locally unless publication is authorized. Historical evidence remains append-only, and old readers/fixtures must retain exact bytes.

## 8. Research and non-claims

- [DimSum (POPL 2023)](https://doi.org/10.1145/3571220), [authors' project](https://plv.mpi-sws.org/dimsum/): independent languages communicate through explicit events and linking combinators. Adopt explicit observation boundaries; this specification does not reproduce its Coq proofs or inherit its guarantees.
- [Semantic Soundness for Language Interoperability (PLDI 2022)](https://doi.org/10.1145/3519939.3523703), [preprint](https://arxiv.org/abs/2202.13158): cross-language safety needs a semantic interoperability contract, not merely a shared representation. No general FFI soundness claim here.
- [MLIR (CGO 2021)](https://doi.org/10.1109/CGO51591.2021.9370308) and [first-class verification dialects](https://doi.org/10.1145/3729309): source distinctions and staged legalization inform #621; no MLIR runtime dependency is introduced.
- [Alive2 (PLDI 2021)](https://doi.org/10.1145/3453483.3454030): bounded LLVM translation validation informs #623, not a proof of arbitrary source programs or Omega native lowering. No Alive2 experiment is claimed here.
- CompCert's verified compiler results are scoped to its semantics and assumptions, not transferable proof for this pipeline. Independent reference executables are development oracles only.

The required Undermind dossier was inspected in its authenticated workspace: its summary recommends composable source-aware dialects and explicit staged lowering. Search rankings and abstracts are research leads, not independent replications. No device, broad adapter or end-to-end programme gate is qualified by this document alone.
