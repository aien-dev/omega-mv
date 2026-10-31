# SPDX-License-Identifier: Apache-2.0
# Sourced only by driver.sh --self-test; all candidates below are disposable fixtures.
summary_status() { awk '$1=="status"{v=$2} END{print v}' "$1"; }

scorer_probe() { # label expected_rc version-2 wire; challenge both wire versions
    sp_label=$1 sp_want=$2 sp_wire=$3
    for sp_version in 1 2; do
        sp_rc=0
        if [ "$sp_version" -eq 1 ]; then
            printf '%s\n' "$sp_wire" | awk 'NR==1{$2=1} NR>2{$2=""; $3=""} {print}' |
                "$BIN_DIR/scorer" > "$BUILD_DIR/selftest/probe.txt" 2>/dev/null || sp_rc=$?
        else
            printf '%s\n' "$sp_wire" | "$BIN_DIR/scorer" > "$BUILD_DIR/selftest/probe.txt" 2>/dev/null || sp_rc=$?
        fi
        st_expect "$sp_label v$sp_version rc" "$sp_want" "$sp_rc"
    done
}

audit_checks() {
    say '== audit regressions'
    scorer_probe 'trailing row' 2 'version 2
attempts 1
task01 om plain 1 1 0 1 10 111 37 148
task02 om plain 1 1 0 0 10 111 37 148'
    scorer_probe 'refused compile marked verified' 2 'version 2
attempts 1
task01 om plain 1 1 1 1 10 111 37 148'
    scorer_probe 'duplicate verified task' 2 'version 2
attempts 3
task01 om feedback 1 1 0 1 10 111 37 148
task01 om feedback 2 0 0 1 10 111 37 148
task02 om feedback 1 1 0 0 10 111 37 148'
    scorer_probe 'nonboolean verified flag' 2 'version 2
attempts 1
task01 om plain 1 1 0 2 10 111 37 148'
    scorer_probe 'nonboolean first flag' 2 'version 2
attempts 1
task01 om plain 1 2 0 0 10 111 37 148'
    scorer_probe 'skipped attempt' 2 'version 2
attempts 1
task01 om plain 2 0 0 0 10 111 37 148'
    scorer_probe 'distinct interleaved tasks' 1 'version 2
attempts 3
task01 om feedback 1 1 0 0 10 111 37 148
task02 om feedback 1 1 0 0 10 111 37 148
task01 om feedback 2 0 0 1 10 111 37 148'
    st_expect 'distinct tasks counted' 2 "$(meta tasks_total "$BUILD_DIR/selftest/probe.txt")"
    st_expect 'distinct verified counted' 1 "$(meta verified_count "$BUILD_DIR/selftest/probe.txt")"

    ac_dir="$BUILD_DIR/selftest/security"
    mkdir -p "$ac_dir"
    cat > "$ac_dir/attack.py" <<EOF
from pathlib import Path
import sys
try:
    Path("$ac_dir/host-marker").write_text("candidate wrote host file")
except OSError:
    print("host write denied", file=sys.stderr)
sys.stdout.write("".join(Path("$TASKS_DIR/task01/expected.txt").read_text().splitlines(True)[2:]))
sys.exit(0)
EOF
    evaluate py "$TASKS_DIR/task01" "$ac_dir/attack.py" "$ac_dir/attack"
    st_expect 'oracle attack compiled' 0 "$COMP_RC"
    st_expect 'oracle attack unverified' 0 "$VERIFIED"
    if [ ! -e "$ac_dir/host-marker" ] && grep -q 'expected.txt' "$ac_dir/attack/run.err"; then
        st_ok 'sandbox denies host write and oracle read'
    else
        st_bad 'sandbox allowed host write or did not exercise oracle read'
    fi
    evaluate py "$TASKS_DIR/task01" "$TASKS_DIR/task01/reference.py" "$ac_dir/reference"
    st_expect 'sandboxed candidate verified' 1 "$VERIFIED"
    say 'sandboxed candidate output:'
    cat "$ac_dir/reference/out.txt"
    # Compile-time oracle reads must fail too (neither runtime-only isolation nor
    # Python -I prevents C/Rust's compile-time file reads).
    {
        printf '#if __has_include("%s")\n#error ORACLE_VISIBLE\n#endif\n' "$TASKS_DIR/task01/expected.txt"
        cat "$TASKS_DIR/task01/reference.c"
    } > "$ac_dir/include.c"
    evaluate c "$TASKS_DIR/task01" "$ac_dir/include.c" "$ac_dir/c-include"
    st_expect 'C compiler cannot see oracle' 0 "$COMP_RC"
    st_expect 'C oracle-invisibility control verified' 1 "$VERIFIED"
    printf 'const ORACLE: &str = include_str!("%s");\n' "$TASKS_DIR/task01/expected.txt" > "$ac_dir/include.rs"
    evaluate rs "$TASKS_DIR/task01" "$ac_dir/include.rs" "$ac_dir/rs-include"
    st_expect 'Rust compile-time oracle unverified' 0 "$VERIFIED"
    if [ "$COMP_RC" -ne 0 ] && grep -q 'No such file or directory' "$ac_dir/rs-include/err.txt"; then
        st_ok 'Rust compiler cannot read oracle'
    else
        st_bad 'Rust oracle include did not fail at the filesystem boundary'
    fi
    # bwrap emits namespace JSON before mount setup; only an in-sandbox handshake
    # distinguishes setup failure from a genuine compiler/candidate exit status.
    mkdir -p "$ac_dir/setup-tools" "$ac_dir/empty-sysroot"
    cat > "$ac_dir/setup-tools/rustc" <<EOF
#!/bin/sh
printf '%s\n' "$ac_dir/empty-sysroot"
EOF
    chmod +x "$ac_dir/setup-tools/rustc"
    ac_rc=0
    ac_out=$(PATH="$ac_dir/setup-tools:$PATH" sh "$SCRIPT_DIR/driver.sh" --evaluate-probe rs \
        "$TASKS_DIR/task01/reference.rs" "$ac_dir/compile-setup" 2>&1) || ac_rc=$?
    st_expect 'compiler mount setup failure NOT_RUN' 3 "$ac_rc"
    if [ -s "$ac_dir/compile-setup/compile.sandbox" ] && [ ! -s "$ac_dir/compile-setup/compile.ready" ]; then
        st_ok 'namespace JSON is not compiler readiness'
    else
        st_bad 'compiler setup negative control did not separate JSON and readiness'
    fi
    ac_bwrap=$(command -v bwrap)
    cat > "$ac_dir/setup-tools/bwrap" <<EOF
#!/bin/sh
for arg do :; done
if [ "\$arg" = /work/candidate ]; then
    exec "$ac_bwrap" --ro-bind "$ac_dir/nonexistent-mount" /unavailable "\$@"
fi
exec "$ac_bwrap" "\$@"
EOF
    chmod +x "$ac_dir/setup-tools/bwrap"
    ac_rc=0
    ac_out=$(PATH="$ac_dir/setup-tools:$PATH" sh "$SCRIPT_DIR/driver.sh" --evaluate-probe py \
        "$TASKS_DIR/task01/reference.py" "$ac_dir/run-setup" 2>&1) || ac_rc=$?
    st_expect 'runtime mount setup failure NOT_RUN' 3 "$ac_rc"
    if [ -s "$ac_dir/run-setup/run.sandbox" ] && [ ! -s "$ac_dir/run-setup/run.ready" ]; then
        st_ok 'namespace JSON is not candidate readiness'
    else
        st_bad 'runtime setup negative control did not separate JSON and readiness'
    fi
    {
        printf 'import os\ntry:\n    os.write(3, b"forged")\nexcept OSError:\n    pass\nelse:\n    raise RuntimeError("readiness fd leaked")\n'
        cat "$TASKS_DIR/task01/reference.py"
    } > "$ac_dir/readiness-fd.py"
    evaluate py "$TASKS_DIR/task01" "$ac_dir/readiness-fd.py" "$ac_dir/readiness-fd"
    st_expect 'candidate cannot inherit readiness fd' 1 "$VERIFIED"
    # A caller may hold host files on arbitrary inheritable descriptors. Both
    # compiler includes and runtime /proc/self/fd reads must lose that access.
    {
        printf 'import os\ntry:\n    os.read(99, 1)\nexcept OSError:\n    pass\nelse:\n    raise RuntimeError("inherited host descriptor leaked")\n'
        cat "$TASKS_DIR/task01/reference.py"
    } > "$ac_dir/inherited-fd.py"
    ac_rc=0
    ac_out=$(bash -c 'exec 99< "$1"; shift; exec "$@"' _ "$TASKS_DIR/task01/expected.txt" \
        sh "$SCRIPT_DIR/driver.sh" --evaluate-probe py "$ac_dir/inherited-fd.py" "$ac_dir/inherited-py" 2>&1) || ac_rc=$?
    st_expect 'inherited descriptor runtime control rc' 0 "$ac_rc"
    case $ac_out in *'COMP_RC=0 RUN_RC=0 VERIFIED=1'*) st_ok 'runtime cannot read inherited host descriptor' ;;
        *) st_bad 'runtime inherited descriptor was not closed' ;; esac
    {
        printf 'const SECRET: &str = include_str!("/proc/self/fd/99");\n'
        cat "$TASKS_DIR/task01/reference.rs"
    } > "$ac_dir/inherited-fd.rs"
    ac_rc=0
    ac_out=$(bash -c 'exec 99< "$1"; shift; exec "$@"' _ "$TASKS_DIR/task01/expected.txt" \
        sh "$SCRIPT_DIR/driver.sh" --evaluate-probe rs "$ac_dir/inherited-fd.rs" "$ac_dir/inherited-rs" 2>&1) || ac_rc=$?
    st_expect 'inherited descriptor compiler control rc' 0 "$ac_rc"
    if grep -q 'No such file or directory' "$ac_dir/inherited-rs/err.txt"; then
        st_ok 'compiler cannot read inherited host descriptor'
    else
        st_bad 'compiler inherited descriptor was not closed'
    fi

    cat > "$ac_dir/ignore-term.py" <<'EOF'
