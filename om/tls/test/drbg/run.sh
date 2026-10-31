#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/drbg.om (HMAC_DRBG, SP 800-90A Rev.1, HMAC-SHA-256). Usage (from the
# omega-mv root):
#   sh om/tls/test/drbg/run.sh /tmp/omtls/seed/out/omc
# Tested against the ACVP/CAVP HMAC_DRBG test methods; not NIST-validated. Builds into
# build/tls/drbg/. Exits 0 only when every vector and API check passes and every mutant and negative
# control is detected. Final line: DRBG PASS vectors=N (acvp=A, cavp=C) api=K mutants=M/M, or DRBG FAIL.
# vectors/*.txt are written by convert.py from the pinned ACVP-Server and CAVP files (see their
# headers); the driver also flips one expected byte of every D record and requires a mismatch.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)
root=$(cd "$lib/../.." && pwd)
out="$root/build/tls/drbg"
vec="$here/vectors"
mkdir -p "$out"
nice="nice -n 19 ionice -c3"
vectors=0
acvp=0
cavp=0
api=0
mut=0
mutok=0
bad=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

# build DRBG BIN: om2 line, sha2, hkdf, the DRBG unit, the driver; compile with omc
build() {
	{ printf 'om2\n'; cat "$lib/sha2.om" "$lib/hkdf.om" "$1" "$here/driver.om"; } > "$2.om"
	if ! $nice "$omc" < "$2.om" > "$2" 2> "$2.err" || [ -s "$2.err" ]; then
		say "omc refused $2.om: $(cat "$2.err")"
		return 1
	fi
	chmod +x "$2"
}

# run_ok NAME FILE: exit 0 and "drbg driver: N checks, 0 failed", N > 0; sets n
run_ok() {
	set +e
	$nice "$out/driver" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	line=$(tail -n 1 "$out/$1.out")
	n=$(printf '%s\n' "$line" | awk '$1 == "drbg" && $2 == "driver:" && $4 == "checks," && $5 == "0" && $6 == "failed" { print $3 }')
	if [ "$rc" -eq 0 ] && [ -n "$n" ] && [ "$n" -gt 0 ]; then
		say "PASS $1: $line"
		return 0
	fi
	n=0
	fail "$1: exit $rc: $(tail -n 3 "$out/$1.out" | tr '\n' ' ')"
	return 1
}

# detected NAME FILE BIN [PATTERN]: exit exactly 1 with a complete summary line with F > 0
# failures (a trap exits 100+N, a signal 128+N); with PATTERN, the output must also contain it
detected() {
	set +e
	$nice "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	last=$(tail -n 1 "$out/$1.out")
	f=$(printf '%s\n' "$last" | awk '$1 == "drbg" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $5 }')
	if [ "$rc" -eq 1 ] && [ -n "$f" ] && [ "$f" -gt 0 ]; then
		if [ $# -lt 4 ] || grep -q "$4" "$out/$1.out"; then
			say "PASS $1: detected ($(grep -c '^MISMATCH\|^BAD\|^STUCK' "$out/$1.out") failure lines; $last)"
			return 0
		fi
	fi
	fail "$1: not detected: exit $rc: $last"
	return 1
}

# mutant NAME FILE SED_EXPR [PATTERN]: apply SED_EXPR to drbg.om, rebuild, require detection
mutant() {
	mut=$((mut + 1))
	sed "$3" "$lib/drbg.om" > "$out/mut-$1.lib"
	cmp -s "$lib/drbg.om" "$out/mut-$1.lib" && { fail "$1: sed changed nothing"; return; }
	build "$out/mut-$1.lib" "$out/mut-$1" || { fail "$1: mutant did not compile"; return; }
	if detected "mutant-$1" "$2" "$out/mut-$1" ${4:+"$4"}; then
		mutok=$((mutok + 1))
	fi
}

build "$lib/drbg.om" "$out/driver" || { say "DRBG FAIL build"; exit 1; }
say "omc     sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
for u in sha2 hkdf drbg; do say "$u.om sha256 $(sha256sum "$lib/$u.om" | cut -d' ' -f1)"; done
for f in acvp cavp; do say "$f.txt sha256 $(sha256sum "$vec/$f.txt" | cut -d' ' -f1)"; done

if run_ok acvp "$vec/acvp.txt"; then acvp=$(grep -c '^D ' "$vec/acvp.txt"); fi
if run_ok cavp "$vec/cavp.txt"; then cavp=$(grep -c '^D ' "$vec/cavp.txt"); fi
vectors=$((acvp + cavp))
printf 'S\n' > "$out/api.txt"
if run_ok api "$out/api.txt"; then api=$n; fi

# source mutants: each must be detected (exit 1 with failures, never a trap)
mutant upd-one-round "$vec/acvp.txt" 's/^if eq len pd 0$/if eq len pd len pd/'
mutant upd-sep-byte "$vec/cavp.txt" 's/^put b 0 uchar r$/put b 0 uchar add r 2/'
mutant inst-v-init "$vec/cavp.txt" 's/^put st add 32 i 1$/put st add 32 i 0/'
mutant gen-no-final-update "$vec/cavp.txt" '/^e = drbg_fill st dst$/,/^ret 0$/ s/^drbg_upd st ad$/n = ctr/'
mutant gen-no-ad-update "$vec/acvp.txt" 's/^if gt len ad 0$/if gt len ad 65536/'
mutant ctr-frozen "$out/api.txt" 's/^drbg_wr st DRCTR add ctr 1$/drbg_wr st DRCTR ctr/'
mutant limit-off "$out/api.txt" 's/^if gt ctr lim$/if gt ctr DRRSMX/'
mutant short-seed "$out/api.txt" 's/^if lt len sm DRMINSM$/if lt len sm 1/'
mutant big-request "$out/api.txt" 's/^if gt len dst DRMAXRQ$/if gt len dst 65537/'
mutant wipe-partial "$out/api.txt" 's/^drbg_zb st$/drbg_zb part st 0 64/'
# stuck output: V is not advanced between blocks; the continuous test must latch (STUCK lines)
mutant stuck-v "$vec/acvp.txt" '/^fn drbg_fill /,/^end$/ s/^drbg_next st x$/e = or e 0/' '^STUCK line'

# expectation mutant: one ReturnedBits hex digit changed -> exactly that record fails
mut=$((mut + 1))
awk '/^D / && !done { c = substr($NF, 1, 1); $NF = (c == "0" ? "1" : "0") substr($NF, 2); done = 1 } { print }' \
	"$vec/cavp.txt" > "$out/mut-expect.txt"
if cmp -s "$vec/cavp.txt" "$out/mut-expect.txt"; then
	fail "expectation mutant changed nothing"
elif detected mutant-expect "$out/mut-expect.txt" "$out/driver" '^MISMATCH line'; then
	mutok=$((mutok + 1))
fi

# negative control: a malformed record (odd hex digit count in the nonce) is refused
grep -m 1 '^D ' "$vec/cavp.txt" | awk '{ $4 = substr($4, 2); print }' > "$out/neg-malformed.txt"
mut=$((mut + 1))
if detected neg-malformed "$out/neg-malformed.txt" "$out/driver" '^BAD line'; then mutok=$((mutok + 1)); fi

if [ "$bad" -ne 0 ]; then
	say "DRBG FAIL vectors=$vectors (acvp=$acvp, cavp=$cavp) api=$api mutants=$mutok/$mut"
	exit 1
fi
say "DRBG PASS vectors=$vectors (acvp=$acvp, cavp=$cavp) api=$api mutants=$mutok/$mut"
