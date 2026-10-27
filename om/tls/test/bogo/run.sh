#!/bin/sh
# BoGo (BoringSSL ssl/test/runner) against the seeded Omega TLS 1.3 client shim.
# usage: sh om/tls/test/bogo/run.sh OMC BORINGSSL_DIR GO
#   OMC            pinned omc
#   BORINGSSL_DIR  BoringSSL checkout at BORINGSSL_COMMIT (om/tls/test/bogo/PINS)
#   GO             go binary (GO_VERSION in PINS)
# env: BOGO_OUT (default build/tls/bogo), BOGO_TEST (runner -test filter, ';'-separated globs),
#      BOGO_WORKERS (default 1)
# outputs in BOGO_OUT: shim, shim-nc (negative control), runner.test, run.log, results.json,
#   summary.txt, failures.txt, nc.log, nc.json
# Runner flags: -allow-unimplemented (the shim exits 89 for any flag or mode it does not implement:
#   server, DTLS, QUIC, TLS < 1.3, resumption, ...; those tests are SKIP, not PASS) and -loose-errors
#   (the shim cannot print BoringSSL error strings, so a test that is expected to fail passes on ANY
#   failure; expected-failure PASS counts are therefore weaker than handshake PASS counts).
# Negative control: shim-nc echoes with XOR 0xfe instead of 0xff; every test in NC_TESTS that passes
#   with the real shim must fail with shim-nc.
# Fixture patch: runner-critical-san.patch marks the runner's subjectAltName critical when the
#   certificate subject is empty (RFC 5280 4.2.1.6 MUST). Unpatched, the runner's leaves have an empty
#   subject and a non-critical SAN, which x509.om refuses (X509_ERR_MALFORMED, a deliberate strictness;
#   BoringSSL, Go and OpenSSL without -x509_strict accept it). Only the fixture changes; the test logic
#   does not. BOGO_UNPATCHED=1 skips the patch to record that policy difference.
# exit 0 PASS (no FAIL, negative control detected), 1 FAIL, 3 NOT_RUN (pin mismatch, tool missing)
set -u
omc=$1 bssl=$2 go=$3
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=${BOGO_OUT:-build/tls/bogo}
mkdir -p "$out" && out=$(cd "$out" && pwd)
pin=$(sed -n 's/^BORINGSSL_COMMIT=//p' "$here/PINS")
gov=$(sed -n 's/^GO_VERSION=//p' "$here/PINS")
have=$(git -C "$bssl" rev-parse HEAD 2>/dev/null)
if [ "$have" != "$pin" ]; then
	echo "BOGO NOT_RUN boringssl at '$have', pinned $pin"
	exit 3
fi
case $("$go" version 2>/dev/null) in
*" $gov "*) ;;
*) echo "BOGO NOT_RUN go is '$("$go" version 2>&1)', pinned $gov"; exit 3 ;;
esac
if [ -z "${BOGO_UNPATCHED:-}" ]; then
	if git -C "$bssl" apply --check "$here/runner-critical-san.patch" 2>/dev/null; then
		git -C "$bssl" apply "$here/runner-critical-san.patch" || { echo "BOGO NOT_RUN fixture patch"; exit 3; }
	elif ! git -C "$bssl" apply -R --check "$here/runner-critical-san.patch" 2>/dev/null; then
		echo "BOGO NOT_RUN fixture patch does not apply"
		exit 3
	fi
elif git -C "$bssl" apply -R --check "$here/runner-critical-san.patch" 2>/dev/null; then
	git -C "$bssl" apply -R "$here/runner-critical-san.patch"
fi
cd "$root" || exit 3
sh om/tls/build.sh "$omc" om/tls/test/bogo/shim.om "$out/shim" || { echo "BOGO FAIL shim build"; exit 1; }
sed 's/^put b i uchar xor get b i 255$/put b i uchar xor get b i 254/' om/tls/test/bogo/shim.om > "$out/shim-nc.om"
if cmp -s om/tls/test/bogo/shim.om "$out/shim-nc.om"; then
	echo "BOGO FAIL negative-control mutation did not apply"
	exit 1
