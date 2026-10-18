#!/bin/sh
# mutants.sh -- prove the differential runner goes red (omega#568). Run by `make test-osh-diff`.
# Usage: mutants.sh RUNNER TOYBIN FIXTURES HOST_RESULT WORKDIR
# HOST_RESULT is the --emit file of the plain run that just passed. The comparator mutants run in compare-only mode
# (--host-results), whose final line is OSH_DIFF_COMPARE_ONLY_*, never OSH_DIFF_PASS; their "native" files are forged
# from host results and are never evidence. The expectation and cleanup mutants run the real shell on one fixture each.
set -u
R=$1 TOY=$2 FX=$3 HOST=$4 W=$5
rm -rf "$W" && mkdir -p "$W" || exit 1
fails=0 n=0
portable=$(grep -c '^scope portable$' "$FX")

# expect NAME WANT_RC LAST_LINE [GREP_PATTERN] -- after the command has written $W/NAME.out
expect() {
    n=$((n + 1))
    rc=$(cat "$W/$1.rc")
    last=$(tail -n 1 "$W/$1.out")
    if [ "$rc" != "$2" ] || [ "$last" != "$3" ]; then
        echo "mutant $1: want rc=$2 last=$3, got rc=$rc last=$last"; fails=$((fails + 1)); return
    fi
    if [ $# -ge 4 ] && ! grep -q -- "$4" "$W/$1.out"; then
        echo "mutant $1: output lacks /$4/"; fails=$((fails + 1)); return
    fi
    echo "mutant $1: as required (rc=$rc, $last)"
}

cmp_only() { # NAME NATIVE_FILE [extra options]
    name=$1 nat=$2; shift 2
    "$R" "$@" --host-results "$HOST" ${nat:+--native "$nat"} "$TOY" "$FX" > "$W/$name.out" 2>&1
    echo $? > "$W/$name.rc"
}

forge() { sed -e 's/ adapter=linux-host / adapter=aienos-native /' -e 's/ aienos=lock:[^ ]* / aienos=forged-by-mutants.sh /' "$HOST"; }

# m00 positive control: a native file that agrees on every fixture gives aienos PASS on every portable fixture
forge > "$W/m00.native"
cmp_only m00 "$W/m00.native"
expect m00 0 OSH_DIFF_COMPARE_ONLY_PASS "aienos PASS $portable FAIL 0 "

# m01 one stdout digest differs (large-output fixture)
forge | awk '/^OSHDIFF vec f07_large_output /{sub(/ out=[0-9]+:[0-9a-f]/, " out=4194304:X")}1' > "$W/m01.native"
cmp_only m01 "$W/m01.native"
expect m01 1 OSH_DIFF_COMPARE_ONLY_FAIL '^f07_large_output .*FAIL.*differs from host'

# m02 one request-record digest differs (byte-exact request comparison)
forge | awk '/^OSHDIFF vec f01_two_stage /{sub(/ req=[0-9a-f]/, " req=X")}1' > "$W/m02.native"
cmp_only m02 "$W/m02.native"
expect m02 1 OSH_DIFF_COMPARE_ONLY_FAIL '^f01_two_stage .*FAIL.*differs from host'

# m03 a stage category differs (receipt category comparison)
forge | awk '/^OSHDIFF vec f09_consumer_early_exit /{sub(/P0=broken_pipe,ok/, "P0=ok,ok")}1' > "$W/m03.native"
cmp_only m03 "$W/m03.native"
expect m03 1 OSH_DIFF_COMPARE_ONLY_FAIL '^f09_consumer_early_exit .*FAIL'

# m04 truncated serial log: no end line, so every portable fixture fails (never partly green)
forge | grep -v '^OSHDIFF end ' > "$W/m04.native"
cmp_only m04 "$W/m04.native"
expect m04 1 OSH_DIFF_COMPARE_ONLY_FAIL "no end line"

# m05 one fixture missing from the native file: NOT_RUN, not PASS; acceptable without --require-native only
forge | grep -v '^OSHDIFF vec f26_partial_launch_2 ' | awk '/^OSHDIFF end n=/{split($0, a, "="); $0 = "OSHDIFF end n=" a[2] - 1}1' > "$W/m05.native"
cmp_only m05 "$W/m05.native"
expect m05 0 OSH_DIFF_COMPARE_ONLY_PASS '^f26_partial_launch_2 .*NOT_RUN .*no native line'
cmp_only m05r "$W/m05.native" --require-native
expect m05r 1 OSH_DIFF_COMPARE_ONLY_FAIL "acceptance: 1 portable fixtures have no native AIENOS result"

# m06 no native file at all, acceptance mode: NOT_RUN is never green
cmp_only m06 "" --require-native
expect m06 1 OSH_DIFF_COMPARE_ONLY_FAIL "acceptance: $portable portable fixtures"

# m07 host results passed off as native (adapter=linux-host): refused
cmp_only m07 "$HOST"
expect m07 1 OSH_DIFF_COMPARE_ONLY_FAIL "not aienos-native"

# m08 a different ABI pin
forge | sed 's/ abi=[^ ]* / abi=aien-protocols-mv@0000000 /' > "$W/m08.native"
cmp_only m08 "$W/m08.native"
expect m08 1 OSH_DIFF_COMPARE_ONLY_FAIL "abi pin"

# m09 a different compiled shell core
forge | sed 's/ core=[0-9a-f]* / core=00 /' > "$W/m09.native"
cmp_only m09 "$W/m09.native"
expect m09 1 OSH_DIFF_COMPARE_ONLY_FAIL "not the same compiled core"

# m10 a vec line for a fixture that does not exist
{ forge | grep -v '^OSHDIFF end '; echo "OSHDIFF vec f99_none req=none"; forge | grep '^OSHDIFF end '; } > "$W/m10.native"
cmp_only m10 "$W/m10.native"
expect m10 1 OSH_DIFF_COMPARE_ONLY_FAIL "unknown fixture f99_none"

# m11 a raw serial log with noise and CR line ends is accepted (positive control for the QEMU path)
{ printf 'AIENOS boot\r\n[ 0.1] console up\r\n'; forge | sed 's/$/\r/'; printf 'osh$ \r\n'; } > "$W/m11.native"
cmp_only m11 "$W/m11.native"
expect m11 0 OSH_DIFF_COMPARE_ONLY_PASS "aienos PASS $portable FAIL 0 "

# Expectation mutants: the real shell runs one fixture whose expectation was altered; the host column must fail.
one() { # NAME AWK_PROGRAM: writes a one-fixture corpus (f01 or as selected by the program) and runs it
    name=$1
    { grep '^abi ' "$FX"; awk "$2" "$FX"; } > "$W/$name.fx"
    "$R" "$TOY" "$W/$name.fx" > "$W/$name.out" 2>&1
    echo $? > "$W/$name.rc"
}
F01='/^fixture f01_two_stage$/{p=1} p{print} p&&/^end$/{p=0}'
one e00 "$F01"
expect e00 0 OSH_DIFF_PASS "host PASS 2 FAIL 0"
sed 's/^stdout "a\\nb\\n"$/stdout "a\\nc\\n"/' "$W/e00.fx" > "$W/e01.fx"; "$R" "$TOY" "$W/e01.fx" > "$W/e01.out" 2>&1; echo $? > "$W/e01.rc"
expect e01 1 OSH_DIFF_FAIL "stdout: want"
sed 's/argv="pass"/argv="trickle"/' "$W/e00.fx" > "$W/e02.fx"; "$R" "$TOY" "$W/e02.fx" > "$W/e02.out" 2>&1; echo $? > "$W/e02.rc"
expect e02 1 OSH_DIFF_FAIL "request records differ"
sed 's/^stages P0=ok,ok$/stages P0=ok,exit:1/' "$W/e00.fx" > "$W/e03.fx"; "$R" "$TOY" "$W/e03.fx" > "$W/e03.out" 2>&1; echo $? > "$W/e03.rc"
expect e03 1 OSH_DIFF_FAIL "stages: want"
sed 's/^status 0$/status 1/' "$W/e00.fx" > "$W/e04.fx"; "$R" "$TOY" "$W/e04.fx" > "$W/e04.out" 2>&1; echo $? > "$W/e04.rc"
expect e04 1 OSH_DIFF_FAIL "status: want 1, got 0"
sed 's/^err P0=OK$/err P0=BROKEN_PIPE/' "$W/e00.fx" > "$W/e05.fx"; "$R" "$TOY" "$W/e05.fx" > "$W/e05.out" 2>&1; echo $? > "$W/e05.rc"
expect e05 1 OSH_DIFF_FAIL "platform errors: want"

# Cleanup mutants: TEST-ONLY faults that break cleanup on purpose; the ABI section 12 checks must catch them.
sed 's/^scope portable$/scope portable\nfault mutant_leak_fd/' "$W/e00.fx" > "$W/c01.fx"; "$R" "$TOY" "$W/c01.fx" > "$W/c01.out" 2>&1; echo $? > "$W/c01.rc"
expect c01 1 OSH_DIFF_FAIL "descriptor leak"
sed 's/^scope portable$/scope portable\nfault mutant_leak_process/' "$W/e00.fx" > "$W/c02.fx"; "$R" "$TOY" "$W/c02.fx" > "$W/c02.out" 2>&1; echo $? > "$W/c02.rc"
expect c02 1 OSH_DIFF_FAIL "outlived the shell"

if [ "$fails" -ne 0 ]; then echo "mutants: FAIL ($fails of $n)"; exit 1; fi
echo "mutants: PASS ($n checks: every mutant red, every control green)"
