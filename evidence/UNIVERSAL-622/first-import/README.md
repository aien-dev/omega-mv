# First bounded handwritten C importer: executed evidence

Functional source freeze: `50f75da28845f21b118bcf8e3499b96c35ddb587`.
Upstream #621 feature source: `cd2c41f3c697524629585b2a228e2f0ab5b99c45`.
This evidence-only successor does not change either runtime source closure.
Independent combined source review L602 is pending. No merge, signed judge,
production transformation authority or complete #622/#295 claim is made.

## Committed-source cold replay

A new local shared clone was checked out detached at the exact source freeze,
with no build directory. The declared source-only bootstrap and native commands
were rebuilt from the pinned bridge source. Three commands executed sequentially:

1. `make -f mk/c-u64-import.mk test-c-u64-import`: PASS. 35 original C programs,
   48 refusal cases, 817 actual native-GCC behavioral observations, 105 existing
   native #621 decode/replay/legality requests; 15 boundary/public controls;
   five successfully compiled C-importer semantic mutants caught by actual
   mismatched values, not compilation/refusal failures.
2. `make PHYSICS_DIR=<exact pinned physics checkout> test-source-aware`: PASS.
   10000 unique valid and 100000 unique malformed units, 131000 consumed-input
   hash-checked structural requests, plus the capability/edge/public/direct/core
   and nine compiled mutant controls recorded in its complete receipt. Sixty
   legacy OSC fixtures retain byte identity and use existing decode/checker2.
3. `make -j2 OUT_DIR=build/L589/foundation PHYSICS_DIR=<same pin> test`: PASS.
   Fourteen base gates plus two existing reference demonstrations. Their canonical
   identity examples are not behavioral-equivalence proofs for C conversion.

The C gate's consumed #621 native artifact hash was compared with the later full
#621 receipt and matched. Exactly105 requests equals three actually executed
component operations for each of35 accepted C programs; see raw receipts.
All actual source/toolchain/artifact identities, machine/OS facts, complete logs,
per-program C and upper values, preserved source bytes and upper units accompany
this record. This is correctness evidence on the physical Linux AArch64 Spark,
not a performance/energy measurement or x86/RV64/GPU qualification.

## Preserved failures

`development/` contains the original native-stub missing-behavior RED, the initial
oracle fixture error from treating valid unused parameters as compiler errors,
and the later eight-case source-spelling/namespace RED. No original fixture was
removed. The GCC unused-parameter diagnostic alone was excluded from the oracle's
error policy; other selected warnings remain errors.

The original source spelling probe compiled and ran valid C examples independently
and reproduced three genuine pre-freeze semantic defects: octal010 yielded8 in C
versus10 after import; backslash line-splicing and trigraph line-splicing yielded1
in C versus3 after import. The corrected profile explicitly refuses these source
forms before canonical graph publication. These are uncommitted development
history, not qualified immutable-candidate receipts. The final cold replay is
exact-source pinned and includes the resulting refusal regressions.

## What this does not establish

The imported C computation is compared using a development upper evaluator; it
is not yet lowered/executed through Omega's CPU backend. The native handwritten
Omega program executed here is the importer and the existing semantic codec.
No C exporter, additional integer widths, general preprocessing/control flow,
10k-C-program acceptance corpus, checker-bound translation proof, Rust/Python
adapter, other CPU backend or GPU operation is supplied by this slice. Source
replay is byte storage, finite comparison is differential evidence, hash equality
is identity, and retained lowering obligations remain unproved. See
`spec/universal/c-u64-import-v1.{md,json}` for precise supported/refused grammar.
