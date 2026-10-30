#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/meaning/meaning.om (FUNCTION_ID and the graph of meaning). Usage (from the
# omega-mv root):
#   sh om/meaning/test/run.sh seed/out/omc
# Builds into build/meaning/. Exits 0 only when every vector passes, the FUNCTION_ID goldens match an
# independent sha256sum of the spec section 9.3 preimage, and every mutant and negative control is
# detected. Final line: MEANING PASS checks=N mutants=M/M, or MEANING FAIL.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/.." && pwd)
root=$(cd "$lib/../.." && pwd)
out="$root/build/meaning"
vec="$here/vectors"
sha2="$root/om/tls/sha2.om"
mkdir -p "$out"
nice="nice -n 19 ionice -c3"
checks=0
mut=0
mutok=0
bad=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

# build LIB BIN: om2 line, sha2.om, LIB, the driver; compile with omc
build() {
	{ printf 'om2\n'; cat "$sha2" "$1" "$here/driver.om"; } > "$2.om"
	if ! $nice "$omc" < "$2.om" > "$2" 2> "$2.err" || [ -s "$2.err" ]; then
		say "omc refused $2.om: $(cat "$2.err")"
		return 1
	fi
	chmod +x "$2"
}

# summary LINE -> "N F" when LINE is a complete driver summary
summary() {
	printf '%s\n' "$1" | awk '$1 == "meaning" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $3, $5 }'
}

