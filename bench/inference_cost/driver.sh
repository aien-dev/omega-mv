#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# bench/inference_cost/driver.sh -- tokens per verified program, Omega vs C vs Python vs Rust.
#
#   driver.sh --check-tasks    every reference verifies and every mutant is rejected, per language
#   driver.sh --self-test      scorer, JSON edge and --run-model end to end against fake_model.om
#   driver.sh --run-model --endpoint URL --model NAME [--langs om,c,py,rs] [--arms plain,feedback]
#             [--attempts K] [--temperature T] [--max-tokens N] [--out DIR]
#
# Plain-text records only (attempt wire version 2, read by scorer.om); JSON exists only at the
# HTTP edge (chat.om). Token counts are the endpoint's reported usage, never estimated.
# Exit: 0 done, 1 a check failed, 2 usage, 3 NOT_RUN (no endpoint, HTTP or protocol failure,
# missing toolchain or compiler pin mismatch).

set -eu

SCRIPT_DIR=$(CDPATH='' cd "$(dirname "$0")" && pwd)
REPO_DIR=$(CDPATH='' cd "$SCRIPT_DIR/../.." && pwd)
TASKS_DIR="$SCRIPT_DIR/tasks"
BUILD_DIR="$REPO_DIR/build/inference_cost"
BIN_DIR="$BUILD_DIR/bin"
OMC="${OMC:-$REPO_DIR/seed/out/omc}"
PIN_FILE="$REPO_DIR/seed/out/omc.sha256"
ALL_LANGS="om c py rs"

say() { printf '%s\n' "$*"; }
die() { printf 'driver.sh: %s\n' "$*" >&2; exit 2; }
not_run() { say "NOT_RUN: $*"; exit 3; }

usage() {
    sed -n '7,11p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

now_ms() { date +%s%3N; }

# ---- compiler pin and tools

check_pin() {
    [ -f "$PIN_FILE" ] || not_run "omc pin file $PIN_FILE missing"
    [ -f "$OMC" ] || not_run "omc not found at $OMC"
    pin=$(awk 'NR==1{print $1}' "$PIN_FILE")
    actual=$(sha256sum "$OMC" | awk '{print $1}')
    [ "$pin" = "$actual" ] || not_run "omc sha256 $actual does not match the pin $pin ($PIN_FILE)"
}

need_tool() {
    command -v "$1" >/dev/null 2>&1 || not_run "$1 is not installed"
}

need_lang_tools() {
    need_tool timeout
    for l in $1; do
        case $l in
            c) need_tool cc ;;
            py) need_tool python3 ;;
            rs) need_tool rustc ;;
            om) ;;
            *) die "unknown language '$l' (om c py rs)" ;;
        esac
    done
}

om_build() { # out_name, then the .om files to compose after the om2 line
    name=$1
    shift
    if ! { printf 'om2\n'; cat "$@"; } | "$OMC" > "$BIN_DIR/$name.tmp" 2> "$BIN_DIR/$name.err"; then
        cat "$BIN_DIR/$name.err" >&2
        die "omc refused $name"
    fi
    chmod +x "$BIN_DIR/$name.tmp"
    mv "$BIN_DIR/$name.tmp" "$BIN_DIR/$name"
}

build_tools() {
    check_pin
    mkdir -p "$BIN_DIR"
    om_build scorer "$SCRIPT_DIR/scorer.om"
    om_build chat "$REPO_DIR/seed/json/json.om" "$REPO_DIR/seed/json/jsonw.om" "$SCRIPT_DIR/chat.om"
    om_build fake_model "$REPO_DIR/seed/json/json.om" "$REPO_DIR/seed/json/jsonw.om" \
        "$REPO_DIR/seed/net/http.om" "$SCRIPT_DIR/fake_model.om"
}

# ---- task spec (version 2): name, param NAME TYPE lines, ret TYPE; types u32 i32 u64

spec_name() { awk '$1=="name"{print $2; exit}' "$1"; }
spec_ret() { awk '$1=="ret"{print $2; exit}' "$1"; }
spec_params() { awk '$1=="param"{print $2, $3}' "$1"; }

ty_om() { case $1 in u32) say uint ;; i32) say int ;; u64) say ulong ;; *) die "bad spec type $1" ;; esac; }
ty_c() { case $1 in u32) say uint32_t ;; i32) say int32_t ;; u64) say uint64_t ;; *) die "bad spec type $1" ;; esac; }
ty_rs() { case $1 in u32|i32|u64) say "$1" ;; *) die "bad spec type $1" ;; esac; }

signature() { # lang spec
    name=$(spec_name "$2")
    ret=$(spec_ret "$2")
    params=$(spec_params "$2")
    case $1 in
        om)
            s="fn $name"
            while read -r pn pt; do s="$s $pn $(ty_om "$pt")"; done <<EOF
$params
EOF
            say "$s to $(ty_om "$ret")" ;;
        c)
            s=""
            while read -r pn pt; do s="${s:+$s, }$(ty_c "$pt") $pn"; done <<EOF
$params
EOF
            say "$(ty_c "$ret") $name($s)" ;;
        py)
            s=""
            while read -r pn pt; do s="${s:+$s, }$pn"; done <<EOF
$params
EOF
            say "def $name($s)" ;;
        rs)
            s=""
            while read -r pn pt; do s="${s:+$s, }$pn: $(ty_rs "$pt")"; done <<EOF
