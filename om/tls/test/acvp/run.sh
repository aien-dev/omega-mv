#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# ACVP sample-vector suite for om/tls: runs NIST's published ACVP-Server sample vectors
# (usnistgov/ACVP-Server, gen-val/json-files, pinned commit below) through the library and compares
# every answer with NIST's expectedResults.json. This is testing against the CAVP/ACVP test methods
# with NIST's published vectors, not NIST validation: these are self-run sample vectors, NOT a CAVP
# certificate. Only an accredited lab submitting through the ACVP production server earns one.
#
# Usage: sh om/tls/test/acvp/run.sh OMC   (OMC = the pinned seed compiler)
# Environment (all optional):
#   ACVP_DIR        an ACVP-Server gen-val/json-files directory; otherwise the pinned commit is fetched
#                   into build/tls/acvp/acvp-server. Every file used is checked against acvp-inputs.sha256.
#   MLKEM_SRC       directory holding sha3.om and mlkem.om when they are not in om/tls yet
#   ACVP_DRBG_ROOT  an omega-mv tree holding om/tls/test/drbg/run.sh when this one does not
# Per algorithm: ACVP <FOLDER> PASS tests=N not_applicable=M, then the in-memory negative control
# (one expected value corrupted must FAIL). Final line:
#   ACVP PASS algos=K tests=N not_applicable=M not_run=NONE           exit 0
#   ACVP PARTIAL algos=K tests=N not_applicable=M not_run=LIST        exit 3 (nothing failed, something not run)
#   ACVP FAIL ...                                                      exit 1
set -eu
omc=$(cd "$(dirname "${1:?usage: run.sh OMC}")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)
root=$(cd "$lib/../.." && pwd)
out="$root/build/tls/acvp"
w="$out/work"
pin=975de31eb83d87039ec88934fdc47d8c312b892d
nice="nice -n 19 ionice -c3"
mkdir -p "$w"
rm -f "$w"/*

if [ -n "${ACVP_DIR:-}" ]; then
	J=$ACVP_DIR
else
	src="$out/acvp-server"
	if [ ! -d "$src/.git" ]; then
		git clone -q --filter=blob:none --no-checkout https://github.com/usnistgov/ACVP-Server.git "$src"
	fi
	git -C "$src" sparse-checkout set --no-cone $(sed 's,^[0-9a-f]*  ,/gen-val/json-files/,' "$here/acvp-inputs.sha256")
	git -C "$src" checkout -q "$pin"
	J="$src/gen-val/json-files"
fi
(cd "$J" && sha256sum -c --quiet "$here/acvp-inputs.sha256") || { echo "ACVP FAIL inputs differ from acvp-inputs.sha256"; exit 1; }
echo "ACVP inputs: ACVP-Server $pin, $(wc -l < "$here/acvp-inputs.sha256") files match acvp-inputs.sha256"

fail=0 algos=0 tests=0 na=0 notrun=""
cat "$here/acvpio.om" "$here/acvp.om" "$here/acvpmain.om" > "$out/acvp.prog.om"
$nice sh "$lib/build.sh" "$omc" "$out/acvp.prog.om" "$out/acvp"

# driver negative controls: a malformed record must be refused (BAD, exit 1)
if printf 'H 1 256 abc\n' | "$out/acvp" > "$w/bad.out"; then
	echo "ACVP FAIL driver accepted a malformed record"; fail=1
elif [ "$(cat "$w/bad.out")" != "1 BAD" ]; then
	echo "ACVP FAIL driver malformed-record reply: $(cat "$w/bad.out")"; fail=1
fi

field() { sed -n "s/.* $1=\([0-9]*\).*/\1/p"; }

run_set() {
	conv=$(python3 "$here/acvp2om.py" "$J" "$2" "$w") || { echo "ACVP $2 FAIL conversion"; fail=1; return; }
	echo "$conv" > "$w/$2.conv.log"
	if [ "$(echo "$conv" | field records)" = 0 ]; then
		n=$(echo "$conv" | field not_applicable)
		echo "ACVP $2 NOT_APPLICABLE tests=0 not_applicable=$n (reasons in $2.map.json)"
		na=$((na + n))
		return
	fi
	for f in "$w/$2".in.*; do
		if ! $nice "$1" < "$f" > "$w/$2.out.${f##*.}"; then
			echo "ACVP $2 FAIL driver exit on $(basename "$f")"; fail=1; return
		fi
	done
	if ! python3 "$here/om2acvp.py" "$J" "$2" "$w"; then
		echo "ACVP $2 FAIL response not built"; fail=1; return
	fi
	if line=$(python3 "$here/acvpcmp.py" "$J/$2/expectedResults.json" "$w/$2.response.json" "$w/$2.map.json" "$2" 2>> "$w/$2.cmp.err"); then
		echo "$line"
		algos=$((algos + 1)) tests=$((tests + $(echo "$line" | field tests))) na=$((na + $(echo "$line" | field not_applicable)))
	else
		echo "$line"; fail=1
	fi
	if python3 "$here/acvpcmp.py" "$J/$2/expectedResults.json" "$w/$2.response.json" "$w/$2.map.json" "$2" --flip > "$w/$2.neg.out" 2>&1; then
		echo "ACVP $2 FAIL negative control (corrupted expected value) not detected"; fail=1
	else
		echo "ACVP $2 negative control FAIL as required: $(tail -1 "$w/$2.neg.out")"
	fi
}

for f in SHA2-256-1.0 SHA2-384-1.0 SHA2-512-1.0 \
	HMAC-SHA2-256-1.0 HMAC-SHA2-384-1.0 HMAC-SHA2-512-1.0 HMAC-SHA2-256-2.0 HMAC-SHA2-384-2.0 HMAC-SHA2-512-2.0 \
	ACVP-AES-GCM-1.0 ACVP-AES-ECB-1.0 ECDSA-SigVer-1.0 ECDSA-SigVer-FIPS186-5 \
	RSA-SigVer-FIPS186-4 RSA-SigVer-FIPS186-5 KDA-HKDF-Sp800-56Cr1 KDA-HKDF-Sp800-56Cr2 \
	TLS-v1.3-KDF-RFC8446 XECDH-SSC-RFC7748 KAS-ECC-SSC-Sp800-56Ar3; do
	run_set "$out/acvp" "$f"
done

# TLS 1.3 key schedule through tls.om's own ts_* path (the 25 tests the native schedule covers)
t=TLS-v1.3-KDF-RFC8446
if [ -f "$w/$t.native.response.json" ]; then
	python3 "$here/acvpcmp.py" "$J/$t/expectedResults.json" "$w/$t.native.response.json" "$w/$t.map.json" "$t-native(tls.om)" --subset || fail=1
	if python3 "$here/acvpcmp.py" "$J/$t/expectedResults.json" "$w/$t.native.response.json" "$w/$t.map.json" "$t-native" --subset --flip > "$w/$t.native.neg.out" 2>&1; then
		echo "ACVP $t-native FAIL negative control not detected"; fail=1
	fi
fi

# ML-KEM-768 (FIPS 203) through mlkem.om
if [ -f "$lib/mlkem.om" ] && [ -f "$lib/sha3.om" ]; then
	mk=$lib
else
	mk=${MLKEM_SRC:-}
fi
if [ -n "$mk" ] && [ -f "$mk/mlkem.om" ] && [ -f "$mk/sha3.om" ]; then
	echo "ACVP ML-KEM sources: $(cd "$mk" && sha256sum sha3.om mlkem.om | tr '\n' ' ')"
	{ echo om2; cat "$mk/sha3.om" "$mk/mlkem.om" "$here/acvpio.om" "$here/mlkemacvp.om" "$here/acvpmain.om"; } > "$out/mlkem.unit.om"
	$nice "$omc" < "$out/mlkem.unit.om" > "$out/mlkem"
	chmod +x "$out/mlkem"
	run_set "$out/mlkem" ML-KEM-keyGen-FIPS203
	run_set "$out/mlkem" ML-KEM-encapDecap-FIPS203
else
	echo "ACVP ML-KEM NOT_RUN (no sha3.om/mlkem.om in om/tls or MLKEM_SRC)"
	notrun="${notrun}ML-KEM,"
fi

# HMAC_DRBG SHA-256 (SP 800-90A) through om/tls/test/drbg, converted from the same pinned ACVP-Server
dr=${ACVP_DRBG_ROOT:-$root}
if [ -f "$dr/om/tls/test/drbg/run.sh" ]; then
	hv="$dr/om/tls/test/drbg/vectors/acvp.txt"
	grep -q "^# ACVP-Server $pin " "$hv" || { echo "ACVP hmacDRBG FAIL vectors not from ACVP-Server $pin"; fail=1; }
	for p in hmacDRBG-1.0/internalProjection.json hmacDRBG-SP800-90Ar1/internalProjection.json; do
		grep -q "^# $p sha256 $(cd "$J" && sha256sum "$p" | cut -d' ' -f1)\$" "$hv" || { echo "ACVP hmacDRBG FAIL $p sha256 differs"; fail=1; }
	done
	if line=$(cd "$dr" && $nice sh om/tls/test/drbg/run.sh "$omc" 2>&1 | tail -1) && echo "$line" | grep -q '^DRBG PASS '; then
		echo "ACVP hmacDRBG (om/tls/test/drbg/run.sh in $dr) $line"
		algos=$((algos + 1))
	else
		echo "ACVP hmacDRBG FAIL: $line"; fail=1
	fi
else
	echo "ACVP hmacDRBG NOT_RUN (no om/tls/test/drbg/run.sh; set ACVP_DRBG_ROOT)"
	notrun="${notrun}hmacDRBG,"
fi

# Outside the FIPS track (no ACVP vectors for these): RFC 8439 / RFC 7748 suites with Wycheproof
for s in chacha x25519; do
	if line=$($nice sh "$lib/test/$s/run.sh" "$omc" 2>&1 | tail -1) && echo "$line" | grep -q ' PASS '; then
		echo "NONFIPS $line"
	else
		echo "NONFIPS $s FAIL: $line"; fail=1
	fi
done

notrun=${notrun%,}
if [ "$fail" -ne 0 ]; then
	echo "ACVP FAIL algos=$algos tests=$tests not_applicable=$na not_run=${notrun:-NONE}"
	exit 1
fi
if [ -n "$notrun" ]; then
	echo "ACVP PARTIAL algos=$algos tests=$tests not_applicable=$na not_run=$notrun"
	exit 3
fi
echo "ACVP PASS algos=$algos tests=$tests not_applicable=$na not_run=NONE"
