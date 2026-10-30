#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/aesgcm.om (AES, FIPS 197; AES-GCM, NIST SP 800-38D; TLS AEAD_AES_128_GCM).
# Usage: sh om/tls/test/aesgcm/run.sh OMC   (OMC = the pinned seed compiler, /tmp/omtls/seed/out/omc)
# Exits 0 only when every vector passes, the tamper controls are refused, every mutant is detected and
# the 1 MiB round trip holds. Final line: AESGCM PASS vectors=N (nist=..., wycheproof=W) mutants=M/M.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)/aesgcm.om
root=$(cd "$here/../../../.." && pwd)
out="$root/build/tls/aesgcm"
vec="$here/vectors"
zip_sha=f9fc479e134cde2980b3bb7cddbcb567b2cd96fd753835243ed067699f26a023
run() { nice -n 19 ionice -c3 "$@"; }
mkdir -p "$out"
total=0
nist=0
mut=0
mutok=0
bad=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

# build LIB PROGRAM BIN: `om2`, the library, the program; accepted only with rc 0 and empty stderr
build() {
	{ printf 'om2\n'; cat "$1" "$2"; } > "$3.om"
	if run "$omc" < "$3.om" > "$3" 2> "$3.err" && [ ! -s "$3.err" ]; then
		chmod +x "$3"
		return 0
	fi
	say "omc refused $3.om: $(cat "$3.err")"
	return 1
}

