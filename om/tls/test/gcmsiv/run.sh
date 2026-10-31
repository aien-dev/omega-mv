#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/gcmsiv.om (AES-GCM-SIV, RFC 8452).
# Usage: sh om/tls/test/gcmsiv/run.sh OMC   (OMC = the pinned seed compiler, seed/out/omc)
# Every test vector of RFC 8452 Appendix C (C.1 AEAD_AES_128_GCM_SIV, C.2 AEAD_AES_256_GCM_SIV,
# C.3 counter wrap) is converted from the committed RFC text at test time, with every intermediate
# value the RFC lists (derived keys, POLYVAL result, masked value, tag, initial counter) checked
# stage by stage, plus the section 8 worked example. Exits 0 only when every vector and refusal
# passes and every mutant is detected. Final line:
# GCMSIV PASS vectors=N (rfc8452 c1=24 c2=24 c3=2 s8=1) mutants=M/M
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
gcm="$tls/aesgcm.om"
lib="$tls/gcmsiv.om"
root=$(cd "$here/../../../.." && pwd)
out="$root/build/tls/gcmsiv"
vec="$here/vectors"
rfc_sha=ca48ec466b401ce5d68d6d1628127a1800adcc16f2573646aaa3d7df284d3c17
run() { nice -n 19 ionice -c3 "$@"; }
mkdir -p "$out"
total=0
mut=0
mutok=0
bad=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

# build LIB PROGRAM BIN: `om2`, aesgcm.om, LIB (a gcmsiv.om), the program; accepted only with rc 0
# and empty stderr
build() {
	{ printf 'om2\n'; cat "$gcm" "$1" "$2"; } > "$3.om"
	if run "$omc" < "$3.om" > "$3" 2> "$3.err" && [ ! -s "$3.err" ]; then
		chmod +x "$3"
		return 0
	fi
	say "omc refused $3.om: $(cat "$3.err")"
	return 1
}

