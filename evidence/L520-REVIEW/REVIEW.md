# L520 independent review of L511

Reviewed source: `85d8343fa04647b198e3ddda00ad8e448837f9ca` only. Review branch: `issues/m1-lifetime-review-20261022`. Date: 2026-10-22. Independent host execution on Spark spark-b87b, Linux aarch64 7.0.0-1019-nvidia, GCC 13.3.0, jq 1.7. No implementation changes, author evidence edits, extra agents, model loads or hardware runs.

## Milestone 1: #576 receipt exporter: HOLD, host acceptance reproduced

### C1, P2: failing JSON records bypass refusal when followed by a valid record

`bench/m1/block3_receipt_csv.sh:7-21` validates with streaming `jq -e`, whose exit status reflects the last result. Export at lines 29-37 processes every input object. Concatenate a receipt with `.failures=1`, `.candidate_bound=false` and zero successful reuses, followed by the genuine valid receipt. The command exits 0 and emits four data rows, including the failing/unbound measurements. Reproduction: `L520/probes/csv.sh OUTPUT_DIR`, retained `csv/stream.json`, `stream.csv` and csv.log. Actual result: exit 0, five CSV lines. Impact: the advertised refusal boundary can be bypassed by an ordinary concatenated JSON input; consumers cannot infer all exported rows passed validation.

Recommended bounded fix: parse one immutable input snapshot, require exactly one top-level object and apply validation and export to that same value. Add invalid-first/valid-last, reversed-order and multiple-valid-record negatives. Preserve the pre-registered FAIL as an allowed measurement verdict, separate from receipt-integrity refusal.

### C2, P2: machine metadata is emitted as an executable spreadsheet formula

`bench/m1/block3_receipt_csv.sh:36` passes unchecked `.host.machine` to `@csv`. Setting it to `=1+1` is accepted and emits the quoted CSV cell `"=1+1"`. CSV quoting escapes separators but does not neutralize formulas in spreadsheet applications. Repro: csv.sh formula probe, retained formula.json/formula.csv, exit 0. No spreadsheet application was launched. Impact is conditional on opening untrusted receipts in a formula-evaluating spreadsheet; the genuine uname-derived `aarch64` value is safe.

Recommended bounded fix: validate the machine identifier as required scalar metadata using a restricted identifier grammar appropriate to uname machine values, or explicitly neutralize formula prefixes for human-facing CSV. Add `=`, `+`, `-`, `@`, tab/newline and quote/comma boundary probes without treating syntactic CSV escaping as formula protection.

### C3, P2: numeric checks accept non-measurements and overflowed timing values

`bench/m1/block3_receipt_csv.sh:10,17-20` compares `.checks > 0` without a numeric type check, so `.checks="invalid"` passes under jq type ordering. A timing value `1e999` passes the numeric-positive predicate and is exported as `1E+999`; fractional 0.5 ns also passes although producer medians are integer nanoseconds. Repro: csv.sh checks-string/overflow/fractional, all exit 0. Impact: malformed receipts yield accepted coefficients, including a value that overflows ordinary downstream binary64 arithmetic.

Recommended bounded fix: require integral positive check counts and finite, bounded nonnegative/positive integer nanoseconds matching the producer's actual representation. Add overflow/exponent/fraction/type controls; choose a documented upper bound rather than relying on jq numeric coercion.

### Independent acceptance and provenance

Fresh `quiet-build make OUT_DIR=build/L520 AIENOS_LOCK_REPO=/home/drakestapleton/workspace/aienos-m5-qual build/L520/rx_plan_reuse_test test-gpu-regalloc-lifetime` exited 0. Fresh run at the exact clean reviewed head, with `OMEGA_CANDIDATE_COMMIT=85d8343fa04647b198e3ddda00ad8e448837f9ca` and `AIENOS_COMMIT=b84c0a67590a934f3f3e001b12ec85ebc086a9eb`, exited 0 with 1304 checks and zero failures. It independently reproduced **OMEGA_PLAN_REUSE_PASS: FAIL**, G1 FAIL and G2-G6 PASS. Task wording MET is a distinct narrower result. No skip, gate weakening or retry-until-green.

The rebuilt plan binary SHA-256 is identical to the author's `aa5467a895dbbe5107e2f4252139ddbe88d72391c95a225f41ef2a6869f8469b`. Relevant plan source, test, evidence writer and aienos.lock have no diff from the author's measured source 7e0257ab to the reviewed head. Genuine stored receipt digest matches block3.csv and exporter regeneration is byte-identical. The producer's candidate binding uses current working-tree Git identity plus tree cleanliness; the exporter checks self-reported equality, not external provenance. A fabricated pair of equal commit strings and binary hash is accepted. Treat that as a trust limit of this offline converter, not independent source attestation. The independent rebuild and matching binaries supply the missing corroboration for this particular author run.

