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
summary. Only the argmax and logits comparisons may fail. Setup, determinism,
execution, batching, other mutation checks, post-mutation recovery, and timeline
checks must pass. Child exit, counted failures and summary must agree.

The receipt must be complete JSON in the consumer's emitted ASCII subset:
objects, arrays, strings without escapes, and finite decimal numbers. Depth is
bounded to 32. Required top-level fields must occur exactly once, have the
expected kinds, and carry matching check counts, failure count, verdict and
invocation settings. The logits hash has exactly 16 lowercase hexadecimal
characters. Nested comparison flags must match the named argmax result; both
other mutation controls must report successful execution and caught=true,
and the batching comparison must report identical=true. Unexpected, incomplete, truncated or unsupported output refuses
qualification. This is not a general JSON library or authentication of supplied
logs. It consumes trusted child output saved by the existing experiment; it
does not prove that adversarially fabricated numerical data is genuine.

## Scope

Host classifier verification does not measure prefetch benefit, establish
CPU/GPU visibility or qualify the actual dependency mutation on hardware.
The full-token experiment, ownership reconciliation for a new prefetch candidate,
and hardware correctness/performance requirements of #640 remain open.