$params
EOF
            say "fn $name($s) -> $(ty_rs "$ret")" ;;
    esac
}

lang_title() { case $1 in om) say Omega ;; c) say C ;; py) say Python ;; rs) say Rust ;; esac; }
lang_fence() { case $1 in om) say omega ;; c) say c ;; py) say python ;; rs) say rust ;; esac; }

# ---- the stdin driver appended after a candidate: inputs.txt in, one decimal per case out

gen_main() { # lang spec
    name=$(spec_name "$2")
    ret=$(spec_ret "$2")
    params=$(spec_params "$2")
    i=0
    args=""
    case $1 in
        om)
            printf '\nfn main to int\nbench_load\nbxn = bench_cases\nfor bxi ulong bxn\n'
            while read -r pn pt; do
                printf 'bxa%d = %s bench_arg\n' "$i" "$(ty_om "$pt")"
                args="$args bxa$i"
                i=$((i + 1))
            done <<EOF
$params
EOF
            printf 'bxr = %s%s\n' "$name" "$args"
            case $ret in
                u32) say 'bench_putu ulong bxr' ;;
                i32) say 'bench_puti long bxr' ;;
                u64) say 'bench_putu bxr' ;;
            esac
            printf 'end\nret 0\nend\n' ;;
        c)
            printf '\nint main(void) {\n    char bench_w[64];\n    unsigned long long bench_n, bench_i;\n'
            printf '    if (scanf("%%63s %%63s %%63s %%llu", bench_w, bench_w, bench_w, &bench_n) != 4) return 92;\n'
            printf '    for (bench_i = 0; bench_i < bench_n; bench_i++) {\n'
            while read -r pn pt; do
                printf '        if (scanf("%%63s", bench_w) != 1) return 92;\n'
                case $pt in
                    u64) printf '        %s bench_a%d = (%s)strtoull(bench_w, 0, 10);\n' "$(ty_c "$pt")" "$i" "$(ty_c "$pt")" ;;
                    *) printf '        %s bench_a%d = (%s)strtoll(bench_w, 0, 10);\n' "$(ty_c "$pt")" "$i" "$(ty_c "$pt")" ;;
                esac
                args="${args:+$args, }bench_a$i"
                i=$((i + 1))
            done <<EOF
$params
EOF
            printf '        %s bench_r = %s(%s);\n' "$(ty_c "$ret")" "$name" "$args"
            case $ret in
                i32) printf '        printf("%%lld\\n", (long long)bench_r);\n' ;;
                *) printf '        printf("%%llu\\n", (unsigned long long)bench_r);\n' ;;
            esac
            printf '    }\n    return 0;\n}\n' ;;
        py)
            k=$(printf '%s\n' "$params" | awk 'NF==2{n++} END{print n+0}')
            printf '\n\nimport sys as _bench_sys\n\n\ndef _bench_main():\n'
            printf '    t = _bench_sys.stdin.read().split()\n    n = int(t[3])\n    p = 4\n    o = []\n'
            printf '    for _ in range(n):\n        a = [int(x) for x in t[p:p + %d]]\n        p += %d\n' "$k" "$k"
            printf '        r = %s(*a)\n        if type(r) is not int:\n            _bench_sys.exit(94)\n' "$name"
            printf '        o.append("%%d\\n" %% r)\n    _bench_sys.stdout.write("".join(o))\n\n\n_bench_main()\n' ;;
        rs)
            printf '\nfn main() {\n    use std::io::Read;\n    let mut bench_s = String::new();\n'
            printf '    std::io::stdin().read_to_string(&mut bench_s).unwrap();\n'
            printf '    let bench_t: Vec<&str> = bench_s.split_whitespace().collect();\n'
            printf '    let bench_n: usize = bench_t[3].parse().unwrap();\n    let mut bench_p = 4;\n'
            printf '    let mut bench_o = String::new();\n    for _ in 0..bench_n {\n'
            while read -r pn pt; do
                printf '        let bench_a%d: %s = bench_t[bench_p].parse().unwrap();\n        bench_p += 1;\n' "$i" "$(ty_rs "$pt")"
                args="${args:+$args, }bench_a$i"
                i=$((i + 1))
            done <<EOF
$params
EOF
            printf '        let bench_r: %s = %s(%s);\n' "$(ty_rs "$ret")" "$name" "$args"
            printf '        bench_o.push_str(&format!("{}\\n", bench_r));\n    }\n    print!("{}", bench_o);\n}\n' ;;
    esac
}