Original six refusal mutants passed. Additional negative CPU, numeric-string timing, failures and unbound controls refused. Missing machine metadata was accepted as an empty cell. C and C.utf8 exports matched; paths with spaces worked. Passing a directory as OUTPUT.csv succeeded and created a randomly named temporary-file basename inside it (P3 path-contract observation, not a blocker). Retained bytes and isolated lookup remain UNKNOWN, wall joules UNMEASURED. G1 FAIL remains open regardless of successful CSV export or zero test failures. Shared-machine timings are observations, not calibrated coefficients.

## Milestone 2: #574 schedule benchmark: HOST PARTIAL, timing qualification HOLD

No source defect found within the reviewed host scope. Fresh standalone build with the author's exact command and own `build/L520/block1_sched` output exited 0. Its SHA-256 equals the author's `4f2f701e06ef902d13788a8fe5f3ca42d464cb61d0b3259685196f2965338805`. `quiet-build build/L520/block1_sched 2000 2000000` exited 0; independent CSV parsing checked 20 rows, ten per schedule, positive times, exact iteration counts and every differential verdict `ok`.

Schedule mapping was checked against `src/omega_program.h:103-104`: SEQUENTIAL=0 and PRELOAD=1, matching the harness cast and labels at `bench/m1/block1_sched.c:64-66`. `src/omega_program.c:326-362` emits each constant immediately before its operation for SEQUENTIAL and bounded batches of constants before dependent operations for PRELOAD. Width masking preserves modular semantics. The rule is still the issue's issue-width/GPR condition at `src/omega_realize_synth.c:169-173`; it picks PRELOAD for all ten tested programs. There is no allocator-policy or schedule default change.

Differential checking at benchmark lines 67-72 uses the production evaluator and native executor, with deterministic fixed boundaries and program-seeded inputs from `omega_realize_differential_inputs`. It covers the ten listed arithmetic/bitwise/composed/32-bit/long programs, not arbitrary program semantics. Timed mapped calls use the same emitted bytes but a dependent recurrence; this measures that workload's call/chain latency, not universal instruction throughput. The separate direct mapping is not independently compared across every timed recurrent input. These are limits, not observed miscompilation.

`L520/probes/schedule.sh OUTPUT_DIR` compiles a separate fault-injection executable with linker wrappers, leaving reviewed sources unchanged. Evaluator error, wrong evaluator result, synthesis error, emission error, native execution error, empty differential input set, mmap failure and mprotect failure each exited 1. Seven argument refusals (zero path/code counts, junk, extra argument, integer overflow, excessive count, negative count) each exited 2. The mmap/mprotect controls can fail in the earlier native verification path; they do not separately establish coverage of the later direct-map error branches. Source inspection confirms those direct-map failures return nonzero. No skip or gate relaxation.

The ten schedule pairs execute in fixed order, with one aggregate observation per arm and no confidence interval or random interleaving. Both the author's and review run were under shared `quiet-build`, not an exclusive quiet measurement. The issue's idle baseline, shared chip-lock measurement protocol, stable repeated comparison and wall-energy qualification were not run by this reviewer. No new claim of a reliably faster schedule is supported. The old historical mmap cost result is source-specific; inspection of this exact `src/omega_exec.c:8-39` confirms this reviewed path still maps/protects/unmaps per call. Review raw timing CSV is retained solely as shared-machine evidence.

Build command (exit 0): `quiet-build cc -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -o build/L520/block1_sched bench/m1/block1_sched.c src/sha256.c src/omega_canonical.c src/omega_validate.c src/omega_core.c src/omega_codec.c src/aarch64_encoder.c src/aarch64_decoder.c src/omega_realize.c src/omega_realize_synth.c src/omega_program.c src/omega_machine.c src/omega_exec.c src/omega_verify.c -lm`. Baseline and wrapper commands/exits/raw results are in L520/block1*, schedule.log, schedule/ and probes/schedule*. Workload source, binary/tool/machine pins and completion timestamp are retained in environment.txt and binaries.sha256. Exact reviewed source head is the report's opening pin; review commits add evidence only.


## Milestone 3: #520 allocator lifetime oracle

Pending final review record.

## Ownership and scope

Full #576/#574/#520 issue bodies and discussions, all current issue/PR snapshot records, L511 handoff/gates and evidence read. Snapshot 19:20:36Z was used while live reads were rate-guarded; no guard bypass. Read-only session database plus git worktree reconciliation retained in L520/sessions.txt and worktrees.txt; L519 and existing review/fix/integration owners kept isolated. No agent restart/resume/reassignment. Exact L511 remote ref independently matched 85d8343f. No remote PR/issue writes, merges, shared remote changes or deletion.
