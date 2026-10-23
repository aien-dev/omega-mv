# Independent review — L615 bounded macOS ARM64 target

**Ledger item:** L619, owner `sa-0-c82fb525` (review only; no ledger closure performed).

**Reviewed source head:** `f43e6755d95a9783040b5ec2147ae519e9b01a11` (`dev/macos-arm64-L615`). Parent reports the later public `verify/macos-native-L615` descendant `a9e596258a03227c8578e398dd90c699451b42ec` is evidence-only; the functional source reviewed here remains the exact f43e6755 head.

**Comparison baseline:** the requested `baseline57f5f4258cebf3d5b2c22853cd062e351265c525` resolves in this checkout to commit `57f5f4258cebf3d5b2c22853cd062e351265c525` (the `baseline` text is a label, not part of the Git object ID).

**Scope reviewed:** `ports/macos-arm64/`, including `adapter.om`, `CONTRACT.md`, `build.sh`, `target.sh`, `tests/export-replay.sh`, `tests/mac-replay.sh`, `tests/check-macho.mjs`, `tests/run.mjs`, fixtures, and supplied evidence/receipts. The Spark checkout was at the requested head and had no reported working-tree changes. No source was modified. No compiler was consumed or rebuilt, no Mac test was run by this reviewer, and no publication/ledger/service action was taken.

## Finding

### Minor — directory output path is treated as successful file output

**Locations:** `ports/macos-arm64/target.sh:9-14` (especially line 14); same pattern at `ports/macos-arm64/build.sh:8-13` (line 13).

Both wrappers create a staged regular file, then invoke `mv "$stage" "$destination"` without first rejecting a directory destination or verifying that the requested destination is the resulting regular file. On Spark, I ran the existing `target.sh` against the real `return42.elf` with an already-existing directory as the output argument. The command exited **0**, left the requested directory in place, and placed a 32,784-byte Mach-O under the incidental name `output-dir.L615.<random>` inside it. Thus a caller can receive success while the requested output path is not a Mach-O; for build.sh the analogous pre-existing `build/adapter/` directory similarly causes the build product to be nested while the build reports success. This is not an input-refusal data-loss issue, but it is a misleading success/incorrect output-path behavior. Validate destination type and verify the final destination is the staged regular file (or otherwise make the destination operation fail closed). Existing regular-file replacement and refusal-preservation behavior is otherwise consistent with the stated contract.

## Review assessment

- **Critical:** none found.
- **Important:** none found within the explicitly bounded target contract.
- **Minor:** one finding above.

I reviewed the contract’s ELF/layout checks, leaf instruction masks and branch/syscall validation, exact hello template and transformations, Mach-O command/header construction, runtime syscall/stack assumptions, refusal precedence, and the shell/build/test paths. The adapter’s decoder constraints appear consistent with the documented subset on inspection; I did not find a concrete mask/branch/header/runtime-ABI defect. Tests include exact output/format assertions, byte preservation, compiled refusals and discriminating compiled controls; the supplied Spark `RESULTS.md` reports 66 checks but explicitly does not claim native Mac execution. Per the parent’s out-of-band update, the exact exported source subsequently passed its separate immutable native macOS ARM64 replay (12 cases) on macOS 26.6.2. That is bounded runtime evidence on that host, not general platform ABI, installer, broader source-form, or issue634 qualification.

**Disposition:** scoped review finds no Critical or Important source-contract issue; one Minor shell destination-type issue remains. Broad macOS support / issue634 stays open.
