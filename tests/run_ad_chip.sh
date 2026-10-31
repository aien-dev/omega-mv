#!/bin/bash
# tests/run_ad_chip.sh -- native .om tape fixture replayed on the GB10 table.
#
#   GB10_CHIP_RUN=1 tests/run_ad_chip.sh --physics-dir DIR --fixture FILE [--evidence-dir DIR]
#
# Pattern of tests/run_tensor_chip.sh (no quiet-flag option).
# 1. Refuses unless GB10_CHIP_RUN=1. The omega tree must be clean (the run
#    commit is its HEAD); the physics checkout must be clean and at
#    physics.lock. FILE is the 748-byte fixture written by the native trainer
#    (aien-train tests/adtrain, mode D) and must lie outside both trees.
# 2. Builds tests/test_omega_ad_fixture.c with -DOMEGA_AD_FIXTURE_CHIP, the C
#    M21 tape, the M20 CPU and GB10 tables and the E1 GB10 executors,
#    -ffp-contract=off, into build/ad-runs/<run-id>/, and refuses a binary
#    with libm math or CUDA symbols.
# 3. Runs it under /tmp/aien-gb10.lock (waits; no timeout; never killed):
#    --chip FILE, then --chip --mutant-sign FILE, which must exit 1.
#    Inside `spark-window run` (SPARK_WINDOW set and AIEN_GB10_LOCK_HELD=1)
#    the window already holds that lock, shared or exclusive, so the script
#    takes no lock of its own: an exclusive flock beside the window's shared
#    hold would wait for its own window. The receipt records which held it.
# 4. PASS needs exit 0 with "AD_FIXTURE_CHIP: ... verdict=PASS" or
#    "verdict=PASS_TOL" (max_ulp <= 2; the receipt records which), the
#    negative control's FAIL, and clean trees with unmoved HEADs after.
# 5. Writes a content-addressed receipt <evidence-dir>/<sha256>.json (0444,
#    never overwritten) for PASS and FAIL alike, plus binary, fixture, logs
#    and stderr as blobs/<sha256>.{bin,fixture,log,stderr}.
# Shell + coreutils + git + jq + gcc. No Python. Exit 0 PASS, 1 FAIL, 2 refused.
set -u
SELF_DIR=$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OMEGA=$(cd "$SELF_DIR/.." && pwd)
PHYS=""
FIX=""
EVID=$HOME/workspace/evidence-out/AD-FIXTURE-GB10
while [ $# -gt 0 ]; do
    case $1 in
        --physics-dir) PHYS=$2; shift 2 ;;
        --fixture) FIX=$2; shift 2 ;;
        --evidence-dir) EVID=$2; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
die() { echo "run_ad_chip: $*" >&2; exit 2; }
[ "${GB10_CHIP_RUN:-}" = 1 ] || die "chip run refused: GB10_CHIP_RUN=1 is not set"
[ -n "$PHYS" ] && [ -d "$PHYS" ] || die "--physics-dir is required"
[ -n "$FIX" ] && [ -f "$FIX" ] || die "--fixture FILE is required"
PHYS=$(cd "$PHYS" && pwd)
FIX=$(cd "$(dirname "$FIX")" && pwd)/$(basename "$FIX")
[ "$(stat -c %s "$FIX")" = 748 ] || die "fixture is not 748 bytes"
[ -z "$(git -C "$OMEGA" status --porcelain)" ] || die "omega tree is dirty; refusing"
COMMIT=$(git -C "$OMEGA" rev-parse HEAD)
LOCKED=$(tr -d ' \n' < "$OMEGA/physics.lock")
PHEAD=$(git -C "$PHYS" rev-parse HEAD)
[ "$PHEAD" = "$LOCKED" ] || die "physics checkout is at $PHEAD, physics.lock pins $LOCKED"
[ -z "$(git -C "$PHYS" status --porcelain)" ] || die "physics checkout is dirty"
mkdir -p "$EVID/blobs"
EVID=$(cd "$EVID" && pwd)
case "$EVID/" in "$OMEGA/"*|"$PHYS/"*) die "evidence dir must be outside the omega tree and physics checkout" ;; esac
case "$FIX" in "$OMEGA/"*|"$PHYS/"*) die "fixture must be outside the omega tree and physics checkout" ;; esac

