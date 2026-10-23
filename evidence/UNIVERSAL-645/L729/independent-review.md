# Independent L729 source review

Reviewer: `/root/recursion_review`, 2026-10-23. Read-only source review in separate
detached worktree `/home/drakestapleton/workspace/omega-recursion-review-L729`.
No author source, existing owner tree, publication state or live service changed.

## Disposition

PASS for the explicitly bounded handwritten `.om` workload/instrumentation and
development-qualification slice at final source
`6de092948a7c8f87dd2cd18a0886c335bd635a3e`. Two medium findings against original
`4ac201c1a0dbbd1cd36c836a98d9a5e46b7c60ac` were reproduced, corrected by the
author, and independently rechecked. No remaining important or critical finding
in the eight-file scope. This is not whole-issue acceptance, signed Spark judge
approval, publication/merge approval or a general translation-equivalence proof.

## Findings and correction evidence

1. **Medium, corrected: exact maximum session refused.** Original
   `tests/universal/recursion/batch.om:139` returned67 unconditionally after8192
   requests. An exactly full valid batch produced8192 valid responses but failed
   the process, contradicting the documented limit. Independent runs observed
   8191 requests -> exit0/8191 responses;8192 -> exit67/8192;8193 -> exit67/8192.
   Correction `a071e8d5fbd6e6ea1a534b1067d431e3d15e0796` checks EOF after the
   permitted batch. Final replay now reports8191 and8192 -> exit0,8193 and8192
   plus one extra byte -> exit67, with no response for extra input. Original raw
   input/output/exit evidence remains in `build/L729/review-probes/batch-*`;
   independent corrected exact8192 probe is `batch-8192.corrected.*`.

2. **Medium, corrected: oracle overwrote error precedence.** Original
   `tests/universal/recursion/gate.mjs:53` checked graph children even after
   version/budget failure, replacing the first named refusal. Version3 plus a
   malformed self child gave reference65 versus native66; fuel4097 plus that
   malformed child gave reference65 versus native67. Native behavior matched the
   specified precedence. Correction `6de09294` checks graph shape only absent a
   prior status and adds compound-invalid cases. Final independent probes agree
   on66 and67 respectively. Original and corrected evidence is retained in
   `build/L729/review-probes/precedence*`.

## Executed verification

The reviewer constructed a separate checkout from the exact original commit,
ran `quiet-build bash tools/source-aware/build`, then ran
`quiet-build node tests/universal/recursion/gate.mjs` there. The initial sandbox
attempt could not spawn child processes (EPERM), so the real run used explicitly
approved escalation. No sandbox failure was counted as test execution.

- Original frozen source run `build/L729/run-lK25hR/receipt.json`: exit0,
  3458/3458 native/reference observations, zero mismatches. Fuel-decrement mutant
  compiled/executed with1135 differing observations; six further compiled native
  mutants produced950/77/224/1/22/195 mismatches for frame limit, storage limit,
  return, argument order, continuation and child order respectively. Every mutant
  process exited0, so these detections came from real differing observations.
- Corrected frozen source run `build/L729/run-CG08gJ/receipt.json`: exit0,
  3464/3464 observations, zero mismatches; all seven compiled faulty versions
  still detected; session exact/overflow controls pass.
- Native tutorial output is exactly `2016\n80\n`, exit0, on both source pins.
- Independent extra controls: nonzero unused triple refuses65, followed in the
  same process by an intact valid response; maximum unsigned64 input remains
  fuel-bounded; compound-invalid precedence agrees after correction.
- The final gate verifies its timed batch outputs before retaining measurements.
  Measurements are shared-host full-process observations including startup,
  validation and pipe transport. They establish no isolated-function speedup.

The parent reports the unchanged inherited codec suite separately. I did not
rerun that unrelated full suite, and do not count its result as reviewer execution.

## Exact identities

- Compiler source:
  `8608fa11f6a2cf1c50db22d871eef16b00d5bc719bc1a222cc3e456984ac5ebf`.
- Reconstructed compiler binary:
  `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
- Original combined native workload source:
  `cb9bb0c3e77e25026ff5165f6c6d0f236b86a10ae7101550441fec5b350f63e6`.
- Original native workload binary:
  `8d2728b66c88813bd9722c1b087cfb6c20d4b2cc727811690f1c75066d37019e`.
- Final combined native workload source:
  `5664e3b04e89231da085078ca4b98099fd4dbe81f0422241e009d96ec0ae94fd`.
- Final native workload binary:
  `c906ec230ffe6c9d90b3aabfd83a089809d9be50732e62c3463ab9b62da35184`.
- Tutorial binary, unchanged by corrections:
  `408ce96883e7f3dc507cc52b7498de1ef4cc15364307b93b0952566fbcd3a1a5`.

Every reviewer gate command, exit, output digest, source digest and raw output is
in the corresponding receipt/run directory. These hashes bind identity; they
are not proofs of translation equivalence.

## Resource and claim audit

Logical resources are explicitly distinct from native stack use. Private state
is serialized; every admitted activation is unwound on failure; the tested entry
does not publish a successful value after exhaustion. Tree children precede their
parent index, so accepted input structures are acyclic, while repeated subtrees
remain limited by total fuel. Iterative baselines preserve logical depth/storage
limits rather than changing the observation when physical execution changes.

The proposed conservative native stack bound is defensible as **source-derived**
for these exact fixtures and this pinned compiler: `alloc` caps live register
slots at16; `save` reserves round16(8*(n+1)), at most144 bytes per call; these
fixtures have no local arrays. Main/request, at most64 admitted recursive calls,
one refused recursive call and the guard/helper chain remain below70 simultaneous
save areas. Thus70*144=10080 bytes is a conservative static bound. The compiler's
program startup reserves8MiB for its stack; compiler source and native ELF
headers were inspected. Actual native high-water is UNMEASURED. This argument
does not replace independent machine-code translation validation.

The source-aware v1 codec is unchanged and still excludes recursive interchange.
The proposed v2 names do not implement canonical function/control-flow records,
a general recursive importer, lowering, authenticated receipt binding, native
cache integration or native ownership-verifier worklist. Those are explicit
remaining issue gates. This review approves no new production language capability
beyond the executed handwritten workload pairs, and does not close #621/#623/
#624/#625/#645/#646/#647 or architecture#295.
