# L588 independent corrective-source review — Omega #625

**Verdict: PASS for the narrow stale-evidence correction at candidate `69790fb4ad1e803ca79245430745b96129d1dcf4` (source correction `c1da86d68fb1d4af20c879a82f373a4556f7e957`).** I found no reproduced stale-PASS retention in either public Make entry or direct runner, and no regression-test assertion gap affecting the targeted cases. This is not source-conversion/proof approval, full suite approval, merge approval, or closure of any parent issue.

## Evidence

Reviewed the immutable candidate in reviewer-owned worktree `/home/drakestapleton/workspace/hermes-desk/universal-L588-review`; `git rev-parse HEAD` returned the exact candidate SHA above. Author worktree and live services were not modified.

Fresh command:

```sh
make OUT_DIR=build/L588 test-universal-semantics
```

Exit 0. Lifecycle controls: `Ran 3 tests ... OK`; semantic replay: `PASS rows_per_oracle=412 bigint_counterexamples=373 compiled_mutants=2`. Receipt at `build/L588/universal-semantics/receipt.json` reports `result=PASS`, `proof=false`, `conversion_implemented=false`, and `source_commit=69790fb4ad1e803ca79245430745b96129d1dcf4`. Receipt SHA-256: `4048b6839ab83b9fda13ba91b39029439213bbf167a4906056a1c62a9eb5a40d`. Mutants were compiled and caught: C XOR/add had 343 mismatches; Rust saturating/wrapping had 179. Oracle outcomes are as scoped by the receipt, not proof of translation.

Additional seeded-stale-receipt controls on isolated `build/L588-controls` output:

- Direct runner, `UNIVERSAL_OUT=... CC=<missing-compiler> python3 tests/universal/semantics.py`: exit 1 with `FAIL: [Errno 2] ... missing-compiler`; seeded receipt absent afterward.
- Public Make, `make OUT_DIR=build/L588-controls CC=false test-universal-semantics`: exit 2 at the oracle build; seeded receipt absent afterward. Lifecycle unit test independently exercises this same build-failure path with a synthetic tree.

The source invalidates the transient output receipt at the beginning of direct runner preflight, and Make has the phony reset as an order-only prerequisite of probe build. Regression tests also seed old PASS receipts and assert absence after missing-compiler, contract-digest, and probe-build failures. `evidence/UNIVERSAL-625/review-fix/` is historical append-only evidence and remained unchanged during replay; checksums were recorded for its JSON/MD artifacts.

## Findings and limits

No findings against the specified stale-evidence remediation. Public and direct failure paths both remove prior transient PASS evidence; the new tests assert actual failure status/diagnostic and receipt deletion, rather than merely testing helper calls.

The evidence remains limited to the handwritten arithmetic/reference-oracle slice on Spark. The receipt explicitly leaves x86-64, RV64GC/QEMU, GPU, checker2 translation proof, and language import/export unrun. No compiler/converter correctness, formal proof, owner/signing capability, generic READY publisher behavior, or broader architecture claim is implied. Full legacy compiler/checker/base Make suites were not rerun, per brief. No source edits were made; generated build output is under the reviewer-owned worktree only.
