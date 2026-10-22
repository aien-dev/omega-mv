# L513 L137 fusion replay

This host-only harness replays the eight original L137 fusion simulator checks from source commit `b260b3a4873f56ef4e3f8ea850fef0c19fdca21c`.

`run.sh` exports the pinned L137 Makefile, source, tests, make fragments, lockfile and quietlock tool. It requires the exact `physics.lock` revision, builds the unmodified `test-gpu-fp8` target with the repository's strict default compiler flags, and compiles the reduced `sim_fused()` entry with the original strict flags. The diagnostic patch changes only how a simulator error message is copied into its bounded destination. It does not alter fusion code, simulator behavior, checks, tolerances or production code.

The harness also compiles a scratch-only one-ULP output corruption control. The original bit-identity assertion must reject it. The mutant patch is never applied to the passing build.

Set `L513_PHYSICS_DIR` to a checkout at the locked Physics commit. By default, logs and source/tool/dependency/binary manifests go into a new temporary evidence directory. Set `L513_EVIDENCE_DIR` to a new, empty directory to choose another location. The script uses `quiet-build` for compilation and never opens a GPU or loads a model.

The original L491 BUILD_FAIL files remain immutable under the prior evidence directory. Fresh L513 red and green artifacts are separate.

The separate historical L137 model numerical gate remains FAIL as recorded by its owner. L513 neither reruns nor changes that gate. The fresh green is host simulation/build evidence only.