fi
sh om/tls/build.sh "$omc" "$out/shim-nc.om" "$out/shim-nc" || { echo "BOGO FAIL shim-nc build"; exit 1; }
cd "$bssl/ssl/test/runner" || exit 3
"$go" test -c -o "$out/runner.test" . || { echo "BOGO NOT_RUN runner build"; exit 3; }
set -- -shim-path "$out/shim" -allow-unimplemented -loose-errors -num-workers "${BOGO_WORKERS:-1}" -json-output "$out/results.json"
[ -n "${BOGO_TEST:-}" ] && set -- "$@" -test "$BOGO_TEST"
"$out/runner.test" "$@" > "$out/run.log" 2>&1
echo "runner exit $?" >> "$out/run.log"
[ -s "$out/results.json" ] || { echo "BOGO FAIL no results.json (see $out/run.log)"; exit 1; }
# results.json is json.MarshalIndent: a test is '    "NAME": {' then "actual", "error" lines.
summ() {
	awk -v F="$2" '
	/^    "[^"]+": \{$/ { if (n != "") emit(); n = $0; sub(/^    "/, "", n); sub(/": \{$/, "", n); a = ""; e = ""; next }
	/^      "actual": / { a = $0; sub(/.*"actual": "/, "", a); sub(/".*/, "", a); next }
	/^      "error": / { e = $0; next }
	function emit(   r) {
		c[a]++
		if (a == "FAIL") {
			r = "other"
			if (match(e, /TLSERR [A-Z0-9_]+ detail [0-9]+( [A-Z0-9_]+)?/)) r = substr(e, RSTART + 7, RLENGTH - 7)
			else if (index(e, "timeout")) r = "timeout"
			else if (index(e, "unexpected success")) r = "unexpected success"
			why[r]++
			print n "\t" r > F
		}
		n = ""
	}
	END {
		if (n != "") emit()
		printf "BOGO-RESULTS tests=%d pass=%d fail=%d skip=%d\n", c["PASS"] + c["FAIL"] + c["SKIP"], c["PASS"], c["FAIL"], c["SKIP"]
		for (r in why) printf "fail-reason %d %s\n", why[r], r
	}' "$1"
}
: > "$out/failures.txt"
summ "$out/results.json" "$out/failures.txt" | sort > "$out/summary.txt"
sort -o "$out/failures.txt" "$out/failures.txt"
cat "$out/summary.txt"
# negative control over handshake tests that passed and are not expected-failure tests
awk '/^    "[^"]+": \{$/ { n = $0; sub(/^    "/, "", n); sub(/": \{$/, "", n) }
	/^      "actual": "PASS"/ { print n }' "$out/results.json" | grep -E "${NC_TESTS:-^(LargePlaintext-TLS13-Padded-|TLS13-|Basic-Client)}" | head -20 > "$out/nc.list"
if [ ! -s "$out/nc.list" ]; then
	echo "BOGO-NC NOT_RUN no passing handshake test to mutate"
	nc=notrun
else
	"$out/runner.test" -shim-path "$out/shim-nc" -allow-unimplemented -loose-errors -num-workers 1 \
		-json-output "$out/nc.json" -test "$(paste -sd';' "$out/nc.list")" > "$out/nc.log" 2>&1
	ncp=$(grep -c '^      "actual": "PASS"' "$out/nc.json")
	echo "BOGO-NC tests=$(wc -l < "$out/nc.list") still_pass=$ncp"
	if [ "$ncp" -eq 0 ]; then nc=ok; else nc=bad; fi
fi
fail=$(sed -n 's/^BOGO-RESULTS .* fail=\([0-9]*\) .*/\1/p' "$out/summary.txt")
if [ "$fail" -ne 0 ] || [ "$nc" = bad ]; then
	echo "BOGO FAIL (failures in $out/failures.txt)"
	exit 1
fi
if [ "$nc" = notrun ]; then
	echo "BOGO NOT_RUN negative control"
	exit 3
fi
echo "BOGO PASS"
exit 0
