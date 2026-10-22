# Commands and evidence boundaries

Run from the repository root at source commit `0daf1a44e3e8bf1c82dc2d0a39131021dd5f8c0a` on AArch64 Linux. The evidence-only child commit does not change executable inputs. Source, tools and binary digests are separate files; source cannot name its own evidence commit.

```sh
timeout 180 quiet-build sh research/locality_l491/run.sh
# Actual RC0: suite.log, suite.exit. 763/0 oracle, 483/0 training,
# 80 sweep requests, 10 killed mutants, source layout and OEV1 refusal controls.

timeout 90 quiet-build env L491_OUT=research/locality_l491/out/final-asan L491_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' sh research/locality_l491/training.sh
# Actual RC0, 483/0: asan.log, asan.exit.

timeout 90 quiet-build make -j2 PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 OUT_DIR=research/locality_l491/out/regression test-autodiff test-train
# Actual RC0: regression.log. 262/0 autodiff and 266/0 train, each plain
# and ASan/UBSan. No device is opened by these CPU targets.

timeout 90 quiet-build env L491_TEST_CFLAGS=-DL491_MUTATE_RNG sh research/locality_l491/training.sh
# Expected actual RC1: rng-mutant.log, 402 pass/81 fail.
# This flag changes the test's RNG replay, not a production source or gate.

out=research/locality_l491/out
"$out/seed-src/seed/out/omc" < "$out/red-scaffold.om" > "$out/red-scaffold"
chmod +x "$out/red-scaffold"
timeout 30 quiet-build "$out/oracle" "$out/red-scaffold"
# Expected actual RC1: red.log, red.exit, red/red-scaffold.om.
# Latest oracle: 86 pass/274 fail; missing behavior is detected.
```

The original RED before implementation was 5 pass/180 fail against the then-smaller oracle; final red replay above uses the committed expanded oracle so its source and counters are reproducible. Early compiler/harness failures are preserved in the L491 handoff logs but are not counted as RED qualification evidence. The final complete bad-RNG run is the qualifying RNG negative control.

The baseline tables come from `baseline.sh`/`baseline.awk` and seven SHA256-locked source inputs at base `a8ffd2b9`. The independent layout oracle compiles the pinned allocator expressions; it does not call a GPU API. Weight payload is source-shape arithmetic only; `baseline/summary.txt` explicitly excludes scales/padding/inactive experts and actual DRAM traffic. OEV1 is evaluated by unchanged `research/cost-sim-583@3bd56731`; its REFUSED_UNKNOWN and lack of makespan/ranking are the expected behavior, not a timing result. `sweep-raw.txt` gives every actual request/result; `sweep.tsv` carries input/output hashes and analytical counters. All physical traffic, chip timings, actual residency, spill/occupancy, energy and model-quality rows remain NOT_RUN/UNKNOWN.

Optional reuse attempt, NOT a passing gate: export L137 `b260b3a4873f56ef4e3f8ea850fef0c19fdca21c` Makefile/mk/src/tests/lockfiles into `out/reuse-l137` and attempt its gpu_fp8_api_test target with PHYSICS_DIR=/nonexistent. Actual RC2: `reused-failures/reuse-l137-build.log`, missing quietlock source and m16_native.h. The smaller original-checks entry `reused-failures/fusion_replay.c` was compiled with cc `-std=gnu11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -ffp-contract=off -ffunction-sections -fdata-sections -Wl,--gc-sections`, include dirs for the export root/src/tests, and the unchanged native sources `omega_blackwell_codegen_moe.c`, `omega_blackwell_codegen.c`, `omega_blackwell_encoder.c`, `omega_gpu_fp8_pack.c`, `omega_blackwell_matmul.c`, `sha256.c`, plus `-lm`. Actual RC1: `reuse-minimal-build.log`, original snprintf format-truncation warning. No binary was produced; native fusion replay is BUILD_FAIL/NOT_RUN. No flags, checks, tolerances or sibling sources were weakened to make it pass.

Independent approval remains pending. These are bounded host/synthetic algorithms and actual CPU-API integration tests. They do not qualify a GPU model, a fine-tuning method, or a production default. For full per-issue dependencies see README.md, REVIEW.md and FROZEN-HARDWARE.md two directories above.
