# L623 focused independent review

- **Reviewed head:** `80d88a6af1425d28881daf8830eb966dc0ae9f3c`
- **Delta:** `a9e596258a03227c8578e398dd90c699451b42ec..80d88a6af1425d28881daf8830eb966dc0ae9f3c`
- **Scope:** only `ports/macos-arm64/target.sh`, `build.sh`, `tests/output-paths.mjs`, `tests/run.mjs`, and their related contract/evidence context. No source or remote state changed. No adapter/Mac retest performed.

## Finding

**No findings in the precise delta.** The prior Minor is resolved: both wrappers check `[ -d destination ]` and exit 73 before staging, which rejects real directories and symlinks resolving to directories. Both publication steps now use GNU `mv -T --`, preventing container-directory semantics if a directory appears after preflight; the disclosed Linux-host dependency appears in `CONTRACT.md`. This is not represented as a hostile-race sandbox.

Normal-file behavior is covered: target creates then replaces the requested Mach-O output, and build replaces the regular adapter with byte-identical seed-built output. The wrapper regression module contains four directory/symlink-directory refusal cases plus these two regular-file cases, and is wired into `tests/run.mjs`. Existing retained Spark output records all six passing, with the combined qualification previously recorded as 67 passing checks; I relied on that retained evidence and source inspection rather than rerunning the harness.

Source/target identity linkage is reasonable for this wrapper-only change: recorded identity evidence compares 17 artifacts (15 Mach-O outputs plus `adapter.om` and the built adapter) against the original native replay inputs, all matching. The delta changes only shell wrapper behavior, test integration, contract text, and evidence; it does not alter adapter source or native output bytes. This does not claim a fresh native macOS run.

## Caveat

GNU `mv -T` is intentionally Linux/GNU-specific and is explicitly disclosed. The test module imports `node:os` unused; this is a harmless test cleanliness nit, not a defect in the reviewed correction.
