#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/rsa.om. Usage: sh om/tls/test/rsa/run.sh PATH_TO_PINNED_OMC
# Builds om2 + om/tls/sha2.om + om/tls/rsa.om + test/rsa/driver.om into build/tls/rsa/, runs every
# vector file (Wycheproof, edge, pyca differential), three library mutants and one expectation
# mutant (each must be detected as MISMATCH with exit 1), and a timing series for 2048 and
# 4096-bit keys. The driver's flipped-byte negative control runs on every case expected valid.
# Exit 0 only when every check passes. Environment: RSA_BENCH_N (default 100 verifies per key size).
set -eu
omc=${1:?usage: run.sh OMC}
pin=befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
lib="$root/om/tls"
vec="$here/vectors"
out="$root/build/tls/rsa"
rm -rf "$out"
mkdir -p "$out"
fail=0
nice_run() { nice -n 19 ionice -c3 "$@"; }

got=$(sha256sum "$omc" | cut -d' ' -f1)
echo "omc $got"
if [ "$got" != "$pin" ]; then
	echo "FAIL omc sha256 is not the pinned $pin"
	echo "RSA FAIL"
	exit 1
fi
echo "rsa.om $(sha256sum "$lib/rsa.om" | cut -d' ' -f1)"
echo "sha2.om $(sha256sum "$lib/sha2.om" | cut -d' ' -f1)"
echo "functions rsa.om $(grep -c '^fn ' "$lib/rsa.om")"

build() { # build RSA_SOURCE OUT_BINARY
	{ echo om2; cat "$lib/sha2.om" "$1" "$here/driver.om"; } > "$2.unit.om"
	rc=0
	nice_run "$omc" < "$2.unit.om" > "$2" 2> "$2.err" || rc=$?
	if [ "$rc" -ne 0 ] || [ -s "$2.err" ]; then
		echo "FAIL build $2 (rc $rc): $(cat "$2.err")"
		rm -f "$2"
		return 1
	fi
	chmod +x "$2"
}

