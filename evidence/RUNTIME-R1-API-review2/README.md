# R1 Runtime API: second review (Rev955) findings fixed (#955, omega-mv#594)

Tree: commit 36f391500d8f24cf9704c4829a71797248df8803 (PR head 7be2c6f7 plus the fixes; base main 62d1d0d6).
Review record: `docs/runtime/API-REVIEW.md`, review 2. Host only (aarch64 Linux, gcc 13.3.0), no GPU, no physics checkout.
Commands ran under `taskset -c 6-19 nice -n 19 ionice -c3`.

| File | Command | Result |
|---|---|---|
| `test-runtime-api.log` | `make -j2 PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-runtime-api` | exit 0: PASS (96 functions, 28 structs, 237 fields, 159 constants), 16/16 negative controls FAILED as they must. |
| `test-runtime-api-mawk.log` | the same with `awk` resolved to mawk 1.3.4 (the awk of Ubuntu runners) | exit 0, 16/16. |
| `red-old-check-new-controls.log` | the new `tools/runtime_api_negative.sh` driving the check script of 7be2c6f7 | exit 1: the three new controls are NOT CAUGHT by the old check. `manifest-fn-named-in-prose` (the reviewer's mutant: `fn omega_gpu_serving_seal` removed from the manifest) gives PASS with 95 functions; `internal-also-manifest` gives PASS; `new-fnptr-function` fails for the wrong name (`declares int`). |
| `aien-test-run.log`, `aien-test-receipt.json` | `aien-test run tests/gates/omega-runtime-api.gate --no-cache` (local release build, sha256 c36cbbd8…, not the CI pin a51042b6) | AIEN_TEST: PASS, receipt commit 36f39150, derived_verdict PASS. |

Not run: the hosted AIEN Checks run of this gate (the workflow takes the portable gate list from main, so the gate
runs there only after it is merged); any chip behaviour.
