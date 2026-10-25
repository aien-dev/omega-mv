#!/bin/sh
# Host output-contract regression. Pass a freshly compiled blake3.om binary.
# Does not compile, access the GPU, or change the live server.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: test-output.sh /path/to/blake3' >&2; exit 2; }
B=$1
[ -x "$B" ] || { echo 'FAIL: binary is not executable' >&2; exit 1; }
[ -c /dev/full ] || { echo 'NOT_RUN: /dev/full unavailable' >&2; exit 77; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
"$B" </dev/null >"$T/out" || bad 'empty-input control failed'
printf '%s\n' af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262 >"$T/want"
cmp "$T/want" "$T/out" || bad 'empty-input digest or newline differs'
rc=0
"$B" </dev/null >/dev/full || rc=$?
[ "$rc" -eq 74 ] || bad "/dev/full returned $rc, wanted 74"
rc=0
"$B" </dev/null 1>&- || rc=$?
[ "$rc" -eq 74 ] || bad "closed stdout returned $rc, wanted 74"
printf '%s\n' 'PASS: empty digest, /dev/full and closed stdout'