import os
import signal
import time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
if os.fork() == 0:
    while True:
        time.sleep(1)
def clamp_add_u16(a, b):
    while True:
        time.sleep(1)
EOF
    ac_rc=0
    timeout --kill-after=1s 13s sh "$SCRIPT_DIR/driver.sh" --deadline-probe \
        "$ac_dir/ignore-term.py" "$ac_dir/deadline" || ac_rc=$?
    st_expect 'TERM-ignoring process group deadline' 0 "$ac_rc"
    ac_pid=$(awk '/"child-pid"/{gsub(/[^0-9]/, ""); print}' "$ac_dir/deadline/run.sandbox")
    if [ -n "$ac_pid" ] && ! kill -0 "$ac_pid" 2>/dev/null; then
        st_ok 'timed-out PID namespace is gone'
    else
        st_bad 'timed-out sandbox process is still alive'
    fi

    mkdir -p "$ac_dir/tools"
    printf '#!/bin/sh\nexit 1\n' > "$ac_dir/tools/bwrap"
    chmod +x "$ac_dir/tools/bwrap"
    ac_rc=0
    ac_out=$(PATH="$ac_dir/tools:$PATH" sh "$SCRIPT_DIR/driver.sh" --run-model \
        --endpoint http://127.0.0.1:1 --model fake --langs py --arms plain \
        --out "$ac_dir/unavailable" 2>&1) || ac_rc=$?
    st_expect 'unavailable sandbox NOT_RUN' 3 "$ac_rc"
    case $ac_out in *'bubblewrap sandbox is unavailable'*) st_ok 'sandbox failure is closed' ;;
        *) st_bad 'sandbox failure did not stop at sandbox preflight' ;; esac
    st_expect 'sandbox failure creates no run' no "$([ -e "$ac_dir/unavailable" ] && say yes || say no)"
    for ac_option in --langs --arms; do
        for ac_value in '' ','; do
            ac_rc=0
            sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint http://127.0.0.1:1 \
                --model fake "$ac_option" "$ac_value" --out "$ac_dir/empty" >/dev/null 2>&1 || ac_rc=$?
            st_expect "empty $ac_option '$ac_value' rejected" 2 "$ac_rc"
        done
    done
    mkdir "$ac_dir/empty-output"
    ac_rc=0
    sh "$SCRIPT_DIR/driver.sh" --output-probe "$ac_dir/empty-output" || ac_rc=$?
    st_expect 'existing empty output can be claimed' 0 "$ac_rc"
    mkdir "$ac_dir/link-target"
    ln -s "$ac_dir/link-target" "$ac_dir/output-link"
    for ac_link in "$ac_dir/output-link" "$ac_dir/output-link/"; do
        ac_rc=0
        sh "$SCRIPT_DIR/driver.sh" --output-probe "$ac_link" >/dev/null 2>&1 || ac_rc=$?
        st_expect 'symlink output rejected, including trailing slash' 2 "$ac_rc"
    done
    st_expect 'symlink target left empty' '' "$(find "$ac_dir/link-target" -mindepth 1 -print -quit)"
    # Force both processes past the initial emptiness check before either tries
    # mkdir on the ownership marker; this exercises the actual acquisition race.
    mkdir -p "$ac_dir/claim-tools" "$ac_dir/claim-barrier"
    ac_mkdir=$(command -v mkdir)
    cat > "$ac_dir/claim-tools/mkdir" <<EOF