# Candidate execution has no host root, home, network or inherited environment. Only
# system runtime libraries and the single executable/source are exposed read-only;
# stdin carries inputs, and the host compares stdout against the oracle afterward.
# Never fall back to direct execution if bubblewrap cannot establish its namespaces.
sandbox() { # mode candidate_file
    sb_mode=$1
    sb_file=$2
    # Only the trusted shell can write readiness; fd 3 is closed before candidate code.
    # bwrap's fd 4 namespace JSON is useful telemetry, not proof mount/setup succeeded.
    sb_start='for fd in /proc/self/fd/*; do
        fd=${fd##*/}
        case $fd in
            0|1|2|3) ;;
            *[!0-9]*) exit 125 ;;
            *) eval "exec ${fd}>&-" || exit 125 ;;
        esac
    done
    printf "ready\n" >&3 || exit 125
    exec 3>&-
    exec "$@"'
    set -- bwrap --unshare-all --unshare-user --disable-userns --die-with-parent --new-session --clearenv --info-fd 4 \
        --setenv PATH /usr/bin --setenv LC_ALL C \
        --ro-bind /usr/lib /usr/lib --symlink usr/lib /lib \
        --dir /usr/bin --ro-bind "$(readlink -f "$(command -v bash)")" /usr/bin/sh \
        --ro-bind "$(readlink -f "$(command -v python3)")" /usr/bin/python3 --symlink usr/bin /bin \
        --proc /proc --dev /dev --tmpfs /tmp --dir /work --chdir /work \
        --ro-bind "$sb_file" /work/candidate
    # Compiler-only system headers/tools; never mount home, /tmp or the checkout.
    case $sb_mode in
        c|rs)
            set -- "$@" --ro-bind /usr/include /usr/include \
                --ro-bind /usr/libexec /usr/libexec \
                --ro-bind "$(readlink -f "$(command -v cc)")" /usr/bin/cc \
                --ro-bind "$(readlink -f "$(command -v as)")" /usr/bin/as \
                --ro-bind "$(readlink -f "$(command -v ld)")" /usr/bin/ld \
                --ro-bind "$(readlink -f "$(command -v cat)")" /usr/bin/cat ;;
    esac
    case $sb_mode in
        om)
            "$@" --ro-bind "$OMC" /compiler --remount-ro / /usr/bin/sh -c "$sb_start" ready /compiler ;;
        c)
            "$@" --remount-ro / /usr/bin/sh -c "$sb_start" ready /usr/bin/sh -c 'cc -O2 -x c -include stdint.h -include stdio.h -include stdlib.h -o /tmp/bin /work/candidate -lm && cat /tmp/bin' ;;
        py)
            "$@" --remount-ro / /usr/bin/sh -c "$sb_start" ready /usr/bin/python3 -I -c \
                'import py_compile; py_compile.compile("/work/candidate", cfile="/tmp/candidate.pyc", doraise=True)' ;;
        rs)
            sb_root=$(rustc --print sysroot) || not_run "cannot resolve Rust sysroot"
            "$@" --ro-bind "$sb_root/bin/rustc" /toolchain/bin/rustc \
                --ro-bind "$sb_root/lib" /toolchain/lib --setenv LD_LIBRARY_PATH /toolchain/lib \
                --remount-ro / /usr/bin/sh -c "$sb_start" ready /usr/bin/sh -c '/toolchain/bin/rustc --sysroot /toolchain -O --edition 2021 -A warnings --crate-name bench -o /tmp/bin /work/candidate && cat /tmp/bin' ;;
        run)
            if [ "$sb_file" = /dev/null ]; then
                "$@" --remount-ro / /usr/bin/sh -c "$sb_start" ready /usr/bin/python3 -I -c 'pass'
            elif [ "${sb_file##*.}" = py ]; then
                "$@" --remount-ro / /usr/bin/sh -c "$sb_start" ready /usr/bin/python3 -I /work/candidate
            else
                "$@" --remount-ro / /usr/bin/sh -c "$sb_start" ready /work/candidate
            fi ;;
        *) die "unknown sandbox mode" ;;
    esac
}

check_sandbox() {
    need_tool bwrap
    need_tool timeout
    need_tool python3
    # The sandbox-only shell needs numeric fd redirections above 9 (dash does not).
    need_tool bash
    # Exercise the actual namespace setup, not merely bwrap's presence in PATH.
    sandbox run /dev/null 3>/dev/null 4>/dev/null >/dev/null 2>&1 ||
        not_run "bubblewrap sandbox is unavailable; no candidate was run"
}

run_candidate() { # file command [args...]
    # GNU timeout owns a process group; bubblewrap also owns a PID namespace.
    # TERM gets a short grace period, then KILL tears down every descendant.
    timeout --kill-after=1s 10s sh "$SCRIPT_DIR/driver.sh" --sandbox run "$1"
}

# ---- evaluate one candidate: sets COMP_RC, RUN_RC and VERIFIED; the work dir holds src, err.txt,
# out.txt. A compiler refusal (COMP_RC != 0) is never verified and its output is never run (#910).
# sh has no locals: every name set here starts with ev_.

