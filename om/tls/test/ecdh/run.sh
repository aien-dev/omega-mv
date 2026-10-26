#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/ecdh.om (ECDH P-256/P-384, SP 800-56A Rev. 3 ECC CDH primitive).
# Usage: sh om/tls/test/ecdh/run.sh PATH_TO_PINNED_OMC
# Builds om2 + ec.om + ecdh.om + test/ecdh/driver.om into build/tls/ecdh/, runs every vector file
# (NIST CAVP ECC CDH primitive P-256/P-384, Wycheproof ecdh secp256r1/secp384r1, edge cases), then
# library mutants and an expectation mutant (each must be detected: exit 1 with MISMATCH lines) and
# one timing series per curve. Exit 0 only when every check passes.
# Environment: ECDH_BENCH_N (default 100 shared-secret computations per curve).
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out="$root/build/tls/ecdh"
pin=$(sed -n 's/^OMC_PIN=//p' "$root/om/units.sh")
rm -rf "$out"
mkdir -p "$out"
fail=0
nice="nice -n 19 ionice -c3"

sum=$(sha256sum "$omc" | cut -d' ' -f1)
echo "omc $sum"
if [ "$sum" != "$pin" ]; then
	echo "FAIL omc is not the pinned compiler $pin"
	echo "ECDH FAIL"
	exit 1
fi

build() { # build SRC_ECDH OUT_BINARY
	{ echo om2; cat "$root/om/tls/ec.om" "$1" "$here/driver.om"; } > "$2.unit.om"
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

# libmut LABEL SED_SCRIPT VECTORFILE: a source mutant of ecdh.om that the vectors must detect
libmut() {
	sed "$2" "$root/om/tls/ecdh.om" > "$out/$1.om"
	if cmp -s "$root/om/tls/ecdh.om" "$out/$1.om"; then
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

build "$root/om/tls/ecdh.om" "$out/driver"
echo "ecdh.om $(sha256sum "$root/om/tls/ecdh.om" | cut -d' ' -f1) functions $(grep -c '^fn ' "$root/om/tls/ecdh.om")"
echo "ec.om $(sha256sum "$root/om/tls/ec.om" | cut -d' ' -f1)"
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
cavp=$(grep -vc '^#' "$here/vectors/cavp_cdh.txt")
edge=$(grep -vc '^#' "$here/vectors/edge.txt")
grep '^NOT_RUN' "$here/vectors/NOT_RUN.txt"

# expectation mutant: the first CAVP shared-secret line expected to be refused (3) instead of 0
awk 'BEGIN { done = 0 } !done && $2 == "S" && $3 == "0" { $3 = "3"; done = 1 } { print }' \
	"$here/vectors/cavp_cdh.txt" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation

# library mutants:
#   peer: partial public-key validation skipped (invalid-curve and off-curve points accepted)
#   step: ladder step order swapped (R0 doubled before R1 = R0 + R1, wrong ladder)
#   swap: conditional swap from the bit alone, not bit xor previous bit
#   bits: ladder over 32L - 1 bits (top scalar bit ignored)
#   formula: one subtraction of the complete addition formula turned into an addition
#   csub: constant-time final subtraction mask forced to 0 (result left unreduced)
#   range: scalar range check [1, n-1] always passing
#   pct: pair-wise consistency comparison ignored
libmut mutant-peer 's/^rc = ec_pub w p$/rc = ulong 0/' wycheproof_secp256r1_ecpoint.txt
libmut mutant-step '/^ecd_padd w EDR1 EDR0 EDR1$/{h;d};/^ecd_padd w EDR0 EDR0 EDR0$/G' cavp_cdh.txt
libmut mutant-swap 's/^sw = xor sw b$/sw = b/' cavp_cdh.txt
libmut mutant-bits 's/^nb = mul get w 0 32$/nb = sub mul get w 0 32 1/' cavp_cdh.txt
libmut mutant-formula '0,/^ecd_sub w ECCP EDT3 EDT3 EDT4$/s//ecd_add w ECCP EDT3 EDT3 EDT4/' cavp_cdh.txt
libmut mutant-csub 's/^mk = sub shr x 63 1$/mk = ulong 0/' cavp_cdh.txt
libmut mutant-range 's/^ret and br shr or z sub 0 z 63$/ret 1/' edge.txt
libmut mutant-pct 's/^if or ne rc 0 ne acc 0$/if ne rc 0/' edge.txt

# timing: the first CAVP P-256 and P-384 shared-secret lines, ECDH_BENCH_N calls each
bn=${ECDH_BENCH_N:-100}
for c in 256 384; do
	awk -v c="cavp-$c-" '$2 == "S" && index($1, c) == 1 { print; exit }' "$here/vectors/cavp_cdh.txt" > "$out/bench-$c.txt"
	if $nice "$out/driver" "$bn" < "$out/bench-$c.txt" > "$out/bench-$c.log" 2>&1; then
		ns=$(awk '{ print $10 }' "$out/bench-$c.log")
		echo "TIME P-$c: $bn shared-secret computations, $ns ns total, $((ns / bn)) ns per call"
	else
		echo "FAIL bench-$c"
		cat "$out/bench-$c.log"
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "ECDH FAIL vectors=$total (cavp=$cavp wycheproof=$wy edge=$edge) mutants=$detected/$mutants"
	exit 1
fi
echo "ECDH PASS vectors=$total (cavp=$cavp wycheproof=$wy edge=$edge) mutants=$detected/$mutants"
