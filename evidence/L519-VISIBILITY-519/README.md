# L519 #519 additive session visibility contract

HOST PARTIAL. Full graph visibility/failure recovery, native correctness and
interleaved A/B/C timing remain NOT_RUN. No production code or range-sync API
changed. Counted bytes are structural observations, not measured transfer cost.

Source baseline includes L511 85d8343f and L519 #522 0f45d8b4. The new
`tests/gpu_session_visibility_oracle.c` reuses the existing session-platform test's
fake implementation by including it with its main renamed. It installs two
separate trace callbacks on that fake, drives the actual production
`omega_gpu_session_sync_to_gpu` and `_to_cpu`, and links the existing freestanding
session/codegen/platform implementation. It never installs the Linux backend,
loads a model, opens a real device or submits a hardware workload.

## Executed

- `quiet-build make OUT_DIR=build/L519 test-gpu-session-visibility`: exit0.
  Four requested allocation sizes (1, 4096, 4097, 16384 bytes), 28 exact traced
  events across their page-rounded actual allocations. Each event records
  direction, typed handle generation, byte offset, full length, order, caller
  lock depth and platform return code. `host.log` contains all traces and build
  commands. Independent device-shadow memory checks every byte, including the
  last byte, across CPU/device producer-consumer transitions.
- `quiet-build make OUT_DIR=build/L519 test-gpu-session-visibility-mutants`:
  exit0, 4/4 mutants compiled and produced actual failed assertions. Omitting a
  sync, shortening the length by one byte, reversing direction, or swallowing
  an injected platform failure all fail this oracle. Raw negative outputs are
  copied alongside this README. The original production sources are untouched.
- Injected failures publish nothing to the device shadow/host respectively;
  subsequent successful sync recovers without latching. Stale handles are
  refused. Close refuses further dispatch without dereferencing stale memory.
  Null/empty buffers preserve the existing session no-op semantics.

## Scope and ownership reconciliation

#518 remains existing PR588 `omega-518@f53511177def30e64f781320c9d9090906dd6642`.
Its exact source implements failed-begin cleanup/abort and existing fake tests
cover that ownership contract. This work neither copies nor changes those
unmerged production sources. The canonical motor branch inspected at
`1ad2dc1af70f01680fd93cb41a7a94fd0951723f` does not have legacy
`src/omega_gpu_decode_graph.c`; do not infer that PR588's fix was integrated
from a canonical branch's existence. No owner process/session was changed.

## Remaining graph region contract and gate packet

Current legacy graph source defines one scratch allocation. Producers/consumers:

| Region | Producer | Consumer / required crossing |
|---|---|---|
| x | CPU embedding at begin, GPU residual updates | GPU initial norm: CPU->GPU at begin even if embedding=NULL (token poison still written); optional CPU read: GPU->CPU |
| token (and poison words) | CPU poison at every begin, GPU argmax output | GPU must see poison; end CPU token read requires GPU->CPU before validation |
| norms, final norm, tensor tables | CPU layer/final-norm setters | GPU layer/norm kernels: successful setter requires CPU->GPU |
| hidden, logits | GPU combine/norm and output head | optional CPU readback requires GPU->CPU before copying |
| xq, qkv, attn, o, gu, act, dn, argmax temporaries | GPU kernels | GPU-to-GPU dependencies retain current barriers/order |
| prefetch x/y | allocation initialization, prefetch kernel | existing prefetch dependency ordering; do not skip visibility without native proof |
| code buffers | CPU encoding | GPU instruction fetch after CPU->GPU code sync |

Graph setters/begin/end/read currently call full-buffer session sync. This test
proves forwarding semantics at that session boundary only. After PR588 is
integrated by desk, the graph owner needs an additive hostile fake trace across
set_layer, set_final_norm, begin(NULL/non-NULL), end, hidden/logit read, prefetch
on/off, varying layers/vocabulary, repeated tokens, close and failed-sync
recovery. Reuse the existing abort fake, with proper graph ownership coordination;
do not treat these rows as executed graph tests. Preserve whole-buffer fallback.

Shared canonical hardware owner then runs pinned whole-buffer host/device timing
and full-token interleaved A/B/C with median/p95, native timestamps, independent
correctness/parity and quiet protocol receipts. Exact source, Physics, binary,
toolchain, machine/driver and workload pins required. No range optimization or
benefit claim before independent verification. Independent review NOT_RUN.
