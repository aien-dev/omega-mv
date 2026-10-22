# Parent review: L537 concurrency correction

Exact author tip: `d84142f73f0fd0e44d22893232fdf0ee2a46a4d4`.
Source milestone: `ab070f60fcb9d31bf8d1caa6caeec435571cf5c0`.
Strict behavioral control milestone: `5d819b58a7e25a44c87f2563ec115a5ebd703d4b`.

## Disposition: HOLD for integration

The original build identity race is corrected under the tested interleavings, but the newly introduced persistent lock opener can erase unrelated file content. A separate narrow follow-on, L546, owns the fix. This is a parent-discovered source defect, not an unrun hardware gate or a reinterpretation of a watchdog as success.

## P1: opening the adjacent lock truncates linked files before acquiring it

`tools/with_build_lock.sh:9` uses `exec 8>"$canonical.omega-build.lock"`. Shell output redirection follows symbolic links, accepts hard links, and truncates the resolved inode before `flock` executes. A pre-existing linked lock leaf therefore destroys another file's bytes even if the build command is just `true`.

Actual parent reproduction on the Spark, private synthetic fixtures only:

1. Create an owned `build/` output and a separate nonempty synthetic victim file.
2. Pre-create `build.omega-build.lock` as either a symbolic link or hard link to that victim.
3. Run `sh tools/with_build_lock.sh /absolute/private/build true`.
4. Both invocations exit0. Both victims are empty afterward. Before SHA256 is `5e08e3db1e21f36f17e8ce814a2d10d2fc40b8cf1e3e503c179cac63c383ac8f`; after SHA256 is `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

The public probe script and raw JSON preserve that result. No real user files, other worktrees, service locks or model/hardware state were modified. This is not a claim of a cross-user exploit or protection against hostile concurrent directory replacement; it is directly observed destructive opening of a pre-existing linked leaf.

Required correction: preserve unrelated bytes, refuse unsafe lock leaves without a false success, retain one persistent valid lock inode and descendant lock lifetime, and test regular reuse plus linked/special-file refusals. Keep canonical output aliases working. Do not unlink active locks, weaken the original race assertions, accept a timeout/crash as a refusal, or replace the current Makefile with a historical snapshot. Exact implementation and dependency choice belong to the fixer after inspecting existing host helpers.

## Independently rerun original acceptance

From the exact author source, parent ran a fresh private output through:

```sh
quiet-build timeout 180s node tests/physics_build_concurrency_controls.mjs PRIVATE_OUTPUT
```

Actual exit0. All8 exact rows passed:

- Original serial/header identity oracle.
- Concurrent different identities.
- Concurrent same identity.
- Concurrent refresh.
- Real object lifetime and nested/alias/recovery/termination checks.
- No-exclusion mutant: exit1, exact `no_mismatched_output_accepted_as_current` failure.
- Early-release mutant: exit1, same exact mismatch assertion.
- No-error-cleanup mutant: exit1, exact `failed_partial_object_removed` failure.

No timeout, signal or unrelated failure counted as a killed control. This confirms the original test boundary while demonstrating why it does not cover the newly found file-safety defect. Hardware, current-model, full production-driver and merge-train qualification remain NOT_RUN, with their existing owners. No GitHub issue/PR was closed or merged.

Raw parent run: `~/workspace/targets/L537-parent-20261022T204002Z` on the Spark. The receipt in this branch copies its actual linked-lock probes, acceptance log, result JSON and individual case logs. Author worktree/evidence remained unchanged; remote tip and clean state were read back before intake.
