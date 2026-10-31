#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# tests/gates/run.sh: the one entry point for the local verification gates of omega.
# Every step runs the SAME command as the hosted job it mirrors (.github/workflows/pr-smoke.yml,
# host-suites.yml, host-suites-2.yml, host-suites-3.yml, rx-host.yml, turing-yield.yml,
# evidence-immutable.yml, crumb.yml). Manifests under tests/gates/ name this script plus one
# step word, so no manifest needs shell expansion (ADR 0028 P6).
#
# Pinned sibling sources. The hosted jobs check out aien-spark/physics at physics.lock (rx-host
# at its own hard-coded commit), aien-dev/aienux at aienos.lock and aien-dev/aienux at
# argus.lock, as physics-src/, aienos-src/ and aienos-argus/ next to the Makefile, and exclude
# them from git status. Here the same commits are materialised under build/pins/ (build/ is
# gitignored) from the sibling local clones ../physics and ../aienos, offline.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
step="${1:?usage: tests/gates/run.sh <step>}"

# Every step builds in its own directory. The Spark judge (aien-test) runs a repository's host gates
# at the same moment in one worktree; with one shared build/ a gate links an object another gate's
# compiler is still writing (omega#511: OMEGA-M12 "undefined reference to omega_tern_*" with the
# object on the link line). OUT_DIR is read by the Makefile (?=) and by every make it spawns.
# The pins stay at build/pins, shared read-only under their own lock. The directory name keeps
# only [A-Za-z0-9_.-] from the step word: "spark:test-r13-host" would put a colon in a make
# target ("multiple target patterns").
export OUT_DIR="build/step-$(printf '%s' "$step" | tr -c 'A-Za-z0-9_.-' '-')"
mkdir -p "$OUT_DIR"
obs() { printf 'AIEN-OBS %s %s\n' "$1" "$2"; }

PINS="$ROOT/build/pins"
NOPHYS="PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0"
lock() { awk 'NF && $1 !~ /^#/ {print $1; exit}' "$1"; }

