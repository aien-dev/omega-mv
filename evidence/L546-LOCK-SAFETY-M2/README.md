# L546 final host regression receipt

Tested code head f7f8eead0d8686e6c03e368600b7b113cc71aa29. The production helper
and tests are identical to the source pinned in the immutable M1 packet. This
packet adds finishing regression evidence and workload/binary/tool pins only.

Actual commands from the assigned worktree, each under quiet-build:

```
quiet-build node tests/physics_header_identity_controls.mjs build/L546/history
# exit0, all9 historical controls PASS, unsafe PR495 concurrency FAIL retained.
quiet-build sh tests/physics_build_identity_test.sh Makefile build/L546/direct dirty
# exit0, dirty driver recipe invalidation, no-op and missing-path refusal pass.
quiet-build make --no-print-directory -j2 OUT_DIR=build/L546/host PHYSICS_DIR=/nonexistent test-gpu-wait test-language
# exit0, OMEGA_GPU_WAIT_HOST PASS, language407/407 PASS.
quiet-build bash tests/gates/run.sh evidence-immutable
# exit0, origin/main a8ffd2b9, zero rewritten evidence files.
quiet-build bash tests/gates/run.sh zero-disk-secrets
# exit0, no plaintext .env files.
sh -n tools/with_build_lock.sh
node --check tests/build_lock_file_safety_test.mjs
node --check tests/build_lock_file_safety_controls.mjs
# each exit0.
```

M1 new safety controls and original8 acceptance all pass. New controls require
actual named FAIL assertions with underlying exit0, no signal/error. Original
acceptance still requires its exact original failure names/exits. No acceptance
test or historical packet was edited. No timeout/crash/unrelated failure counted
as refused lock or successful control. No gate was skipped or weakened.

The source finding was already independently minimized by the parent. The private
RED here reproduced both exact SHA256 changes before the fix. Ranked explanations
were truncating open, missing link/type refusal, then an unrelated command effect.
Direct helper tests with a trivial marker command isolate the first two causes;
append-only mutant preserves bytes but still violates refusal/command boundary.
A regular truncation mutant independently catches lock-file byte loss. No debug
instrumentation was added to production.

Self-review: checked no unlink or global exclusion, same canonical lock name/fd8,
all pre-existing invalid leaf types refused before opening, append mode without
writes, post-open name/fd identity and link-count checks, flock/exec unchanged.
Existing Linux host dependencies are used. Caller-owned namespace is required;
hostile concurrent path replacement is outside the scoped contract. No security
attestation is made. Parent independent corrective-head review remains NOT_RUN.

fixture-binary-tool-pins.json records SHA256 of actual generated inputs, tools,
objects and executables, including real lifecycle binaries and host outputs.
Raw workload sources, dependency files and logs are included for replay; arbitrary
binary files are pinned rather than committed. M1 environment.txt records Linux,
versions and shared-box load. Its x86-specific optional GCC path did not exist on
this aarch64 machine; this packet records the actual resolved cc executable.
Its sh --version informational probe reports dash's unsupported option; shell
identity and hash are pinned here. These are environment queries, not test gates.
No performance or energy measurement is claimed.

Integration dependencies: retain L532 safe test base52dbe78b, L537 sourceab070f60
and strict gate5d819b58 through author tipd84142f7, including the PR495-derived
stamp/dependency delta. This head supersedes the unsafe L537 helper only. Original
PR495 production proposal still has its recorded concurrency failure. No original
Makefile snapshot replacement or ownership change occurred. Completed L537 and
stopped L532 were not resumed; separate L541/decoder/parent600 work was untouched.

Desk disposition: READY HOST pending personal parent re-review; L537 HOLD remains
until that review. Independent review NOT_RUN. Full production-driver/chip/current
canonical artifact/current35B/shared train qualification NOT_RUN, with existing
canonical owner/L386. L487/L488 retain intake, integration and closure. No remote
PR/issue writes, merge, force push, service/driver/model changes or extra agents.