evaluate() { # lang task_dir candidate work
    ev_l=$1
    ev_t=$2
    ev_c=$3
    ev_w=$4
    rm -rf "$ev_w"
    mkdir -p "$ev_w"
    ev_s="$ev_t/spec.txt"
    COMP_RC=0
    case $ev_l in
        om)
            # om2 diag: the human "omc: line N: error K" plus an "omc-diag 1 ..." record on refusal.
            { printf 'om2 diag\n'; sed '1{/^om2\( .*\)\{0,1\}$/d;}' "$ev_c"; printf '\n'; cat "$SCRIPT_DIR/io.om"; gen_main om "$ev_s"; } > "$ev_w/src.om"
            timeout --kill-after=1s 10s sh "$SCRIPT_DIR/driver.sh" --sandbox om "$ev_w/src.om" < "$ev_w/src.om" > "$ev_w/bin" 2> "$ev_w/err.txt" 3> "$ev_w/compile.ready" 4> "$ev_w/compile.sandbox" || COMP_RC=$? ;;
        c)
            { cat "$ev_c"; gen_main c "$ev_s"; } > "$ev_w/src.c"
            timeout --kill-after=1s 10s sh "$SCRIPT_DIR/driver.sh" --sandbox c "$ev_w/src.c" > "$ev_w/bin" 2> "$ev_w/err.txt" 3> "$ev_w/compile.ready" 4> "$ev_w/compile.sandbox" || COMP_RC=$? ;;
        py)
            { cat "$ev_c"; gen_main py "$ev_s"; } > "$ev_w/src.py"
            timeout --kill-after=1s 10s sh "$SCRIPT_DIR/driver.sh" --sandbox py "$ev_w/src.py" > "$ev_w/compile.out" 2> "$ev_w/err.txt" 3> "$ev_w/compile.ready" 4> "$ev_w/compile.sandbox" || COMP_RC=$? ;;
        rs)
            { cat "$ev_c"; gen_main rs "$ev_s"; } > "$ev_w/src.rs"
            timeout --kill-after=1s 10s sh "$SCRIPT_DIR/driver.sh" --sandbox rs "$ev_w/src.rs" > "$ev_w/bin" 2> "$ev_w/err.txt" 3> "$ev_w/compile.ready" 4> "$ev_w/compile.sandbox" || COMP_RC=$? ;;
    esac
    [ "$(cat "$ev_w/compile.ready")" = ready ] ||
        not_run "bubblewrap could not start the compiler sandbox; no unsandboxed fallback"
    VERIFIED=0
    RUN_RC=-
    if [ "$COMP_RC" -eq 0 ]; then
        RUN_RC=0
        if [ "$ev_l" = py ]; then
            run_candidate "$ev_w/src.py" < "$ev_t/inputs.txt" > "$ev_w/out.txt" 2> "$ev_w/run.err" 3> "$ev_w/run.ready" 4> "$ev_w/run.sandbox" || RUN_RC=$?
        else
            chmod +x "$ev_w/bin"
            run_candidate "$ev_w/bin" < "$ev_t/inputs.txt" > "$ev_w/out.txt" 2> "$ev_w/run.err" 3> "$ev_w/run.ready" 4> "$ev_w/run.sandbox" || RUN_RC=$?
        fi
        [ "$(cat "$ev_w/run.ready")" = ready ] ||
            not_run "bubblewrap could not start the candidate sandbox; no unsandboxed fallback"
        if [ "$RUN_RC" -eq 0 ] && tail -n +3 "$ev_t/expected.txt" | cmp -s - "$ev_w/out.txt"; then
            VERIFIED=1
        fi
    fi
}

# ---- --check-tasks

task_ids() { for d in "$TASKS_DIR"/task*/; do basename "$d"; done; }

check_tasks() {
    check_sandbox
    need_lang_tools "$ALL_LANGS"
    cwork="$BUILD_DIR/check"
    rm -rf "$cwork"
    bad=0
    ntask=$(task_ids | wc -l)
    for lang in $ALL_LANGS; do
        rok=0
        mok=0
        mwrong=0
        fails=""
        for tid in $(task_ids); do
            tdir="$TASKS_DIR/$tid"
            evaluate "$lang" "$tdir" "$tdir/reference.$lang" "$cwork/$lang-$tid-ref"
            if [ "$COMP_RC" -eq 0 ] && [ "$VERIFIED" -eq 1 ]; then
                rok=$((rok + 1))
            else
                fails="$fails $tid:reference(comp_rc=$COMP_RC,run_rc=$RUN_RC)"
            fi
            evaluate "$lang" "$tdir" "$tdir/mutant.$lang" "$cwork/$lang-$tid-mut"
            if [ "$COMP_RC" -eq 0 ] && [ "$VERIFIED" -eq 0 ]; then
                mok=$((mok + 1))
                [ "$RUN_RC" != 0 ] || mwrong=$((mwrong + 1))
            else
                fails="$fails $tid:mutant(comp_rc=$COMP_RC,verified=$VERIFIED)"
            fi
        done
        say "check $lang: references $rok/$ntask verified, mutants $mok/$ntask compiled and rejected ($mwrong by wrong output, $((mok - mwrong)) by a nonzero exit)"
        if [ -n "$fails" ]; then
            say "check $lang: FAIL$fails"
            bad=1
        fi
    done
    return $bad
}

# ---- --run-model

user_prompt() { # lang task_dir
    cat "$2/statement.txt"
    printf '\n%s signature: %s\n' "$(lang_title "$1")" "$(signature "$1" "$2/spec.txt")"
    printf 'Reply with only this function (and any helper functions it needs) in one fenced code block. No main function, no input or output.\n'
}

add_msg() { # conv_dir role file
    n=$(( $(ls "$1" | wc -l) / 2 + 1 ))
    f=$(printf '%s/%02d' "$1" "$n")
    say "$2" > "$f.role"
    cat "$3" > "$f.txt"
}

