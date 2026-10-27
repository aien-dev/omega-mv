#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Software integrity test and show-version service of om/tls (fips.om fips_integ / fips_itool /
# fips_version, build.sh integrity step).
# usage: sh om/tls/test/integrity/run.sh OMC [OUTDIR]
# Checks: a build.sh program passes its self-tests and reports state 0 then 1 with the HMAC in its
# slot; that HMAC equals an independent openssl HMAC-SHA-256 over the file with the slot zeroed
# (NOT_RUN without openssl); an unpatched program, a byte changed in an unused text and a byte
# changed at the end of the file all refuse with check 16 and exit 3; the tool refuses usage errors
# (2), a missing file (3), a file without the marker and one with two markers (4). Code mutants
# (integrity check skipped; slot not masked; marker test always false) must each be caught.
# Last line: "INTEGRITY PASS checks=N mutants=M" or "INTEGRITY FAIL ...".
set -u
omc=${1:?usage: run.sh OMC [OUTDIR]}
here=$(cd "$(dirname "$0")" && pwd); lib=$(cd "$here/../.." && pwd)
out=${2:-$(mktemp -d)}; mkdir -p "$out"
ok=0; bad=0
say() { echo "$*"; }
pass() { ok=$((ok + 1)); say "PASS $*"; }
fail() { bad=$((bad + 1)); say "FAIL $*"; }
marker=omega-tls-integrity-v1-hmac-sha256:
say "omc $(sha256sum "$omc" | cut -c1-64)"
for u in hkdf fips tls build; do f=$lib/$u.om; [ "$u" = build ] && f=$lib/build.sh; say "$(basename "$f") $(sha256sum "$f" | cut -c1-64)"; done
# slot OFFSET of the 64 hex digits in FILE
slot() { echo $(( $(grep -abo "$marker" "$1" | cut -d: -f1) + 35 )); }
# put BYTE (one char) at OFFSET in FILE
poke() { printf '%s' "$3" | dd of="$1" bs=1 seek="$2" conv=notrunc status=none; }
# expect NAME BIN CHECK RC: the version program printed "check CHECK" and exited RC
expect() {
	"$2" > "$out/$1.out" 2>&1; rc=$?
	if [ "$rc" -eq "$4" ] && grep -qx "check $3" "$out/$1.out"; then pass "$1: check $3 exit $rc"
	else fail "$1: wanted check $3 exit $4, got exit $rc: $(tr '\n' '|' < "$out/$1.out")"; fi
}

sh "$lib/build.sh" "$omc" "$here/version.om" "$out/ver" 2> "$out/ver.err" || { say "INTEGRITY FAIL build: $(cat "$out/ver.err")"; exit 1; }
expect good "$out/ver" 0 0
hex=$(grep -ao "${marker}[0-9a-f]*" "$out/ver" | cut -c36-)
l1=$(sed -n 1p "$out/good.out"); l3=$(sed -n 3p "$out/good.out")
if [ "$l1" = "Omega TLS module (om/tls) version 0.1.0 integrity hmac-sha256:$hex state 0" ] &&
   [ "$l3" = "Omega TLS module (om/tls) version 0.1.0 integrity hmac-sha256:$hex state 1" ] &&
   [ "${#hex}" -eq 64 ] && [ "$hex" != "$(printf '%064d' 0)" ]; then pass "version: state 0 then 1, slot $hex"
else fail "version lines: $l1 / $l3"; fi
if grep -qx 'short 0' "$out/good.out"; then pass "version: short buffer returns 0"; else fail "version short buffer"; fi
if command -v openssl > /dev/null; then
	cp "$out/ver" "$out/ver.z"; poke "$out/ver.z" "$(slot "$out/ver")" "$(printf '%064d' 0)"
	ref=$(openssl dgst -sha256 -hmac 'omega-tls module integrity key v1' "$out/ver.z" | sed 's/.*= //')
	if [ "$ref" = "$hex" ]; then pass "oracle: openssl HMAC-SHA-256 equals the slot"; else fail "oracle: openssl $ref slot $hex"; fi
else say "NOT_RUN oracle: openssl absent"; fi

"$omc" < "$out/ver.unit.om" > "$out/ver.unpatched" && chmod +x "$out/ver.unpatched"
expect unpatched "$out/ver.unpatched" 16 3
cp "$out/ver" "$out/ver.text"; poke "$out/ver.text" "$(grep -abo 'Omega TLS module (om/tls)' "$out/ver" | head -1 | cut -d: -f1)" o
expect text-byte "$out/ver.text" 16 3
cp "$out/ver" "$out/ver.app"; printf 'x' >> "$out/ver.app"
expect appended-byte "$out/ver.app" 16 3
cp "$out/ver" "$out/ver.slot"; last=$(printf '%s' "$hex" | cut -c64); [ "$last" = 0 ] && nd=1 || nd=0
poke "$out/ver.slot" $(( $(slot "$out/ver") + 63 )) "$nd"
expect slot-last-digit "$out/ver.slot" 16 3