STARTED=$(date -u +%Y-%m-%dT%H:%M:%SZ)
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)-${COMMIT:0:12}
mkdir -p "$OMEGA/build/ad-runs" || die "cannot create build/ad-runs"
OUT=$(mktemp -d "$OMEGA/build/ad-runs/$RUN_ID.XXXXXX") || die "cannot create a run directory"
RUN_ID=$(basename "$OUT")
BIN=$OUT/test_omega_ad_fixture
cp "$FIX" "$OUT/fixture.bin" || die "cannot copy fixture"
FIX_SHA=$(sha256sum "$OUT/fixture.bin" | cut -d' ' -f1)
NV=$PHYS/third_party/nvidia-open-580.173.02
FAIL_REASON=""
(cd "$OMEGA" && gcc -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -ffp-contract=off \
    -DOMEGA_AD_FIXTURE_CHIP -Isrc -Isrc/tensor -Isrc/autodiff -I"$PHYS/forge" -I"$PHYS/nvrm" -I"$PHYS/m16" \
    -I"$NV/src/common/sdk/nvidia/inc" -I"$NV/kernel-open/common/inc" \
    -I"$NV/kernel-open/nvidia-uvm" -I"$NV/src/nvidia/arch/nvalloc/unix/include" \
    -o "$BIN" tests/test_omega_ad_fixture.c src/autodiff/omega_autodiff.c \
    src/tensor/omega_tensor.c src/tensor/omega_tensor_cpu.c \
    src/tensor/omega_tensor_reduce_seam.c src/tensor/omega_tensor_gb10.c \
    src/omega_numeric_reduce.c src/omega_numeric_transc.c src/omega_numeric_reduce_gb10.c \
    src/omega_numeric.c src/omega_numeric_gb10.c src/omega_numeric_divsqrt_gb10.c \
    src/omega_numeric_provenance.c src/omega_blackwell_codegen.c \
    src/omega_blackwell_encoder.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c \
    src/forge_realization.c src/aegis_verification.c src/omega_om_units.c \
    "$PHYS/forge/forge_descriptor.c" "$PHYS/forge/forge_realize.c" "$PHYS/sha256_clean.c" \
    "$PHYS/nvrm/nvrm.c" "$PHYS/m16/m16_native.c") > "$OUT/build.log" 2>&1 || FAIL_REASON="build failed"
if [ -z "$FAIL_REASON" ]; then
    nm -u "$BIN" | grep -Eq '\b(sqrtf?|expf?|logf?|powf?|fmaf?|sinf?|cosf?|roundf?|fabsf?)\b' && FAIL_REASON="libm symbols in binary"
    nm -u "$BIN" | grep -Eq 'libcuda(rt)?\.so|\b(cuInit|cuCtx|cuMem|cuStream|cudaMalloc)\b' && FAIL_REASON="CUDA symbols in binary"