build_wire() { # conv_dir
    printf 'version 1\nmodel %s\nmax_tokens %s\ntemperature %s\nmessages %d\n' \
        "$MODEL" "$MAXTOK" "$TEMP" $(( $(ls "$1" | wc -l) / 2 ))
    for f in "$1"/*.txt; do
        printf 'msg %s %d\n' "$(cat "${f%.txt}.role")" $(( $(wc -c < "$f") ))
        cat "$f"
        printf '\n'
    done
}

feedback() { # lang work -> the feedback message on stdout
    if [ "$COMP_RC" -ne 0 ]; then
        case $1 in
            om) d=$(grep -m 1 '^omc: line' "$2/err.txt" || true)
                r=$(grep -m 1 '^omc-diag 1' "$2/err.txt" || true)
                d="$d${r:+
$r}" ;;
            c) d=$(grep 'error' "$2/err.txt" | head -n 3 | sed "s|$2/||g") ;;
            rs) d=$(grep -E '^(error|  *--> )' "$2/err.txt" | head -n 6 | sed "s|$2/||g") ;;
            py) d=$(grep -E 'line [0-9]+|Error' "$2/err.txt" | head -n 3 | sed "s|$2/||g") ;;
        esac
        [ -n "$d" ] || d=$(tail -n 3 "$2/err.txt" | sed "s|$2/||g")
        printf 'Your code did not compile. Compiler output:\n%s\n' "$(printf '%s' "$d" | head -c 600)"
        if [ "$1" = om ]; then
            printf 'Line 1 of the compiled unit is the line "om2 diag" put before your code.\n'
        fi
    else
        printf 'Your code compiled but returned wrong results on hidden tests (exit status %s).\n' "$RUN_RC"
    fi
    printf 'Reply with the corrected complete function in one fenced code block.\n'
}

meta() { awk -v k="$1" '$1==k{print $2; exit}' "$2"; }

claim_output() { # output directory; a persistent marker gives one invocation ownership
    OUT=$1
    while :; do
        case $OUT in /) die "--out must be a fresh directory" ;;
            */) OUT=${OUT%/} ;; *) break ;; esac
    done
    [ ! -L "$OUT" ] || die "--out must not be a symbolic link"
    if [ -e "$OUT" ]; then
        [ -d "$OUT" ] || die "--out must be a directory"
        co_entries=$(find -H "$OUT" -mindepth 1 -maxdepth 1 -print -quit) ||
            die "--out cannot be inspected"
        [ -z "$co_entries" ] || die "--out must be empty; existing runs are never reused"
    fi
    mkdir -p "$OUT"
    [ ! -L "$OUT" ] || die "--out must not be a symbolic link"
    mkdir "$OUT/.inference-cost-owner" 2>/dev/null ||
        die "--out is already claimed; existing runs are never reused"
    co_entries=$(find -H "$OUT" -mindepth 1 -maxdepth 1 ! -name .inference-cost-owner -print -quit) ||
        die "--out cannot be inspected"
    if [ -n "$co_entries" ]; then
        rmdir "$OUT/.inference-cost-owner"
        die "--out must be empty; existing runs are never reused"
    fi
}

