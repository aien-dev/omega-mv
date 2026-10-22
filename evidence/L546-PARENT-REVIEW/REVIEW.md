# L546 corrective parent review

**ACCEPT_HOST_SCOPE** at exact source tip `c100a6eb145e47dc669f4f0422c60b968496025e`, source milestone `f7f8eead0d8686e6c03e368600b7b113cc71aa29`. The prior linked-lock data-loss finding is resolved in this follow-on, not in held original L537 `d84142f73f0fd0e44d22893232fdf0ee2a46a4d4`.

Parent read the complete corrected helper, both safety test/control files, production Makefile dispatch and author packets. The lock now rejects symbolic links, multiply-linked leaves and non-regular types before opening. Append opening preserves pre-existing regular bytes. After opening, descriptor link count and device/inode identity must match the persistent leaf before taking the flock. No active lock is unlinked. Same canonical output scope, nested jobserver and inherited descriptor lifetime remain intact. Only existing Linux shell/stat/flock/proc primitives are used.

This is a cooperating-build fix in a trusted caller-owned output namespace. It does not claim isolation against hostile concurrent pathname replacement, untrusted Makefile/configuration input or a malicious user with write access to the shared directory. That explicit limitation is not a waiver of the static unsafe-leaf tests, all of which actually run.

## Parent's actual fresh execution

Source head and remote ref were read back. Fresh output: `~/workspace/targets/L546-parent-20261022T210316Z` on the Spark.

1. `quiet-build timeout 150s node tests/build_lock_file_safety_controls.mjs PRIVATE/safety`: exit0. Production18 safety cases pass, through direct helper and actual Make dispatch. Unsafe leaves refuse2 with the specific diagnostic, preserved victim hash/stable inode and absent execution marker. Positive new/empty/nonempty/alias cases execute normally. Three shell behavioral controls each exit1 with only the exact expected failing assertions: original unsafe opening, unchecked append, and truncating a regular lock. Signals, deadlines and unrelated errors do not count.
2. `quiet-build timeout 150s node tests/physics_build_concurrency_controls.mjs PRIVATE/concurrency`: exit0, all8 original exact rows pass. Includes real tiny objects, orphan compiler exclusion after make death, nested parallelism/jobserver, alias/independent-output behavior and cleanup/recovery. Three original behavioral controls retain their exact mismatch/partial-object failures.
3. `quiet-build timeout 150s node tests/physics_header_identity_controls.mjs PRIVATE/history`: exit0, all9 original historical outcomes retained. Original PR495 concurrency still actually fails its mismatched-output assertion; it has not been relabeled fixed.

Raw actual command output/results and file hashes are included, with the reproduction driver. Original Makefile and existing8/9 tests are unchanged from L537. Author L532/L537/L546 evidence remains unchanged. Parent did not edit author source or run any driver/model/GPU/service operation.

## Desk disposition

Lift the specific linked-lock source HOLD only for corrected `c100a6eb...`. Carry its L532 safe-test base and L537 stamp/dependency/serialization changes; do not integrate the original unsafe PR495 or L537 helper alone. Parent re-review verifies this bounded host fix, not production deployment or whole-repository eligibility. Existing full driver/current-model/hardware/shared-train and integration gates remain with their canonical owners. No GitHub issue or PR is closed/merged by this review.
