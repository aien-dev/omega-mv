# Semantic atoms and compact local codebook v0

**Status: PROPOSED / NOT QUALIFIED.** Finite component contract only, not a master plan. [SEM-1 Omega #721](https://github.com/aien-dev/omega-mv/issues/721), [cross-project #315](https://github.com/aien-dev/aien-architecture-mv/issues/315). The formal ABI is not frozen until source-aware IR #621, semantics #625, lowering/verification #623 and reuse #651 owners review it.

## Meaning and local reference

One bit selects between two **already known, agreed alternatives**. Eight bits select 256 alternatives. The receiver must already hold and independently authenticate the same complete dictionary definition, generation, effect/numeric contract and selected realization. The short code is **not** an identifier with global permanence, authorization or proof.

A canonical semantic atom v0 points to the existing 32-byte program/body ID (spec/program-identity.md; spec/semantic-object.md), versioned typed inputs/outputs, explicit numeric/trap/effect semantics, constraints and proof/admission identity. An admitted realization is separately bound to the machine ISA/backend, exact compiled bytes and independent verification receipt. Local codebook installation fixes a table from u8 indices to **complete 32-byte IDs**, domain root, namespace, epoch and allowed capabilities. Changing any index, meaning, contract or generation invalidates stale refs. Unknown or ambiguous semantics and unsupported privilege are refusals.

Canonical bytes and SHA algorithm are inherited from the existing Omega canonical object implementation; this proposal may **not** add another canonical encoder, separate library authority, unversioned hash reduction or model-supplied verifier. Protected effects still require existing World/AEGIS approval. Remote negotiation is [Protocols #54](https://github.com/aien-dev/aien-protocols-mv/issues/54); local reference alone is not a trusted network message.

## Small native research example

[dispatch-v0.om](../examples/semantic-codebook/dispatch-v0.om) is an isolated deterministic .om smoke using three pure wrapping u8 functions:
- short ref 0: add one;
- short ref 1: multiply by two;
- short ref 2: two times input plus an 8-bit parameter.

[codebook-v0.txt](../examples/semantic-codebook/codebook-v0.txt) has exact synthetic bytes hashed by SHA256. A provided 256-bit synthetic test root is compared as four little-endian u64 limbs, and an epoch (9) is required. Unknown index, stale epoch and changed test root must refuse. The status/result is a bounded tagged u16: 0x00vv valid u8 value; 0xff01 stale epoch; 0xff02 missing index; 0xff03 wrong root. No real canonical full-ID validation, authenticated origin or capability checks are present in this tiny program. It is **not a secure codebook** and cannot carry u64 results or effectful operations. It demonstrates the selection/refusal shape only.

The caller can choose the supplied synthetic root; matching the root is not proof that the caller has authority. In later revisions a separate, typed status/result structure and **independently verified admitted** full ID are mandatory.

Native .om compilation and execution have not run in this tool session. [README](../examples/semantic-codebook/README.md) contains the exact AArch64 reproduction commands and nine expected cases. A future smoke PASS would only prove the bounded example under the selected toolchain, not the full SEM-1 contract.

## Truthful compression accounting

For n uses of a semantic operation, original encoding/body length B, per-call index/parameter/lookup bytes C, and dictionary/install/proof setup D:

    baseline = n * B
    factored = B + n * C + D
    total savings requires n * (B - C) > B + D

Count all dictionary and parameter bits; do not assume an index by itself physically eliminated any model weights or memory traffic. Code size and **actual executed** instructions are distinct from descriptions of what a program means, task latency, semantic generalization and joules.

[SEM-2 Omega #722](https://github.com/aien-dev/omega-mv/issues/722) owns true executable byte factoring (current M11 omega_refactor_program copies executable bytes unchanged while decreasing reported instruction cost). [SEM-3 Omega #723](https://github.com/aien-dev/omega-mv/issues/723) owns parameterized meaning mining, existing GEN-3 #478 owns verified abstraction records, reuse #651 and trace compilation #658 own durable skill procedures, and [Benchmarks #43](https://github.com/aien-dev/benchmarks-mv/issues/43) owns independent measurement.

## Admission and qualification before production

- Freeze canonical semantic bytes, effect/numerical state, ABI/refusal code, lookup namespace and codebook root under existing owners.
- On every admission independently verify true semantic ID, external proof/signature chain, dictionary root and generation; no self-minted pass.
- Test stale dictionary, id reuse, conflicting namespace, malformed lengths, unknown index, numeric-contract mutation, mismatched effect/capabilities, wrong tenant and expired/revoked entries.
- Report actual original and factored *serialized* bytes including first-load and warm setup, compiled code bytes, dynamic instructions and full latency separately. A representation that stores exactly the same executable bytes must not claim binary compression.
- Compare exact cache, whole-program library and learned parameterized knowledge on genuinely unseen tasks with preregistered independent benchmark.
- No live 35B cutover, chip run, private trace export, driver restart or release promotion from this proposal. Keep source, test, independent reviewer, PR and final receipts in the owner issue.

Status of all required qualification gates is **NOT_RUN** until actual source-pinned execution and independent review.