run_model() {
    ENDPOINT=""
    MODEL=""
    LANGS="om,c,py,rs"
    ARMS="plain,feedback"
    ATTEMPTS=3
    TEMP=0.2
    MAXTOK=4096
    OUT=""
    while [ $# -gt 0 ]; do
        case $1 in
            --endpoint) ENDPOINT=${2:-}; shift 2 ;;
            --model) MODEL=${2:-}; shift 2 ;;
            --langs) LANGS=${2:-}; shift 2 ;;
            --arms) ARMS=${2:-}; shift 2 ;;
            --attempts) ATTEMPTS=${2:-}; shift 2 ;;
            --temperature) TEMP=${2:-}; shift 2 ;;
            --max-tokens) MAXTOK=${2:-}; shift 2 ;;
            --out) OUT=${2:-}; shift 2 ;;
            *) die "unknown --run-model option '$1'" ;;
        esac
    done
    [ -n "$ENDPOINT" ] || not_run "no --endpoint given; no model was contacted"
    [ -n "$MODEL" ] || die "--model NAME is required"
    case $MODEL in *[!A-Za-z0-9._:/@+-]*) die "--model must be one word of [A-Za-z0-9._:/@+-]" ;; esac
    case $ATTEMPTS in ''|*[!0-9]*|0) die "--attempts must be a positive integer" ;; esac
    case $MAXTOK in ''|*[!0-9]*|0) die "--max-tokens must be a positive integer" ;; esac
    case $LANGS in ''|,*|*,|*,,*|*[!a-z,]*) die "--langs requires a nonempty comma-separated language list" ;; esac
    case $ARMS in ''|,*|*,|*,,*|*[!a-z,]*) die "--arms requires a nonempty comma-separated arm list" ;; esac
    langs=$(printf '%s' "$LANGS" | tr ',' ' ')
    arms=$(printf '%s' "$ARMS" | tr ',' ' ')
    for a in $arms; do case $a in plain|feedback) ;; *) die "unknown arm '$a' (plain feedback)" ;; esac; done
    need_lang_tools "$langs"
    need_tool curl
    check_sandbox
    ENDPOINT=${ENDPOINT%/}
    [ -n "$OUT" ] || OUT="$BUILD_DIR/run-$(date -u +%Y%m%dT%H%M%SZ)-$$"
    claim_output "$OUT"
    {
        say "version 1"
        say "endpoint $ENDPOINT"
        say "model_requested $MODEL"
        say "langs $langs"
        say "arms $arms"
        say "attempts $ATTEMPTS"
        say "temperature $TEMP"
        say "max_tokens $MAXTOK"
        say "omc_sha256 $(sha256sum "$OMC" | awk '{print $1}')"
        say "started $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$OUT/run.txt"
    : > "$OUT/rows.txt"
    say "# task lang arm attempt http_code model_reported prompt_tokens completion_tokens total_tokens http_ms" > "$OUT/calls.txt"
    for lang in $langs; do
        for arm in $arms; do
            for tid in $(task_ids); do
                tdir="$TASKS_DIR/$tid"
                base="$OUT/$lang/$arm/$tid"
                conv="$base/conv"
                mkdir -p "$conv"
                user_prompt "$lang" "$tdir" > "$base/user.txt"
                add_msg "$conv" system "$SCRIPT_DIR/prompts/system-$lang.txt"
                add_msg "$conv" user "$base/user.txt"
                att=1
                while [ "$att" -le "$ATTEMPTS" ]; do
                    a="$base/a$att"
                    mkdir -p "$a"
                    build_wire "$conv" > "$a/request.wire"
                    "$BIN_DIR/chat" req < "$a/request.wire" > "$a/request.json" ||
                        not_run "could not encode the request for $tid $lang $arm attempt $att (chat req rc $?)"
                    t0=$(now_ms)
                    code=$(curl -sS --max-time 600 -o "$a/response.json" -w '%{http_code}' \
                        -H 'Content-Type: application/json' -H "X-Bench-Task: $tid" -H "X-Bench-Lang: $lang" \
                        --data-binary @"$a/request.json" "$ENDPOINT/v1/chat/completions" 2> "$a/curl.err") ||
                        not_run "HTTP failure for $tid $lang $arm attempt $att: $(cat "$a/curl.err")"
                    t1=$(now_ms)
                    [ "$code" = 200 ] || not_run "HTTP $code for $tid $lang $arm attempt $att"
                    "$BIN_DIR/chat" resp < "$a/response.json" > "$a/meta.txt" 3> "$a/candidate.$lang" ||
                        not_run "unusable response for $tid $lang $arm attempt $att (chat resp rc $?): no usage, an error object or no choices"
                    p=$(meta prompt_tokens "$a/meta.txt")
                    c=$(meta completion_tokens "$a/meta.txt")
                    t=$(meta total_tokens "$a/meta.txt")
                    m=$(meta model "$a/meta.txt")
                    evaluate "$lang" "$tdir" "$a/candidate.$lang" "$a/eval"
                    t2=$(now_ms)
                    first=0
                    [ "$att" -ne 1 ] || first=1
                    say "$tid $lang $arm $att $first $COMP_RC $VERIFIED $((t2 - t0)) $p $c $t" >> "$OUT/rows.txt"
                    say "$tid $lang $arm $att $code $m $p $c $t $((t1 - t0))" >> "$OUT/calls.txt"
                    [ "$VERIFIED" -eq 0 ] || break
                    if [ "$arm" = feedback ] && [ "$att" -lt "$ATTEMPTS" ]; then
                        { printf '```%s\n' "$(lang_fence "$lang")"; cat "$a/candidate.$lang"; printf '\n```\n'; } > "$a/assistant.txt"
                        feedback "$lang" "$a/eval" > "$a/feedback.txt"
                        add_msg "$conv" assistant "$a/assistant.txt"
                        add_msg "$conv" user "$a/feedback.txt"
                    fi
                    att=$((att + 1))
                done
            done
        done
    done
    { say "version 2"; say "attempts $(wc -l < "$OUT/rows.txt")"; cat "$OUT/rows.txt"; } > "$OUT/attempts.txt"
    rm -f "$OUT/rows.txt"
    src=0
    "$BIN_DIR/scorer" < "$OUT/attempts.txt" > "$OUT/summary.txt" || src=$?
    [ "$src" -le 1 ] || die "scorer rejected $OUT/attempts.txt (rc $src)"
    say "models_reported $(awk 'NR>1{print $6}' "$OUT/calls.txt" | sort -u | tr '\n' ' ')" >> "$OUT/run.txt"
    say "finished $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$OUT/run.txt"
    say "run $OUT"
    grep '^models_reported' "$OUT/run.txt"
    report "$OUT/summary.txt"
}

report() { # summary.txt -> one line per (lang, arm)
    awk '/^group /{g=$2 " " $3} /^tasks_total /{t=$2} /^attempts_total /{a=$2}
        /^verified_count /{v=$2} /^first_attempt_verified /{f=$2} /^total_tokens /{tt=$2}
        /^tokens_per_verified /{tpv=$2} /^verified_per_1k_tokens /{k=$2} /^verified_per_minute /{m=$2}
        /^end$/{printf "%-11s verified %s/%s attempts %s first_attempt_verified %s total_tokens %s tokens_per_verified %s verified_per_1k_tokens %s verified_per_minute %s\n", g, v, t, a, f, tt, tpv, k, m}' "$1"
}

# ---- --self-test

ST_FAIL=0
st_ok() { say "ok   $*"; }
st_bad() { say "FAIL $*"; ST_FAIL=1; }
st_expect() { # label want got
    if [ "$2" = "$3" ]; then st_ok "$1 = $3"; else st_bad "$1: want $2, got $3"; fi
}

group_field() { # summary lang arm field
    awk -v g="group $2 $3" -v f="$4" '$0==g{on=1; next} on&&$1==f{print $2; exit} /^end$/{on=0}' "$1"
}

