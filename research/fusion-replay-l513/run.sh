#!/bin/sh
set -eu

L513_ROOT=$(git rev-parse --show-toplevel)
L513_SOURCE_COMMIT=b260b3a4873f56ef4e3f8ea850fef0c19fdca21c
L513_PHYSICS_PIN=6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf
L513_PHYSICS_DIR=${L513_PHYSICS_DIR:-/home/drakestapleton/workspace/l182-pins/physics}
L513_RUN_DIR=$(mktemp -d /tmp/omega-l513-green.XXXXXX)
L513_EVIDENCE_DIR=${L513_EVIDENCE_DIR:-"$L513_RUN_DIR/evidence"}
L513_EXPORT_DIR=$L513_RUN_DIR/export
L513_MUTANT_DIR=$L513_RUN_DIR/mutant
L513_REPLAY_C=$L513_ROOT/research/fusion-replay-l513/fusion_replay.c
L513_FIX_PATCH=$L513_ROOT/research/fusion-replay-l513/diagnostic-bounded-copy.patch
L513_MUTANT_PATCH=$L513_ROOT/research/fusion-replay-l513/behavior-mutant.patch
L513_CC=$(command -v cc)

mkdir -p "$L513_EVIDENCE_DIR"
if find "$L513_EVIDENCE_DIR" -mindepth 1 -print -quit | grep -q .; then
    echo "L513 evidence directory must be new and empty: $L513_EVIDENCE_DIR" >&2
    exit 2
fi
if ! git -C "$L513_ROOT" cat-file -e "$L513_SOURCE_COMMIT^{tree}"; then
    echo "L137 source commit is unavailable" >&2
    exit 2
fi
L513_ACTUAL_PHYSICS=$(git -C "$L513_PHYSICS_DIR" rev-parse HEAD)
L513_LOCKED_PHYSICS=$(tr -d '[:space:]' < "$L513_ROOT/physics.lock")
if [ "$L513_ACTUAL_PHYSICS" != "$L513_PHYSICS_PIN" ] || [ "$L513_LOCKED_PHYSICS" != "$L513_PHYSICS_PIN" ]; then
    echo "Physics checkout and L137 lock must both equal $L513_PHYSICS_PIN" >&2
    exit 2
fi

mkdir -p "$L513_EXPORT_DIR"
git -C "$L513_ROOT" archive --format=tar "$L513_SOURCE_COMMIT" Makefile mk src tests physics.lock tools/quietlock | tar -xf - -C "$L513_EXPORT_DIR"
cp "$L513_REPLAY_C" "$L513_EXPORT_DIR/fusion_replay.c"
patch -d "$L513_EXPORT_DIR" -p0 < "$L513_FIX_PATCH"

cat > "$L513_EVIDENCE_DIR/commands.txt" <<EOF2
source_commit=$L513_SOURCE_COMMIT
source_tree=$(git -C "$L513_ROOT" rev-parse "$L513_SOURCE_COMMIT^{tree}")
physics_commit=$L513_ACTUAL_PHYSICS
cc=$L513_CC
$(cc --version | head -1)
replay_compile=$L513_CC -std=gnu11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -ffp-contract=off -ffunction-sections -fdata-sections -Wl,--gc-sections -I$L513_EXPORT_DIR -I$L513_EXPORT_DIR/src -I$L513_EXPORT_DIR/tests -o $L513_RUN_DIR/fusion_replay $L513_EXPORT_DIR/fusion_replay.c $L513_EXPORT_DIR/src/omega_blackwell_codegen_moe.c $L513_EXPORT_DIR/src/omega_blackwell_codegen.c $L513_EXPORT_DIR/src/omega_blackwell_encoder.c $L513_EXPORT_DIR/src/omega_gpu_fp8_pack.c $L513_EXPORT_DIR/src/omega_blackwell_matmul.c $L513_EXPORT_DIR/src/sha256.c -lm
full_target=quiet-build timeout 300 make -C $L513_EXPORT_DIR -j2 PHYSICS_DIR=$L513_PHYSICS_DIR OUT_DIR=out test-gpu-fp8
replay_run=quiet-build timeout 30 $L513_RUN_DIR/fusion_replay
negative_control=one-ULP output corruption in a separate scratch copy; strict exact-output check must fail
EOF2