# pin <dir-name> <sibling repo> <sha40>: build/pins/<dir-name> is a detached checkout of <sha40>
# taken from the object store of ../<sibling repo>. Reused when already at that commit.
pin() {
  local name="$1" repo="$2" sha="$3" dir="$PINS/$1"
  if [ ! -d "../$repo/.git" ] && [ ! -f "../$repo/.git" ]; then
    echo "run.sh: ../$repo is missing; the hosted job checks out aien-dev/$repo at $sha" >&2
    exit 3
  fi
  mkdir -p "$PINS"
  (
    flock -x 9
    if [ ! -d "$dir" ] || [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" != "$sha" ]; then
      rm -rf "$dir"
      git clone --quiet --shared --no-checkout "../$repo" "$dir"
      git -C "$dir" checkout --quiet --detach "$sha"
    fi
  ) 9>"$PINS/.$name.lock"
  obs "pin_${name//-/_}" "\"$sha\""
}
physics_pin() { pin physics-src physics "$(lock physics.lock)"; export PHYSICS_DIR="$PINS/physics-src"; }
aienos_pin()  { pin aienos-src aienos "$(lock aienos.lock)"; export AIENOS_R7_DIR="$PINS/aienos-src"; export AIENOS_COMMIT="$(lock aienos.lock)"; }
argus_pin()   { pin aienos-argus aienos "$(lock argus.lock)"; export ARGUS_REPO="$PINS/aienos-argus"; }
export OMEGA_CANDIDATE_COMMIT="$(git rev-parse HEAD)"

# receipt <file name or glob> <grep pattern>...: the hosted "Show ... receipt" steps.
# Only receipts written during this step count: a hosted job starts from a fresh checkout, so it
# can only see its own run, while a reused local tree keeps build/qual-runs from earlier runs.
STEP_MARK="$(mktemp /tmp/omega-gate-step.XXXXXX)"
trap 'rm -f "$STEP_MARK"' EXIT
receipt() {
  local r; r="$(find build/qual-runs -name "$1" -type f -newer "$STEP_MARK" | head -n 1)"; shift
  test -n "$r"
  local p; for p in "$@"; do grep -q "$p" "$r"; done
  sha256sum "$r"
}
gates_pass() { grep -E 'TOTAL GATES: [0-9]+ \| PASSED: [0-9]+ \| FAILED: 0' "$1" && ! grep -E 'FAILED: [1-9]' "$1"; }

# speed <make target> <receipt file> <gate word>: a speed test (tests/runtime/rx_quiet_reps.h) that found
# the machine too busy to time, or a leg it claims faster disturbed significantly more often than the
# other leg, ends with `AIEN-OBS not_run_reason "cpu_busy"` (or "leg_skew") and exit 77; make reports
# any failing recipe as 2, so the refusal is read from the test's own receipt for this step: "failures": 0
# and the gate word NOT_RUN, which the test writes only when nothing it judged failed. Then the step goes
# on, so every other check is still judged, and ends NOT_RUN (exit 77) only if nothing else failed, with
# reason leg_skew when any refused receipt says so, else cpu_busy.
# Anything else is the failure it was. A passing run gets the receipt check it always had.
BUSY=0
BUSY_REASON=cpu_busy
speed() {
  local target="$1" file="$2" word="$3" rc=0 r f bad=1
  make "$target" || rc=$?
  if [ "$rc" -eq 0 ]; then
    receipt "$file" '"failures": 0,' '"candidate_bound": true,' "\"$word\": \"PASS\""
    return
  fi
  # Every receipt of that name written during this step must say so; any other one keeps the failure.
  r="$(find build/qual-runs -name "$file" -type f -newer "$STEP_MARK")"
  if [ -n "$r" ]; then
    bad=0
    while IFS= read -r f; do
      grep -q '"failures": 0,' "$f" && grep -q '"candidate_bound": true,' "$f" &&
        grep -q "\"$word\": \"NOT_RUN\"" "$f" || bad=1
      if grep -q '"not_run_reason": "leg_skew"' "$f"; then BUSY_REASON=leg_skew; fi
    done <<< "$r"
  fi
  if [ "$bad" = 0 ]; then
    echo "run.sh: $target: refused to time ($BUSY_REASON; the test's NOT RUN lines say why); not judged, the rest of the step runs" >&2
    printf '%s\n' "$r" | xargs sha256sum
    BUSY=1
    return 0
  fi
  return "$rc"
}

case "$step" in
  crumb-verify)
    # Hosted crumb.yml builds the crumb tool from the aien-architecture commit in crumb.lock.
    # Same here, from the sibling checkout ../aien-architecture into a temp dir outside the
    # tree, so a newer crumb on PATH can never change the verdict.
    if [ ! -d ../aien-architecture ]; then
      echo "run.sh: ../aien-architecture is missing; the hosted job checks out aien-dev/aien-architecture at crumb.lock" >&2
      exit 3
    fi
    tool="$(mktemp -d /tmp/omega-crumb-tool.XXXXXX)"
    trap 'rm -rf "$tool" "$STEP_MARK"' EXIT
    git -C ../aien-architecture archive "$(lock crumb.lock)" tools/crumb | tar -x -C "$tool"
    make -s -C "$tool/tools/crumb" crumb
    obs crumb_tool_commit "\"$(lock crumb.lock)\""
    "$tool/tools/crumb/crumb" verify .
    ;;
  # pr-smoke.yml
  base)
    physics_pin; set -o pipefail
    make test | tee build/base.log
    grep -E 'TOTAL GATES: ([0-9]+) \| PASSED: \1 \| FAILED: 0' build/base.log
    ! grep -qE 'FAILED: [1-9]|\[FAIL\]|\(FAIL\)' build/base.log
    ;;
  language)           # pr-smoke.yml and host-suites-3.yml (same command)
    set -o pipefail; mkdir -p build
    make $NOPHYS test-language | tee build/lang.log
    grep -E '^PASS ([0-9]+)/\1$' build/lang.log
    ! grep -q '^FAIL' build/lang.log
    ;;
  numeric-transc)     # pr-smoke.yml and host-suites-3.yml (same command and checks)
    set -o pipefail; mkdir -p build
    make $NOPHYS test-numeric-transc | tee build/transc.log
    test "$(grep -cE '^mutant [0-9]+ killed' build/transc.log)" -eq 10
    grep -qE '^test-numeric-transc: PASS \(all 10 mutants killed\)' build/transc.log
    ! grep -q '^FAIL' build/transc.log
    obs mutants_killed 10
    ;;
  # host-suites.yml
  searchtrace)        make $NOPHYS test-searchtrace ;;
  estimation)         for t in test-estimation test-est-tools test-estimation-v3 test-est3c-tools; do make $NOPHYS $t; done ;;
  ans)                make $NOPHYS test-inertial-alignment ;;
  numeric-cpu)        for t in test-numeric-cpu test-numeric-qualify test-e1-combine test-chipwait-campaign test-gpu-wait test-gpu-wait-mutants test-gpu-code-alloc test-mufu-ftz; do make $NOPHYS $t; done ;;
  train)              make $NOPHYS test-train ;;
  compiler)           make $NOPHYS test-compiler-quick; make $NOPHYS test-compiler-full ;;
  one-layer-acceptance) make $NOPHYS test-one-layer-acceptance ;;
  physics0)           make $NOPHYS physics0-test ;;
  chip-tool-selftests) for t in test-m19r-qualify test-gate14-combine test-window-lane-guard test-unwritten-trap-host test-ldst-host test-dgraph-receipt; do make $NOPHYS $t; done ;;
  # seed compiler (no hosted mirror; Spark only): rebuild from the loader, full qualification, IR oracles required
  seed)               make OUT_DIR=build build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2
                      sh seed/omega-from-seed.sh
                      sh seed/host/check.sh 2>&1 | tee "$OUT_DIR.seed-check.log"
                      if grep -q "^SKIP" "$OUT_DIR.seed-check.log"; then echo "OMEGA-SEED: a seed gate was skipped" >&2; exit 1; fi
                      # gate5 checks that seed/omc/size-history.tsv ends with a row for this omc; check.sh does not run it
                      sh seed/omc/gate5.sh 2>&1 | tee "$OUT_DIR.seed-gate5.log"
                      tail -1 "$OUT_DIR.seed-gate5.log" | grep -q "^GATE5 PASS" || { echo "OMEGA-SEED: gate5 failed" >&2; exit 1; } ;;
  # omega-mv#731 ORIG-4 (no hosted mirror; host only): the shadow advisor's glue over the
  # pinned aien-train units (native/orig4/orig4.pin, one aien-train main commit), materialised
  # under build/pins from the sibling clone; every verdict is made by the .om units.
  orig4-shadow)
    train_sha="$(awk '$1 == "unit" && $3 == "aien-train" {print $4; exit}' native/orig4/orig4.pin)"
    pin aien-train-src aien-train "$train_sha"
    set -o pipefail
    OUT="$ROOT/$OUT_DIR/orig4" ORIG4_TRAIN_GIT="$PINS/aien-train-src" \
      sh tests/orig4/run.sh | tee "$OUT_DIR.orig4.log"
    grep -q '^ORIG4_SHADOW_RUN_PASS$' "$OUT_DIR.orig4.log"
    obs shadow_test_checks "$(sed -n 's/.*ORIG4_SHADOW_TEST_PASS checks=\([0-9]*\).*/\1/p' "$OUT_DIR.orig4.log" | head -n 1)"
    obs cost_wall_ms_mlp "$(sed -n 's/^COST_WALL records=780 ms=\([0-9]*\) image=mlp fixture$/\1/p' "$OUT_DIR.orig4.log" | head -n 1)"
    ;;
  # host-suites-2.yml
  visor)              aienos_pin; make $NOPHYS test-visor ;;
  turing)             make $NOPHYS test-turing; make $NOPHYS test-turing-exp001-a test-turing-exp001-coders; make $NOPHYS test-turing-verify-indep; make $NOPHYS test-turing-qcont test-turing-qrecord ;;
  brownian)           make $NOPHYS test-brownian-tps; make $NOPHYS test-brownian-active ;;
  polyglot)           make $NOPHYS test-polyglot-asm; make $NOPHYS test-polyglot-encoder ;;
  algebra)            make $NOPHYS test-algebra test-algebra-asan; make $NOPHYS test-realize; make $NOPHYS test-phase-twin; make $NOPHYS test-ma-digital-bottom ;;
  path)               make $NOPHYS test-path ;;
  crumbline)          make $NOPHYS test-crumbline test-library test-vcstore test-resolve test-vcstore-genesis test-resolve-genesis test-genesis test-genesis-real test-program-ir test-vc-bridge ;;
  runtime)
    physics_pin; aienos_pin
    # host-suites-2.yml sets only PHYSICS_DIR and AIENOS_R7_DIR, and only on the steps that list
    # them; the qualify self-test and the aienos.lock source proof run with neither (the latter
    # checks the default cache, which an inherited AIENOS_R7_DIR override would bypass).
    unset AIENOS_COMMIT OMEGA_CANDIDATE_COMMIT
    make test-r15-instr
    make test-r15-receipt
    make test-r15-preflight test-r15-g15-diag
    make test-r16-inventory
    make test-r16-negative
    make test-r16-surface
    make test-r16-negative-mutants
    env -u PHYSICS_DIR -u AIENOS_R7_DIR make test-r16-qualify-selftest
    env -u PHYSICS_DIR -u AIENOS_R7_DIR make test-aienos-lock-source
    make test-allen
    ;;
  argus)
    physics_pin; aienos_pin; argus_pin
    ARGUS_STREAMS="$(mktemp -d /tmp/argus-streams.XXXXXX)"; export ARGUS_STREAMS
    make test-argus-runtime test-machine-identity-argus
    ;;
  bw-hazard)          physics_pin; make test-bw-hazard ;;
  numeric2)           make $NOPHYS test-numeric-reduce-cpu test-divsqrt-host ;;
  tensor)             make $NOPHYS test-tensor; make $NOPHYS test-tensor-store; make $NOPHYS test-m20-receipt; make $NOPHYS test-tensor-mutations ;;
  gpu-decode-graph)   physics_pin; make test-gpu-decode-graph ;;
  # host-suites-3.yml
  numeric-lifecycle)  make $NOPHYS test-numeric-lifecycle ;;
  numeric-transc-digest)
    set -o pipefail; mkdir -p build
    make $NOPHYS test-numeric-transc-digest | tee build/digest.log
    n=$(grep -c 'matches frozen digest' build/digest.log); obs frozen_digests_matched "$n"
    test "$n" -ge 1
    ;;
  gpu-attention)      # host-suites-3.yml gpu-attention (omega#470): physics-src at physics.lock, lock check on;
                      # host-only, sim and sweep tiers, nothing touches a GPU (the Spark has nvdisasm, so the
                      # listing check runs here where the hosted runner printed "skipped")
    physics_pin; set -o pipefail; mkdir -p build
    make -j"$(nproc)" test-gpu-attention | tee build/attn.log
    test "$(grep -cE '^PASS: [0-9]+ checks, 0 failed$' build/attn.log)" -eq 6
    ! grep -qE '^FAIL' build/attn.log
    ;;
  sm121dis)           # L386 bar C6 (docs/OMEGA.md): Omega's own sm_121 disassembler. Host only. Every kernel the
                      # library builds, decoded by build/sm121dis and compared word for word with nvdisasm (the
                      # offline oracle the Spark has), plus the faulty-decoder red check; then ZERO-NVDISASM: the
                      # listing and provenance checks under a decoy nvdisasm and strace, plus its planted-call red check.
    physics_pin; set -o pipefail; mkdir -p build
    make -j"$(nproc)" test-sm121dis | tee build/sm121dis.log
    grep -qE '^kernels: [0-9]+, words: [0-9]+, failed kernels: 0$' build/sm121dis.log
    grep -qE '^red check: the faulty decoder FAILS the comparison' build/sm121dis.log
    make test-zero-nvdisasm | tee build/zero-nvdisasm.log
    grep -qE '^ZERO-NVDISASM: PASS' build/zero-nvdisasm.log
    grep -qE '^red check: the planted nvdisasm calls FAIL the gate' build/zero-nvdisasm.log
    ;;
  gpu-libm-free)      # omega-mv#471: no libm symbol left undefined in libomega_gpu.a (physics-src at physics.lock)
    physics_pin; set -o pipefail; mkdir -p build
    make -j"$(nproc)" test-gpu-libm-free | tee build/libm-free.log
    grep -q '^PASS: libomega_gpu.a is libm-free$' build/libm-free.log
    ;;
  numeric-transc-full) make $NOPHYS test-numeric-transc-full ;;
  m6|m7|m8|m9|m10|m11|m12|m13|m14|m15|m17|m5|m19|m19r-qualify|gate14-combine)
    physics_pin; set -o pipefail; mkdir -p build
    make "test-$step" | tee "build/gate-$step.log"
    gates_pass "build/gate-$step.log"
    ;;
  # rx-host.yml
  rx-host-reference)
    pin physics-src-6d7cf0d physics 6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf
    export PHYSICS_COMMIT=6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf PHYSICS_DIR="$PINS/physics-src-6d7cf0d"
    aienos_pin; set -o pipefail; mkdir -p build
    make test-r3 test-r7 test-r16-operator-authority-misuse test-r8 test-r9 test-r12 test-rx-emergency test-action-graph test-state-projection test-capability-query test-capability-graph test-skillroute-compose test-semantic-comm test-cognitive-routing test-sem-incremental test-cortex test-jspace-prod test-composition test-composition-attach-asan test-composition-gate test-fabric test-fabric-living | tee build/rx-host-reference.log
    make test-machine-identity | tee -a build/rx-host-reference.log
    make test-effect-cap64 | tee build/effect-cap64.log
    grep -q '^OMEGA_EFFECT_CAP64_PASS$' build/effect-cap64.log
    receipt omega_state_projection_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_STATE_PROJECTION_PASS": "PASS"'
    receipt rx_action_graph_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_ACTION_GRAPH_IR_PASS": "PASS"'
    # omega-mv#404 item 2: PASS means the package energy counter (hwmon aien_spbm, found by the
    # same name-prefix test rx_sem_incremental uses) measured the work; HOST_PASS_ENERGY_UNMEASURED
    # means this host has no such counter. The verdict must match this host; PASS_UNBOUND and FAIL
    # never pass. Which one ran is recorded as an observation.
    if grep -qs '^aien_spbm' /sys/class/hwmon/*/name; then inc_verdict=PASS; else inc_verdict=HOST_PASS_ENERGY_UNMEASURED; fi
    receipt omega_incremental_semantics_receipt.json "\"OMEGA_INCREMENTAL_SEMANTICS_PASS\": \"$inc_verdict\"" '"false_hits": 0,'
    obs incremental_semantics_verdict "\"$inc_verdict\""
    receipt rx_capability_query_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_CAPABILITY_QUERY_PASS": "PASS"'
    receipt rx_semantic_comm_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_SEMANTIC_COMMUNICATION_PASS": "PASS"'
    receipt rx_cognitive_routing_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_COGNITIVE_ROUTING_PASS": "PASS"'
    speed test-workflow-fusion rx_workflow_fusion_receipt.json OMEGA_WORKFLOW_FUSION_PASS
    make test-typed-results
    receipt rx_typed_results_receipt.json '"failures": 0,' '"candidate_bound": true,' '"OMEGA_TYPED_RESULT_CONSTRAINTS_PASS": "PASS"'
    make test-costmodel
    receipt rx_aegis_resident_receipt.json '"failures": 0,' '"candidate_bound": true,' '"R8_CONSTITUTIONAL_AEGIS": "PASS"'
    r="$(find build/qual-runs -name rx_heartbeat_receipt.json -type f -newer "$STEP_MARK" | head -n 1)"
    native="$(find build/qual-runs -name rx_native_authority_receipt.json -type f -newer "$STEP_MARK" | head -n 1)"
    generation="$(find build/qual-runs -name rx_generation_barrier_receipt.json -type f -newer "$STEP_MARK" | head -n 1)"
    test -n "$r"; test -n "$native"; test -n "$generation"
    if git diff --quiet && git diff --cached --quiet; then obs tree_cleanliness '"clean"'; else obs tree_cleanliness '"dirty"'; fi
    tools/rx_host_receipts.sh heartbeat "$r"
    tools/rx_host_receipts.sh native "$native" "$OMEGA_CANDIDATE_COMMIT"
    tools/rx_host_receipts.sh generation "$generation" "$OMEGA_CANDIDATE_COMMIT"
    if [ "$BUSY" = 1 ]; then obs not_run_reason "\"$BUSY_REASON\""; exit 77; fi
    ;;
  faculty-arm)
    pin physics-src-6d7cf0d physics 6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf
    export PHYSICS_DIR="$PINS/physics-src-6d7cf0d"
    aienos_pin; argus_pin; set -o pipefail; mkdir -p build
    make OUT_DIR=build/pid $NOPHYS test-program-id
    make OUT_DIR=build/prz $NOPHYS test-program-realize
    R10_ALLOW_NO_WIN=1 make test-r10
    receipt rx_omega_faculty_receipt.json '"failures": 0,' '"candidate_bound": true,' '"tree_dirty": false,'
    make test-regulate
    make test-r11
    receipt rx_aien_faculty_receipt.json '"failures": 0,' '"candidate_bound": true,' '"tree_dirty": false,'
    make "$OUT_DIR/rx_r13_living_host" "$OUT_DIR/rx_r13_living_testbuild_host"
    make "$OUT_DIR/rx_r14_recovery_host"
    make test-r16-authpath-linkmap | tee build/r16-g3-linkmap-host.log
    grep -qx 'R16-G3 static: R16_G3_LINKMAP=STATIC_PASS mode=host (checks 1-3 of 5 on this build; not the gate)' build/r16-g3-linkmap-host.log
    grep -qx 'R16 gate: R16_G3_AUTHPATH=NOT_RUN (checks 4-5, exec trace and living run, not run in static-only mode)' build/r16-g3-linkmap-host.log
    make test-r16-authpath-linkmap-silicon | tee build/r16-g3-linkmap-silicon.log
    grep -qx 'R16-G3 static: R16_G3_LINKMAP=STATIC_PASS mode=silicon (checks 1-3 of 5 on this build; not the gate)' build/r16-g3-linkmap-silicon.log
    grep -qx 'R16 gate: R16_G3_AUTHPATH=NOT_RUN (checks 4-5, exec trace and living run, not run in static-only mode)' build/r16-g3-linkmap-silicon.log
    make test-prod-hygiene | tee build/prod-hygiene-host.log
    grep -q '^R16 prod: PROD_HYGIENE=PASS mode=host ' build/prod-hygiene-host.log
    R16_PROD_NO_RUN=1 make test-prod-hygiene-silicon | tee build/prod-hygiene-silicon.log
    grep -q '^R16 prod: PROD_HYGIENE=PASS mode=silicon ' build/prod-hygiene-silicon.log
    make test-prod-refuses-test-pieces | tee build/prod-refuses.log
    grep -q '^R16 prod: PROD_REFUSES_TEST_PIECES=PASS ' build/prod-refuses.log
    EMPIRICAL_ALLOW_NO_WIN=1 make test-empirical
    receipt rx_empirical_optimizer_receipt.json '"failures": 0,' '"candidate_bound": true,' '"tree_dirty": false,' '"mismatches": 0,' '"OMEGA_EMPIRICAL_OPTIMIZER_PASS": "\(PASS\|CHAIN_PASS_NO_WIN\)"'
    PLAN_REUSE_NO_ENERGY=1 make test-plan-reuse
    receipt 'rx_plan_reuse_receipt.*.json' '"failures": 0,' '"G3_outcome_and_evidence": "PASS"' '"G4_false_applicability_zero": "PASS"' '"G5_identity": "PASS"'
    make test-rx-deadline-cancel
    make test-i11-wake-merge
    make test-c4-requal
    ;;
  # rx-host.yml chip-only-declared and spark-only targets (SKIP on GitHub, never a pass there)
  chip:*|spark:*)
    t="${step#*:}"
    # Chip and spark steps follow physics.lock like every other step. Makefile check-physics-lock
    # (a prerequisite of omegatool, rx_composition_gate_gpu and libomega_gpu.a) refuses any other
    # physics commit, and omegatool itself now needs the physics API of the lock (nvrm_channel_n,
    # L136 via #614). The former hard-coded 6d7cf0d4 failed every such chip gate on the judge once
    # #614 moved the lock to 048e5f5 (main 18c653b3, 2026-10-25). faculty-arm and rx-host-reference
    # keep their own pin: they mirror the hosted rx-host job, which checks out that commit itself.
    physics_pin
    aienos_pin; argus_pin
    make "$t"
    ;;
  # turing-yield.yml
  turing-yield)       make test-turing-yield test-turing-energy ;;
  # evidence-immutable.yml: reviewed evidence is additions-only. Hosted compares the PR base
  # and head; locally the base is origin/main (or AIEN_BASE_REF).
  evidence-immutable)
    base="${AIEN_BASE_REF:-origin/main}"
    obs base_ref "\"$(git rev-parse "$base")\""
    changed="$(git diff --name-status --diff-filter=MDRT "$base...HEAD" -- evidence/)"
    if [ -n "$changed" ]; then
      echo "These existing evidence files would be changed:"; echo "$changed"
      echo "Add a new, content-addressed file instead of rewriting reviewed evidence."
      obs evidence_rewrites "$(printf '%s\n' "$changed" | grep -c .)"
      exit 1
    fi
    obs evidence_rewrites 0
    echo "evidence/: additions only"
    ;;
  zero-disk-secrets)
    echo "Auditing repository for prohibited plaintext secret files..."
    if find . -maxdepth 4 -name ".env*" -not -path "*/.git/*" -not -path "*/build/*" | grep -q .; then
      echo "::error::Prohibited plaintext .env file found. Use an approved AIEN SecretProvider."
      obs env_files_found true; exit 1
    fi
    obs env_files_found false
    echo "Zero plaintext .env files verified."
    ;;
  runtime-api)        # omega-mv#594: the Runtime API manifest against the C headers, plus its negative controls.
                      # Host only (gcc for -aux-info), no GPU, no physics checkout.
    make $NOPHYS test-runtime-api ;;
  om-units)           # The C entry points whose algorithm is Omega (om/units.sh): omc born from the seed here,
                      # the generator pinned by om/omega-bridge.lock; regenerated binding, SHA-256 and BLAKE3
                      # byte parity against the C they replaced, and the BLAKE3 mutants. NOT_RUN (3) without tools.
                      # Then the program-mode om/blake3/blake3.om output contract (#726): digest, /dev/full, closed stdout.
    pin omega-bridge omega-bridge "$(lock om/omega-bridge.lock)"
    sh seed/omega-from-seed.sh > build/om-units-seed.log 2>&1 || { tail -3 build/om-units-seed.log; exit 1; }
    export OMEGA_OMC="$ROOT/seed/out/omc" OMEGA_BRIDGE="$PINS/omega-bridge"
    obs omc_sha256 "\"$(sha256sum < "$OMEGA_OMC" | cut -c1-64)\""
    sh om/units.sh --check
    sh om/sha256/parity/run.sh
    sh om/blake3/parity/run.sh
    "$OMEGA_OMC" < om/blake3/blake3.om > build/om-units-blake3 && chmod 700 build/om-units-blake3
    sh om/blake3/test-output.sh build/om-units-blake3
    ;;
  tilelab)            # omega#906: native .om tile planner and streaming (m,s,p) softmax. omc born from the seed
                      # here, then seed/tilelab/gate.sh (expected output, mutants failing for their own reason).
    sh seed/omega-from-seed.sh > build/tilelab-seed.log 2>&1 || { tail -3 build/tilelab-seed.log; exit 1; }
    obs omc_sha256 "\"$(sha256sum < seed/out/omc | cut -c1-64)\""
    sh seed/tilelab/gate.sh
    ;;
  trace)              # omega#608: local trace segment (seed/trace/segment.om), built by the committed seed/out/omc
                      # after its recorded SHA-256 is checked: 71 checks against expected.txt (torn-tail recovery
                      # keeps N-1 records, CRC/length/sequence damage, header refusals, O_EXCL, O_NOFOLLOW) and
                      # 4 source mutants (CRC check, torn tail, alignment, O_EXCL) that must each fail their check.
    sha256sum -c seed/out/omc.sha256
    obs omc_sha256 "\"$(sha256sum < seed/out/omc | cut -c1-64)\""
    sh seed/trace/gate.sh
    ;;
  tracecli)           # omega#610: offline read-only trace CLI (seed/trace/tracecli.om), built by the
                      # committed seed/out/omc after its recorded SHA-256 is checked. Golden stdout and
                      # exit codes, a mode-0000 refusal, and 4 source mutants (partial exit, compare
                      # equality, type filter, read-only open flags) that must each fail their own check.
    sha256sum -c seed/out/omc.sha256
    obs omc_sha256 "\"$(sha256sum < seed/out/omc | cut -c1-64)\""
    sh seed/trace/cli-gate.sh
    ;;
  security758)        # omega#758 (#774): relational comparison suite under seed/qualification/security673/relational,
                      # run by the OM-2 case gate rebuilt from seed/qualification/om2/gate.om with the committed
                      # seed/out/omc (sha checked) and the scanner seed/qualification/om2/leak/leakscan.om. Exits
                      # 0/0/0 for qualification/bindings/consumer, 1 with exactly the two intended FAIL rows for
                      # binding-negative; the L4 secret-dividend scanner PASS is a separate tripwire, not a total.
    sha256sum -c seed/out/omc.sha256
    OMC=seed/out/omc; H="$(cut -c1-64 seed/out/omc.sha256)"
    obs omc_sha256 "\"$H\""
    R=seed/qualification/security673/relational
    sec="$OUT_DIR/security758"; rm -rf "$sec"; mkdir -p "$sec"
    "$OMC" < seed/qualification/om2/gate.om > "$sec/gate.bin"; chmod 700 "$sec/gate.bin"
    obs gate_sha256 "\"$(sha256sum < "$sec/gate.bin" | cut -c1-64)\""
    sec_run() { local rc=0; "$sec/gate.bin" "$OMC" "$H" "$R/$1.cases" "$sec/$1" > "$sec/$1.log" 2>&1 || rc=$?
      tail -1 "$sec/$1.log"; [ "$rc" = "$2" ] || { echo "SECURITY758 FAIL $1.cases exit=$rc expected=$2" >&2; exit 1; }; }
    sec_run qualification 0; sec_run bindings 0; sec_run consumer 0; sec_run binding-negative 1
    grep -qx 'OM2GATE FAIL cases=2 pass=0 fail=2 invalid=0 not_run=0' "$sec/binding-negative.log"
    grep -q '^VERDICT wrong-source-bytes FAIL ' "$sec/binding-negative.log"
    grep -q '^VERDICT wrong-checker-bytes FAIL ' "$sec/binding-negative.log"
    echo "SECURITY758 PASS qualification consumer bindings, binding-negative 2 intended FAIL rows"
    sec_run l4-tripwire 0
    echo "SECURITY758 TRIPWIRE L4-division-gap: scanner still PASSES a secret dividend (known false negative, README L4; not a qualification)"
    ;;
  native)             # omega#663/#664: native .om units built by the committed seed/out/omc after its recorded
                      # SHA-256 is checked. The metadata test must pass and each recorded ECOSYSTEM663 mutant must
                      # exit 1; with native/autodiff664 present, its tape suite (test + 3 compiled mutants) and the
                      # C M21 fixture replay (CPU table, value-exact, mutant-sign control must FAIL) run too.
    sha256sum -c seed/out/omc.sha256
    OMC="$ROOT/seed/out/omc"
    obs omc_sha256 "\"$(sha256sum < "$OMC" | cut -c1-64)\""
    nat="$OUT_DIR/native"; rm -rf "$nat"; mkdir -p "$nat"
    native_build() { "$OMC" < "$1" > "$2" && [ "$(od -An -tx1 -N4 "$2" | tr -d ' ')" = 7f454c46 ] && chmod 700 "$2"; }
    cat native/ecosystem663/metadata.om tests/ecosystem663/metadata-test.om > "$nat/metadata-test.om"
    native_build "$nat/metadata-test.om" "$nat/metadata-test.elf"
    "$nat/metadata-test.elf" > "$nat/metadata-test.stdout"
    grep "^M20_METADATA_V1_PASS checks=" "$nat/metadata-test.stdout"
    for m in evidence/ECOSYSTEM663/mutant-*.om; do
      native_build "$m" "$nat/mutant.elf"
      rc=0; "$nat/mutant.elf" > /dev/null 2>&1 || rc=$?
      [ "$rc" = 1 ] || { echo "run.sh: $m was not rejected (exit $rc, need 1)" >&2; exit 1; }
      echo "mutant $m: exit 1 as required"
    done
    if [ -d native/autodiff664 ]; then
      OMC="$OMC" OUT="$ROOT/$nat/tape" sh tests/autodiff664/run-tape.sh
      make $NOPHYS test-autodiff-fixture FIXTURE=tests/fixtures/autodiff664/adtrain.fix
    fi
    ;;
  *) echo "run.sh: unknown step '$step'" >&2; exit 2 ;;
esac
