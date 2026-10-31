# Prefetch control classification

This is the result classifier used by `tools/run_gpu_l2d_prefetch_chip.sh` for
Omega #640. The previous script accepted every nonzero control exit, including
model construction failures before any numerical comparison. The script now
calls this native Omega program and refuses a reused evidence directory.

The historical consumer is `tests/gpu_decode_graph_test.c` from Omega main
`cd5897e3f92f72a015892c67b17f096e433b54f7`. It is a retained C reference, not a
new native model or device implementation. No historical kernels are changed.
The classifier targets the script's exact control invocation: two layers,
vocabulary 151936, position 300, three repetitions, cached weights and
`--prefetch-red`. This switch leaves the separate receipt field `red` at zero;
it sets the prefetch-specific switch printed in the log instead.

## Build and invocation

Use the qualified native host compiler. The development qualification consumes
L788's compiler executable with SHA256
`1d1aad082539399920bc6c90ff41f0a0c3a1ca3225e518fd0d259e0aeb71732d`.
It is a proposed compiler dependency, not a compiler merged into this base.
Its existing reconstruction is documented in the native host/compiler lane.

Compile `classify.om` by feeding its source to that compiler and saving stdout
to a new executable file. Check compilation succeeds before making it executable.
The program takes three arguments: observed child exit code, log path, receipt
path. Run it through `quiet-build` for host qualification. The existing GPU
script requires `PREFETCH_CLASSIFIER` to name that executable before doing work.
The script copies the executable into the new evidence directory and verifies
its digest against `classify.sha256`, then verifies `classify.om` against
`source.sha256`, before any device work. Those committed digests belong to the
qualified compiler profile; update them only after requalification.
GPU execution still requires the established reservation and lock.

| Classifier exit | Output | Meaning |
| --- | --- | --- |
| 0 | `PREFETCH_CONTROL: CAUGHT` | Completed control, expected named numerical failure |
| 1 | `PREFETCH_CONTROL: UNCAUGHT` | Completed control passed; experiment fails |
| 65 | `PREFETCH_CONTROL: INVALID` | Evidence cannot qualify the control |

Other exits, signals and classifier timeouts are also invalid in the script.
The script bounds each graph/control/command-list run at 600 seconds and
the classifier at 10 seconds,
with bounded termination grace periods. Missing positive logits hashes and failed binary-hash recording also prevent
a passing experiment.

## Evidence contract

Both input files are bounded to 64 KiB. The log must identify the prefetch
control, contain all 13 named checks in source order, and end with its matching
summary. The control is CAUGHT only when the argmax or logits comparison fails
(slot 4 or 5). The unordered o_proj race also makes graph output differ between
runs, so C determinism, B versus C, post-mutation C and timeline (slots 3, 7, 10,
11) may fail with it; these alone never qualify. Setup, A, B execution, both
mutation controls and timeline off must pass. Child exit, counted failures and
summary must agree. Three GB10 red runs (2026-10-14, 2026-10-15, 2026-10-31,
MEASURED) all fail exactly slots 3, 4, 5, 7, 10 and 11; the first classifier
version accepted only slots 4 and 5 and refused all three as INVALID.

The receipt must be complete JSON in the consumer's emitted ASCII subset:
objects, arrays, strings without escapes, and finite decimal numbers. Depth is
bounded to 32. Required top-level fields must occur exactly once, have the
expected kinds, and carry matching check counts, failure count, verdict and
invocation settings. The logits hash has exactly 16 lowercase hexadecimal
characters. Nested comparison flags must match the named argmax result; both
other mutation controls must report successful execution and caught=true,
and the batching comparison must report identical=false exactly when slot 7 failed. Unexpected, incomplete, truncated or unsupported output refuses
qualification. This is not a general JSON library or authentication of supplied
logs. It consumes trusted child output saved by the existing experiment; it
does not prove that adversarially fabricated numerical data is genuine.

## Scope

Host classifier verification does not measure prefetch benefit, establish
CPU/GPU visibility or qualify the actual dependency mutation on hardware.
The full-token experiment, ownership reconciliation for a new prefetch candidate,
and hardware correctness/performance requirements of #640 remain open.

## Corrected compiler compatibility

The compiler safety correction in Omega PR676 also reproduces this component
without changing its executable bytes. See [evidence/prefetch-640/compiler-correction](../../evidence/prefetch-640/compiler-correction/README.md) for the exact compiler/source pins, binary comparisons and fresh host replays. The original qualification remains preserved.

## Automatic host verification

`OMEGA-PREFETCH-NATIVE` now selects this component's actual native tests.
Its entry is the existing experiment script with `--host-qualification`.
This mode never enters the GPU experiment. It checks both compiler source and
executable against `compiler.sha256`, creates private scratch, builds `gate.om`,
and runs it. The gate itself builds the classifier and the two independent
fixture generators, executes all53 cases, and compares exact exits and verdict
bytes. Each child has a30-second deadline. Scratch and child outputs remain
available for inspection; the shell creates a separate directory on each run.

The required compiler is main's seeded `seed/out/omc`, executable
`62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`, built from
`seed/omc/omc.om` `96d51b63c8f451661ac715395078e04f39195e50c9e8fe37746cda121e4d7a1e`
(both pinned in `compiler.sha256`). The original qualification used the corrected
mapping profile from Omega PR676 commit `1dffceaeb0dd26fb69ea64d367a49a9950c25a62`,
executable 9f48f66a; the pin moved to main's compiler when this branch merged main,
and the gate was rerun with it. The entry refuses with 3 for any other compiler. It
does not fetch code, accept an arbitrary environment compiler, silently select an
older profile, or waive missing prerequisites.

Native process supervision reuses the existing gate669 pattern from that
compiler revision. `gate-child.om` is the existing supervisor control fixture.
Direct `gate.om` invocation takes the absolute compiler path and optionally
control0/1/2 for wrong exit, self-signal and deadline overrun. Each requires a
fresh `build/prefetch-native-gate` directory; repeats refuse91. Controlled
failure exits99 and never emits the success observation. This is trusted host
qualification, not a security sandbox or process-tree isolation mechanism.

Independent replay passes53 cases and catches all six tested refusal/control
scenarios. The existing runner passes twice with separate scratch in the exact
combined candidate. Evidence is in `evidence/prefetch-640/native-gate/`.
The verification planner must inspect the committed manifest; dirty-file
preview alone does not load a new manifest. Operator review of pipeline changes
and all pre-existing gates remain required. No allow-list exemption was added.
