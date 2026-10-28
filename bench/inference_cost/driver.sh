#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# bench/inference_cost/driver.sh -- Held-out Omega inference-cost benchmark driver.
# Evaluates verified programs produced per unit of inference cost.
#
# Line-oriented plain-text data format (no JSON).
# Wire version: 1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TASKS_DIR="$SCRIPT_DIR/tasks"
BUILD_DIR="$REPO_DIR/build/inference_cost"
mkdir -p "$BUILD_DIR"

# Locate and verify pinned omc compiler
OMC="${OMC:-$REPO_DIR/seed/out/omc}"
PIN_SHA="2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f"

if [ ! -f "$OMC" ]; then
    if [ -f "/home/drakestapleton/workspace/tilelab/omega-mv/seed/out/omc" ]; then
        OMC="/home/drakestapleton/workspace/tilelab/omega-mv/seed/out/omc"
    else
        echo "driver.sh: omc compiler not found at $OMC" >&2
        exit 3
    fi
fi

ACTUAL_SHA="$(sha256sum "$OMC" | awk '{print $1}')"
if [ "$ACTUAL_SHA" != "$PIN_SHA" ]; then
    echo "driver.sh: omc sha256 mismatch (expected $PIN_SHA, got $ACTUAL_SHA)" >&2
    exit 3
fi

# Build native .om scorer if needed
SCORER_BIN="$BUILD_DIR/scorer"
if [ ! -f "$SCORER_BIN" ] || [ "$SCRIPT_DIR/scorer.om" -nt "$SCORER_BIN" ]; then
    "$OMC" < "$SCRIPT_DIR/scorer.om" > "$SCORER_BIN.tmp"
    chmod +x "$SCORER_BIN.tmp"
    mv "$SCORER_BIN.tmp" "$SCORER_BIN"
fi

# Measure wall milliseconds helper
now_ms() {
    date +%s%3N
}

# Run one task evaluation:
# evaluate_task <task_id> <candidate_file> <attempt_num> <first_attempt> <p_tokens> <c_tokens>
evaluate_task() {
    local task_id="$1"
    local candidate_file="$2"
    local att_num="$3"
    local first_att="$4"
    local p_tok="$5"
    local c_tok="$6"
    local t_tok=$(( p_tok + c_tok ))

    local task_dir="$TASKS_DIR/$task_id"
    local check_file="$task_dir/check.om"
    local bin_file="$BUILD_DIR/${task_id}_eval"

    local t_start
    t_start=$(now_ms)

    # Compile candidate + check
    local comp_rc=0
    if ! cat "$candidate_file" "$check_file" | "$OMC" > "$bin_file" 2>/dev/null; then
        comp_rc=$?
    fi

    local ver_flag=0
    if [ "$comp_rc" -eq 0 ]; then
        chmod +x "$bin_file"
        if "$bin_file" >/dev/null 2>&1; then
            ver_flag=1
        fi
        rm -f "$bin_file"
    fi

    local t_end
    t_end=$(now_ms)
    local wall_ms=$(( t_end - t_start ))
    if [ "$wall_ms" -le 0 ]; then
        wall_ms=1
    fi

    echo "$task_id $att_num $first_att $comp_rc $ver_flag $wall_ms $p_tok $c_tok $t_tok"
}

# Run full evaluation across all 20 tasks
# mode: "ref" or "mutant"
run_evaluation() {
    local mode="$1"
    local outfile="$BUILD_DIR/attempts_${mode}.txt"

    local task_list
    task_list=$(find "$TASKS_DIR" -mindepth 1 -maxdepth 1 -type d | sort)
    local task_count
    task_count=$(echo "$task_list" | wc -l)

    echo "version 1" > "$outfile"
    echo "attempts $task_count" >> "$outfile"

    for tdir in $task_list; do
        local tid
        tid=$(basename "$tdir")
        local cand
        if [ "$mode" = "ref" ]; then
            cand="$tdir/reference.om"
        else
            cand="$tdir/mutant.om"
        fi

        # Approximate token counts based on characters
        local p_words
        p_words=$(wc -w < "$tdir/statement.txt")
        local p_tok=$(( p_words * 4 / 3 + 10 ))

        local c_words
        c_words=$(wc -w < "$cand")
        local c_tok=$(( c_words * 4 / 3 + 5 ))

        evaluate_task "$tid" "$cand" 1 1 "$p_tok" "$c_tok" >> "$outfile"
    done

    "$SCORER_BIN" < "$outfile"
}

