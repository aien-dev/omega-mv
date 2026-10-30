# Universal programme: #625 first implementation checkpoint

Scope: [architecture #295](https://github.com/aien-dev/aien-architecture-mv/issues/295), [Omega #625](https://github.com/aien-dev/omega-mv/issues/625). This is an issue-local checkpoint, not another master plan or a whole-programme qualification.

## Implemented source

Source commit `d9ce93af22745635809ab474893e748334d10211` adds the versioned observation/fidelity contract, hashed profile manifest and independent arithmetic fixture runner. It changes no existing compiler, binary format, identity domain or checker2 source. C/Rust/CPython are development reference programs, not production compiler/runtime dependencies.

`make OUT_DIR=build/universal-625 test-universal-semantics` compiles reference C and Rust, runs masked Python and the existing OSC interpreter/AArch64 native code path on identical deterministic inputs, then checks unmasked Python bigint counterexamples. It requires actual native AArch64 execution; no skip-to-PASS if unavailable.

Author run: 412 input pairs per reference, four arithmetic results per pair; 373 pairs differ from unmasked Python bigint. Two successfully compiled semantic mutants produce wrong output and are caught. These are tested arithmetic observations, NOT source conversions or translation proofs. The initial author receipt pins all input files and produced reference binaries; it ran before source commit, so use its file hashes rather than misreading its parent `source_commit` as a clean-source replay. Independent review and final immutable replay are separate required steps.

## Repository snapshots

Five fetches succeeded. Shared checkouts were dirty and were NOT reset, rebased or pulled over. Fresh detached worktrees from fetched origin/main were inspected, then the Omega snapshot received its own issue branch. `repository-pins.json` records exact local/fetched heads and dirty counts. `issue-audit.json` enumerates exactly all 15 child issues with code paths, hashes and deltas. It is a scoped source/ownership inventory, NOT proof that every uninspected branch or historical receipt has been exhaustively qualified.

Existing work retained: strict OSC decoder #444, independent checker/signed receipt #446, verified-program #505, Omegese kernel #601, GPU runtime #593, portability #596, teaching #619 and E05 architecture #248. Separate Omega Bridge exporter owners L483/L551 and firmware/Interplane/seeded compiler owners remain intact. A finished export branch is not a source importer or evidence of a supported cross-target cell.

## Baseline evidence and failures

- Omega `make -j2 OUT_DIR=build/L569-baseline test-compiler-quick test-checker2-differential`: exit 0. Quick compiler is explicitly smoke, not full qualification. Includes actual compiler, native/interpreter, strict decoder and checker differential outputs in `omega-mv.log`.
- Sovereign Core `cargo test --offline -p aien-abi-core --target-dir target/L569-baseline`: exit 0, 12 integration tests. Does not qualify #706 component interoperability.
- Aienux `make -C native/kernel OUT=../../target/L569-baseline gsp-fw-inspect-test`: exit 0, 22 malformed-container controls plus installed firmware pin check. This is firmware-file inspection, NOT RISC-V host backend/boot or GPU execution.
- Architecture initial `python3 tools/check_doctrine.py`: exit 2 because that path does not exist. Preserved as a discovery error, not a failing doctrine assertion. The actual existing `bash scripts/check_doctrine.sh` was then found through its gate manifest and exited 0 (30 scanned files, no findings). Final evidence must retain both.
- CUDApp `make -j2 OUT_DIR=build/L569-baseline test`: timed out at the runner's 240-second bound. Partial hosted/freestanding and mutant outputs are retained. No aggregate PASS, no device qualification, no retry-until-green.
- Omega bare `make OUT_DIR=build/L569-base test`: exit 2 because the isolated checkout's default sibling `../physics` is absent (`m16_native.h`). This is preserved, not waived. A separate fresh physics worktree was created at exact `physics.lock` pin `6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf`; the declared PHYSICS_DIR run has separate output, never replacing this failure.

The full local Spark judge and hosted signed-bundle verification remain publication gates. This branch is NOT merge-approved at this checkpoint. No issue is closed. Later #621/#622/#623 foundations, Rust/Python import/export, target profiles/x86/RV64GC, typed FFI, tensor lowering and real-device qualification remain incomplete.

## Research used

Authenticated Undermind dossier inspected: 239 ranked papers and summary recommending composable source-aware semantic dialects and staged lowering. Authors' DimSum project page read directly. References/limitations are in the contract; rankings and abstracts do not count as replication or confer proofs on Omega. No new research-based runtime mechanism is claimed by this spec slice.
