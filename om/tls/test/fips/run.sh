#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/fips.om: self-tests, SP 800-90B health tests, approved-mode DRBG wiring,
# the X25519 pairwise consistency test and the module error state. Tested against the FIPS 140-3 /
# CAVP test methods, not NIST-validated.
# usage: sh om/tls/test/fips/run.sh OMC
#   1. driver.om on the real library: every self-test passes, health-test boundaries hold, the DRBG
#      and PCT work, and the PCT failure latches the error state (tls_connect, tls_seal, tls_open
#      refuse with TLS_ERR_SELFTEST); `driver drbgerr` latches through a DRBG in its error state;
#      `driver ecgenerr` latches through a failed approved-mode ECDH key generation (tls_ecgen).
#   2. KAT mutants: one hex digit of each known answer changed; the driver must print
#      "REFUSED detail N" for that self-test, pass the refusal checks and exit 3.
#   3. Code mutants in sha2.om, drbg.om (stuck V), x25519.om and aesgcm.om: the self-tests refuse.
#   4. Code mutants in fips.om and tls.om (health cut-offs, PCT compare, entropy draw, refusal lines,
#      ECDH key draw and latch):
#      the driver reports FAIL check lines and exits 1.
# Final line: FIPS PASS checks=C drbgerr=D ecgenerr=E kat-mutants=K/K code-mutants=M/M, or FIPS FAIL.
set -eu
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)
root=$(cd "$lib/../.." && pwd)
out="$root/build/tls/fips"
rm -rf "$out"; mkdir -p "$out"
nice="nice -n 19 ionice -c3"
units="sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 fips tls"
bad=0; kmut=0; kok=0; cmut=0; cok=0; checks=0; derr=0; eerr=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL $*"; bad=1; }

say "omc sha256 $(sha256sum "$omc" | cut -d' ' -f1)"
for u in $units; do say "$u.om sha256 $(sha256sum "$lib/$u.om" | cut -d' ' -f1)"; done

# build NAME [UNIT MUTATED-FILE]: compose the library (one unit optionally replaced) + driver
build() {
	d="$out/$1"; mkdir -p "$d"
	for u in $units; do cp "$lib/$u.om" "$d/"; done
	cp "$lib/build.sh" "$d/"
	if [ $# -ge 3 ]; then cp "$3" "$d/$2.om"; fi
	$nice sh "$d/build.sh" "$omc" "$here/driver.om" "$d/bin" 2> "$d.err" || { say "omc refused $1: $(cat "$d.err")"; return 1; }
}

# run NAME ARGS...: exit code in rc, output in $out/NAME.out
run() {
	n=$1; shift
	set +e
	$nice "$out/$n/bin" "$@" < /dev/null > "$out/$n.out" 2>&1
	rc=$?
	set -e
}

summary() { awk '$1 == "fips" && $2 == "driver:" && $4 == "checks," && $6 == "failed" { print $3, $5 }' "$1" | tail -n 1; }

build real || { say "FIPS FAIL build"; exit 1; }
run real
s=$(summary "$out/real.out")
if [ "$rc" -eq 0 ] && [ "${s#* }" = 0 ]; then checks=${s% *}; say "PASS driver: $(tail -n 1 "$out/real.out")"
else fail "driver: exit $rc: $(tr '\n' ' ' < "$out/real.out")"; fi
run real drbgerr
cp "$out/real.out" "$out/drbgerr.out"
s=$(summary "$out/drbgerr.out")
if [ "$rc" -eq 0 ] && [ "${s#* }" = 0 ]; then derr=${s% *}; say "PASS driver drbgerr: $(tail -n 1 "$out/drbgerr.out")"
else fail "driver drbgerr: exit $rc: $(tr '\n' ' ' < "$out/drbgerr.out")"; fi
run real ecgenerr
cp "$out/real.out" "$out/ecgenerr.out"
s=$(summary "$out/ecgenerr.out")
if [ "$rc" -eq 0 ] && [ "${s#* }" = 0 ]; then eerr=${s% *}; say "PASS driver ecgenerr: $(tail -n 1 "$out/ecgenerr.out")"
else fail "driver ecgenerr: exit $rc: $(tr '\n' ' ' < "$out/ecgenerr.out")"; fi

# refused NAME DETAIL: the self-tests refused with that detail and every refusal check held
refused() {
	if [ "$rc" -eq 3 ] && grep -qx "REFUSED detail $2" "$out/$1.out" && ! grep -q '^FAIL' "$out/$1.out"; then
		return 0
	fi
	return 1
}

# katmut TEXT DETAIL: change the first hex digit of text TEXT in fips.om
katmut() {
	kmut=$((kmut + 1))
	awk -v n="$1" '$1 == "text" && $2 == n { c = substr($3, 2, 1); r = (c == "0") ? "1" : "0"; $3 = "\"" r substr($3, 3) } { print }' "$lib/fips.om" > "$out/k-$1.om"
	cmp -s "$lib/fips.om" "$out/k-$1.om" && { fail "kat $1: mutation did not apply"; return; }
	build "k-$1" fips "$out/k-$1.om" || { fail "kat $1: did not compile"; return; }
	run "k-$1"
	if refused "k-$1" "$2"; then kok=$((kok + 1)); say "PASS kat-mutant $1: REFUSED detail $2, connect/seal/open refused"
	else fail "kat-mutant $1: exit $rc: $(tr '\n' ' ' < "$out/k-$1.out")"; fi
}

katmut FKS256 1
katmut FKS384 2
katmut FKS512 3
katmut FKH256 4
katmut FKH384 5
katmut FKHO 6
katmut FKGC 7
katmut FKED 9
katmut FKR1S 10
katmut FKR2S 11
katmut FKCC 12
katmut FKXO 13
katmut FKDR 14
katmut FKTD 15

# codemut LABEL UNIT SED EXPECT: EXPECT is "detail N" (self-test refusal) or "check N" (driver FAIL
# line for check N, exit 1)
codemut() {
	cmut=$((cmut + 1))
	sed "$3" "$lib/$2.om" > "$out/c-$1.om"
	cmp -s "$lib/$2.om" "$out/c-$1.om" && { fail "code $1: sed changed nothing"; return; }
	build "c-$1" "$2" "$out/c-$1.om" || { fail "code $1: did not compile"; return; }
	run "c-$1" ${5:+"$5"}
	case "$4" in
	detail*) if refused "c-$1" "${4#detail }"; then cok=$((cok + 1)); say "PASS code-mutant $1: REFUSED ${4}"; return; fi ;;
	check*) if [ "$rc" -eq 1 ] && grep -q "^FAIL check ${4#check } " "$out/c-$1.out"; then cok=$((cok + 1)); say "PASS code-mutant $1: FAIL ${4}"; return; fi ;;
	esac
	fail "code-mutant $1: expected $4: exit $rc: $(tr '\n' ' ' < "$out/c-$1.out")"
}

