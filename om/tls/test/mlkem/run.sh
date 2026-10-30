#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/sha3.om and om/tls/mlkem.om (ML-KEM-768, FIPS 203).
# Usage: sh om/tls/test/mlkem/run.sh PATH_TO_PINNED_OMC
# Builds om2 + sha3.om + mlkem.om + driver.om into build/tls/mlkem/, runs the NIST CAVP SHA-3/SHAKE,
# NIST ACVP ML-KEM-768 and Wycheproof ML-KEM-768 record files (regenerate them with gen_vectors.py),
# the length checks, a negative control, an expectation mutant and seven library mutants (each must
# be detected: exit 1 with MISMATCH lines), a composition check against the other om/tls units, and
# a keygen / encaps / decaps timing (informational, not a constant-time proof).
# Environment: MLKEM_BENCH_N (default 200) calls per timed operation.
# Exit 0 only when every check passes; the final line is
#   MLKEM PASS vectors=N (wycheproof=W fips203=K) sha3=S mutants=M/M     or     MLKEM FAIL ...
# N counts every checked call (ML-KEM and SHA-3 records plus the length checks); W the calls from the
# Wycheproof file, K from the NIST ACVP FIPS 203 file, S from the NIST CAVP SHA-3/SHAKE file.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
sha3="$root/om/tls/sha3.om"
lib="$root/om/tls/mlkem.om"
out="$root/build/tls/mlkem"
rm -rf "$out"
mkdir -p "$out"
fail=0
vectors=0
wyche=0
fips=0
s3=0
mut=0
mutok=0
nice="nice -n 19 ionice -c3"

build() { # build SRC_SHA3 SRC_MLKEM OUT_BINARY
	{ echo om2; cat "$1" "$2" "$here/driver.om"; } > "$3.unit.om"
	if ! $nice "$omc" < "$3.unit.om" > "$3" 2> "$3.err" || [ -s "$3.err" ]; then
		echo "FAIL build $3: $(cat "$3.err")"
		rm -f "$3"
		return 1
	fi
	chmod +x "$3"
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

# libmut LABEL UNIT(sha3|mlkem) SED_SCRIPT VECTORS: a library mutant built from a sed edit of one unit
libmut() {
	if [ "$2" = sha3 ]; then src=$sha3; else src=$lib; fi
	sed "$3" "$src" > "$out/$1.om"
	if cmp -s "$src" "$out/$1.om"; then
		mut=$((mut + 1))
		echo "FAIL $1: sed did not change the source"
		fail=1
		return
	fi
	if [ "$2" = sha3 ]; then ok=0; build "$out/$1.om" "$lib" "$out/$1" || ok=1
	else ok=0; build "$sha3" "$out/$1.om" "$out/$1" || ok=1; fi
	if [ "$ok" -eq 0 ]; then
		mutant "$out/$1" "$4" "$1"
	else
		mut=$((mut + 1))
		fail=1
	fi
}

if ! build "$sha3" "$lib" "$out/driver"; then
	echo "MLKEM FAIL build"
	exit 1
fi
echo "omc $(sha256sum "$omc" | cut -d' ' -f1)"
echo "sha3.om $(sha256sum "$sha3" | cut -d' ' -f1)"
echo "mlkem.om $(sha256sum "$lib" | cut -d' ' -f1)"
echo "driver $(sha256sum "$out/driver" | cut -d' ' -f1)"

printf 'L\n' > "$out/length.txt"
for v in nist-sha3 acvp wycheproof length; do
	f="$here/vectors/$v.txt"
	[ "$v" = length ] && f="$out/length.txt"
	if [ ! -s "$f" ]; then
		echo "FAIL $v: $f missing (NOT_RUN is not PASS; regenerate with gen_vectors.py)"
		fail=1
		continue
	fi
	run "$out/driver" "$f" "$v"
	n=$(calls "$out/$v.log")
	vectors=$((vectors + ${n:-0}))
	case $v in
	nist-sha3) s3=${n:-0} ;;
	acvp) fips=${n:-0} ;;
	wycheproof) wyche=${n:-0} ;;
	esac
done
echo "NOT_RUN SHA3_224*, SHA3_384*: not implemented (ML-KEM needs only SHA3-256/512 and SHAKE128/256)"
echo "NOT_RUN SHA3_*Monte.rsp, SHAKE*Monte.rsp: Monte Carlo chains not converted by gen_vectors.py"
echo "NOT_RUN ACVP ML-KEM-512 and ML-KEM-1024 groups: only ML-KEM-768 is implemented"

