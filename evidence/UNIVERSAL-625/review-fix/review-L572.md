# Independent review L572 — Omega #625

**Reviewed source:** `d9ce93af22745635809ab474893e748334d10211`  
**Base:** `a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a`  
**Verdict:** The bounded semantic-contract/arithmetic-oracle slice passed a fresh replay on this exact source; one medium-impact stale-evidence defect remains in the reusable runner. This is not merge approval or a qualification of the broader universal-conversion programme.

## Findings (graded by user impact)

1. **MEDIUM — failed rerun can leave a stale PASS receipt.** `tests/universal/semantics.py:73` writes `receipt.json` only after every check passes and does not invalidate/remove a prior receipt at startup. Re-running with a previously successful `UNIVERSAL_OUT` and then encountering a stale contract digest, missing oracle, timeout, or semantic mismatch exits `FAIL` while leaving the old `result: PASS` receipt at the expected path. A downstream consumer that checks the receipt rather than the latest command status can mistake stale evidence for current verification. Invalidate the prior receipt before any checks (or write each attempt to a unique run directory and atomically publish a new pass receipt only after success); add a regression that seeds an old PASS receipt, forces a failure, then asserts no PASS receipt remains.

## Fresh replay

Command on Spark:

```sh
cd /home/drakestapleton/workspace/hermes-desk/universal-L569-omega-mv && make OUT_DIR=build/L572 test-universal-semantics
```

Result: exit 0; `PASS rows_per_oracle=412 bigint_counterexamples=373 compiled_mutants=2`. All 412 outputs matched byte-for-byte for C, explicit-wrapping Rust, explicitly masked Python, and existing OSC interpreter/native execution. The unmasked CPython oracle differed on 373 rows. Both compiled mutants were caught (C XOR/add: 343 mismatching rows; Rust saturating/wrapping: 179). Receipt: `/home/drakestapleton/workspace/hermes-desk/universal-L569-omega-mv/build/L572/universal-semantics/receipt.json`, SHA-256 `1f097ba843992da12b48a9429dc263afc1aa0ecb01dbd9b357d97ce697ee760f`; its `source_commit` is the exact reviewed SHA. Receipt identifies `DIFFERENTIAL_COMPATIBILITY`, `proof: false`, and `conversion_implemented: false`.

## Scope and limits

The contract appropriately distinguishes C unsigned-width behavior (including integer promotions), explicit Rust wrapping from ordinary arithmetic, Python bigint from explicit masking, equivalence from directed refinement, content identity hashes from semantic proof, and test timeout from program semantics. The fixtures are independent handwritten development oracles, not a converter. The C fixture exercises `uint64_t`, not the contract's narrow-integer promotion example; that example remains specification-only in this slice. The replay was on AArch64 Linux with the available C/Rust/Python toolchain and the existing OSC interpreter/native APIs. x86-64, RV64GC/QEMU, GPU, checker2 translation proof, and language import/export were not run and are not claimed. Existing full compiler/checker suite was not rerun, per brief. No source files were modified; only ignored output under `build/L572` was generated.