# run_ok NAME FILE WANT: exit 0 and a complete "aesgcm driver: WANT checks, 0 failed" line, WANT
# being the number of records the converter wrote (no record may be lost); adds WANT to total
run_ok() {
	set +e
	run "$out/driver" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	n=$(printf '%s\n' "$line" | awk '$1 == "aesgcm" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
	if [ "$rc" -eq 0 ] && [ -n "$n" ] && [ "$n" -gt 0 ] && [ "$n" -eq "$3" ]; then
		total=$((total + n))
		say "PASS $1: $line"
	else
		fail "$1: exit $rc, want $3 checks: $(tail -n 3 "$out/$1.out" | tr '\n' ' ')"
	fi
}

# run_detect NAME FILE BIN [WANT]: detected only when BIN exits exactly 1 (the driver's own "a check
# failed"; a trap exits 100+N, a signal 128+N), prints a complete summary line with F > 0 (F = WANT
# when given), one MISMATCH line per failure and no BAD (malformed record) line.
run_detect() {
	mut=$((mut + 1))
	set +e
	run "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	f=$(printf '%s\n' "$line" | awk '$1 == "aesgcm" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $5 }')
	nm=$(grep -c '^MISMATCH line ' "$out/$1.out" || true)
	nb=$(grep -c '^BAD line ' "$out/$1.out" || true)
	if [ "$rc" -eq 1 ] && [ -n "$f" ] && [ "$f" -gt 0 ] && [ "$nm" -eq "$f" ] && [ "$nb" -eq 0 ] &&
		{ [ $# -lt 4 ] || [ "$f" -eq "$4" ]; }; then
		mutok=$((mutok + 1))
		say "PASS $1: detected, exit 1, $nm MISMATCH ($line)"
	else
		fail "$1: not detected: exit $rc, MISMATCH $nm, BAD $nb, last: $line"
	fi
}

# mutant NAME SED: the library with one sed substitution (which must change exactly one line),
# compiled with the driver and run on FIPS 197 + Wycheproof; must be detected
mutant() {
	sed "$2" "$lib" > "$out/lib-$1.om"
	nd=$(diff "$lib" "$out/lib-$1.om" | grep -c '^>' || true)
	if [ "$nd" -ne 1 ]; then
		mut=$((mut + 1))
		fail "mutant $1: sed changed $nd lines, want 1"
		return
	fi
	if build "$out/lib-$1.om" "$here/driver.om" "$out/mut-$1"; then
		run_detect "mutant-$1" "$out/small.txt" "$out/mut-$1"
	else
		mut=$((mut + 1))
		fail "mutant $1: build refused"
	fi
}

say "omc     sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
build "$lib" "$here/driver.om" "$out/driver" || { say "AESGCM FAIL (driver build)"; exit 1; }
say "library sha256 $(sha256sum "$lib" | cut -d' ' -f1)"
say "library functions $(grep -c '^fn ' "$lib")"

# FIPS 197 Appendix C.1-C.3 (+ Appendix B)
nf=$(grep -c '^B ' "$vec/fips197.txt")
run_ok fips197 "$vec/fips197.txt" "$nf"

# Wycheproof aes_gcm_test.json, every test (converted by gen_wycheproof.py; header records the
# commit and sha256)
nw=$(grep -c '^W ' "$vec/wycheproof_aes_gcm.txt")
run_ok wycheproof "$vec/wycheproof_aes_gcm.txt" "$nw"
wv=$(grep -c '^W [0-9]* V ' "$vec/wycheproof_aes_gcm.txt")
wi=$(grep -c '^W [0-9]* I ' "$vec/wycheproof_aes_gcm.txt")

# NIST CAVP gcmtestvectors.zip, committed unchanged; converted at test time into build/
if [ "$(sha256sum "$vec/gcmtestvectors.zip" | cut -d' ' -f1)" != "$zip_sha" ]; then
	fail "gcmtestvectors.zip sha256 mismatch"
else
	for f in gcmEncryptExtIV128 gcmEncryptExtIV192 gcmEncryptExtIV256; do
		unzip -p "$vec/gcmtestvectors.zip" "$f.rsp" | awk '{ sub(/\r$/, "") }
			function h(s) { return s == "" ? "-" : s }
			/^Count = / { c++ }
			/^Key = / { k = $3 } /^IV = / { iv = $3 } /^PT = / { pt = $3 }
			/^AAD = / { a = $3 } /^CT = / { ct = $3 }
			/^Tag = / { print "E", h(k), h(iv), h(a), h(pt), h(ct), h($3); n++ }
			END { if (c != n || n == 0) exit 1 }' > "$out/$f.txt" || fail "$f: conversion lost records"
		nc=$(unzip -p "$vec/gcmtestvectors.zip" "$f.rsp" | grep -c '^Count = ')
		run_ok "$f" "$out/$f.txt" "$nc"
		nist=$((nist + nc))
	done
	nfail=0
	for f in gcmDecrypt128 gcmDecrypt192 gcmDecrypt256; do
		unzip -p "$vec/gcmtestvectors.zip" "$f.rsp" | awk '{ sub(/\r$/, "") }
			function h(s) { return s == "" ? "-" : s }
			/^Count = / { c++ }
			/^Key = / { k = $3 } /^IV = / { iv = $3 } /^CT = / { ct = $3 }
			/^AAD = / { a = $3 } /^Tag = / { t = $3 }
			/^PT = / { print "D", h(k), h(iv), h(a), h(ct), h(t), h($3); n++ }
			/^FAIL$/ { print "D", h(k), h(iv), h(a), h(ct), h(t), "FAIL"; n++ }
			END { if (c != n || n == 0) exit 1 }' > "$out/$f.txt" || fail "$f: conversion lost records"
		nc=$(unzip -p "$vec/gcmtestvectors.zip" "$f.rsp" | grep -c '^Count = ')
		run_ok "$f" "$out/$f.txt" "$nc"
		nist=$((nist + nc))
		nfail=$((nfail + $(grep -c ' FAIL$' "$out/$f.txt")))
	done
	say "nist: $nist vectors ($nfail decrypt vectors expect FAIL)"
fi

# parameter refusals: return 2, nothing written. ctl = klen + 256 tlen + 65536 ivlen.
ctl() { printf '%s' $(($1 + 256 * $2 + 65536 * $3)); }
{
	say "# X CTL KIALEN BUFLEN WANT: aes_gcm_seal and aes_gcm_open both return WANT"
	say "X $(ctl 15 16 12) 27 16 2"
	say "X $(ctl 0 16 12) 12 16 2"
	say "X $(ctl 17 16 12) 29 16 2"
	say "X $(ctl 33 16 12) 45 16 2"
	say "X $(ctl 255 16 12) 300 16 2"
	say "X $(ctl 16 0 12) 28 16 2"
	say "X $(ctl 16 17 12) 28 32 2"
	say "X $(ctl 16 255 12) 28 300 2"
	say "X $(ctl 16 16 0) 16 16 2"
	say "X $(ctl 16 16 12) 27 16 2"
	say "X $(ctl 32 16 1000) 1031 16 2"
	say "X $(ctl 16 16 12) 28 15 2"
	say "X $(ctl 24 1 1) 24 1 2"
	say "X $(ctl 24 4 1) 25 3 2"
	say "# Y KNLEN AADLEN BUFLEN WANT: aead_aes128gcm_seal and _open both return WANT"
	say "Y 27 0 16 2"
	say "Y 29 0 16 2"
	say "Y 0 0 16 2"
	say "Y 28 5 15 2"
	say "Y 28 0 0 2"
} > "$out/params.txt"
run_ok params "$out/params.txt" 19

# 1 MiB in-place seal and open (three runs; the first warms up), throughput printed by the driver
printf 'T 1048576\nT 1048576\nT 1048576\n' > "$out/speed.txt"
run_ok speed "$out/speed.txt" 3
grep '^aesgcm speed ' "$out/speed.out" || true

# mutants run on FIPS 197 + Wycheproof
cat "$vec/fips197.txt" "$vec/wycheproof_aes_gcm.txt" > "$out/small.txt"
# S-box circuit: output gate S2 = T123 # T124 (XNOR) flipped to XOR
mutant sbox 's/^put s 5 not xor get s 130 get s 131$/put s 5 xor get s 130 get s 131/'
# GHASH reduction polynomial R = 0xe1 || 0^120 changed to 0xe3 || 0^120
mutant ghash 's/^const AGR ulong 0xe100000000000000$/const AGR ulong 0xe300000000000000/'
# key schedule Rcon: the xtime reduction 0x11b changed to 0x11d (Rcon[9], Rcon[10] wrong)
mutant rcon 's/^rc = xor rc 0x11b$/rc = xor rc 0x11d/'
# tag check bypassed (open accepts any tag): caught by the tamper controls and the invalid vectors
mutant tagcheck 's/^d = or d xor get buf add n i gcm_tbyte k i$/d = or d 0/'
# expectation mutant: one expected tag hex digit of Wycheproof tcId 1 changed -> exactly 1 failure
awk '$1 == "W" && $2 == "1" { c = substr($9, 32, 1); $9 = substr($9, 1, 31) (c == "0" ? "1" : "0") } { print }' \
	"$vec/wycheproof_aes_gcm.txt" > "$out/mut-expect.txt"
if cmp -s "$vec/wycheproof_aes_gcm.txt" "$out/mut-expect.txt"; then
	mut=$((mut + 1))
	fail "expectation mutant: awk changed nothing"
else
	run_detect mutant-expect "$out/mut-expect.txt" "$out/driver" 1
fi

say "NOT_RUN wycheproof aes_gcm_siv_test.json: AES-GCM-SIV (RFC 8452) is not implemented"
say "NOT_RUN wycheproof aria_gcm_test.json, seed_gcm_test.json, sm4_gcm_test.json: other block ciphers"
say "NOT_RUN wycheproof c2sp_chunked_encryption_aes_{128,256}_gcm_test.json: chunked-encryption construction, not raw AES-GCM"

if [ "$bad" -ne 0 ] || [ "$mutok" -ne "$mut" ]; then
	say "AESGCM FAIL vectors=$total mutants=$mutok/$mut"
	exit 1
fi
say "wycheproof: $nw tests: valid $wv passed, invalid $wi refused, acceptable 0"
say "checks outside the vectors: params=19 speed=3 (both passed)"
say "AESGCM PASS vectors=$((nf + nist + nw)) (fips197=$nf, nist=$nist, wycheproof=$nw) mutants=$mutok/$mut"