# library code under self-test
codemut sha256-k0 sha2 's/0x428a2f98/0x428a2f99/' "detail 1"
codemut drbg-stuck-v drbg '/^fn drbg_fill /,/^end$/ s/^drbg_next st x$/e = or e 0/' "detail 14"
codemut x25519-a24 x25519 's/^const XCA24 ulong 121665$/const XCA24 ulong 121666/' "detail 13"
codemut gcm-no-decrypt aesgcm '/^fn gcm_open1 /,/^ret 0$/ s/^gcm_ctr k s part buf 0 n$/n = n/' "detail 8"
# health tests, PCT, entropy draw, latch and refusal lines
codemut rct-cutoff fips 's/^const FHRCT1 ulong 7$/const FHRCT1 ulong 8/' "check 21"
codemut apt-cutoff fips 's/^const FHAPT1 ulong 34$/const FHAPT1 ulong 35/' "check 24"
codemut apt-window fips 's/^put FPST 7 rem add p 1 FHAPTW$/put FPST 7 rem add p 1 256/' "check 24"
codemut ht-unused fips '/^fn fips_draw /,/^ret 0$/ s/^fips_ht ulong get b i$/n = n/' "check 15"
codemut pct-nocompare fips '/^fn fips_pctx /,/^end$/ s/^d = or d xor ulong get o i ulong get pub i$/d = d/' "check 70"
codemut latch-keep fips '/^fn fips_fail /,/^ret code$/ s/^put FPST 0 2$/put FPST 0 1/' "check 60"
codemut pr-no-reseed fips '/^fn fips_drbgget /,/^ret 0$/ s/^r = drbg_reseed st e$/r = ulong 0/' "check 36"
codemut connect-hook tls '/^e = tls_fips c$/,/^end$/d' "check 62"
codemut seal-refuse tls '/^fn tls_seal /,/^for i ulong 5$/ s/^if eq fips_bad 1$/if eq fips_bad 2/' "check 65"
codemut open-refuse tls '/^fn tls_open /,/^for i ulong 5$/ s/^if eq fips_bad 1$/if eq fips_bad 2/' "check 66"
codemut wipe-drbg tls '/^fn tls_wipe /,/^end$/ {/^tls_zero c TLDRBG DRSZ$/d}' "check 58"
codemut rand-approved tls '/^fn tls_rand /,/^end$/ s/^if eq and get c TLFL 4 0$/if eq and get c TLFL 0 0/' "check 57"
codemut ecgen-getrandom tls '/^fn tls_ecgen /,/^ret 0$/ s/^r = tls_rand c part d 0 nb$/r = ulong 0\nif ne random d 0 nb 0 long nb\nr = TLERAND\nend/' "check 45"
codemut ecgen-nolatch tls '/^fn tls_ecgen /,/^ret 0$/ {/^r = fips_fail 21$/d}' "check 71" ecgenerr

if [ "$bad" -eq 0 ] && [ "$kok" -eq "$kmut" ] && [ "$cok" -eq "$cmut" ]; then
	say "FIPS PASS checks=$checks drbgerr=$derr ecgenerr=$eerr kat-mutants=$kok/$kmut code-mutants=$cok/$cmut"
	exit 0
fi
say "FIPS FAIL checks=$checks drbgerr=$derr ecgenerr=$eerr kat-mutants=$kok/$kmut code-mutants=$cok/$cmut"
exit 1