FAKE_PID=""
start_fake() { # mode tasks_dir -> sets FAKE_PORT
    "$BIN_DIR/fake_model" "$1" "$2" 1000 > "$BUILD_DIR/selftest/fake.out" 2>&1 &
    FAKE_PID=$!
    i=0
    while [ $i -lt 100 ] && ! grep -q '^PORT ' "$BUILD_DIR/selftest/fake.out"; do
        sleep 0.05
        i=$((i + 1))
    done
    FAKE_PORT=$(sed -n 's/^PORT //p' "$BUILD_DIR/selftest/fake.out")
    [ -n "$FAKE_PORT" ] || die "fake_model did not start"
}

stop_fake() {
    if [ -n "$FAKE_PID" ]; then
        kill "$FAKE_PID" 2>/dev/null || true
        wait "$FAKE_PID" 2>/dev/null || true
        FAKE_PID=""
    fi
}

fake_run() { # name mode arms attempts -> sets FR_RC, SUMMARY
    start_fake "$2" "$TASKS_DIR"
    FR_RC=0
    fr_out=$(sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint "http://127.0.0.1:$FAKE_PORT" \
        --model fake --langs om,c,py,rs --arms "$3" --attempts "$4" --out "$BUILD_DIR/selftest/$1" 2>&1) || FR_RC=$?
    stop_fake
    SUMMARY="$BUILD_DIR/selftest/$1/summary.txt"
    say "--- run $1 (fake mode $2, arms $3, attempts $4): rc $FR_RC"
    say "$fr_out"
}

