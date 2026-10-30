#!/bin/sh
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

# Differential of the seeded-Omega checker2 port (seed/oscgpu/c2*.om, built by om_build.sh) against the
# C checker2 (the oracle): for every case both run `checker2 check ...` and stdout, stderr and the exit
# code must be byte-identical. Cases:
#   corpus     every .ir under the IR directories (default: the checker2 differential corpus with its
#              kneg kernel negatives, and the osc_gpu kernels) and the owner-receipt fixture a01.ir
#   flip       per corpus file: every byte of the first 64, then 64 positions spread over the rest,
#              changed by xor 1 (even positions) or xor 255 (odd positions)
#   trunc      per corpus file: lengths 0..7, then 8 lengths spread up to size - 1; one byte appended
#   path       missing file, directory, empty path, path over PATH_MAX, component over NAME_MAX, file
#              used as a directory, symlink loop, unreadable file (skipped as root), /dev/null, a unit
#              padded past the 16 MiB the port keeps in memory, and files of 2^28 - 1 and 2^28 bytes
#              (the size where the C's slurp gives up)
#   argv       no arguments, `check` alone, extra argument, misspelt and empty commands
# `version` and `receipt ...` are not ported (the port prints the usage line) and are not compared.
# With --mutant phase-apart the port is built without its GUARD:phase-apart test; the run must then
# report differences (negative control). Logs: <work>/omdiff.log, one line per case, both outputs on
# every difference. The last line is OMDIFF PASS cases=N differ=0, or OMDIFF FAIL ... and exit 1.
# Usage: sh tests/checker2/om_diff.sh [--mutant phase-apart] [c-checker2] [work-dir] [ir-dir ...]
set -u
MUT=""
if [ "${1:-}" = --mutant ]; then
    MUT=$2
    shift 2
fi
C2=${1:-build/checker2/checker2}
W=${2:-build/checker2/om${MUT:+-mutant}}
[ $# -ge 2 ] && shift 2 || shift $#
IRDIRS=${*:-build/checker2/differential/ir build/osc_gpu/ir}
FIX=tests/checker2/fixtures/owner-receipt/a01.ir

[ -x "$C2" ] || { echo "om_diff: no C checker2 at $C2 (make checker2)"; exit 2; }
mkdir -p "$W" || exit 2
OM=$W/c2om
if [ -n "$MUT" ]; then
    sh tests/checker2/om_build.sh "$OM" --mutant "$MUT" || exit 2
else
    sh tests/checker2/om_build.sh "$OM" || exit 2
fi
LOG=$W/omdiff.log
T=$W/tmp
mkdir -p "$T" || exit 2
: >"$LOG"
cases=0 differ=0

# one_case <label> <args...>: runs both checkers with the same argument vector
one_case() {
    label=$1
    shift
    { "$C2" "$@" 2>"$T/ce"; echo "rc=$?"; } >"$T/co"
    { "$OM" "$@" 2>"$T/oe"; echo "rc=$?"; } >"$T/oo"
    cases=$((cases + 1))
    if cmp -s "$T/co" "$T/oo" && cmp -s "$T/ce" "$T/oe"; then
        echo "SAME $label" >>"$LOG"
    else
        differ=$((differ + 1))
        {
            echo "DIFF $label"
            echo "  C  stdout: $(cat "$T/co")"
            echo "  C  stderr: $(cat "$T/ce")"
            echo "  OM stdout: $(cat "$T/oo")"
            echo "  OM stderr: $(cat "$T/oe")"
        } >>"$LOG"
    fi
}

# flip <file> <pos>: the file with byte pos changed into $T/m.ir
flip() {
    v=$(od -An -tu1 -j"$2" -N1 "$1" | tr -d ' ')
    if [ $(($2 % 2)) = 0 ]; then x=1; else x=255; fi
    {
        head -c "$2" "$1"
        # shellcheck disable=SC2059
        printf "\\$(printf %03o $((v ^ x)))"
        tail -c +$(($2 + 2)) "$1"
    } >"$T/m.ir"
}

files=$( { find $IRDIRS -name '*.ir' -type f; echo "$FIX"; } | sort)
for f in $files; do
    one_case "corpus $f" check "$f"
    size=$(wc -c <"$f")
    # flips: every one of the first 64 bytes, then 64 spread positions
    p=0
    while [ $p -lt "$size" ] && [ $p -lt 64 ]; do
        flip "$f" $p
        one_case "flip $f@$p" check "$T/m.ir"
        p=$((p + 1))
    done
    if [ "$size" -gt 64 ]; then
        k=0
        while [ $k -lt 64 ]; do
            p=$((64 + k * (size - 64) / 64))
            flip "$f" $p
            one_case "flip $f@$p" check "$T/m.ir"
            k=$((k + 1))
        done
    fi
    # truncations and one trailing byte
    for n in 0 1 2 3 4 5 6 7; do
        [ $n -lt "$size" ] || continue
        head -c $n "$f" >"$T/m.ir"
        one_case "trunc $f:$n" check "$T/m.ir"
    done
    k=1
    while [ $k -le 8 ]; do
        n=$((8 + k * (size - 9) / 8))
        if [ $n -lt "$size" ] && [ $n -ge 8 ]; then
            head -c $n "$f" >"$T/m.ir"
            one_case "trunc $f:$n" check "$T/m.ir"
        fi
        k=$((k + 1))
    done
    { cat "$f"; printf '\000'; } >"$T/m.ir"
    one_case "trail $f" check "$T/m.ir"
done

# paths
one_case "path missing" check "$T/no-such-file.ir"
one_case "path directory" check "$T"
one_case "path empty" check ""
long=$(printf "%05000d" 0 | tr 0 a)
one_case "path over PATH_MAX" check "$T/$long"
comp=$(printf "%0300d" 0 | tr 0 b)
one_case "path component over NAME_MAX" check "$T/$comp"
one_case "path not a directory" check "$FIX/x.ir"
rm -f "$T/loop"
ln -s loop "$T/loop"
one_case "path symlink loop" check "$T/loop"
if [ "$(id -u)" != 0 ]; then
    cp "$FIX" "$T/noread.ir" && chmod 000 "$T/noread.ir"
    one_case "path unreadable" check "$T/noread.ir"
    rm -f "$T/noread.ir"
fi
one_case "path /dev/null" check /dev/null
cp "$FIX" "$T/big.ir" && truncate -s 16777217 "$T/big.ir"
one_case "path 16 MiB + 1 (trailing zeros)" check "$T/big.ir"
truncate -s 0 "$T/big.ir" && truncate -s 268435455 "$T/big.ir"
one_case "path 2^28 - 1 zeros" check "$T/big.ir"
truncate -s 268435456 "$T/big.ir"
one_case "path 2^28 zeros" check "$T/big.ir"
rm -f "$T/big.ir" "$T/m.ir" "$T/loop"

# argument vectors
one_case "argv none"
one_case "argv check alone" check
one_case "argv extra argument" check "$FIX" extra
one_case "argv misspelt chec" chec "$FIX"
one_case "argv misspelt checkx" checkx "$FIX"
one_case "argv upper CHECK" CHECK "$FIX"
one_case "argv empty command" "" "$FIX"

if [ $differ = 0 ] && [ $cases -gt 0 ]; then
    line="OMDIFF PASS cases=$cases differ=0"
    rc=0
else
    line="OMDIFF FAIL cases=$cases differ=$differ${MUT:+ (mutant $MUT)}"
    rc=1
fi
grep '^DIFF' "$LOG" | head -20
echo "om_diff: log $LOG"
echo "$line" | tee -a "$LOG"
exit $rc