git -C "$L513_ROOT" archive --format=tar "$L513_SOURCE_COMMIT" Makefile mk src tests physics.lock tools/quietlock | sha256sum > "$L513_EVIDENCE_DIR/export.sha256"
sha256sum "$L513_REPLAY_C" "$L513_FIX_PATCH" "$L513_MUTANT_PATCH" "$L513_ROOT/physics.lock" > "$L513_EVIDENCE_DIR/harness-and-lock.sha256"
sha256sum "$L513_EXPORT_DIR/tests/gpu_fp8_api_test.c" "$L513_EXPORT_DIR/tests/bw_warp_sim.h" "$L513_EXPORT_DIR/src/omega_blackwell_codegen_moe.c" "$L513_EXPORT_DIR/src/omega_blackwell_codegen.c" "$L513_EXPORT_DIR/src/omega_blackwell_encoder.c" "$L513_EXPORT_DIR/src/omega_gpu_fp8_pack.c" "$L513_EXPORT_DIR/src/omega_blackwell_matmul.c" "$L513_EXPORT_DIR/src/sha256.c" "$L513_EXPORT_DIR/tools/quietlock/quietlock.c" > "$L513_EVIDENCE_DIR/source.sha256"
sha256sum "$L513_PHYSICS_DIR/m16/m16_native.h" "$L513_PHYSICS_DIR/m16/m16_native.c" "$L513_PHYSICS_DIR/nvrm/nvrm.c" > "$L513_EVIDENCE_DIR/physics-dependencies.sha256"
for L513_TOOL in "$L513_CC" "$(readlink -f "$L513_CC")" "$(command -v gcc)" "$(command -v make)" "$(command -v ar)" "$(command -v ld)" "$(command -v as)" "$(command -v git)" "$(command -v timeout)" "$(command -v tar)" "$(command -v patch)" "$(command -v sha256sum)" "$(command -v quiet-build)"; do
    sha256sum "$L513_TOOL"
done > "$L513_EVIDENCE_DIR/tools.sha256"

set +e
quiet-build "$L513_CC" -std=gnu11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -ffp-contract=off -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$L513_EXPORT_DIR" -I"$L513_EXPORT_DIR/src" -I"$L513_EXPORT_DIR/tests" -o "$L513_RUN_DIR/fusion_replay" "$L513_EXPORT_DIR/fusion_replay.c" "$L513_EXPORT_DIR/src/omega_blackwell_codegen_moe.c" "$L513_EXPORT_DIR/src/omega_blackwell_codegen.c" "$L513_EXPORT_DIR/src/omega_blackwell_encoder.c" "$L513_EXPORT_DIR/src/omega_gpu_fp8_pack.c" "$L513_EXPORT_DIR/src/omega_blackwell_matmul.c" "$L513_EXPORT_DIR/src/sha256.c" -lm > "$L513_EVIDENCE_DIR/replay-build.log" 2>&1
L513_REPLAY_BUILD_RC=$?
printf '%s\n' "$L513_REPLAY_BUILD_RC" > "$L513_EVIDENCE_DIR/replay-build.exit"
if [ "$L513_REPLAY_BUILD_RC" -ne 0 ]; then
    cat "$L513_EVIDENCE_DIR/replay-build.log" >&2
    exit 1
fi
quiet-build timeout 30 "$L513_RUN_DIR/fusion_replay" > "$L513_EVIDENCE_DIR/replay.log" 2>&1
L513_REPLAY_RC=$?
printf '%s\n' "$L513_REPLAY_RC" > "$L513_EVIDENCE_DIR/replay.exit"
set -e
if [ "$L513_REPLAY_RC" -ne 0 ] || ! grep -q 'checks=8 failed=0' "$L513_EVIDENCE_DIR/replay.log"; then
    cat "$L513_EVIDENCE_DIR/replay.log" >&2
    exit 1
