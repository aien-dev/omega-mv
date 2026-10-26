#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/x509.om. Usage: sh om/tls/test/x509/run.sh OMC
# Builds om2 + sha2 hkdf ec rsa x509 + driver.om into build/tls/x509/, runs vectors/cases.txt (plus a
# record for the system store), compares every chain case with openssl verify (differential; the
# intentional differences are listed in vectors/openssl-diff.txt), and runs the source mutants.
# Exit 0 only when everything passes; final line "X509 PASS cases=N (openssl-parity=P/N) mutants=M/M".
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
root=$(cd "$tls/../.." && pwd)
pin=$(sed -n 's/^OMC_PIN=//p' "$root/om/units.sh")
out="$root/build/tls/x509"
vec="$here/vectors"
sys=/etc/ssl/certs/ca-certificates.crt
mkdir -p "$out"
bad=0
say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }
run() { nice -n 19 ionice -c3 "$@"; }

[ "$(sha256sum < "$omc" | cut -c1-64)" = "$pin" ] || { say "FAIL omc is not the pinned compiler"; exit 1; }

# build LIB DRIVER BIN
build() {
	{ echo om2; cat "$tls/sha2.om" "$tls/hkdf.om" "$tls/ec.om" "$tls/rsa.om" "$1" "$2"; } > "$3.om"
	if ! run "$omc" < "$3.om" > "$3" 2> "$3.err" || [ -s "$3.err" ]; then
		say "omc refused $3.om: $(cat "$3.err")"
		return 1
	fi
	chmod +x "$3"
}

build "$tls/x509.om" "$here/driver.om" "$out/driver" || { say "X509 FAIL build"; exit 1; }
say "omc     sha256 $pin"
say "library sha256 $(sha256sum < "$tls/x509.om" | cut -c1-64) ($(grep -c '^fn ' "$tls/x509.om") functions)"
say "driver  sha256 $(sha256sum < "$out/driver" | cut -c1-64)"

# the system store record: every BEGIN CERTIFICATE block of the bundle must load
nsys=$(grep -c -- '-----BEGIN CERTIFICATE-----' "$sys")
{ cat "$vec/cases.txt"; echo "S system_store $nsys SYS"; } > "$out/cases.txt"
ncase=$(grep -c '^C ' "$out/cases.txt")

# drive FILE BIN NAME -> sets rc, last
drive() {
	set +e
	(cd "$vec" && run "$2" < "$1") > "$out/$3.out" 2>&1
	rc=$?
	set -e
	last=$(tail -n 1 "$out/$3.out")
}