#!/bin/sh
for arg do :; done
if [ "\$arg" = "$ac_dir/raced-output/.inference-cost-owner" ]; then
    : > "$ac_dir/claim-barrier/\$\$"
    while [ "\$(find "$ac_dir/claim-barrier" -type f | wc -l)" -lt 2 ]; do
        sleep 0.01
    done
fi
exec "$ac_mkdir" "\$@"
EOF
    chmod +x "$ac_dir/claim-tools/mkdir"
    (
        ac_rc=0
        PATH="$ac_dir/claim-tools:$PATH" timeout --kill-after=1s 5s sh "$SCRIPT_DIR/driver.sh" \
            --output-probe "$ac_dir/raced-output" >"$ac_dir/race1.log" 2>&1 || ac_rc=$?
        say "$ac_rc" > "$ac_dir/race1.rc"
    ) &
    ac_pid1=$!
    (
        ac_rc=0
        PATH="$ac_dir/claim-tools:$PATH" timeout --kill-after=1s 5s sh "$SCRIPT_DIR/driver.sh" \
            --output-probe "$ac_dir/raced-output" >"$ac_dir/race2.log" 2>&1 || ac_rc=$?
        say "$ac_rc" > "$ac_dir/race2.rc"
    ) &
    ac_pid2=$!
    wait "$ac_pid1"
    wait "$ac_pid2"
    case $(cat "$ac_dir/race1.rc"):$(cat "$ac_dir/race2.rc") in
        0:2|2:0) st_ok 'only one concurrent output claimant succeeds' ;;
        *) st_bad 'concurrent output claim did not have exactly one owner' ;;
    esac
}

