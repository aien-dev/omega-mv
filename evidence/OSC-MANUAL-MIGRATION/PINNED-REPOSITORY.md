# Repository regression with the pinned physics dependency — L561

The earlier `make test` failure was a dependency mismatch, not a passing test and not a reason to disable the lock. It is preserved in `copy-repository-test.txt`.

For the corrective run, an isolated detached physics worktree was created at exactly `6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf`, the commit required by `physics.lock`. The shared physics checkout remained at `e95e3ed2a86fe4bffe4d954fa94c27dfb5284280`. A separate output directory prevented reuse of objects previously compiled against the wrong dependency.

## Executed commands

From the migration worktree at `63e06754b1f0c3dbc0795bdcb4ecca6b00a9846e` on the Spark:

```
git -C ../physics worktree add --detach "$PWD/build/manual-migration/physics-6d7cf0d" 6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf
git -C build/manual-migration/physics-6d7cf0d rev-parse HEAD
git -C ../physics rev-parse HEAD
timeout 120 quiet-build make test \
  PHYSICS_DIR="$PWD/build/manual-migration/physics-6d7cf0d" \
  OUT_DIR=build/manual-migration/repository-pinned
```

No `PHYSICS_LOCK_CHECK=0`, test skip or source edit was used. The existing target completed with RC=0: 14/14 M4 semantic gates and the arithmetic/physics reference demonstrations passed. Raw compiler output and gate results are retained in `pinned-repository-test.txt`; machine-readable provenance and parsed counts are in `pinned-repository-result.json`.

## Scope

This clears the repository's default `make test` target for the migration head. It is **not** every Makefile test target, a GPU qualification, independent review of the copier, or proof that legacy implementations have been retired. The target builds the historical C reference implementation; those binaries do not enter the seed-native compiler/copier construction chain or replace its focused native gates. The executed gate dispatch was inspected before running: `--run-gates` selects M4 semantic gates, plus the two reference demonstrations named by the Makefile.

This milestone changes verification evidence, not the production implementation. The previous copier milestone's full-default-target blocker is resolved; its limitations about partial-write testing, host orchestration and lack of system-wide retirement remain.
