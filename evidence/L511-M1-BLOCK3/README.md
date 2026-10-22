# M1 block 3 host receipt (#576)

Measured 2026-10-22 on Spark, Linux aarch64, kernel 7.0.0-1019-nvidia. Source commit `7e0257ab56020de37b547b2f4afc5dde13553e9a`; binary SHA-256 `aa5467a895dbbe5107e2f4252139ddbe88d72391c95a225f41ef2a6869f8469b`. The exact binary was built from that commit with `OUT_DIR=build/L511` and AIENOS authority source at the `aienos.lock` commit. Input workload is the existing `rx_plan_reuse_test` arrangement suite: 40 instances each of reverse tower and merge towers, 39 accepted reuses each. The JSON receipt is content-addressed by its SHA-256.

Commands and exits:

- `quiet-build make OUT_DIR=build/L511 AIENOS_LOCK_REPO=/home/drakestapleton/workspace/aienos-m5-qual build/L511/rx_plan_reuse_test`: exit 0.
- `quiet-measure env OMEGA_CANDIDATE_COMMIT=7e0257ab56020de37b547b2f4afc5dde13553e9a build/L511/rx_plan_reuse_test`: stopped our waiting command, exit 130, because a pre-existing shared-lock holder prevented an exclusive timing window. It did not execute the test.
- `quiet-build env OMEGA_CANDIDATE_COMMIT=7e0257ab56020de37b547b2f4afc5dde13553e9a build/L511/rx_plan_reuse_test`: exit 0, 1304 checks, zero failures. Run was on a shared machine; timings are measured but not quiet-machine coefficients. Raw log: handoffs/omega-issue-sweep-20261022/L511-block3-shared-run.log.
- `bench/m1/block3_receipt_csv.sh <receipt.json> evidence/L511-M1-BLOCK3/block3.csv`: exit 0.
- `tests/m1/block3_csv_test.sh`: exit 0; six mutants covering unbound, dirty, failing, incomplete, unequal outcome, and wrong revision were refused.

Measured medians: reverse tower first/reuse end to end 2.985/2.863 ms; merge towers 3.943/3.744 ms. Cognitive CPU medians are 67.864/3.136 us and 124.824/3.280 us. Equality checks passed in all 40 cases of each workload. The pre-registered `OMEGA_PLAN_REUSE_PASS` remains **FAIL** because reverse tower misses G1 (operation-count reduction); the narrower task wording is MET. Preserve both verdicts.

Retained bytes and isolated lookup time are UNKNOWN because the existing qualification receipt does not report them. Wall joules are UNMEASURED because no wall energy meter was used. No GPU or model qualification is claimed. These missing fields and quiet-machine rerun remain open for #576/#578.