audit_reuse_check() {
    find "$BUILD_DIR/selftest/good" -type f -exec sha256sum {} \; | sort > "$BUILD_DIR/selftest/before.sha256"
    ac_rc=0
    ac_out=$(sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint http://127.0.0.1:1 --model fake \
        --langs py --arms feedback --out "$BUILD_DIR/selftest/good" 2>&1) || ac_rc=$?
    st_expect 'nonempty output rejected' 2 "$ac_rc"
    case $ac_out in *'--out must be empty'*) st_ok 'output refused before HTTP' ;; *) st_bad 'wrong output refusal' ;; esac
    ln -s "$BUILD_DIR/selftest/good" "$BUILD_DIR/selftest/good-link"
    for ac_link in "$BUILD_DIR/selftest/good-link" "$BUILD_DIR/selftest/good-link/"; do
        ac_rc=0
        sh "$SCRIPT_DIR/driver.sh" --run-model --endpoint http://127.0.0.1:1 --model fake \
            --langs py --arms feedback --out "$ac_link" >/dev/null 2>&1 || ac_rc=$?
        st_expect 'run-model refuses symlink to completed run' 2 "$ac_rc"
    done
    find "$BUILD_DIR/selftest/good" -type f -exec sha256sum {} \; | sort > "$BUILD_DIR/selftest/after.sha256"
    if cmp -s "$BUILD_DIR/selftest/before.sha256" "$BUILD_DIR/selftest/after.sha256"; then
        st_ok 'reused output remains byte-identical'
    else
        st_bad 'reused output changed'
    fi
}
