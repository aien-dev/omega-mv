#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/ec.om (ECDSA P-256/P-384 verification).
# Usage: sh om/tls/test/ec/run.sh PATH_TO_PINNED_OMC
# Builds om2 + sha2.om + ec.om + test/ec/driver.om into build/tls/ec/, runs every vector file
# (Wycheproof, NIST CAVP SigVer, edge cases), library mutants and one expectation mutant (each must
# be detected: exit 1 with MISMATCH lines), and one timing series per curve.
# Exit 0 only when every check passes. Environment: EC_BENCH_N (default 200 verifies per curve).
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out="$root/build/tls/ec"
pin=befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
rm -rf "$out"
mkdir -p "$out"
fail=0
nice="nice -n 19 ionice -c3"

sum=$(sha256sum "$omc" | cut -d' ' -f1)
echo "omc $sum"
if [ "$sum" != "$pin" ]; then
	echo "FAIL omc is not the pinned compiler $pin"
	echo "EC FAIL"
	exit 1
fi

build() { # build SRC_EC OUT_BINARY
	{ echo om2; cat "$root/om/tls/sha2.om" "$1" "$here/driver.om"; } > "$2.unit.om"
	if ! $nice "$omc" < "$2.unit.om" > "$2" 2> "$2.err" || [ -s "$2.err" ]; then
		echo "FAIL build $2: $(cat "$2.err")"
		return 1
	fi
	chmod +x "$2"
}

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

# mutant BINARY FILE LABEL: exit status exactly 1, at least one MISMATCH line and the summary line;
# any other outcome (0, 65 bad input, 100+N trap, a signal) fails, so a crash is not a detection.
mutants=0
detected=0
mutant() {
	mutants=$((mutants + 1))
	rc=0
	$nice "$1" < "$2" > "$out/$3.log" 2>&1 || rc=$?
	nm=$(grep -c '^MISMATCH ' "$out/$3.log" || true)
	if [ "$rc" -eq 1 ] && [ "$nm" -gt 0 ] && grep -q '^cases .* mismatches [1-9]' "$out/$3.log"; then
		detected=$((detected + 1))
		echo "PASS $3 detected: $nm mismatches, $(tail -n 1 "$out/$3.log")"
	else
		echo "FAIL $3: exit $rc, $nm MISMATCH lines (need exit 1 and at least one): $(tail -n 1 "$out/$3.log")"
		fail=1
	fi
}

# libmut LABEL SED_EXPR VECTORFILE: a source mutant of ec.om that the vectors must detect
libmut() {
	sed "$2" "$root/om/tls/ec.om" > "$out/$1.om"
	if cmp -s "$root/om/tls/ec.om" "$out/$1.om"; then
		mutants=$((mutants + 1))
		echo "FAIL $1: sed did not change the source"
		fail=1
	elif build "$out/$1.om" "$out/$1"; then
		mutant "$out/$1" "$here/vectors/$3" "$1"
	else
		mutants=$((mutants + 1))
		fail=1
	fi
}

build "$root/om/tls/ec.om" "$out/driver"
echo "ec.om $(sha256sum "$root/om/tls/ec.om" | cut -d' ' -f1) functions $(grep -c '^fn ' "$root/om/tls/ec.om")"
echo "sha2.om $(sha256sum "$root/om/tls/sha2.om" | cut -d' ' -f1)"
echo "driver $(sha256sum "$out/driver" | cut -d' ' -f1)"

total=0
wy=0
for f in "$here"/vectors/*.txt; do
	b=$(basename "$f" .txt)
	[ "$b" = NOT_RUN ] && continue
	n=$(grep -vc '^#' "$f")
	total=$((total + n))
	case "$b" in wycheproof_*) wy=$((wy + n)) ;; esac
	run "$out/driver" "$f" "$b"
done
cavp=$(grep -vc '^#' "$here/vectors/cavp_sigver.txt")
edge=$(grep -vc '^#' "$here/vectors/edge.txt")
grep '^NOT_RUN' "$here/vectors/NOT_RUN.txt"

# expectation mutant: the first expected-valid Wycheproof line flipped to expected-invalid
awk 'BEGIN { done = 0 } !done && $4 == "0" { $4 = "1"; done = 1 } { print }' \
	"$here/vectors/wycheproof_secp256r1_sha256.txt" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation

# library mutants: curve constant b of P-256 (one limb), the point-addition formula (X3 = r^2 + HHH
# - 2V instead of - HHH), field reduction (Montgomery n0' with 2 instead of 5 Newton steps, 12 bits
# right), and DER strictness (non-minimal INTEGER padding accepted)
libmut mutant-b256 's/^put w 120 0x27d2604b$/put w 120 0x27d2604c/' wycheproof_secp256r1_sha256.txt
libmut mutant-padd 's/^ec_sub w ECCP p p ECA7$/ec_add w ECCP p p ECA7/' wycheproof_secp384r1_sha384.txt
libmut mutant-n0 's/^for k ulong 5$/for k ulong 2/' cavp_sigver.txt
libmut mutant-der 's/^if lt get hs add v 1 128$/if lt get hs add v 1 0/' edge.txt

# timing: the first valid DER line of the P-256/SHA-256 and P-384/SHA-384 Wycheproof files
bn=${EC_BENCH_N:-200}
for c in secp256r1_sha256 secp384r1_sha384; do
	awk '$4 == "0" && $3 == "d" { print; exit }' "$here/vectors/wycheproof_$c.txt" > "$out/bench-$c.txt"
	if $nice "$out/driver" "$bn" < "$out/bench-$c.txt" > "$out/bench-$c.log" 2>&1; then
		ns=$(awk '{ print $10 }' "$out/bench-$c.log")
		echo "TIME $c: $bn verifies, $ns ns total, $((ns / bn)) ns per verify"
	else
		echo "FAIL bench-$c"
		cat "$out/bench-$c.log"
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "EC FAIL vectors=$total (wycheproof=$wy cavp=$cavp edge=$edge) mutants=$detected/$mutants"
	exit 1
fi
echo "EC PASS vectors=$total (wycheproof=$wy cavp=$cavp edge=$edge) mutants=$detected/$mutants"