# run_ok NAME FILE WANT: exit 0 and a complete "gcmsiv driver: WANT checks, 0 failed" line
run_ok() {
	set +e
	run "$out/driver" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	n=$(printf '%s\n' "$line" | awk '$1 == "gcmsiv" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
	if [ "$rc" -eq 0 ] && [ -n "$n" ] && [ "$n" -gt 0 ] && [ "$n" -eq "$3" ]; then
		total=$((total + n))
		say "PASS $1: $line"
	else
		fail "$1: exit $rc, want $3 checks: $(tail -n 3 "$out/$1.out" | tr '\n' ' ')"
	fi
}

# run_detect NAME FILE BIN STAGE [WANT]: detected only when BIN exits exactly 1 (a trap exits
# 100+N, a signal 128+N), prints a complete summary line with F > 0 (F = WANT when given), one
# MISMATCH line per failure, no BAD line, and at least one mismatch at STAGE
run_detect() {
	mut=$((mut + 1))
	set +e
	run "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	f=$(printf '%s\n' "$line" | awk '$1 == "gcmsiv" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $5 }')
	nm=$(grep -c '^MISMATCH line ' "$out/$1.out" || true)
	nb=$(grep -c '^BAD line ' "$out/$1.out" || true)
	ns=$(grep -c "^MISMATCH line [0-9]* stage $4\$" "$out/$1.out" || true)
	stages=$(awk '$1 == "MISMATCH" { print $5 }' "$out/$1.out" | sort | uniq -c | awk '{ printf "%s%s=%s", s, $2, $1; s = "," }')
	if [ "$rc" -eq 1 ] && [ -n "$f" ] && [ "$f" -gt 0 ] && [ "$nm" -eq "$f" ] && [ "$nb" -eq 0 ] &&
		[ "$ns" -gt 0 ] && { [ $# -lt 5 ] || [ "$f" -eq "$5" ]; }; then
		mutok=$((mutok + 1))
		say "PASS $1: detected at stage $4, exit 1, $nm MISMATCH ($stages) ($line)"
	else
		fail "$1: not detected at stage $4: exit $rc, MISMATCH $nm ($stages), BAD $nb, last: $line"
	fi
}

# mutant NAME STAGE SED: gcmsiv.om with one sed substitution (which must change exactly one line),
# compiled with the driver and run on every RFC vector; must be detected at STAGE
mutant() {
	sed "$3" "$lib" > "$out/lib-$1.om"
	nd=$(diff "$lib" "$out/lib-$1.om" | grep -c '^>' || true)
	if [ "$nd" -ne 1 ]; then
		mut=$((mut + 1))
		fail "mutant $1: sed changed $nd lines, want 1"
		return
	fi
	if build "$out/lib-$1.om" "$here/driver.om" "$out/mut-$1"; then
		run_detect "mutant-$1" "$out/rfc.txt" "$out/mut-$1" "$2"
	else
		mut=$((mut + 1))
		fail "mutant $1: build refused"
	fi
}

say "omc     sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
build "$lib" "$here/driver.om" "$out/driver" || { say "GCMSIV FAIL (driver build)"; exit 1; }
say "library sha256 $(sha256sum "$lib" | cut -d' ' -f1)"
say "library functions $(grep -c '^fn ' "$lib")"

# RFC 8452 text, committed unchanged (https://www.rfc-editor.org/rfc/rfc8452.txt)
if [ "$(sha256sum "$vec/rfc8452.txt" | cut -d' ' -f1)" != "$rfc_sha" ]; then
	say "GCMSIV FAIL (rfc8452.txt sha256 mismatch)"
	exit 1
fi

# Appendix C, every record: one G line per "Plaintext (N bytes) =" field. Field lines are
# "   Name = hex"; a value continues on deeper-indented hex-only lines (also across a page break).
# The converter checks every declared byte count, the key/nonce/intermediate sizes, that the result
# is 16 bytes longer than the plaintext, and the record counts per appendix section.
awk '{ sub(/\r$/, ""); sub(/\f/, "") }
	function h(s) { return s == "" ? "-" : s }
	function die(m) { print "rfc8452 converter: " m " (record at line " ln ")" > "/dev/stderr"; err = 1; exit 1 }
	function flush() {
		if (ln == 0) return
		if (length(v["P"]) != 2 * pb) die("plaintext length")
		if (length(v["A"]) != 2 * ab) die("aad length")
		if (length(v["R"]) != 2 * rb || rb != pb + 16) die("result length")
		if (length(v["K"]) != 32 && length(v["K"]) != 64) die("key length")
		if (length(v["E"]) != length(v["K"])) die("encryption key length")
		if (length(v["N"]) != 24) die("nonce length")
		if (length(v["H"]) != 32 || length(v["S"]) != 32 || length(v["M"]) != 32) die("intermediate length")
		if (length(v["T"]) != 32 || length(v["C"]) != 32) die("tag or counter length")
		print "G", ln, v["K"], v["N"], h(v["A"]), h(v["P"]), v["H"], v["E"], v["S"], v["M"], v["T"], v["C"], v["R"]
		cnt[sec]++
		ln = 0
	}
	/^Gueron, et al\./ { next }
	/^RFC 8452 / { next }
	/^$/ { next }
	/^C\.1\.  AEAD_AES_128_GCM_SIV$/ { flush(); sec = "c1"; next }
	/^C\.2\.  AEAD_AES_256_GCM_SIV$/ { flush(); sec = "c2"; next }
	/^C\.3\.  Counter Wrap Tests$/ { flush(); sec = "c3"; next }
	/^Acknowledgements$/ { flush(); sec = ""; next }
	sec == "" { next }
	/^   [^ ].* =/ {
		name = $0; sub(/ =.*/, "", name); sub(/^   /, "", name)
		val = $0; sub(/^[^=]*= */, "", val); gsub(/ /, "", val)
		if (name ~ /^Plaintext \([0-9]+ bytes\)$/) {
			flush(); ln = NR; split("", v); pb = name; gsub(/[^0-9]/, "", pb); pb += 0; f = "P"
		} else if (name ~ /^AAD \([0-9]+ bytes\)$/) {
			ab = name; gsub(/[^0-9]/, "", ab); ab += 0; f = "A"
		} else if (name ~ /^Result \([0-9]+ bytes\)$/) {
			rb = name; gsub(/[^0-9]/, "", rb); rb += 0; f = "R"
		} else if (name == "Key") f = "K"
		else if (name == "Nonce") f = "N"
		else if (name == "Record authentication key") f = "H"
		else if (name == "Record encryption key") f = "E"
		else if (name == "POLYVAL input") f = "I"
		else if (name == "POLYVAL result") f = "S"
		else if (name == "POLYVAL result XOR nonce") f = "X"
		else if (name == "... and masked") f = "M"
		else if (name == "Tag") f = "T"
		else if (name == "Initial counter") f = "C"
		else die("unknown field " name)
		if (val !~ /^[0-9a-f]*$/) die("non-hex value in " name)
		v[f] = val
		next
	}
	/^ +[0-9a-f]+$/ { if (f == "") die("orphan continuation"); val = $0; gsub(/ /, "", val); v[f] = v[f] val; next }
	{ f = "" }
	END {
		if (err) exit 1
		flush()
		if (cnt["c1"] != 24 || cnt["c2"] != 24 || cnt["c3"] != 2) {
			print "rfc8452 converter: counts c1=" cnt["c1"] " c2=" cnt["c2"] " c3=" cnt["c3"] ", want 24 24 2" > "/dev/stderr"
			exit 1
		}
	}' "$vec/rfc8452.txt" > "$out/rfc.txt" || { say "GCMSIV FAIL (rfc8452 conversion)"; exit 1; }
np=$(grep -c 'Plaintext (' "$vec/rfc8452.txt" || true)
ng=$(grep -c '^G ' "$out/rfc.txt")
[ "$ng" -eq 50 ] && [ "$ng" -eq "$np" ] || { say "GCMSIV FAIL (converted $ng records, RFC has $np plaintext fields)"; exit 1; }
c1=$(awk '$1 == "G" && $2 < 1634' "$out/rfc.txt" | wc -l | tr -d ' ')
run_ok rfc8452-appendix-c "$out/rfc.txt" $((8 * ng))

# Section 8 worked example (hand-copied from the RFC; the converter does not parse section 8)
run_ok rfc8452-section8 "$vec/section8.txt" 8

# parameter refusals and boundary lengths
{
	say "# P KNLEN AADLEN BUFLEN WANT"
	say "P 0 0 16 2"
	say "P 16 0 16 2"
	say "P 27 0 16 2"
	say "P 29 0 16 2"
	say "P 32 0 16 2"
	say "P 36 0 16 2"
	say "P 43 0 16 2"
	say "P 45 0 16 2"
	say "P 28 0 0 2"
	say "P 28 0 15 2"
	say "P 44 5 15 2"
	say "P 28 0 16 0"
	say "P 44 0 16 0"
	say "P 28 65536 65536 0"
} > "$out/params.txt"
run_ok params "$out/params.txt" 14

# mutants, one per stage, each run on every Appendix C vector
# POLYVAL: mulX_GHASH without the reduction (the POLYVAL key is H * x unreduced)
mutant polyval polyval 's/^h = xor shr h 1 and m AGR$/h = shr h 1/'
# key derivation: the LE32 block counter placed in byte 1 instead of byte 0
mutant derive derive 's/^put s add 150 mul b 2 or shl add c b 56 shr h 32$/put s add 150 mul b 2 or shl add c b 48 shr h 32/'
# tag input: bit 7 of byte 15 of S ^ nonce not cleared
mutant mask mask 's/^put k 126 and get k 126 GSMSK$/put k 126 get k 126/'
# counter block: bit 7 of byte 15 of the tag not set
mutant ctrbit counter 's/^put k 128 or tl 0x80$/put k 128 tl/'
# tag check bypassed (open accepts any tag)
mutant tagcheck refuse 's/^d = or xor get k 125 th xor get k 126 tl$/d = ulong 0/'
# expectation mutant: the last tag hex digit of the first C.1 record changed -> exactly 1 failure
awk '$1 == "G" && !done { c = substr($11, 32, 1); $11 = substr($11, 1, 31) (c == "0" ? "1" : "0"); done = 1 } { print }' \
	"$out/rfc.txt" > "$out/mut-expect.txt"
if cmp -s "$out/rfc.txt" "$out/mut-expect.txt"; then
	mut=$((mut + 1))
	fail "expectation mutant: awk changed nothing"
else
	run_detect mutant-expect "$out/mut-expect.txt" "$out/driver" tag 1
fi

c2=$(awk '$1 == "G" && $2 >= 1634 && $2 < 2247' "$out/rfc.txt" | wc -l | tr -d ' ')
c3=$(awk '$1 == "G" && $2 >= 2247' "$out/rfc.txt" | wc -l | tr -d ' ')
say "NOT_RUN wycheproof aes_gcm_siv_test.json: not vendored"

if [ "$bad" -ne 0 ] || [ "$mutok" -ne "$mut" ]; then
	say "GCMSIV FAIL vectors=$((ng + 1)) mutants=$mutok/$mut"
	exit 1
fi
say "GCMSIV PASS vectors=$((ng + 1)) (rfc8452 c1=$c1 c2=$c2 c3=$c3 s8=1) checks=$total mutants=$mutok/$mut"
