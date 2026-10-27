#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/chacha.om (ChaCha20, Poly1305, AEAD_CHACHA20_POLY1305, RFC 8439).
# Usage: sh om/tls/test/chacha/run.sh OMC   (OMC = the pinned seed compiler, /tmp/omtls/seed/out/omc)
# Exits 0 only when every vector passes, the tamper controls are refused, every mutant is detected
# and the 1 MiB round trip holds. Final line: CHACHA PASS vectors=N (wycheproof=W) mutants=M/M.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)/chacha.om
root=$(cd "$here/../../../.." && pwd)
out="$root/build/tls/chacha"
vec="$here/vectors"
wyc=${WYCHEPROOF:-/tmp/wycheproof}
wyc_commit=d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9
run() { nice -n 19 ionice -c3 "$@"; }
mkdir -p "$out"
total=0
wtotal=0
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

# run_ok NAME FILE: exit 0 and a complete "chacha driver: N checks, 0 failed" line; adds N to total
run_ok() {
	set +e
	run "$out/driver" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	n=$(printf '%s\n' "$line" | awk '$1 == "chacha" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
	if [ "$rc" -eq 0 ] && [ -n "$n" ] && [ "$n" -gt 0 ]; then
		total=$((total + n))
		last=$n
		say "PASS $1: $line"
	else
		last=0
		fail "$1: exit $rc: $(tail -n 3 "$out/$1.out" | tr '\n' ' ')"
	fi
}

# run_detect NAME FILE BIN [WANT]: detected only when BIN exits exactly 1 (the driver's own "a check
# failed"; a trap exits 100+N, a signal 128+N), prints a complete summary line with F > 0 (F = WANT
# when given), at least one MISMATCH line and no BAD (malformed record) line.
run_detect() {
	mut=$((mut + 1))
	set +e
	run "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	f=$(printf '%s\n' "$line" | awk '$1 == "chacha" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $5 }')
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

say "omc     sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
build "$lib" "$here/driver.om" "$out/driver" || { say "CHACHA FAIL (driver build)"; exit 1; }
say "library sha256 $(sha256sum "$lib" | cut -d' ' -f1)"
say "driver  sha256 $(sha256sum "$out/driver" | cut -d' ' -f1)"

# 1. RFC 8439 sections 2.1.1-2.8.2 and Appendix A.1-A.5 (gen_rfc.py over the committed RFC text)
run_ok rfc8439 "$vec/rfc8439-vectors.txt"

# 2. Wycheproof (committed conversions; regenerated and compared when the pinned clone is present)
if [ -d "$wyc/.git" ] && [ "$(git -C "$wyc" rev-parse HEAD)" = "$wyc_commit" ] && command -v python3 > /dev/null 2>&1; then
	python3 "$here/gen_wycheproof.py" aead "$wyc/testvectors_v1/chacha20_poly1305_test.json" "$wyc_commit" > "$out/wp-aead.txt" 2> /dev/null
	python3 "$here/gen_wycheproof.py" counter "$wyc/testvectors_v1/chacha20_counter_test.json" "$wyc_commit" > "$out/wp-counter.txt" 2> /dev/null
	if cmp -s "$out/wp-aead.txt" "$vec/wycheproof-chacha20-poly1305.txt" && cmp -s "$out/wp-counter.txt" "$vec/wycheproof-chacha20-counter.txt"; then
		say "PASS wycheproof-regenerate: committed conversions equal a fresh conversion at $wyc_commit"
	else
		fail "wycheproof-regenerate: committed conversions differ from $wyc"
	fi
else
	say "NOTE wycheproof clone at $wyc_commit not found: committed conversions used without regeneration"
fi
run_ok wycheproof-chacha20-poly1305 "$vec/wycheproof-chacha20-poly1305.txt"
wtotal=$((wtotal + last))
run_ok wycheproof-chacha20-counter "$vec/wycheproof-chacha20-counter.txt"
wtotal=$((wtotal + last))
say "NOTE wycheproof chacha20_poly1305_test.json: $(sed -n 3p "$vec/wycheproof-chacha20-poly1305.txt" | cut -c3-) (0 valid sealed+opened, 1 invalid tag refused, 2 invalid nonce size refused; no acceptable)"
say "NOTE wycheproof chacha20_counter_test.json: $(sed -n 3p "$vec/wycheproof-chacha20-counter.txt" | cut -c3-) (NOT_RUN = XChaCha20 groups, not implemented)"

# 3. differential corpus against python cryptography (OpenSSL), seeded
if python3 -c 'import cryptography' > /dev/null 2>&1; then
	seed=${CHACHA_SEED:-1}
	python3 "$here/gen_diff.py" "$seed" 903 > "$out/diff.txt"
	run_ok "cryptography-diff(seed $seed)" "$out/diff.txt"
else
	say "NOT_RUN cryptography-diff: python3 cryptography not importable"
fi

# 4. tamper controls: the RFC 8439 2.8.2 record with one ciphertext byte, then one tag byte, flipped
#    and the expected code set to 1; the driver passes only if open returns 1 and zeroes the buffer
grep '^A 808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f070000004041424344454647 ' \
	"$vec/rfc8439-vectors.txt" > "$out/aead282.txt"
[ "$(wc -l < "$out/aead282.txt")" -eq 1 ] || fail "tamper: RFC 2.8.2 record not found"
awk '{ c = substr($5, 1, 1); $5 = (c == "0" ? "1" : "0") substr($5, 2); $6 = 1; print }' "$out/aead282.txt" > "$out/tamper-ct.txt"
awk '{ n = length($5); c = substr($5, n, 1); $5 = substr($5, 1, n - 1) (c == "0" ? "1" : "0"); $6 = 1; print }' "$out/aead282.txt" > "$out/tamper-tag.txt"
awk '{ c = substr($3, 1, 1); $3 = (c == "0" ? "1" : "0") substr($3, 2); $6 = 1; print }' "$out/aead282.txt" > "$out/tamper-aad.txt"
cat "$out/tamper-ct.txt" "$out/tamper-tag.txt" "$out/tamper-aad.txt" > "$out/tamper.txt"
run_ok tamper-refused "$out/tamper.txt"
# and the same tampered records claimed valid (code 0) must be reported as mismatches
awk '{ $6 = 0; print }' "$out/tamper.txt" > "$out/tamper-claimed.txt"
set +e
run "$out/driver" < "$out/tamper-claimed.txt" > "$out/tamper-claimed.out" 2>&1
rc=$?
set -e
if [ "$rc" -eq 1 ] && [ "$(tail -n 1 "$out/tamper-claimed.out")" = "chacha driver: 3 checks, 3 failed" ]; then
	say "PASS tamper-claimed-valid: 3 of 3 tampered records rejected"
else
	fail "tamper-claimed-valid: exit $rc: $(tail -n 1 "$out/tamper-claimed.out")"
fi

cat "$vec/rfc8439-vectors.txt" "$vec/wycheproof-chacha20-poly1305.txt" > "$out/mutant-input.txt"

# 5. expectation mutant: one hex digit of the RFC 2.5.2 Poly1305 tag changed -> exactly 1 failure
awk '/^P 85d6be78/ && !d { n = length($4); c = substr($4, n, 1); $4 = substr($4, 1, n - 1) (c == "0" ? "1" : "0"); d = 1 } { print }' \
	"$vec/rfc8439-vectors.txt" > "$out/mut-expect.txt"
if cmp -s "$vec/rfc8439-vectors.txt" "$out/mut-expect.txt"; then
	mut=$((mut + 1))
	fail "mutant-expectation: mutation did not change the vectors"
else
	run_detect mutant-expectation "$out/mut-expect.txt" "$out/driver" 1
fi

# 6. library mutants (each a real constant/operator, one line, checked to change the source)
# lib_mutant NAME AWK-PROGRAM
lib_mutant() {
	awk "$2" "$lib" > "$out/mut-$1.lib"
	if cmp -s "$lib" "$out/mut-$1.lib"; then
		mut=$((mut + 1))
		fail "mutant-$1: mutation did not change the library"
	elif build "$out/mut-$1.lib" "$here/driver.om" "$out/mut-$1"; then
		run_detect "mutant-$1" "$out/mutant-input.txt" "$out/mut-$1"
	else
		mut=$((mut + 1))
		fail "mutant-$1: build failed"
	fi
}
# ChaCha20 quarter round: first rotation 16 -> 15
lib_mutant qr-rotation '$0 == "d = rotl xor d a 16" { $0 = "d = rotl xor d a 15" } { print }'
# Poly1305 clamp: r[15] mask 15 -> 31 (one more bit of r kept)
lib_mutant poly-clamp '$0 == "put c 15 and get c 15 15" { $0 = "put c 15 and get c 15 31" } { print }'
# Poly1305 2^130 - 5 fold in the block multiply: carry * 5 -> carry * 4 (first occurrence only)
lib_mutant poly-fold '$0 == "d = add get p 9 mul c 5" && !d { $0 = "d = add get p 9 mul c 4"; d = 1 } { print }'
# Poly1305 final reduction: the h / h - p select mask inverted
lib_mutant poly-final '$0 == "m = sub shr d 63 1" { $0 = "m = sub 0 shr d 63" } { print }'

# 7. 1 MiB seal/open round trip (bench.om), output compared with python cryptography, timed
if build "$lib" "$here/bench.om" "$out/bench"; then
	t0=$(date +%s%N)
	set +e
	run "$out/bench" > "$out/bench.bin"
	rc=$?
	set -e
	t1=$(date +%s%N)
	got=$(sha256sum "$out/bench.bin" | cut -d' ' -f1)
	if [ "$rc" -ne 0 ] || [ "$(wc -c < "$out/bench.bin")" -ne 1048592 ]; then
		fail "bench-1MiB: exit $rc, $(wc -c < "$out/bench.bin") bytes"
	elif python3 -c 'import cryptography' > /dev/null 2>&1; then
		want=$(python3 -c '
import hashlib
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
kn = bytes(range(44)); ad = bytes(range(160, 172))
pt = bytes((31 * i + 7) & 255 for i in range(1 << 20))
print(hashlib.sha256(ChaCha20Poly1305(kn[:32]).encrypt(kn[32:], pt, ad)).hexdigest())')
		if [ "$got" = "$want" ]; then
			say "PASS bench-1MiB: 16 open+reseal rounds restored the plaintext; sealed sha256 $got equals cryptography"
		else
			fail "bench-1MiB: sealed sha256 $got, cryptography $want"
		fi
	else
		say "PASS bench-1MiB: 16 open+reseal rounds restored the plaintext (NOT_RUN oracle: no python cryptography)"
	fi
	ns=$((t1 - t0))
	say "THROUGHPUT bench-1MiB: 33 MiB through seal/open in $ns ns = $(awk -v n="$ns" 'BEGIN { printf "%.1f", 33 * 1048576 / (n / 1e9) / 1e6 }') MB/s (whole process, incl. start-up and pattern checks)"
else
	fail "bench-1MiB: build failed"
fi

if [ "$bad" -ne 0 ]; then
	say "CHACHA FAIL vectors=$total (wycheproof=$wtotal) mutants=$mutok/$mut"
	exit 1
fi
say "CHACHA PASS vectors=$total (wycheproof=$wtotal) mutants=$mutok/$mut"