drive "$out/cases.txt" "$out/driver" main
checks=$(printf '%s\n' "$last" | awk '$1 == "x509" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
if [ "$rc" -eq 0 ] && [ -n "$checks" ]; then
	say "PASS cases.txt: $last"
	grep '^fuzz ' "$out/main.out" | sed 's/^/PASS /'
else
	fail "cases.txt: exit $rc: $(grep -E 'MISMATCH|driver' "$out/main.out" | head -n 20 | tr '\n' ' ')"
fi
fuzzn=$(awk '$1 == "fuzz" { sub("n=", "", $3); s += $3 } END { print s + 0 }' "$out/main.out")

# differential: openssl verify for every C record
parity=0
: > "$out/diff.txt"
if command -v openssl > /dev/null 2>&1; then
	say "openssl $(openssl version)"
	while read -r tag name want now host store certs; do
		[ "$tag" = C ] || continue
		set -- $certs
		[ "$store" = SYS ] && ca=$sys || ca="$vec/$store"
		leaf="$out/ossl-leaf.pem"
		unt="$out/ossl-untrusted.pem"
		: > "$unt"
		ok=1
		openssl x509 -inform DER -in "$vec/$1" -out "$leaf" 2> /dev/null || ok=0
		shift
		for c in "$@"; do openssl x509 -inform DER -in "$vec/$c" >> "$unt" 2> /dev/null || ok=0; done
		if [ "$ok" -eq 1 ]; then
			uarg=""
			[ -s "$unt" ] && uarg="-untrusted $unt"
			if run openssl verify -x509_strict -purpose sslserver -verify_hostname "$host" -attime "$now" \
				-CAfile "$ca" $uarg "$leaf" > "$out/ossl.txt" 2>&1; then o=accept; else o=reject; fi
		else
			o=reject
		fi
		[ "$want" = X509_OK ] && m=accept || m=reject
		listed=$(awk -v n="$name" '$1 == n { print "y" }' "$vec/openssl-diff.txt")
		if [ "$o" = "$m" ]; then
			parity=$((parity + 1))
			[ -z "$listed" ] || fail "openssl-diff.txt lists $name but openssl agrees ($o)"
		else
			echo "$name x509=$m openssl=$o $(grep -m1 'error' "$out/ossl.txt" | tr -s ' ')" >> "$out/diff.txt"
			[ -n "$listed" ] || fail "unlisted openssl difference: $name x509=$m openssl=$o: $(tr '\n' ' ' < "$out/ossl.txt")"
		fi
	done < "$out/cases.txt"
	sed 's/^/DIFF /' "$out/diff.txt"
else
	say "NOT_RUN openssl differential: openssl not installed"
	fail "differential not run"
fi

# mutants: each must make the driver finish normally with exit 1 and MISMATCH lines
mut=0
mutok=0
# mutant SED NAME: library mutant. Detected when the driver finishes with exit 1 and MISMATCH lines,
# or dies on a runtime trap (exit >= 100: the main run fails on any trap, so the mutant is caught)
mutant() {
	mut=$((mut + 1))
	sed "$1" "$tls/x509.om" > "$out/mut-$2-lib.om"
	if cmp -s "$tls/x509.om" "$out/mut-$2-lib.om"; then fail "mutant $2: sed changed nothing"; return; fi
	if ! build "$out/mut-$2-lib.om" "$here/driver.om" "$out/mut-$2"; then fail "mutant $2: build"; return; fi
	drive "$out/cases.txt" "$out/mut-$2" "mut-$2"
	n=$(grep -c '^MISMATCH ' "$out/mut-$2.out" || true)
	if [ "$rc" -eq 1 ] && [ "$n" -gt 0 ] && printf '%s\n' "$last" | grep -q "^x509 driver: [0-9]* checks, $n failed$"; then
		mutok=$((mutok + 1))
		say "PASS mutant $2 detected ($n MISMATCH: $(grep '^MISMATCH ' "$out/mut-$2.out" | head -n 2 | cut -d' ' -f4 | tr '\n' ' '))"
	elif [ "$rc" -ge 100 ]; then
		mutok=$((mutok + 1))
		say "PASS mutant $2 detected (driver trapped, exit $rc, after $n MISMATCH)"
	else
		fail "mutant $2 not detected: exit $rc: $last"
	fi
}
# name matching: a wildcard with only one further label ("*.test") is accepted
mutant 's/^if or eq lab 0 eq dots 0$/if eq lab 0/' name-wildcard-labels
# date comparison: notAfter compared the wrong way round
mutant 's/^if le get r X5RNA nw$/if gt get r X5RNA nw/' date-not-after
# date boundary: now == notAfter accepted again (RFC-inclusive bound that OpenSSL refuses)
mutant 's/^if le get r X5RNA nw$/if lt get r X5RNA nw/' date-at-not-after
# trust anchors: a store entry that is not self-issued accepted as an anchor
mutant 's/^if eq chain_self kd w 0$/if eq chain_self kd w 9/' anchor-self-issued
# basicConstraints: cA FALSE accepted for an issuer
mutant 's/^if eq get w add X5RSZ X5RCA 0$/if eq get w add X5RSZ X5RCA 2/' bc-ca-flag
# security review finding 1: unique IDs repeated or out of order accepted
mutant 's/^if or eq v 0 le g k$/if eq v 0/' unique-id-order
# finding 5: nameConstraints refused only when critical (the pre-review behaviour)
mutant 's/^if eq o 20$/if eq o 99/' name-constraints-noncritical
# finding 6: modulus above 4096 bits accepted for presented keys
mutant 's/^if or lt b 2048 gt b 4096$/if lt b 2048/' rsa-modulus-max
# finding 6: exponent policy dropped (only e >= 1)
mutant 's/^if or lt v 3 eq and v 1 0$/if lt v 1/' rsa-exponent
# finding 2: no leaf size limit
mutant 's/^if gt get w add X5WIX 1 X5LMAX$/if gt get w add X5WIX 1 X5BIG/' leaf-size
# finding 2: element caps removed: a 10001-element list hits the loop bound (trap)
mutant 's/^if gt n X5NMAX$/if gt n 99999/' elemcap
# expectation mutant: one expected code changed
mut=$((mut + 1))
sed 's/^C rsa_good X509_OK /C rsa_good X509_ERR_EXPIRED /' "$out/cases.txt" > "$out/mut-expect.txt"
drive "$out/mut-expect.txt" "$out/driver" mut-expect
if [ "$rc" -eq 1 ] && printf '%s\n' "$last" | grep -q '^x509 driver: [0-9]* checks, 1 failed$' && grep -q '^MISMATCH .* rsa_good ' "$out/mut-expect.out"; then
	mutok=$((mutok + 1))
	say "PASS mutant expectation detected"
else
	fail "expectation mutant not detected: exit $rc: $last"
fi

say "driver checks=${checks:-0} fuzz-mutations=$fuzzn"
if [ "$bad" -ne 0 ]; then
	say "X509 FAIL cases=$ncase (openssl-parity=$parity/$ncase) mutants=$mutok/$mut"
	exit 1
fi
say "X509 PASS cases=$ncase (openssl-parity=$parity/$ncase) mutants=$mutok/$mut"
