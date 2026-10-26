#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/sha2.om and om/tls/hkdf.om. Usage (from the omega-mv root):
#   sh om/tls/test/sha2/run.sh /tmp/omtls/seed/out/omc
# Builds into build/tls/sha2/. Exits 0 only when every vector passes and every mutant and negative
# control is detected. Final line: SHA2 PASS vectors=N (wycheproof=W) mutants=M/M, or SHA2 FAIL.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)
root=$(cd "$lib/../.." && pwd)
out="$root/build/tls/sha2"
vec="$here/vectors"
mkdir -p "$out"
nice="nice -n 19 ionice -c3"
checks=0
vectors=0
wyche=0
mut=0
mutok=0
bad=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

# build SHA2 HKDF BIN: om2 line, the two libraries, the driver; compile with omc
build() {
	{ printf 'om2\n'; cat "$1" "$2" "$here/driver.om"; } > "$3.om"
	if ! $nice "$omc" < "$3.om" > "$3" 2> "$3.err" || [ -s "$3.err" ]; then
		say "omc refused $3.om: $(cat "$3.err")"
		return 1
	fi
	chmod +x "$3"
}

# vectors in FILE: one per record line, 100 per Monte record
count() {
	awk '/^[HGTPX] / { n++ } /^M / { n += 100 } END { print n + 0 }' "$1"
}

