# L533 review packet

Read `L533-review.md` and `L533-review.json` for scope, finding C1 and dispositions.
Source under review is L51904aab149, source/test files remain unchanged. All
adversarial outputs are in the reviewer's own build/L533 or this evidence folder.

Actual commands/exits:

| Command (from repository root unless stated) | Exit/result |
|---|---|
| `quiet-build make OUT_DIR=build/L533 test-turing-gpu-replay test-turing-gpu-replay-sanitize test-gpu-session-visibility test-gpu-session-visibility-mutants` | 0,421/0 twice,28 events,4/4 assertion kills |
| `quiet-build sh evidence/L533-REVIEW/run-replay.sh` | 0, independent integer/snapshot/refusal checks |
| `quiet-build sh evidence/L533-REVIEW/run-visibility.sh` | 0, patterned repeats/refusal/recovery; initial wrapper compile failure retained separately |
| `quiet-build sh tests/physics_build_identity_test.sh build/L533/pr495.Makefile build/L533/physics495 dirty` | 0 |
| Same with exact PR546 and `identity` | 0 |
| Same with baseline `identity` | 1, expected directory-switch RED |
| Same with PR546 `dirty` | 1, expected dirty-content RED |
| `quiet-build sh evidence/L533-REVIEW/physics-controls.sh` | 0, intentionally wrong input survives recipe spy, confirms its artifact scope limit |
| `quiet-build make -C build/L533/pr499-source OUT_DIR=build/L533-review test-osc-ir-decode-quick` before missing inputs restored | 0,55 units; smoke only |
| `sh evidence/L533-REVIEW/check-decoder-closure.sh build/L533/pr499-source` before/after restoration | 1 RED /0 GREEN |
| quick binary from same source directory after restoration | 0,59 units; smoke only |
| `quiet-build sh evidence/L533-REVIEW/run-decoder-probe.sh` | 0, ASan/UBSan independent raw wire and full output clearing |
| `quiet-build make -C build/L533/pr499-source OUT_DIR=build/L533-review test-osc-ir-decode` after restoration | 0, full plain+sanitizer+10mutants; complete outputs retained |

`reproduce-decoder.sh` reconstructs the complete pinned export and full gate.
The original restricted export used exact Makefile,mk,src/compiler,sha256,
tests/compiler,tools/quietlock. The correction adds src/osh and tests/physics0/osc.
No gate configuration or counts were weakened. Test budgets were not increased
until green; the full rerun was justified by discovered missing real inputs.

Makefiles materialized using `git show FULL_SHA:Makefile` from existing local
objects (exact SHA/hash in pins). All builds/CPU-heavy tests used quiet-build.
This is correctness evidence on shared Spark; no performance measurement,
GPU access, model loading or native qualification. Binary/tool/source pins are
in pins.txt; raw results are separate from the report's interpretation.