# detected NAME FILE BIN: BIN reported a difference (exit 1 with F > 0 failures) or refused the
# input with a trap (exit 100..199); a signal (128+N within that range is not produced by omc
# binaries, which trap through exit) or exit 0 is not detection
detected() {
	set +e
	$nice "$3" < "$2" > "$out/$1.out" 2>&1
	rc=$?
	set -e
	last=$(tail -n 1 "$out/$1.out")
	s=$(summary "$last")
	f=${s#* }
	if [ "$rc" -eq 1 ] && [ -n "$s" ] && [ "$f" -gt 0 ]; then
		say "PASS $1: detected ($(grep -c '^FAIL' "$out/$1.out") FAIL lines, first: $(grep -m 1 '^FAIL' "$out/$1.out"))"
		return 0
	fi
	if [ "$rc" -ge 100 ] && [ "$rc" -le 127 ]; then
		say "PASS $1: detected (trap, exit $rc)"
		return 0
	fi
	fail "$1: not detected: exit $rc: $last"
	return 1
}

# mutant NAME SED_EXPR: apply SED_EXPR to meaning.om, rebuild, require detection on the vectors
mutant() {
	mut=$((mut + 1))
	sed "$2" "$lib/meaning.om" > "$out/mut-$1.om.lib"
	if cmp -s "$lib/meaning.om" "$out/mut-$1.om.lib"; then
		fail "$1: sed changed nothing"
		return
	fi
	build "$out/mut-$1.om.lib" "$out/mut-$1" || { fail "$1: mutant did not compile"; return; }
	if detected "mutant-$1" "$vec" "$out/mut-$1"; then
		mutok=$((mutok + 1))
	fi
}

# fid W K C... -> SHA-256 of "omega.function.v1" 0x00 || 01 || W u16 || K u16 || C u64 BE each
fid() {
	w=$1
	k=$2
	shift 2
	{
		printf 'omega.function.v1\000\001'
		printf '%04x%04x' "$w" "$k" | xxd -r -p
		for c in "$@"; do printf '%016x' "$c" | xxd -r -p; done
	} | sha256sum | cut -d' ' -f1
}
zeros() { i=0; while [ "$i" -lt "$1" ]; do printf ' 0'; i=$((i + 1)); done; }

build "$lib/meaning.om" "$out/driver" || { say "MEANING FAIL build"; exit 1; }
say "omc        sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
say "sha2.om    sha256 $(sha256sum "$sha2" | cut -d' ' -f1)"
say "meaning.om sha256 $(sha256sum "$lib/meaning.om" | cut -d' ' -f1)"
say "driver.om  sha256 $(sha256sum "$here/driver.om" | cut -d' ' -f1)"
say "vectors    sha256 $(sha256sum "$vec" | cut -d' ' -f1)"

# FUNCTION_ID goldens in the vectors must equal the independently built preimage hashes
# (x + 2 at w 64: a = 2 1 0...; zero at w 64: 66 zeros; 3x^3 + 5x + 7 at w 8: a = 7 8 9 3 0 0 0 0 0 0,
# the coefficient vector computed by Wolfram, spec section 9.2)
g1=$(fid 64 66 2 1 $(zeros 64))
g2=$(fid 64 66 $(zeros 66))
g3=$(fid 8 10 7 8 9 3 0 0 0 0 0 0)
for want in "H 0 $g1" "H 2 $g1" "H 8 $g2" "H 9 $g2" "H 19 $g3"; do
	if grep -qx "$want" "$vec"; then
		say "PASS golden: $want"
	else
		fail "golden: vectors lack \"$want\""
	fi
done

# the vectors
set +e
$nice "$out/driver" < "$vec" > "$out/vectors.out" 2>&1
rc=$?
set -e
last=$(tail -n 1 "$out/vectors.out")
s=$(summary "$last")
if [ "$rc" -eq 0 ] && [ -n "$s" ] && [ "${s#* }" = 0 ] && [ "${s% *}" -gt 0 ]; then
	checks=${s% *}
	grep '^[RG] ' "$out/vectors.out" | while IFS= read -r l; do say "  $l"; done
	say "PASS vectors: $last"
else
	fail "vectors: exit $rc: $(tail -n 5 "$out/vectors.out" | tr '\n' ' ')"
fi

# source mutants: each must be detected by the vectors
# K one too small: x(x-1)...(x-8) and 0 at w 8 collapse, the iff check sees equal ids for different functions
mutant kof-minus-1 's/^kk = mg_kof w$/kk = sub mg_kof w 1/'
# Newton inverse with 4 steps (48 bits): wrong coefficients at w 64, reconstruction fails
mutant inverse-4-steps 's/^for i ulong 5$/for i ulong 4/'
# Newton inverse started from 1 instead of u (1 correct bit, 32 after 5 steps)
mutant inverse-start-1 's/^y = u$/y = ulong 1/'
# and with the all-ones constant no longer recognised: x and 255 at w 8 falls back, C 15 16 fails
mutant and-ones-dropped '/^if eq o 9$/,/^end$/s/^if eq c m$/if lt c 0/'
# and with 1 accepted as the identity: x and 1 treated as x, the iff check or the canon trap fires
mutant and-one-accepted '/^if eq o 9$/,/^end$/s/^if eq c m$/if eq c 1/'
# ApplyPayload size 161 -> 160: program_id goldens fail
mutant apply-payload-160 's/^mg_b32 161$/mg_b32 160/'
# storage sort comparator always "not greater": reverse rebuild bytes differ or load refuses order
mutant sort-cmp-never '/^fn mg_gt /,/^fn mg_sget /s/^ret 1$/ret 0/'
# FUNCTION_ID domain tag changed: the independent goldens fail
mutant fid-domain 's/^text mgtfd "omega.function.v1"$/text mgtfd "omega.function.v2"/'

# expectation mutants: a must-not-collapse pair asserted equal, a must-collapse pair asserted
# different, each must fail exactly the changed record
mut=$((mut + 1))
sed 's/^N 1 3$/C 1 3/' "$vec" > "$out/mut-expect-c.txt"
if cmp -s "$vec" "$out/mut-expect-c.txt"; then fail "expect-c changed nothing"
elif detected mutant-expect-c "$out/mut-expect-c.txt" "$out/driver"; then mutok=$((mutok + 1)); fi
mut=$((mut + 1))
sed 's/^C 0 1$/N 0 1/' "$vec" > "$out/mut-expect-n.txt"
if cmp -s "$vec" "$out/mut-expect-n.txt"; then fail "expect-n changed nothing"
elif detected mutant-expect-n "$out/mut-expect-n.txt" "$out/driver"; then mutok=$((mutok + 1)); fi

# negative control: a refused program defined with D is reported (pchk code 4: constant 256 at w 8)
printf 'D 8 x 256 add\n' > "$out/neg-define.txt"
detected neg-define "$out/neg-define.txt" "$out/driver" || true

if [ "$bad" -ne 0 ]; then
	say "MEANING FAIL checks=$checks mutants=$mutok/$mut"
	exit 1
fi
say "MEANING PASS checks=$checks mutants=$mutok/$mut"