# run_ok NAME FILE: exit 0 and "sha2 driver: N checks, 0 failed", N > 0
run_ok() {
	set +e
	$nice "$out/driver" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	n=$(printf '%s\n' "$line" | awk '$1 == "sha2" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
	if [ "$rc" -eq 0 ] && [ -n "$n" ] && [ "$n" -gt 0 ]; then
		checks=$((checks + n))
		v=$(count "$2")
		vectors=$((vectors + v))
		say "PASS $1: $v vectors, $line"
	else
		fail "$1: exit $rc: $(tail -n 3 "$out/$1.out" | tr '\n' ' ')"
	fi
}

# detected NAME FILE BIN [WANT]: BIN completed normally and reported a difference: exit status
# exactly 1 (a trap exits 100+N, a signal 128+N) and a complete summary line with F > 0 failures
# (exactly WANT when given)
detected() {
	set +e
	$nice "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	last=$(tail -n 1 "$out/$1.out")
	f=$(printf '%s\n' "$last" | awk '$1 == "sha2" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $5 }')
	ok=0
	if [ "$rc" -eq 1 ] && [ -n "$f" ] && [ "$f" -gt 0 ]; then
		ok=1
		if [ $# -ge 4 ] && [ "$f" -ne "$4" ]; then
			ok=0
		fi
	fi
	if [ "$ok" -eq 1 ]; then
		say "PASS $1: detected ($(grep -c '^MISMATCH\|^BAD' "$out/$1.out") failure lines; $last)"
		return 0
	fi
	fail "$1: not detected: exit $rc: $last"
	return 1
}

# mutant NAME FILE SED_EXPR WHICH: apply SED_EXPR to sha2.om (WHICH sha2) or hkdf.om (WHICH hkdf),
# rebuild the driver, and require detection on FILE
mutant() {
	mut=$((mut + 1))
	s="$lib/sha2.om"
	h="$lib/hkdf.om"
	if [ "$4" = sha2 ]; then
		sed "$3" "$lib/sha2.om" > "$out/mut-$1.lib"
		cmp -s "$lib/sha2.om" "$out/mut-$1.lib" && { fail "$1: sed changed nothing"; return; }
		s="$out/mut-$1.lib"
	else
		sed "$3" "$lib/hkdf.om" > "$out/mut-$1.lib"
		cmp -s "$lib/hkdf.om" "$out/mut-$1.lib" && { fail "$1: sed changed nothing"; return; }
		h="$out/mut-$1.lib"
	fi
	build "$s" "$h" "$out/mut-$1" || { fail "$1: mutant did not compile"; return; }
	if detected "mutant-$1" "$2" "$out/mut-$1"; then
		mutok=$((mutok + 1))
	fi
}

build "$lib/sha2.om" "$lib/hkdf.om" "$out/driver" || { say "SHA2 FAIL build"; exit 1; }
say "omc     sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
say "sha2.om sha256 $(sha256sum "$lib/sha2.om" | cut -d' ' -f1)"
say "hkdf.om sha256 $(sha256sum "$lib/hkdf.om" | cut -d' ' -f1)"

# NIST CAVP SHAVS byte-oriented .rsp -> driver records (Len is in bits; Msg is "00" when Len = 0)
for a in 256 384 512; do
	for k in ShortMsg LongMsg; do
		awk -v a=$a '{ sub(/\r$/, "") }
			/^Len = / { n = $3 }
			/^Msg = / { m = $3 }
			/^MD = /  { if (n == 0) m = "-"; print "H", a, m, $3 }' \
			"$vec/SHA$a$k.rsp" > "$out/nist-$a-$k.txt"
	done
	awk -v a=$a '{ sub(/\r$/, "") }
		/^Seed = / { printf "M %s %s", a, $3 }
		/^MD = /   { printf " %s", $3; k++ }
		END        { print ""; if (k != 100) exit 1 }' \
		"$vec/SHA${a}Monte.rsp" > "$out/nist-$a-Monte.txt"
done
for a in 256 384 512; do
	for k in ShortMsg LongMsg Monte; do
		run_ok "nist-sha$a-$k" "$out/nist-$a-$k.txt"
	done
done
run_ok rfc4231-hmac "$vec/rfc4231-hmac.txt"
run_ok rfc5869-hkdf "$vec/rfc5869-hkdf.txt"
run_ok generated-long "$vec/generated-long.txt"
w0=$vectors
run_ok wycheproof-hmac "$vec/wycheproof-hmac.txt"
run_ok wycheproof-hkdf "$vec/wycheproof-hkdf.txt"
wyche=$((vectors - w0))

# source mutants (each must give MISMATCH lines and exit 1, never a trap)
mutant sha256-k0 "$out/nist-256-ShortMsg.txt" 's/^put k 0 0x428a2f98$/put k 0 0x428a2f99/' sha2
mutant sha512-k0 "$out/nist-512-ShortMsg.txt" 's/^put k 0 0x428a2f98d728ae22$/put k 0 0x428a2f98d728ae23/' sha2
mutant hmac-ipad "$vec/rfc4231-hmac.txt" 's/^put pad i xor get kb i 0x36$/put pad i xor get kb i 0x37/' hkdf
mutant hkdf-ctr "$vec/wycheproof-hkdf.txt" 's/^put t 64 uchar n$/put t 64 uchar xor n 1/' hkdf

# expectation mutant: one MD hex digit changed -> exactly 2 failures (one-shot and split)
mut=$((mut + 1))
awk 'NR == 10 { c = substr($4, 1, 1); $4 = (c == "0" ? "1" : "0") substr($4, 2) } { print }' \
	"$out/nist-256-ShortMsg.txt" > "$out/mut-expect.txt"
if cmp -s "$out/nist-256-ShortMsg.txt" "$out/mut-expect.txt"; then
	fail "expectation mutant changed nothing"
elif detected mutant-expect "$out/mut-expect.txt" "$out/driver" 2; then
	mutok=$((mutok + 1))
fi

# negative controls: a malformed record (odd hex digit count) is refused; a valid HMAC vector with
# one tag byte flipped is refused (the driver also flips a message byte of every valid T record and
# an OKM byte of every valid X record, see driver.om)
printf 'H 256 61626 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n' > "$out/neg-malformed.txt"
detected neg-malformed "$out/neg-malformed.txt" "$out/driver" 1 || true
awk 'NR == 4 { c = substr($5, 1, 2); $5 = (c == "00" ? "01" : "00") substr($5, 3) } { print }' \
	"$vec/rfc4231-hmac.txt" > "$out/neg-tag.txt"
detected neg-tag-flip "$out/neg-tag.txt" "$out/driver" || true

if [ "$bad" -ne 0 ]; then
	say "SHA2 FAIL vectors=$vectors (wycheproof=$wyche) mutants=$mutok/$mut"
	exit 1
fi
say "SHA2 PASS vectors=$vectors (wycheproof=$wyche) mutants=$mutok/$mut"