run_self_test() {
    echo "=== Running inference-cost benchmark self-test ==="
    
    # 1. Positive control: Reference implementations must achieve verified_count 20 and status PASS
    echo "[1/4] Positive control: replaying 20 reference implementations..."
    local ref_log="$BUILD_DIR/ref_summary.txt"
    if ! run_evaluation "ref" > "$ref_log"; then
        echo "SELF-TEST FAILED: reference replay returned non-zero exit code" >&2
        cat "$ref_log" >&2
        return 1
    fi
    cat "$ref_log"
    if ! grep -q "^status PASS" "$ref_log" || ! grep -q "^verified_count 20" "$ref_log"; then
        echo "SELF-TEST FAILED: expected 20 verified and status PASS in reference replay" >&2
        return 1
    fi
    echo "Positive control: PASS (20/20 verified)"

    # 2. Negative control: Mutant set must produce verified_count 0 and status FAIL
    echo "[2/4] Negative control: replaying 20 mutants..."
    local mut_log="$BUILD_DIR/mut_summary.txt"
    local mut_rc=0
    set +e
    run_evaluation "mutant" > "$mut_log"
    mut_rc=$?
    set -e
    if [ "$mut_rc" -eq 0 ]; then
        echo "SELF-TEST FAILED: mutant replay should have exited non-zero" >&2
        cat "$mut_log" >&2
        return 1
    fi
    cat "$mut_log"
    if ! grep -q "^status FAIL" "$mut_log" || ! grep -q "^verified_count 0" "$mut_log"; then
        echo "SELF-TEST FAILED: expected 0 verified and status FAIL in mutant replay" >&2
        return 1
    fi
    echo "Negative control: PASS (mutants caught, verified_count 0, status FAIL)"

    # 3. Format rejection: bad version
    echo "[3/4] Rejection control: bad version..."
    local bad_ver_rc=0
    set +e
    printf "version 99\nattempts 0\n" | "$SCORER_BIN" >/dev/null 2>&1
    bad_ver_rc=$?
    set -e
    if [ "$bad_ver_rc" -ne 3 ]; then
        echo "SELF-TEST FAILED: bad version should exit with code 3 (got $bad_ver_rc)" >&2
        return 1
    fi
    echo "Rejection control (bad version): PASS (exit 3)"

    # 4. Format rejection: truncated / malformed input
    echo "[4/4] Rejection control: truncated input..."
    local trunc_rc=0
    set +e
    printf "version 1\nattempts 5\n" | "$SCORER_BIN" >/dev/null 2>&1
    trunc_rc=$?
    set -e
    if [ "$trunc_rc" -ne 2 ]; then
        echo "SELF-TEST FAILED: truncated attempts should exit with code 2 (got $trunc_rc)" >&2
        return 1
    fi
    echo "Rejection control (truncated): PASS (exit 2)"

    echo "=== SELF-TEST PASS: inference-cost benchmark verified ==="
    return 0
}

usage() {
    echo "Usage: $0 [--self-test | --replay-ref | --replay-mutants | --run-model]"
    exit 1
}

case "${1:-}" in
    --self-test)
        run_self_test
        ;;
    --replay-ref)
        run_evaluation "ref"
        ;;
    --replay-mutants)
        run_evaluation "mutant" || true
        ;;
    --run-model)
        echo "driver.sh: No GPU or local model endpoint configured without GPU loading; model runs marked NOT_RUN."
        echo "Use --replay-ref for deterministic benchmark evaluation."
        exit 0
        ;;
    *)
        usage
        ;;
esac