# the tool alone (build.sh's integrity step): compose it exactly as build.sh does
{ printf 'om2\n'; for u in sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 fips tls; do cat "$lib/$u.om"; done
  printf 'fn main to int\nret fips_itool\nend\n'; } > "$out/itool.om"
"$omc" < "$out/itool.om" > "$out/itool" && chmod +x "$out/itool"
tool() { "$out/itool" "$@" > /dev/null 2>&1; echo $?; }
[ "$(tool)" -eq 2 ] && pass "tool: no argument exit 2" || fail "tool: no argument"
[ "$(tool "$out/absent")" -eq 3 ] && pass "tool: missing file exit 3" || fail "tool: missing file"
printf 'no marker here\n' > "$out/plain"
[ "$(tool "$out/plain")" -eq 4 ] && pass "tool: no marker exit 4" || fail "tool: no marker"
cp "$out/ver.unpatched" "$out/twice"; printf '%s%064d' "$marker" 0 >> "$out/twice"
[ "$(tool "$out/twice")" -eq 4 ] && pass "tool: two markers exit 4" || fail "tool: two markers"
cp "$out/ver.unpatched" "$out/cut"; printf '%s0000' "$marker" >> "$out/cut"
[ "$(tool "$out/cut")" -eq 4 ] && pass "tool: second marker with a cut slot exit 4" || fail "tool: cut slot"
cp "$out/ver.unpatched" "$out/repatch"
if [ "$(tool "$out/repatch")" -eq 0 ] && cmp -s "$out/repatch" "$out/ver"; then pass "tool: patching the unpatched build gives build.sh's bytes"
else fail "tool: repatch differs from build.sh output"; fi

# code mutants: each must make a check above fail
mk=0; mbad=0
mutant() {
	d=$out/m-$1; mkdir -p "$d"
	for u in sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 fips tls; do cp "$lib/$u.om" "$d/"; done
	cp "$lib/build.sh" "$d/"
	sed -i "$2" "$d/fips.om"
	if cmp -s "$d/fips.om" "$lib/fips.om"; then mbad=$((mbad + 1)); say "FAIL mutant $1: sed changed nothing"; return; fi
	sh "$d/build.sh" "$omc" "$here/version.om" "$d/ver" 2> "$d/err"; b=$?
	"$omc" < "$d/ver.unit.om" > "$d/unp" 2> /dev/null && chmod +x "$d/unp"
	gd=x; sd=x
	if [ "$b" -eq 0 ]; then
		"$d/ver" > "$d/good.out" 2>&1; gd=$?
		cp "$d/ver" "$d/slot"; mh=$(grep -ao "${marker}[0-9a-f]*" "$d/ver" | cut -c36-)
		[ "$(printf '%s' "$mh" | cut -c64)" = 0 ] && nd=1 || nd=0
		poke "$d/slot" $(( $(slot "$d/ver") + 63 )) "$nd"
		"$d/slot" > "$d/slot.out" 2>&1; sd=$?
	fi
	"$d/unp" > "$d/unp.out" 2>&1; ud=$?
	if [ "$gd" = 0 ] && [ "$ud" = 3 ] && grep -qx 'check 16' "$d/unp.out" && [ "$sd" = 3 ] && grep -qx 'check 16' "$d/slot.out"
	then mbad=$((mbad + 1)); say "FAIL mutant $1 survived"
	else mk=$((mk + 1)); say "PASS mutant $1 killed (build $b, patched exit $gd, unpatched exit $ud, slot digit exit $sd)"; fi
}
mutant fail-ignored 's/^ret fips_fail 16$/put FPST 1 0/'
mutant no-mask '/^fn fips_imask /,/^end$/ s/^put b i 48$/put b i get b i/'
mutant no-marker '/^fn fips_imark /,/^ret 0$/ s/^if eq d 0$/if eq d 1/'
mutant cmp-short 's/^ret fips_cmp m part FKIH FIMK 64$/ret fips_cmp part m 0 1 part FKIH FIMK 2/'

if [ "$bad" -eq 0 ] && [ "$mbad" -eq 0 ]; then say "INTEGRITY PASS checks=$ok mutants=$mk"; exit 0; fi
say "INTEGRITY FAIL checks=$ok failed=$bad mutants=$mk survived=$mbad"; exit 1