fi
STATUS=-1
MSTATUS=-1
GB10_LOCK=""
BIN_SHA_BUILT=""
[ -z "$FAIL_REASON" ] && { BIN_SHA_BUILT=$(sha256sum "$BIN" | cut -d' ' -f1); [ -n "$BIN_SHA_BUILT" ] || FAIL_REASON="cannot hash binary after build"; }
if [ -z "$FAIL_REASON" ]; then
    # waits for the lock; no timeout; never killed
    if [ "${AIEN_GB10_LOCK_HELD:-}" = 1 ] && [ -n "${SPARK_WINDOW:-}" ]; then
        GB10_LOCK="spark-window $SPARK_WINDOW"
        gb10() { "$@"; }
    else
        GB10_LOCK="flock /tmp/aien-gb10.lock"
        gb10() { flock /tmp/aien-gb10.lock "$@"; }
    fi
    GB10_CHIP_RUN=1 gb10 "$BIN" --chip "$OUT/fixture.bin" > "$OUT/ad.log" 2> "$OUT/ad.stderr"
    STATUS=$?
    GB10_CHIP_RUN=1 gb10 "$BIN" --chip --mutant-sign "$OUT/fixture.bin" \
        > "$OUT/ad-mutant.log" 2> "$OUT/ad-mutant.stderr"
    MSTATUS=$?
    [ "$STATUS" = 0 ] || FAIL_REASON="test binary exit status $STATUS"
    grep -Eq '^AD_FIXTURE_CHIP: exact=[0-9]+/75 max_ulp=[0-9]+ verdict=(PASS|PASS_TOL)$' "$OUT/ad.log" \
        || FAIL_REASON="${FAIL_REASON:-no AD_FIXTURE_CHIP PASS line}"
    [ "$MSTATUS" = 1 ] || FAIL_REASON="${FAIL_REASON:-mutant-sign control exit $MSTATUS, want 1}"
    grep -q '^AD_FIXTURE_CHIP: .* verdict=FAIL (mutant-sign)$' "$OUT/ad-mutant.log" \
        || FAIL_REASON="${FAIL_REASON:-mutant-sign control printed no FAIL line}"
fi
FINISHED=$(date -u +%Y-%m-%dT%H:%M:%SZ)
[ -z "$(git -C "$OMEGA" status --porcelain)" ] && CLEAN_AFTER=true || { CLEAN_AFTER=false; FAIL_REASON="${FAIL_REASON:-omega tree dirty after run}"; }
[ "$(git -C "$OMEGA" rev-parse HEAD)" = "$COMMIT" ] && HEAD_SAME=true || { HEAD_SAME=false; FAIL_REASON="${FAIL_REASON:-HEAD moved during run}"; }
[ -z "$(git -C "$PHYS" status --porcelain)" ] && PCLEAN_AFTER=true || { PCLEAN_AFTER=false; FAIL_REASON="${FAIL_REASON:-physics dirty after run}"; }
[ "$(git -C "$PHYS" rev-parse HEAD)" = "$PHEAD" ] && PHEAD_SAME=true || { PHEAD_SAME=false; FAIL_REASON="${FAIL_REASON:-physics HEAD moved during run}"; }

