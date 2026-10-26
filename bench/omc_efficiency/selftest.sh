#!/bin/sh
# OMC-EFF conductor self-test (PROTOCOL.md, PROTOCOL-2.md): proves the conductor's checks can fail.
# No measured timing value is printed (wall, runtime and counter columns are only tested for presence;
# runtime=123 is a synthetic line). Exit 0 only when every expectation below holds.
#
#   sh bench/omc_efficiency/selftest.sh OMC SCRATCHDIR
set -u
[ $# -eq 2 ] || { echo "usage: selftest.sh OMC SCRATCHDIR" >&2; exit 64; }
OMC=$(readlink -f "$1") || exit 64
S=$2
HERE=$(cd "$(dirname "$0")" && pwd)
[ -e "$S" ] && { echo "selftest.sh: $S exists" >&2; exit 64; }
mkdir -p "$S/src" "$S/want" "$S/bin/omc" "$S/bin/cO2" "$S/bin/cO0" "$S/bin/neg" || exit 70
cd "$S" || exit 70
"$OMC" < "$HERE/conductor.om" > conductor || exit 70
chmod +x conductor
for c in fib sieve; do
    cp "$HERE/kernels/$c.om" "$HERE/kernels/$c.c" src/ && cp "$HERE/want/$c.out" want/ || exit 70
    "$OMC" < "src/$c.om" > "bin/omc/$c" || exit 70
    chmod +x "bin/omc/$c"
    env -i /usr/bin/gcc -B/usr/bin/ -O2 -static -o "bin/cO2/$c" "src/$c.c" || exit 70
    env -i /usr/bin/gcc -B/usr/bin/ -O0 -static -o "bin/cO0/$c" "src/$c.c" || exit 70
done
# the control arm of each case is the other case's program: correct bytes, wrong answer
cp bin/omc/sieve bin/neg/fib && cp bin/omc/fib bin/neg/sieve || exit 70
fails=0
expect() { # name want_rc got_rc want_summary got_summary
    if [ "$2" = "$3" ] && [ "$4" = "$5" ]; then echo "PASS $1 rc=$3 $5"
    else echo "FAIL $1 want rc=$2 '$4' got rc=$3 '$5'"; fails=$((fails + 1)); fi
}
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib sieve > ok.raw; rc=$?
expect clean_run 0 $rc "S rows=14 ok=12 fail=0 ctl_detected=2 ctl_missed=0" "$(tail -n 1 ok.raw)"
z=$(awk '$1=="P" && ($7==0 || $8==0)' ok.raw | wc -l)
expect counters_nonzero 0 0 "zero_rows=0" "zero_rows=$z"
z=$(awk '$1=="T" && $5!="neg" && (NF!=10 || $9!=1 || $10==0)' ok.raw | wc -l)
expect runtime_present 0 0 "bad_T_rows=0" "bad_T_rows=$z"
cp want/fib.out want.keep && printf '39088168\n' > want/fib.out
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib > nc1.raw; rc=$?
expect wrong_want 1 $rc "S rows=7 ok=0 fail=6 ctl_detected=1 ctl_missed=0" "$(tail -n 1 nc1.raw)"
cp want.keep want/fib.out && cp bin/omc/fib bin/neg/fib
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib > nc2.raw; rc=$?
expect control_matches 1 $rc "S rows=7 ok=6 fail=0 ctl_detected=0 ctl_missed=1" "$(tail -n 1 nc2.raw)"
cp bin/omc/sieve bin/neg/fib && cp bin/cO2/fib cO2.keep && printf 'x' >> bin/cO2/fib
./conductor build 1 0 9 "$OMC" 3 omc cO2 cO0 fib > nc3.raw; rc=$?
expect wrong_reference 1 $rc "S rows=3 ok=2 fail=1 ctl_detected=0 ctl_missed=0" "$(tail -n 1 nc3.raw)"
cp cO2.keep bin/cO2/fib
./conductor build 1 0 9 "$OMC" 3 omc cO2 cO0 fib > b.raw; rc=$?
expect clean_build 0 $rc "S rows=3 ok=3 fail=0 ctl_detected=0 ctl_missed=0" "$(tail -n 1 b.raw)"
# OMC-EFF-2: the in-process runtime line on stderr is required, and read exactly
cp bin/cO2/fib cO2.keep
printf '#!/bin/sh\nprintf "39088169\\n"\n' > bin/cO2/fib && chmod +x bin/cO2/fib
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib > nc4.raw; rc=$?
expect no_runtime_line 1 $rc "S rows=7 ok=5 fail=1 ctl_detected=1 ctl_missed=0" "$(tail -n 1 nc4.raw)"
printf '#!/bin/sh\nprintf "39088169\\n"\nprintf "12x\\n" >&2\n' > bin/cO2/fib
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib > nc5.raw; rc=$?
expect bad_runtime_line 1 $rc "S rows=7 ok=5 fail=1 ctl_detected=1 ctl_missed=0" "$(tail -n 1 nc5.raw)"
printf '#!/bin/sh\nprintf "39088169\\n"\nprintf "123\\n" >&2\n' > bin/cO2/fib
./conductor run 1 0 7 1 4 omc cO2 cO0 neg fib > ok2.raw; rc=$?
v=$(awk '$1=="T" && $5=="cO2" {print $10}' ok2.raw)
expect runtime_read_exactly 0 $rc "S rows=7 ok=6 fail=0 ctl_detected=1 ctl_missed=0 runtime=123" "$(tail -n 1 ok2.raw) runtime=$v"
cp cO2.keep bin/cO2/fib
./conductor run 1 0 > /dev/null; rc=$?
expect usage 2 $rc "" ""
./conductor run 1 0 0 1 4 omc cO2 cO0 neg fib > /dev/null; rc=$?
expect seed_zero 2 $rc "" ""
echo "SELFTEST fails=$fails"
[ $fails -eq 0 ]
