#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Host-only (no GPU) check that M18 clean-clone log selection never uses the
# fixed host path, including when that directory is not writable and when
# TMPDIR is set. A mutant that puts the fixed path back fails
# M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
cflags="-std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -I$root/src"
src=$root/src/omega_clean_clone.c
testc=$root/tests/m18_clean_clone_log_test.c
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/m18-clean-clone-log.XXXXXX")
trap 'chmod -R u+w "$work" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM

fail=0
say() { printf '%s\n' "$1"; }

# The implementation sources must not carry the old fixed paths back.
if grep -n -e '/tmp/clean_clone_m18.log' -e '/tmp/omega_clean_m18' \
    "$root/src/omega_clean_clone.c" "$root/src/omega_clean_clone.h" \
    "$root/src/omega_blackwell_gates.c"
then
    say "FAIL M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP"
    fail=1
else
    say "PASS M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP source"
fi

"$cc" $cflags -o "$work/good" "$testc" "$src"
set +e
"$work/good" "$work/run1" >"$work/good.out"
good_rc=$?
set -e
if [ "$good_rc" -ne 0 ] || grep -q '^FAIL ' "$work/good.out"; then
    say "good run printed a failure:"
    cat "$work/good.out"
    fail=1
else
    say "PASS good selection"
fi
for name in \
    M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP \
    M18_CLEAN_CLONE_LOG_UNDER_TMPDIR \
    M18_CLEAN_CLONE_LOG_UNDER_OUT_DIR \
    M18_CLEAN_CLONE_LOG_UNDER_OUT_DIR_INSIDE_REPO \
    M18_CLEAN_CLONE_LOG_CREATE_FAILED_DISTINCT \
    M18_CLEAN_CLONE_COMMAND_USES_LOG \
    M18_CLEAN_CLONE_LOG_KEPT \
    M18_CLEAN_CLONE_LOG_PATH_PRINTED
do
    if grep -q "^PASS $name$" "$work/good.out"; then
        say "PASS saw $name"
    else
        say "FAIL missing $name"
        fail=1
    fi
done

# /tmp not writable, TMPDIR set to a directory on this workspace.
ro_out=$work/ro.out
set +e
unshare --user --map-root-user --mount sh -c '
        mount -t tmpfs -o ro,nosuid,nodev,mode=0555 none /tmp || exit 77
        if touch /tmp/m18-ro-probe 2>/dev/null; then exit 78; fi
        export M18_REQUIRE_TMP_UNWRITABLE=1
        exec "$1" "$2"
    ' sh "$work/good" "$work/run-ro" >"$ro_out"
ro_rc=$?
set -e
if [ "$ro_rc" -eq 0 ] && grep -q '^PASS M18_CLEAN_CLONE_LOG_TMP_UNWRITABLE_STILL_SELECTS$' "$ro_out" \
    && ! grep -q '^FAIL ' "$ro_out"
then
    say "PASS /tmp unwritable with TMPDIR set"
else
    say "FAIL /tmp unwritable with TMPDIR set (rc $ro_rc)"
    cat "$ro_out" || true
    fail=1
fi

# Mutant: restore the fixed log path. It must fail the named assertion and
# must not be able to create that file (this run also keeps /tmp read-only).
awk '
/snprintf\(log_path, log_cap, "%s\/clean_clone_m18.log", dir\)/ {
    print "    int n = snprintf(log_path, log_cap, \"%s\", \"/tmp/clean_clone_m18.log\"); (void)dir;"
    replaced++
    next
}
{ print }
END { if (replaced != 1) exit 2 }
' "$src" >"$work/mutant.c"
"$cc" $cflags -o "$work/mutant" "$testc" "$work/mutant.c"
set +e
unshare --user --map-root-user --mount sh -c '
        mount -t tmpfs -o ro,nosuid,nodev,mode=0555 none /tmp || exit 77
        if touch /tmp/m18-ro-probe 2>/dev/null; then exit 78; fi
        export M18_REQUIRE_TMP_UNWRITABLE=1
        exec "$1" "$2"
    ' sh "$work/mutant" "$work/run-mut" >"$work/mut.out"
mut_rc=$?
set -e
if [ "$mut_rc" -ne 0 ] \
    && grep -q '^FAIL M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP$' "$work/mut.out" \
    && ! grep -q '^PASS M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP$' "$work/mut.out"
then
    say "PASS mutant M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP"
else
    say "FAIL mutant did not fail M18_CLEAN_CLONE_LOG_NOT_FIXED_TMP (rc $mut_rc)"
    cat "$work/mut.out" || true
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    say "m18_clean_clone_log_test: PASS"
    exit 0
fi
say "m18_clean_clone_log_test: FAIL"
exit 1