# negative control: one input byte of the first ACVP keyGen record flipped (d byte 0 +1 in its low nibble)
awk 'BEGIN { done = 0 } !done && /^K / {
	c = substr($2, 2, 1); n = index("0123456789abcdef", c) % 16
	$2 = substr($2, 1, 1) substr("0123456789abcdef", n + 1, 1) substr($2, 3); done = 1 } { print }' \
	"$here/vectors/acvp.txt" > "$out/neg-d.txt"
if cmp -s "$here/vectors/acvp.txt" "$out/neg-d.txt"; then
	echo "FAIL negative-control: awk did not change the vectors"
	fail=1
else
	mutant "$out/driver" "$out/neg-d.txt" negative-control
fi

# expectation mutant: the first Wycheproof encaps line expected to fail the modulus check (E 1)
# expected to succeed instead
awk 'BEGIN { done = 0 } !done && /^E 1 / { sub(/^E 1 /, "E 0 "); done = 1 } { print }' \
	"$here/vectors/wycheproof.txt" > "$out/mut-expect.txt"
mutant "$out/driver" "$out/mut-expect.txt" mutant-expectation

# library mutants
libmut mut-zeta mlkem 's/^put z 1 1729$/put z 1 1730/' "$here/vectors/acvp.txt"
libmut mut-ninv mlkem 's/^put f i mlkem_red mul get f i 3303$/put f i mlkem_red mul get f i 3302/' \
	"$here/vectors/acvp.txt"
libmut mut-comp mlkem 's/^x = add shl get f i d 1664$/x = add shl get f i d 1665/' "$here/vectors/acvp.txt"
libmut mut-reject mlkem 's/^mk = sub 0 shr or df sub 0 df 63$/mk = sub shr or df sub 0 df 63 1/' \
	"$here/vectors/acvp.txt"
libmut mut-ekchk mlkem 's/^if ne mlkem_ekchk ek 0$/if eq mlkem_ekchk ek 2/' "$here/vectors/wycheproof.txt"
libmut mut-dkchk mlkem 's/^if ne mlkem_dkchk dk 0$/if gt mlkem_dkchk dk 255/' "$here/vectors/wycheproof.txt"
libmut mut-keccak-rc sha3 's/^put k 1 0x0000000000008082$/put k 1 0x0000000000008083/' \
	"$here/vectors/nist-sha3.txt"

# composition: every om/tls unit in build.sh order with sha3 and mlkem after x25519 (name collisions,
# call order); tls.om is included when it composes, else reported (it is edited by the TLS lane)
printf 'fn main to int\nret 0\nend\n' > "$out/comp-main.om"
comp() { # comp LABEL UNITS...
	l=$1; shift
	{ echo om2; for u in "$@"; do cat "$root/om/tls/$u.om"; done; cat "$out/comp-main.om"; } > "$out/$l.unit.om"
	$nice "$omc" < "$out/$l.unit.om" > "$out/$l" 2> "$out/$l.err"
}
if comp compose sha2 hkdf x25519 sha3 mlkem chacha aesgcm ec rsa x509 && [ ! -s "$out/compose.err" ]; then
	echo "PASS compose: sha2 hkdf x25519 sha3 mlkem chacha aesgcm ec rsa x509"
else
	echo "FAIL compose: $(cat "$out/compose.err")"
	fail=1
fi
if comp compose-tls sha2 hkdf drbg x25519 sha3 mlkem chacha aesgcm ec ecdh rsa x509 rev fips tls && [ ! -s "$out/compose-tls.err" ]; then
	echo "PASS compose-tls: ... x509 tls"
else
	echo "FAIL compose-tls: $(cat "$out/compose-tls.err")"
	fail=1
fi

# timing (informational): N keygen_seed, N encaps, N decaps calls after one discarded warm-up series
n=${MLKEM_BENCH_N:-200}
printf 'T %s\nT %s\n' "$n" "$n" > "$out/timing.txt"
if $nice "$out/driver" < "$out/timing.txt" > "$out/timing.log" 2>&1; then
	awk -v n="$n" '/^TIME line 2 / { printf "TIME %s: %d calls, %d ns, %d ns per call\n", $4, n, $8, $8 / n }' \
		"$out/timing.log"
else
	echo "FAIL timing: $(cat "$out/timing.log")"
	fail=1
fi

line="vectors=$vectors (wycheproof=$wyche fips203=$fips) sha3=$s3 mutants=$mutok/$mut"
if [ "$fail" -ne 0 ] || [ "$mutok" -ne "$mut" ] || [ "$mut" -lt 5 ]; then
	echo "MLKEM FAIL $line"
	exit 1
fi
echo "MLKEM PASS $line"