# store_blob SRC EXT -> prints the digest (same rules as run_tensor_chip.sh).
store_blob() {
    local src=$1 ext=$2 d dst tmp have
    d=$(sha256sum "$src" | cut -d' ' -f1); [ -n "$d" ] || die "cannot hash $src"
    dst=$EVID/blobs/$d.$ext
    if [ -e "$dst" ]; then
        have=$(sha256sum "$dst" | cut -d' ' -f1)
        [ "$have" = "$d" ] || die "existing blob $dst does not match its digest"
    else
        tmp=$(mktemp "$EVID/blobs/.tmp.XXXXXX") || die "cannot create temp blob"
        cp "$src" "$tmp" || { rm -f "$tmp"; die "cannot store $ext blob"; }
        have=$(sha256sum "$tmp" | cut -d' ' -f1)
        [ "$have" = "$d" ] || { rm -f "$tmp"; die "stored $ext blob does not match its digest"; }
        chmod 0444 "$tmp" || { rm -f "$tmp"; die "cannot seal $ext blob"; }
        mv -n "$tmp" "$dst"; rm -f "$tmp"
        have=$(sha256sum "$dst" | cut -d' ' -f1)
        [ "$have" = "$d" ] || die "blob $dst does not match its digest after publish"
    fi
    echo "$d"
}
BIN_SHA=""; LOG_SHA=""; ERR_SHA=""; MLOG_SHA=""
if [ -f "$BIN" ]; then BIN_SHA=$(store_blob "$BIN" bin) || exit 2; fi
[ -z "$BIN_SHA_BUILT" ] || [ "$BIN_SHA" = "$BIN_SHA_BUILT" ] || FAIL_REASON="${FAIL_REASON:-binary changed between build and receipt}"
STORED_FIX=$(store_blob "$OUT/fixture.bin" fixture) || exit 2
[ "$STORED_FIX" = "$FIX_SHA" ] || FAIL_REASON="${FAIL_REASON:-fixture changed during run}"
if [ -f "$OUT/ad.log" ]; then LOG_SHA=$(store_blob "$OUT/ad.log" log) || exit 2; fi
if [ -f "$OUT/ad.stderr" ]; then ERR_SHA=$(store_blob "$OUT/ad.stderr" stderr) || exit 2; fi
if [ -f "$OUT/ad-mutant.log" ]; then MLOG_SHA=$(store_blob "$OUT/ad-mutant.log" log) || exit 2; fi
VERDICT=PASS; [ -n "$FAIL_REASON" ] && VERDICT=FAIL
line() { grep "^$1" "$2" 2>/dev/null | head -1; }
TMP=$OUT/receipt.json
jq -n --arg suite AD_FIXTURE_GB10 --arg status "$VERDICT" --arg reason "$FAIL_REASON" \
    --arg run_id "$RUN_ID" --arg commit "$COMMIT" --arg physics "$PHEAD" --arg pin "$LOCKED" \
    --argjson clean_after "$CLEAN_AFTER" --argjson head_same "$HEAD_SAME" \
    --argjson pclean_after "$PCLEAN_AFTER" --argjson phead_same "$PHEAD_SAME" \
    --arg started "$STARTED" --arg finished "$FINISHED" \
    --arg bin "$BIN_SHA" --arg fix "$FIX_SHA" --arg log "$LOG_SHA" --arg errlog "$ERR_SHA" --arg mlog "$MLOG_SHA" \
    --arg exit_status "$STATUS" --arg mexit "$MSTATUS" \
    --arg chip_line "$(line AD_FIXTURE_CHIP: "$OUT/ad.log")" \
    --arg mutant_line "$(line AD_FIXTURE_CHIP: "$OUT/ad-mutant.log")" \
    --arg host "$(uname -n)" --arg kernel "$(uname -r)" --arg gb10_lock "$GB10_LOCK" \
    '{suite:$suite,status:$status,reason:$reason,run_id:$run_id,started_utc:$started,finished_utc:$finished,
      omega_commit:$commit,omega_tree_clean_before:true,omega_tree_clean_after:$clean_after,
      omega_commit_unchanged_after:$head_same,physics_commit:$physics,physics_lock_pin:$pin,
      physics_tree_clean_before:true,physics_tree_clean_after:$pclean_after,physics_commit_unchanged_after:$phead_same,
      fixture_sha256:$fix,binary_sha256:$bin,chip_log_sha256:$log,chip_stderr_sha256:$errlog,
      mutant_log_sha256:$mlog,chip_exit_status:$exit_status,mutant_exit_status:$mexit,
      chip_line:$chip_line,mutant_line:$mutant_line,
      tolerance:"PASS max_ulp 0; PASS_TOL max_ulp <= 2; else FAIL",
      realization:"M20 omega_tensor_gb10_realization under the C M21 tape",
      quiet_flag:"not used",gb10_lock:$gb10_lock,host:$host,kernel:$kernel}' > "$TMP" || die "receipt json (jq) failed, no receipt written"
[ -s "$TMP" ] || die "empty receipt, not written"
[ "$VERDICT" != PASS ] || { [ -n "$BIN_SHA" ] && [ -n "$LOG_SHA" ]; } || die "PASS without binary and log digests refused"
DIG=$(sha256sum "$TMP" | cut -d' ' -f1)
(set -o noclobber; cat "$TMP" > "$EVID/$DIG.json") || die "receipt $DIG.json already exists"
chmod 0444 "$EVID/$DIG.json"
echo "receipt: $EVID/$DIG.json status=$VERDICT ${FAIL_REASON}"
[ "$VERDICT" = PASS ]