self_test() {
    . "$SCRIPT_DIR/audit.sh"
    need_lang_tools "$ALL_LANGS"
    check_sandbox
    need_tool curl
    rm -rf "$BUILD_DIR/selftest"
    mkdir -p "$BUILD_DIR/selftest"
    trap stop_fake EXIT
    ntask=$(task_ids | wc -l)
    say "== scorer"
    rc=0
    printf 'version 1\nattempts 2\ntask01 1 1 0 1 10 111 37 148\ntask02 1 1 0 1 10 111 37 148\n' |
        "$BIN_DIR/scorer" > /dev/null || rc=$?
    st_expect "scorer v1 all verified rc" 0 "$rc"
    rc=0
    printf 'version 9\nattempts 0\n' | "$BIN_DIR/scorer" > /dev/null 2>&1 || rc=$?
    st_expect "scorer unknown version rc" 3 "$rc"
    rc=0
    printf 'version 1\nattempts 2\ntask01 1 1 0 1 10 111 37 148\n' | "$BIN_DIR/scorer" > /dev/null 2>&1 || rc=$?
    st_expect "scorer truncated v1 rc" 2 "$rc"
    rc=0
    printf 'version 2\nattempts 1\ntask01 java plain 1 1 0 1 10 111 37 148\n' | "$BIN_DIR/scorer" > /dev/null 2>&1 || rc=$?
    st_expect "scorer v2 unknown language rc" 2 "$rc"
    rc=0
    printf 'version 2\nattempts 1\ntask01 om plain 1 1 1 0 10 111 37 148\n' | "$BIN_DIR/scorer" > /dev/null || rc=$?
    st_expect "scorer v2 refused compile rc (FAIL)" 1 "$rc"
    audit_checks

    say "== JSON edge (chat.om)"
    rc=0
    printf '{"error":{"message":"overloaded"}}' | "$BIN_DIR/chat" resp > /dev/null 3> /dev/null || rc=$?
    st_expect "error object rc" 4 "$rc"
    rc=0
    printf '{"model":"m","choices":[{"message":{"content":"x"}}]}' | "$BIN_DIR/chat" resp > /dev/null 3> /dev/null || rc=$?
    st_expect "response without usage rc" 4 "$rc"
    rc=0
    printf 'not json' | "$BIN_DIR/chat" resp > /dev/null 3> /dev/null || rc=$?
    st_expect "non-JSON body rc" 4 "$rc"

    say "== tasks"
    if check_tasks; then st_ok "check-tasks"; else st_bad "check-tasks"; fi

    fake_run good good plain 1
    st_expect "good rc" 0 "$FR_RC"
    st_expect "good global verdict" PASS "$(summary_status "$SUMMARY")"
    for l in $ALL_LANGS; do
        st_expect "good $l plain verdict" PASS "$(group_field "$SUMMARY" "$l" plain status)"
        st_expect "good $l plain verified" "$ntask" "$(group_field "$SUMMARY" "$l" plain verified_count)"
        st_expect "good $l plain first_attempt_verified" "$ntask" "$(group_field "$SUMMARY" "$l" plain first_attempt_verified)"
        st_expect "good $l plain prompt_tokens" "$((ntask * 111))" "$(group_field "$SUMMARY" "$l" plain total_prompt_tokens)"
        st_expect "good $l plain completion_tokens" "$((ntask * 37))" "$(group_field "$SUMMARY" "$l" plain total_completion_tokens)"
        st_expect "good $l plain total_tokens" "$((ntask * 148))" "$(group_field "$SUMMARY" "$l" plain total_tokens)"
    done
    st_expect "good model reported" "fake-omega-bench" "$(awk 'NR==2{print $6}' "$BUILD_DIR/selftest/good/calls.txt")"

    fake_run bad bad plain 1
    st_expect "bad rc" 0 "$FR_RC"
    st_expect "bad global verdict" FAIL "$(summary_status "$SUMMARY")"
    for l in $ALL_LANGS; do
        st_expect "bad $l plain verdict" FAIL "$(group_field "$SUMMARY" "$l" plain status)"
        st_expect "bad $l plain compiled" "$ntask" "$(group_field "$SUMMARY" "$l" plain compiled_count)"
        st_expect "bad $l plain verified" 0 "$(group_field "$SUMMARY" "$l" plain verified_count)"
    done

    fake_run bad-then-good bad-then-good plain,feedback 2
    st_expect "bad-then-good rc" 0 "$FR_RC"
    st_expect "bad-then-good global verdict" FAIL "$(summary_status "$SUMMARY")"
    for l in $ALL_LANGS; do
        st_expect "bad-then-good $l plain verdict" FAIL "$(group_field "$SUMMARY" "$l" plain status)"
        st_expect "bad-then-good $l feedback verdict" PASS "$(group_field "$SUMMARY" "$l" feedback status)"
        st_expect "bad-then-good $l plain verified" 0 "$(group_field "$SUMMARY" "$l" plain verified_count)"
        st_expect "bad-then-good $l plain attempts" "$((ntask * 2))" "$(group_field "$SUMMARY" "$l" plain attempts_total)"
        st_expect "bad-then-good $l feedback verified" "$ntask" "$(group_field "$SUMMARY" "$l" feedback verified_count)"
        st_expect "bad-then-good $l feedback first_attempt_verified" 0 "$(group_field "$SUMMARY" "$l" feedback first_attempt_verified)"
        st_expect "bad-then-good $l feedback attempts" "$((ntask * 2))" "$(group_field "$SUMMARY" "$l" feedback attempts_total)"
        st_expect "bad-then-good $l feedback total_tokens" "$((ntask * 2 * 148))" "$(group_field "$SUMMARY" "$l" feedback total_tokens)"
        st_expect "bad-then-good $l feedback tokens_per_verified" "296.000" "$(group_field "$SUMMARY" "$l" feedback tokens_per_verified)"
    done
    st_expect "bad-then-good verified attempts are all attempt 2" "$((4 * ntask))" \
        "$(awk '$3=="feedback" && $7==1 && $4==2' "$BUILD_DIR/selftest/bad-then-good/attempts.txt" | wc -l)"

    fake_run refuse refuse plain 1
    st_expect "refuse rc" 0 "$FR_RC"
    st_expect "refuse global verdict" FAIL "$(summary_status "$SUMMARY")"
    for l in $ALL_LANGS; do
        st_expect "refuse $l plain verdict" FAIL "$(group_field "$SUMMARY" "$l" plain status)"
        st_expect "refuse $l plain compiled" 0 "$(group_field "$SUMMARY" "$l" plain compiled_count)"
        st_expect "refuse $l plain verified" 0 "$(group_field "$SUMMARY" "$l" plain verified_count)"
    done
    st_expect "refuse never ran a binary" 0 "$(find "$BUILD_DIR/selftest/refuse" -name out.txt | wc -l)"
    audit_reuse_check

    say "== NOT_RUN paths"
    start_fake good "$TASKS_DIR"
    port=$FAKE_PORT
    stop_fake
    rc=0
    out=$(sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint "http://127.0.0.1:$port" --model fake \
        --langs om --arms plain --attempts 1 --out "$BUILD_DIR/selftest/unreachable" 2>&1) || rc=$?
    say "$out"
    st_expect "unreachable endpoint rc" 3 "$rc"
    case $out in *NOT_RUN*) st_ok "unreachable endpoint prints NOT_RUN" ;; *) st_bad "unreachable endpoint did not print NOT_RUN" ;; esac
    start_fake good "$BUILD_DIR/selftest/no-such-tasks"
    rc=0
    out=$(sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint "http://127.0.0.1:$FAKE_PORT" --model fake \
        --langs om --arms plain --attempts 1 --out "$BUILD_DIR/selftest/http404" 2>&1) || rc=$?
    stop_fake
    say "$out"
    st_expect "HTTP 404 rc" 3 "$rc"
    rc=0
    out=$(sh "$SCRIPT_DIR/driver.sh" --run-model --model fake 2>&1) || rc=$?
    say "$out"
    st_expect "missing --endpoint rc" 3 "$rc"

    if [ "$ST_FAIL" -eq 0 ]; then
        say "SELF-TEST PASS"
        return 0
    fi
    say "SELF-TEST FAIL"
    return 1
}

# ---- main

[ $# -ge 1 ] || usage
mode=$1
shift
case $mode in
    --sandbox) sandbox "$@" ;;
    --output-probe) claim_output "$1" ;;
    --evaluate-probe)
        evaluate "$1" "$TASKS_DIR/task01" "$2" "$3"
        say "COMP_RC=$COMP_RC RUN_RC=$RUN_RC VERIFIED=$VERIFIED" ;;
    --deadline-probe)
        evaluate py "$TASKS_DIR/task01" "$1" "$2"
        [ "$COMP_RC" -eq 0 ] && [ "$VERIFIED" -eq 0 ] &&
            { [ "$RUN_RC" -eq 124 ] || [ "$RUN_RC" -eq 137 ]; } ;;
    --check-tasks) build_tools; check_tasks ;;
    --self-test) build_tools; self_test ;;
    --run-model)
        case " $* " in *" --endpoint "*) build_tools ;; esac
        run_model "$@" ;;
    -h|--help) usage ;;
    *) usage ;;
esac
