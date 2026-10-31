# Native verification entry qualification

Production classifier is unchanged. The added native gate compiles it and
its existing independent fixtures, runs53 cases, and checks exact process
results and verdict text. Process helper provenance: existing Omega gate669
at1dffceaeb0dd26fb69ea64d367a49a9950c25a62. Original fixture sources are unchanged.

Independent review rebuilt the gate byte-identically (cd4782ba...), passed53,
and tested repeated scratch91, wrong exit99, self-signal99, actual30-second
child deadline99, wrong verdict text99 despite classifier exit0, and actual
native compilation failure99. Exact identities/results are in receipt.json
and independent-review.txt. No refusal/control emits the gate PASS marker.

Parent executed the actual host-qualification shell entry twice in a private
combined snapshot: current native/prefetch and runner plus only the committed
compiler source/binary from1dffceae. Both return0 and observation53. Entry
checks compiler hashes before building. Running it on the main-based branch
without the dependency returns3 before any test or device work. This refusal
is preserved, not waived. No owner's working tree was used as a compiler source.

Reproduce in the exact combined candidate:
quiet-build timeout120 bash tools/run_gpu_l2d_prefetch_chip.sh --host-qualification
Native gate compilation consumes stdin and emits ELF on stdout. Its native
coordinator can also be built independently and run with the absolute compiler
path, as described in independent-review.txt. Never execute compiler output
unless compilation succeeds. The original full classifier qualification and
compiled numerical guard controls remain in the parent evidence directory.

Current pipeline configuration changes require operator review. This packet
is neither a signed Spark verdict nor a claim that legacy/hardware gates ran.
GPU experiment and full Omega640 acceptance remain NOT_RUN/INCOMPLETE.