fi

set +e
quiet-build timeout 300 make -C "$L513_EXPORT_DIR" -j2 PHYSICS_DIR="$L513_PHYSICS_DIR" OUT_DIR=out test-gpu-fp8 > "$L513_EVIDENCE_DIR/full-test.log" 2>&1
L513_FULL_RC=$?
printf '%s\n' "$L513_FULL_RC" > "$L513_EVIDENCE_DIR/full-test.exit"
set -e
if [ "$L513_FULL_RC" -ne 0 ]; then
    tail -80 "$L513_EVIDENCE_DIR/full-test.log" >&2
    exit 1
fi

mkdir -p "$L513_MUTANT_DIR/tests"
cp "$L513_EXPORT_DIR/tests/gpu_fp8_api_test.c" "$L513_MUTANT_DIR/tests/gpu_fp8_api_test.c"
cp "$L513_REPLAY_C" "$L513_MUTANT_DIR/fusion_replay.c"
patch -d "$L513_MUTANT_DIR" -p0 < "$L513_MUTANT_PATCH"
set +e
quiet-build "$L513_CC" -std=gnu11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -ffp-contract=off -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$L513_MUTANT_DIR" -I"$L513_EXPORT_DIR" -I"$L513_EXPORT_DIR/src" -I"$L513_EXPORT_DIR/tests" -o "$L513_RUN_DIR/fusion_replay_mutant" "$L513_MUTANT_DIR/fusion_replay.c" "$L513_EXPORT_DIR/src/omega_blackwell_codegen_moe.c" "$L513_EXPORT_DIR/src/omega_blackwell_codegen.c" "$L513_EXPORT_DIR/src/omega_blackwell_encoder.c" "$L513_EXPORT_DIR/src/omega_gpu_fp8_pack.c" "$L513_EXPORT_DIR/src/omega_blackwell_matmul.c" "$L513_EXPORT_DIR/src/sha256.c" -lm > "$L513_EVIDENCE_DIR/mutant-build.log" 2>&1
L513_MUTANT_BUILD_RC=$?
printf '%s\n' "$L513_MUTANT_BUILD_RC" > "$L513_EVIDENCE_DIR/mutant-build.exit"
if [ "$L513_MUTANT_BUILD_RC" -eq 0 ]; then
    quiet-build timeout 30 "$L513_RUN_DIR/fusion_replay_mutant" > "$L513_EVIDENCE_DIR/mutant.log" 2>&1
    L513_MUTANT_RC=$?
else
    L513_MUTANT_RC=99
fi
printf '%s\n' "$L513_MUTANT_RC" > "$L513_EVIDENCE_DIR/mutant.exit"
set -e
if [ "$L513_MUTANT_BUILD_RC" -ne 0 ] || [ "$L513_MUTANT_RC" -ne 1 ] || ! grep -q 'BIT-IDENTICAL' "$L513_EVIDENCE_DIR/mutant.log" || ! grep -q 'checks=8 failed=1' "$L513_EVIDENCE_DIR/mutant.log"; then
    cat "$L513_EVIDENCE_DIR/mutant.log" >&2 2>/dev/null || true
    echo "one-ULP negative control did not trigger the expected single assertion" >&2
    exit 1
fi

sha256sum "$L513_RUN_DIR/fusion_replay" "$L513_EXPORT_DIR/out/gpu_fp8_api_test" "$L513_RUN_DIR/fusion_replay_mutant" > "$L513_EVIDENCE_DIR/binaries.sha256"
printf 'REDUCED_REPLAY=PASS (8/8)\nFULL_TEST_GPU_FP8=PASS\nONE_ULP_NEGATIVE_CONTROL=CAUGHT (1/8)\nGPU_RUN=NOT_RUN\nMODEL_LOAD=NOT_RUN\nPhysics=%s\nEvidence=%s\n' "$L513_ACTUAL_PHYSICS" "$L513_EVIDENCE_DIR"
