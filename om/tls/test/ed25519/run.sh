#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/ed25519.om. Usage: sh om/tls/test/ed25519/run.sh PATH_TO_PINNED_OMC
# Builds om2 + sha2.om + x25519.om + ed25519.om + driver.om into build/tls/ed25519/, runs the RFC 8032
# section 7.1, Wycheproof and edge vector files (vectors/, written by gen_vectors.py), a negative
# control, an expectation mutant and eight library mutants (each must be detected: exit 1 with
# MISMATCH lines).
# Exit 0 only when every check passes; the final line is
#   ED25519 PASS vectors=N (rfc8032=R, wycheproof=W) edge=E mutants=M/M   or   ED25519 FAIL ...
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
lib="$root/om/tls/ed25519.om"
out="$root/build/tls/ed25519"
rm -rf "$out"
mkdir -p "$out"
fail=0
vectors=0
rfc=0
wyche=0
edge=0
mut=0
mutok=0
nice="nice -n 19 ionice -c3"

build() { # build SRC_ED25519 OUT_BINARY
	{ echo om2; cat "$root/om/tls/sha2.om" "$root/om/tls/x25519.om" "$1" "$here/driver.om"; } > "$2.unit.om"
	if ! $nice "$omc" < "$2.unit.om" > "$2" 2> "$2.err" || [ -s "$2.err" ]; then
		echo "FAIL build $2: $(cat "$2.err")"
		rm -f "$2"
		return 1
	fi
	chmod +x "$2"
}

# calls LOG: the checked-call count from the driver's summary line
calls() { awk '/^cases / { print $4 }' "$1"; }

# run BINARY FILE LABEL: the driver must exit 0 with no mismatch
run() {
	if $nice "$1" < "$2" > "$out/$3.log" 2>&1; then
		echo "PASS $3: $(tail -n 1 "$out/$3.log")"
	else
		echo "FAIL $3 (exit $?):"
		cat "$out/$3.log"
		fail=1
	fi
}

# mutant BINARY FILE LABEL: the driver must finish normally and report a mismatch: exit status
# exactly 1, at least one MISMATCH line, and its summary line. Any other outcome (0, 65 bad
# input, 100+N trap, a signal) fails the run, so a mutant that crashes is not counted as detected.
mutant() {
	mut=$((mut + 1))
	rc=0
	$nice "$1" < "$2" > "$out/$3.log" 2>&1 || rc=$?
	nm=$(grep -c '^MISMATCH ' "$out/$3.log" || true)
	if [ "$rc" -eq 1 ] && [ "$nm" -gt 0 ] && grep -q '^cases .* mismatches [1-9]' "$out/$3.log"; then
		echo "PASS $3 detected: $nm mismatches, $(tail -n 1 "$out/$3.log")"
		mutok=$((mutok + 1))
	else
		echo "FAIL $3: exit $rc, $nm MISMATCH lines (need exit 1 and at least one): $(tail -n 1 "$out/$3.log")"
		fail=1
	fi
}

# libmut LABEL SED_SCRIPT: a library mutant built from a sed edit of ed25519.om, run on all vectors
libmut() {
	sed "$2" "$lib" > "$out/$1.om"
	if cmp -s "$lib" "$out/$1.om"; then
		mut=$((mut + 1))
		echo "FAIL $1: sed did not change the source"
		fail=1
	elif build "$out/$1.om" "$out/$1"; then
		mutant "$out/$1" "$out/all.txt" "$1"
	else
		mut=$((mut + 1))
		fail=1
	fi
}

if ! build "$lib" "$out/driver"; then
	echo "ED25519 FAIL build"
	exit 1
fi
echo "omc $(sha256sum "$omc" | cut -d' ' -f1)"
echo "ed25519.om $(sha256sum "$lib" | cut -d' ' -f1)"
echo "driver $(sha256sum "$out/driver" | cut -d' ' -f1)"

for v in rfc8032 wycheproof edge; do
	want=$(grep -c '^V ' "$here/vectors/$v.txt")
	run "$out/driver" "$here/vectors/$v.txt" "$v"
	n=$(calls "$out/$v.log")
	if [ "${n:-0}" -ne "$want" ]; then
		echo "FAIL $v: ${n:-0} checked calls, file has $want V lines"
		fail=1
	fi
	case $v in
	rfc8032) rfc=${n:-0}; vectors=$((vectors + ${n:-0})) ;;
	wycheproof) wyche=${n:-0}; vectors=$((vectors + ${n:-0})) ;;
	edge) edge=${n:-0} ;;
	esac
done
cat "$here/vectors/rfc8032.txt" "$here/vectors/wycheproof.txt" "$here/vectors/edge.txt" > "$out/all.txt"

# negative control: the last signature byte of RFC 8032 TEST 1 flipped (..100b -> ..100a)
sed 's/^\(V 0 d75a980182b10ab7[0-9a-f]* [0-9a-f]*\)100b /\1100a /' "$here/vectors/rfc8032.txt" > "$out/neg-sig.txt"
if cmp -s "$here/vectors/rfc8032.txt" "$out/neg-sig.txt"; then
	echo "FAIL negative-control: sed did not change the vectors"
	fail=1
else
	mutant "$out/driver" "$out/neg-sig.txt" negative-control
fi

# expectation mutant: the first line expected to return 3 expected to return 1 instead
awk 'BEGIN { done = 0 } !done && /^V 3 / { sub(/^V 3 /, "V 1 "); done = 1 } { print }' \
	"$here/vectors/wycheproof.txt" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation

# library mutants
libmut mut-d 's/^const EDD1 ulong 0x00700a4d4141d8ab$/const EDD1 ulong 0x00700a4d4141d8ac/'
libmut mut-sqrtm1 's/^const EDI0 ulong 0xc4ee1b274a0ea0b0$/const EDI0 ulong 0xc4ee1b274a0ea0b1/'
libmut mut-s-lt-l 's/^if eq ed25519_gel s 1$/if eq ed25519_gel s 2/'
libmut mut-canon-y 's/^d = or d ulong xor get e 31 and get b 31 127$/d = ulong 0/'
libmut mut-x0-sign 's/^if and eq r 1 eq s 1$/if and eq r 1 eq s 2/'
libmut mut-hash-order '/^sha512_update c part sg 0 32$/{N;s/^\(.*\)\n\(.*\)$/\2\n\1/;}'
libmut mut-cmp31 's/^for i ulong 32$/for i ulong 31/'
libmut mut-dbl 's/^ed25519_fsub w EDTG EDTA EDTB$/ed25519_fadd w EDTG EDTA EDTB/'

if [ "$fail" -ne 0 ] || [ "$mutok" -ne "$mut" ]; then
	echo "ED25519 FAIL vectors=$vectors (rfc8032=$rfc, wycheproof=$wyche) edge=$edge mutants=$mutok/$mut"
	exit 1
fi
echo "ED25519 PASS vectors=$vectors (rfc8032=$rfc, wycheproof=$wyche) edge=$edge mutants=$mutok/$mut"
