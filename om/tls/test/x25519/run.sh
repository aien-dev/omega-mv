#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/x25519.om. Usage: sh om/tls/test/x25519/run.sh PATH_TO_PINNED_OMC
# Builds om2 + x25519.om + driver.om into build/tls/x25519/, runs the RFC 7748 and Wycheproof vector
# files, a negative control, an expectation mutant and five library mutants (each must be detected:
# exit 1 with MISMATCH lines), and a low/high Hamming weight timing pair (informational).
# Environment: X25519_ITER1M=1 also runs the RFC 7748 1,000,000-iteration test (177 s measured on
# the DGX Spark GB10 host, 2026-10-26, under nice 19);
# X25519_BENCH_N (default 200) calls per timing scalar.
# Exit 0 only when every check passes; the final line is
#   X25519 PASS vectors=N (wycheproof=W) mutants=M/M     or     X25519 FAIL ...
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
lib="$root/om/tls/x25519.om"
out="$root/build/tls/x25519"
rm -rf "$out"
mkdir -p "$out"
fail=0
vectors=0
wyche=0
mut=0
mutok=0
nice="nice -n 19 ionice -c3"

build() { # build SRC_X25519 OUT_BINARY
	{ echo om2; cat "$1" "$here/driver.om"; } > "$2.unit.om"
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

# libmut LABEL SED_SCRIPT VECTORS: a library mutant built from a sed edit of x25519.om
libmut() {
	sed "$2" "$lib" > "$out/$1.om"
	if cmp -s "$lib" "$out/$1.om"; then
		mut=$((mut + 1))
		echo "FAIL $1: sed did not change the source"
		fail=1
	elif build "$out/$1.om" "$out/$1"; then
		mutant "$out/$1" "$3" "$1"
	else
		mut=$((mut + 1))
		fail=1
	fi
}

if ! build "$lib" "$out/driver"; then
	echo "X25519 FAIL build"
	exit 1
fi
echo "omc $(sha256sum "$omc" | cut -d' ' -f1)"
echo "x25519.om $(sha256sum "$lib" | cut -d' ' -f1)"
echo "driver $(sha256sum "$out/driver" | cut -d' ' -f1)"

for v in rfc7748 wycheproof; do
	run "$out/driver" "$here/vectors/$v.txt" "$v"
	n=$(calls "$out/$v.log")
	vectors=$((vectors + ${n:-0}))
	[ "$v" = wycheproof ] && wyche=${n:-0}
done
if [ "${X25519_ITER1M:-0}" = 1 ]; then
	t0=$(date +%s)
	run "$out/driver" "$here/vectors/rfc7748-iter1m.txt" rfc7748-iter1m
	t1=$(date +%s)
	echo "TIME rfc7748-iter1m: $((t1 - t0)) s wall"
	n=$(calls "$out/rfc7748-iter1m.log")
	vectors=$((vectors + ${n:-0}))
else
	echo "NOTE rfc7748-iter1m not run (opt-in: X25519_ITER1M=1)"
fi

# negative control: one input byte of the first RFC 7748 section 5.2 vector flipped (u byte 0 e6 -> e7)
sed 's/^\(X 0 a546e36b[0-9a-f]* \)e6db/\1e7db/' "$here/vectors/rfc7748.txt" > "$out/neg-u.txt"
if cmp -s "$here/vectors/rfc7748.txt" "$out/neg-u.txt"; then
	echo "FAIL negative-control: sed did not change the vectors"
	fail=1
else
	mutant "$out/driver" "$out/neg-u.txt" negative-control
fi

# expectation mutant: the first ZeroSharedSecret line expected to return 0 instead of 1
awk 'BEGIN { done = 0 } !done && /^X 1 / { sub(/^X 1 /, "X 0 "); done = 1 } { print }' \
	"$here/vectors/wycheproof.txt" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation

# library mutants
libmut mut-a24 's/^const XCA24 ulong 121665$/const XCA24 ulong 121666/' "$here/vectors/rfc7748.txt"
libmut mut-step 's/^x25519_fsub w XCTD XCX3 XCZ3$/x25519_fadd w XCTD XCX3 XCZ3/' "$here/vectors/rfc7748.txt"
libmut mut-clamp 's/^put e 31 or and get e 31 127 64$/put e 31 and get e 31 127/' "$here/vectors/wycheproof.txt"
libmut mut-zero 's/^z = xor shr or z sub 0 z 63 1$/z = shr or z sub 0 z 63/' "$here/vectors/wycheproof.txt"
libmut mut-canon 's/^c = ulong 19$/c = ulong 0/' "$here/vectors/wycheproof.txt"

# timing sanity (informational, not a proof of constant time): clamped low and high Hamming weight
# scalars, interleaved low high high low so warm-up and drift fall on both; sums per weight
n=${X25519_BENCH_N:-200}
u=e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c
lo=0000000000000000000000000000000000000000000000000000000000000040
hi=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
printf 'T %s %s %s\n' "$n" "$lo" "$u" "$n" "$hi" "$u" "$n" "$hi" "$u" "$n" "$lo" "$u" > "$out/timing.txt"
$nice "$out/driver" < "$out/timing.txt" > "$out/timing.log" 2>&1 || true
tlo=$(awk '/^TIME line [14] / { s += $7; c++ } END { if (c == 2) print s }' "$out/timing.log")
thi=$(awk '/^TIME line [23] / { s += $7; c++ } END { if (c == 2) print s }' "$out/timing.log")
if [ -n "$tlo" ] && [ -n "$thi" ]; then
	echo "TIME low-weight scalar (clamped weight 1): $((2 * n)) calls, $tlo ns, $((tlo / (2 * n))) ns per call"
	echo "TIME high-weight scalar (clamped weight 252): $((2 * n)) calls, $thi ns, $((thi / (2 * n))) ns per call"
else
	echo "FAIL timing: $(cat "$out/timing.log")"
	fail=1
fi

if [ "$fail" -ne 0 ] || [ "$mutok" -ne "$mut" ]; then
	echo "X25519 FAIL vectors=$vectors (wycheproof=$wyche) mutants=$mutok/$mut"
	exit 1
fi
echo "X25519 PASS vectors=$vectors (wycheproof=$wyche) mutants=$mutok/$mut"
