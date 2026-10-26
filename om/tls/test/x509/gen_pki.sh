#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: build the X.509 test PKI for om/tls/x509.om with the openssl CLI and write the
# committed fixtures into vectors/pki/ (DER certificates, PEM stores, CertificateVerify messages and
# signatures). Keys are fresh random keys, so a rerun produces different (equally valid) fixtures;
# cases.txt does not depend on key bytes. Usage: sh gen_pki.sh   (OpenSSL 3.0 or later)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out="$here/vectors/pki"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT
rm -rf "$out"
mkdir -p "$out" "$w/new"
: > "$w/index.txt"
echo 1000 > "$w/serial"
cat > "$w/ca.cnf" <<EOF
[ ca ]
default_ca = d
[ d ]
dir = $w
database = $w/index.txt
new_certs_dir = $w/new
serial = $w/serial
policy = p
unique_subject = no
copy_extensions = none
email_in_dn = no
[ p ]
commonName = supplied
organizationName = optional
EOF
cat > "$w/ext.cnf" <<'EOF'
[ ca_root ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
[ ca_int ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ ca_int0 ]
basicConstraints = critical,CA:TRUE,pathlen:0
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ ca_noca ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ ca_noku ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ ca_nc ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
nameConstraints = critical,permitted;DNS:example.test
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ ca_ekucl ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
extendedKeyUsage = clientAuth
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
[ leaf ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test,DNS:example.test
authorityKeyIdentifier = keyid
[ leaf_ec ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test,DNS:example.test
authorityKeyIdentifier = keyid
[ leaf_min ]
subjectAltName = DNS:www.example.test
[ leaf_wild ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
subjectAltName = DNS:*.wild.test
[ leaf_wild1 ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
subjectAltName = DNS:*.test
[ leaf_part ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
subjectAltName = DNS:f*.wild.test
[ leaf_crit ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test
1.3.6.1.4.1.55555.1 = critical,ASN1:NULL
[ leaf_noncrit ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test
1.3.6.1.4.1.55555.1 = ASN1:NULL
[ leaf_client ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = clientAuth
subjectAltName = DNS:www.example.test
[ leaf_ca ]
basicConstraints = critical,CA:TRUE
keyUsage = critical,digitalSignature,keyCertSign
subjectAltName = DNS:www.example.test
[ leaf_nosan ]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = serverAuth
[ leaf_kuenc ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test
[ leaf_self ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyCertSign
extendedKeyUsage = serverAuth
subjectAltName = DNS:www.example.test
EOF
ossl() { nice -n 19 ionice -c3 openssl "$@"; }
key() { # key NAME SPEC
	case $2 in
	rsa*) ossl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:${2#rsa} -out "$w/$1.key" 2> /dev/null ;;
	pss) ossl genpkey -algorithm RSA-PSS -pkeyopt rsa_keygen_bits:2048 -out "$w/$1.key" 2> /dev/null ;;
	p256) ossl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -out "$w/$1.key" ;;
	p384) ossl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-384 -pkeyopt ec_param_enc:named_curve -out "$w/$1.key" ;;
	ed25519) ossl genpkey -algorithm ED25519 -out "$w/$1.key" ;;
	esac
}
# cert NAME KEYSPEC ISSUER(or self) EXT START END MD [SIGOPT...]
cert() {
	n=$1 ks=$2 is=$3 ext=$4 sd=$5 ed=$6 md=$7
	shift 7
	[ -f "$w/$n.key" ] || key "$n" "$ks"
	ossl req -new -key "$w/$n.key" -subj "/O=omtls test/CN=$n" -out "$w/$n.csr"
	so=""
	for o in "$@"; do so="$so -sigopt $o"; done
	if [ "$is" = self ]; then
		ossl ca -batch -config "$w/ca.cnf" -selfsign -keyfile "$w/$n.key" -in "$w/$n.csr" -out "$w/$n.pem" \
			-startdate "$sd" -enddate "$ed" -md "$md" -extfile "$w/ext.cnf" -extensions "$ext" -notext $so 2> "$w/err" || { cat "$w/err"; exit 1; }
	else
		ossl ca -batch -config "$w/ca.cnf" -cert "$w/$is.pem" -keyfile "$w/$is.key" -in "$w/$n.csr" -out "$w/$n.pem" \
			-startdate "$sd" -enddate "$ed" -md "$md" -extfile "$w/ext.cnf" -extensions "$ext" -notext $so 2> "$w/err" || { cat "$w/err"; exit 1; }
	fi
	ossl x509 -in "$w/$n.pem" -outform DER -out "$out/$n.der"
}
R0=200101000000Z; R1=20510101000000Z
I0=240101000000Z; I1=360101000000Z
L0=260101000000Z; L1=270101000000Z
PSS256="rsa_padding_mode:pss rsa_pss_saltlen:digest"
# roots
cert rootR rsa2048 self ca_root $R0 $R1 sha256
cert rootE p384 self ca_root $R0 $R1 sha384
cert rootX rsa2048 self ca_root $R0 $R1 sha256
# same subject name as rootR, different key (store must try every candidate)
key rootRdup rsa2048
ossl req -new -key "$w/rootRdup.key" -subj "/O=omtls test/CN=rootR" -out "$w/rootRdup.csr"
ossl ca -batch -config "$w/ca.cnf" -selfsign -keyfile "$w/rootRdup.key" -in "$w/rootRdup.csr" -out "$w/rootRdup.pem" \
	-startdate $R0 -enddate $R1 -md sha256 -extfile "$w/ext.cnf" -extensions ca_root -notext 2> /dev/null
# intermediates
cert intR rsa2048 rootR ca_int $I0 $I1 sha256
cert intE p256 rootE ca_int $I0 $I1 sha384
cert intRexp rsa2048 rootR ca_int 200101000000Z 250101000000Z sha256
cert intNoCA rsa2048 rootR ca_noca $I0 $I1 sha256
cert intNoKU rsa2048 rootR ca_noku $I0 $I1 sha256
cert intP0 rsa2048 rootR ca_int0 $I0 $I1 sha256
cert intP0b rsa2048 intP0 ca_int $I0 $I1 sha256
cert intNC rsa2048 rootR ca_nc $I0 $I1 sha256
cert intEKUc rsa2048 rootR ca_ekucl $I0 $I1 sha256
cert intPSS rsa2048 rootR ca_int $I0 $I1 sha384 rsa_padding_mode:pss rsa_pss_saltlen:digest
prev=rootR
for i in 1 2 3 4 5 6 7 8; do cert "d$i" rsa2048 $prev ca_int $I0 $I1 sha256; prev=d$i; done
# leaves under intR
cert lR rsa2048 intR leaf $L0 $L1 sha256
cert lR384 rsa2048 intR leaf $L0 $L1 sha384
cert lR512 rsa2048 intR leaf $L0 $L1 sha512
cert lRmin rsa2048 intR leaf_min $L0 $L1 sha256
cert lPSS256 rsa2048 intR leaf $L0 $L1 sha256 $PSS256
cert lPSS384 rsa3072 intR leaf $L0 $L1 sha384 $PSS256
cert lPSS512 rsa4096 intR leaf $L0 $L1 sha512 $PSS256
cert lExp rsa2048 intR leaf 240101000000Z 250101000000Z sha256
cert lNyv rsa2048 intR leaf 280101000000Z 290101000000Z sha256
cert l2050 rsa2048 intR leaf 490101000000Z 20500101000000Z sha256
cert lWild rsa2048 intR leaf_wild $L0 $L1 sha256
cert lWild1 rsa2048 intR leaf_wild1 $L0 $L1 sha256
cert lPart rsa2048 intR leaf_part $L0 $L1 sha256
cert lCrit rsa2048 intR leaf_crit $L0 $L1 sha256
cert lNonCrit rsa2048 intR leaf_noncrit $L0 $L1 sha256
cert lClient rsa2048 intR leaf_client $L0 $L1 sha256
cert lCA rsa2048 intR leaf_ca $L0 $L1 sha256
cert lNoSAN rsa2048 intR leaf_nosan $L0 $L1 sha256
cert lKUenc rsa2048 intR leaf_kuenc $L0 $L1 sha256
cert lR1024 rsa1024 intR leaf $L0 $L1 sha256
cert lSHA1 rsa2048 intR leaf $L0 $L1 sha1
cert lRpss pss intR leaf $L0 $L1 sha256
cert lEd ed25519 intR leaf $L0 $L1 sha256
cert lSelf rsa2048 self leaf_self $L0 $L1 sha256
# leaves under other intermediates
cert lE256 p256 intE leaf_ec $L0 $L1 sha256
cert lE384 p384 intE leaf_ec $L0 $L1 sha512
cert lE384b p384 rootE leaf_ec $L0 $L1 sha384
cert lEint p256 intE leaf_ec $L0 $L1 sha384
cert lViaExp rsa2048 intRexp leaf $L0 $L1 sha256
cert lViaNoCA rsa2048 intNoCA leaf $L0 $L1 sha256
cert lViaNoKU rsa2048 intNoKU leaf $L0 $L1 sha256
cert lViaP0 rsa2048 intP0b leaf $L0 $L1 sha256
cert lViaNC rsa2048 intNC leaf $L0 $L1 sha256
cert lViaEKUc rsa2048 intEKUc leaf $L0 $L1 sha256
cert lViaPSS rsa2048 intPSS leaf $L0 $L1 sha256
cert lD7 rsa2048 d7 leaf $L0 $L1 sha256
cert lD8 rsa2048 d8 leaf $L0 $L1 sha256
# stores
cat "$w/rootR.pem" > "$out/storeR.pem"
cat "$w/rootE.pem" > "$out/storeE.pem"
cat "$w/rootX.pem" > "$out/storeX.pem"
cat "$w/rootRdup.pem" "$w/rootR.pem" > "$out/storeDup.pem"
cat "$w/intR.pem" > "$out/storeInt.pem"
{ cat "$w/rootX.pem"; printf 'junk line outside any block\r\n'; cat "$w/rootE.pem" "$w/rootR.pem"; } > "$out/storeAll.pem"
# a store whose second block has a bad base64 character
{ cat "$w/rootX.pem"; sed '3s/^./*/' "$w/rootR.pem"; } > "$out/storeBad.pem"
# damaged certificates
python3 - "$out" <<'EOF'
import sys
d = sys.argv[1]
b = bytearray(open(d + "/lR.der", "rb").read())
b[-5] ^= 0x01
open(d + "/lRbadsig.der", "wb").write(b)
b = bytearray(open(d + "/lR.der", "rb").read())
open(d + "/lRtrunc.der", "wb").write(b[:-10])
# outer signatureAlgorithm sha256WithRSAEncryption -> sha384WithRSAEncryption; tbs keeps sha256
oid = bytes.fromhex("2a864886f70d01010b")
i = b.rfind(oid)
b[i + len(oid) - 1] = 0x0c
open(d + "/lRalgmis.der", "wb").write(b)
import random
r = random.Random(509)
open(d + "/garbage.der", "wb").write(bytes([0x30, 0x82, 0x01, 0x00]) + bytes(r.randrange(256) for _ in range(256)))
EOF
# CertificateVerify fixtures: content is the TLS 1.3 server signed content (RFC 8446 4.4.3) over a
# fixed 32-byte transcript hash
{ i=0; while [ $i -lt 64 ]; do printf ' '; i=$((i + 1)); done
  printf 'TLS 1.3, server CertificateVerify\000'
  printf '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f' | xxd -r -p; } > "$out/cv.msg"
sg() { ossl dgst "-$1" -sign "$w/$2.key" $3 -out "$out/$4" "$out/cv.msg"; }
sg sha256 lE256 "" cv_e256.sig
sg sha384 lE384 "" cv_e384.sig
sg sha256 lR "-sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:digest" cv_r256.sig
sg sha384 lPSS384 "-sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:digest" cv_r384.sig
sg sha512 lPSS512 "-sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:digest" cv_r512.sig
sg sha256 lR "" cv_rpkcs1.sig
sg sha256 lR1024 "-sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:digest" cv_r1024.sig
python3 - "$out" <<'EOF'
import sys
d = sys.argv[1]
m = bytearray(open(d + "/cv.msg", "rb").read())
m[-1] ^= 1
open(d + "/cv_bad.msg", "wb").write(m)
EOF
ls "$out" | wc -l
