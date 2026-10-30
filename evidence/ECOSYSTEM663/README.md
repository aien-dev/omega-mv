# M20 metadata-v1 host qualification

Functional source: `3a995ba8dace538c1532c747d829ca6e4a73582d`.
Base: `cd5897e3f92f72a015892c67b17f096e433b54f7`.
Compiler ELF SHA256:
`191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
Compiler source SHA256:
`8608fa11f6a2cf1c50db22d871eef16b00d5bc719bc1a222cc3e456984ac5ebf`.
Existing seed package declares source
`29b12760f4c962606411d6bf25032aedef063301`. This worker consumed the
already-built exact compiler in L775's extracted source package; the
programme's separate #660 worker owns fresh compiler reconstruction.

## Observations

| Case | Compile exit | Native exit | Observation |
|---|---|---|---|
| Final source | 0 | 0 | 14457 checks pass |
| Faulty offset accounting | 0 | 1 | check 30 fails |
| Faulty generation comparison | 0 | 1 | check 37 fails |
| Faulty generation retirement | 0 | 1 | check 54 fails |

Of the positive checks, 14400 enumerate tiny rank-two layouts. Dimensions are
1..3, each stride 0..4, offset 0..3 and storage capacity 1..16. A separate
algorithm visits every logical element and compares its index with capacity;
it does not use the production maximal-extent formula. Remaining analytic
cases include scalar/rank-eight shapes, full supported shape capacity,
overflow-sized inputs, singleton dimensions with huge strides, storage-only
dtypes, exact last element, stale handles, double release and retirement.
Independent final-source review is recorded separately by the reviewer.

All three mutants are complete handwritten `.om` translation units and compile
successfully. The offset mutant removes offset from remaining extent. The
generation mutant bypasses the equality test. The retirement mutant fails to
mark maximal generation retired. They fail the unchanged positive test suite.
Compiler refusal is not counted as a detected runtime mutant.

`readelf.txt` identifies an AArch64 ELF with no dynamic section or interpreter.
The test's output uses native Linux write/exit operations. No C, Rust, Python,
framework or GPU library executes the component. Existing shell commands,
`quiet-build`, concatenation, `chmod`, `readelf` and `/usr/bin/time` are disclosed
development orchestration, not delivered native automation.

The host was Linux `7.0.0-1019-nvidia`, AArch64. This is CPU execution only.
No model, tokenizer, dataset, GPU, simulator, network or production service was
used. Native allocation, floating-point operations, gradients, model training,
hardware qualification and production cutover remain NOT_RUN.

## Retained development failure

`compile-first.om` and its stderr/exit preserve the first test compile failure:
the write result (`long`) was compared with an explicitly converted `int`.
Compiler error 7 correctly refused the mismatched types. `compile-second.om`
records the single conversion correction (`long`) and compiled successfully;
its original native output reports 14454 checks. The final source then adds
three slot-width cases (slot zero, maximum u32, too-large slot), reaching
14457 checks. Earlier evidence is retained without being relabelled final.

## Cost and interpretation

`shared-host-cost.txt` retains a separate `/usr/bin/time -v` invocation including
the existing quiet-build wrapper. It recorded 0.01 s wall time and 1716 KiB
maximum resident memory at that tool's resolution. This is a single shared-host
development observation, with no baseline or speedup claim. Compiler and full
workflow cost, model quality, energy and hardware performance are unmeasured.

## Reproduction

Use the exact compiler above. Concatenate the committed native module followed
by the committed test into one translation unit, compile from stdin, require
compiler exit 0 before marking the result executable, then execute it. Expect
`M20_METADATA_V1_PASS checks=14457` and exit 0. Repeat with each committed
`mutant-*.om` and expect compiler 0, execution 1 and the check listed above.
`commands.json` records the concrete commands used; `source-artifacts.sha256`
binds the source, original contract files, actual compiler and resulting ELFs.
`raw.sha256` binds the raw observations and exact `.om` units.

This bounded host slice does not close #663/#664 or Architecture #303. The
legacy sources remain because their consumers have not switched. Their removal
requires full native functionality and consumer qualification through existing
L408/L418/L491/L495 owners.