cases=0
wcases=0
ctrls=0
# run BINARY FILE LABEL: the driver must exit 0 with no mismatch
run() {
	rc=0
	nice_run "$1" < "$2" > "$out/$3.log" 2>&1 || rc=$?
	last=$(tail -n 1 "$out/$3.log")
	if [ "$rc" -eq 0 ] && ! grep -q '^MISMATCH ' "$out/$3.log"; then
		echo "PASS $3: $last"
		n=$(echo "$last" | awk '{ print $2 }')
		c=$(echo "$last" | awk '{ print $8 }')
		cases=$((cases + n))
		ctrls=$((ctrls + c))
		case "$3" in wycheproof/*) wcases=$((wcases + n)) ;; esac
	else
		echo "FAIL $3 (exit $rc):"
		cat "$out/$3.log"
		fail=1
	fi
}

mutants=0
detected=0
# mutant BINARY FILE LABEL: exit status exactly 1, at least one MISMATCH line and the summary line
# with a nonzero mismatch count. Anything else (0, 65 bad input, 100+N trap, a signal) is a FAIL.
mutant() {
	mutants=$((mutants + 1))
	rc=0
	nice_run "$1" < "$2" > "$out/$3.log" 2>&1 || rc=$?
	nm=$(grep -c '^MISMATCH ' "$out/$3.log" || true)
	if [ "$rc" -eq 1 ] && [ "$nm" -gt 0 ] && grep -q '^cases .* mismatches [1-9]' "$out/$3.log"; then
		echo "PASS $3 detected: $nm mismatches, $(tail -n 1 "$out/$3.log")"
		detected=$((detected + 1))
	else
		echo "FAIL $3: exit $rc, $nm MISMATCH lines (need exit 1 and at least one): $(tail -n 1 "$out/$3.log")"
		fail=1
	fi
}

# libmutant LABEL SED_EXPR VECTOR_FILE: build a sed-mutated copy of rsa.om and require detection
libmutant() {
	sed "$2" "$lib/rsa.om" > "$out/$1.om"
	if cmp -s "$lib/rsa.om" "$out/$1.om"; then
		mutants=$((mutants + 1))
		echo "FAIL $1: sed did not change the source"
		fail=1
	elif build "$out/$1.om" "$out/$1"; then
		mutant "$out/$1" "$3" "$1"
	else
		mutants=$((mutants + 1))
		fail=1
	fi
}

if ! build "$lib/rsa.om" "$out/driver"; then
	echo "RSA FAIL"
	exit 1
fi
echo "driver $(sha256sum "$out/driver" | cut -d' ' -f1)"

mkdir -p "$out/wycheproof"
for f in "$vec"/wycheproof/*.txt; do
	run "$out/driver" "$f" "wycheproof/$(basename "$f" .txt)"
done
run "$out/driver" "$vec/edge.txt" edge
run "$out/driver" "$vec/pyca_gen.txt" pyca_gen

while IFS= read -r l; do
	case "$l" in '#'*) ;; *) echo "NOT_RUN $l" ;; esac
done < "$vec/NOT_RUN.txt"

# Wycheproof expectation split (every case above passed, so these are the outcomes)
split=$(cat "$vec"/wycheproof/*.txt | awk '
	/^#/ { next }
	{ id = $8 }
	id ~ /\/valid$/ { v++ }
	id ~ /\/invalid$/ { i++ }
	id ~ /\/acceptable/ { if ($1 == "0") aa++; else ar++ }
	END { printf "valid=%d invalid=%d acceptable=%d (accepted %d: SmallModulus/SmallPublicKey; refused %d: MissingNull)", v, i, aa + ar, aa, ar }')
echo "WYCHEPROOF $split"

s256="$vec/wycheproof/rsa_signature_2048_sha256_test.txt"
p256="$vec/wycheproof/rsa_pss_2048_sha256_mgf1_32_test.txt"
# expectation mutant: the first expected-valid line flipped to expected-refused
awk 'BEGIN { done = 0 } !done && /^0 / { sub(/^0 /, "n "); done = 1 } { print }' "$s256" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation
# library mutant 1: Montgomery multiply row drops its carry
libmutant mutant-mont-carry 's/^cy = shr x 32$/cy = 0/' "$s256"
# library mutant 2: SHA-256 DigestInfo prefix byte 15 changed (05 -> 06)
libmutant mutant-digestinfo 's/^const RSD2B ulong 0x4801650304020105$/const RSD2B ulong 0x4801650304020106/' "$s256"
# library mutant 3: MGF1 counter steps by 2
libmutant mutant-mgf1-counter 's/^ct = add ct 1$/ct = add ct 2/' "$p256"

# timing: the first valid case of a 2048 and a 4096-bit PKCS#1 and PSS file, RSA_BENCH_N verifies
bn=${RSA_BENCH_N:-100}
for t in rsa_signature_2048_sha256_test rsa_signature_4096_sha256_test \
	rsa_pss_2048_sha256_mgf1_32_test rsa_pss_4096_sha256_mgf1_32_test; do
	grep -m 1 '^0 ' "$vec/wycheproof/$t.txt" > "$out/bench-$t.txt"
	if nice_run "$out/driver" "$bn" < "$out/bench-$t.txt" > "$out/bench-$t.log" 2>&1; then
		ns=$(awk '{ print $10 }' "$out/bench-$t.log")
		echo "TIME $t: $bn verifies, $ns ns total, $((ns / bn)) ns per verify (nice 19)"
	else
		echo "FAIL bench $t"
		cat "$out/bench-$t.log"
		fail=1
	fi
done

if [ "$fail" -ne 0 ] || [ "$detected" -ne "$mutants" ]; then
	echo "RSA FAIL vectors=$cases (wycheproof=$wcases) mutants=$detected/$mutants"
	exit 1
fi
echo "RSA PASS vectors=$cases (wycheproof=$wcases $split controls=$ctrls) mutants=$detected/$mutants"
