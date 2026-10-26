#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Interop and refusal table for the om/tls client against local `openssl s_server` instances.
# usage: sh om/tls/test/tls/interop.sh CLIENT    (CLIENT = a build of test/tls/client.om)
# A throwaway PKI is generated with the openssl CLI (test tooling) under build/tls/interop/.
# Every server listens on 127.0.0.1 only. Each row prints the expected and observed outcome;
# exit 0 only when every row matches. Needs openssl (3.x) and python3 (the tamper proxy).
set -u
client=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
d="$root/build/tls/interop"
rm -rf "$d"; mkdir -p "$d"; cd "$d" || exit 2
q() { "$@" > /dev/null 2>&1 || { echo "SETUP FAIL: $*"; exit 2; }; }

# ---- PKI: root -> intermediate -> leaves (EC P-256 unless noted) ----
cat > ca.cnf <<'EOF'
[ca]
default_ca = d
[d]
database = index.txt
new_certs_dir = .
serial = serial
default_md = sha256
policy = p
copy_extensions = copy
unique_subject = no
[p]
commonName = supplied
[v3ca]
basicConstraints = critical,CA:true
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
[v3leaf]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth
EOF
: > index.txt; echo 1000 > serial
key() { q openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1.key"; }
q openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out root.key
q openssl req -x509 -new -key root.key -subj /CN=omega-tls-test-root -days 30 -sha256 \
  -addext basicConstraints=critical,CA:true -addext keyUsage=critical,keyCertSign,cRLSign -out root.pem
key int
q openssl req -new -key int.key -subj /CN=omega-tls-test-int -out int.csr
q openssl ca -batch -config ca.cnf -cert root.pem -keyfile root.key -extensions v3ca -days 30 -in int.csr -out int.pem -notext
# leaf NAME SAN [extra ca args]: signed by the intermediate
leaf() {
  n=$1; san=$2; shift 2
  [ -f "$n.key" ] || key "$n"
  q openssl req -new -key "$n.key" -subj "/CN=$n" -addext "subjectAltName=$san" -out "$n.csr"
  q openssl ca -batch -config ca.cnf -cert int.pem -keyfile int.key -extensions v3leaf "$@" -in "$n.csr" -out "$n.pem" -notext
}
leaf good DNS:localhost -days 10
leaf wrongname DNS:other.example -days 10
leaf expired DNS:localhost -startdate 20200101000000Z -enddate 20200201000000Z
leaf future DNS:localhost -startdate 20990101000000Z -enddate 20991231000000Z
q openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rsa.key
leaf rsa DNS:localhost -days 10
q openssl req -x509 -new -key good.key -subj /CN=localhost -addext subjectAltName=DNS:localhost -days 10 -out self.pem
q openssl req -x509 -new -key good.key -subj /CN=localhost -addext subjectAltName=DNS:localhost \
  -addext basicConstraints=critical,CA:false -addext keyUsage=critical,digitalSignature -days 10 -out selfee.pem
cp good.key selfee.key
# bad signature: the good leaf with the last byte of its signature flipped
q openssl x509 -in good.pem -outform DER -out good.der
python3 -c "import sys;b=bytearray(open('good.der','rb').read());b[-1]^=1;open('badsig.der','wb').write(b)"
q openssl x509 -inform DER -in badsig.der -out badsig.pem
cp good.key badsig.key; cp good.key self.key
# an unrelated root signs a leaf for localhost (unknown issuer)
q openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out other.key
q openssl req -x509 -new -key other.key -subj /CN=other-root -days 30 -addext basicConstraints=critical,CA:true -addext keyUsage=critical,keyCertSign -out other.pem
q openssl req -new -key good.key -subj /CN=localhost -addext subjectAltName=DNS:localhost -out stray.csr
q openssl x509 -req -in stray.csr -CA other.pem -CAkey other.key -CAcreateserial -days 10 -copy_extensions copy -out stray.pem
cp good.key stray.key

port=15400
# nextport: the next port above $port with no listener on 127.0.0.1 (another process may hold one;
# connecting to it would test that process instead of this row's server)
nextport() {
  port=$((port + 1))
  while ss -Hltn "sport = :$port" | grep -q .; do port=$((port + 1)); done
}
fail=0; rows=0
# row LABEL EXPECT CERT CHAIN SERVER-ARGS... : EXPECT is OK or the TLS error name (and x509 name)
row() {
  label=$1; expect=$2; cert=$3; chain=$4; shift 4
  nextport; rows=$((rows + 1))
  if [ "$chain" = - ]; then ch=""; else ch="-cert_chain $chain"; fi
  # shellcheck disable=SC2086
  openssl s_server -accept "127.0.0.1:$port" -naccept 1 -cert "$cert.pem" -key "$cert.key" $ch -www "$@" > "s$port.log" 2>&1 &
  sp=$!
  sleep 0.4
  timeout 30 "$client" localhost "$port" get 127.0.0.1 "$d/root.pem" > "c$port.out" 2> "c$port.err"
  rc=$?
  kill "$sp" 2> /dev/null; wait "$sp" 2> /dev/null
  if [ "$rc" = 0 ] && grep -q '^HTTP/1.0 200 ok' "c$port.out"; then got=OK; else got="$(tr -d '\n' < "c$port.err") (exit $rc)"; fi
  case "$got" in
  "$expect"*) v=PASS ;;
  *) v=FAIL; fail=1 ;;
  esac
  printf '%s | %-28s | expect %-60s | got %s\n' "$v" "$label" "$expect" "$got"
}
row "good EC chain, chacha"     OK                                                     good int.pem -tls1_3 -ciphersuites TLS_CHACHA20_POLY1305_SHA256
row "good EC chain, aes128gcm"  OK                                                     good int.pem -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256
row "good RSA leaf (rsa_pss)"   OK                                                     rsa  int.pem -tls1_3
row "expired leaf"              "TLSERR TLS_ERR_CERTIFICATE detail 2 X509_ERR_EXPIRED"  expired int.pem -tls1_3
row "not-yet-valid leaf"        "TLSERR TLS_ERR_CERTIFICATE detail 3"                  future int.pem -tls1_3
row "wrong name"                "TLSERR TLS_ERR_CERTIFICATE detail 4"                  wrongname int.pem -tls1_3
row "self-signed leaf (CA:TRUE)" "TLSERR TLS_ERR_CERTIFICATE detail 15"                 self - -tls1_3
row "self-signed leaf (CA:FALSE)" "TLSERR TLS_ERR_CERTIFICATE detail 5"                 selfee - -tls1_3
row "bad cert signature"        "TLSERR TLS_ERR_CERTIFICATE detail 6"                  badsig int.pem -tls1_3
row "unknown issuer"            "TLSERR TLS_ERR_CERTIFICATE detail 5"                  stray - -tls1_3
row "missing intermediate"      "TLSERR TLS_ERR_CERTIFICATE detail 5"                  good - -tls1_3
# the server itself refuses these: our ClientHello offers nothing it accepts (alert 70 / 40)
row "TLS 1.2 only server"       "TLSERR TLS_ERR_ALERT_RECEIVED detail 70"              good int.pem -tls1_2
row "AES-256 suite only"        "TLSERR TLS_ERR_ALERT_RECEIVED detail 40"              good int.pem -tls1_3 -ciphersuites TLS_AES_256_GCM_SHA384
row "P-256 group only"          "TLSERR TLS_ERR_ALERT_RECEIVED detail 40"              good int.pem -tls1_3 -groups P-256

# crafted ServerHellos (fakesh.py): the client's own refusals
fake() {
  nextport; rows=$((rows + 1))
  python3 "$here/fakesh.py" "$port" "$1" > "f$port.log" 2>&1 &
  fp=$!
  sleep 0.4
  timeout 30 "$client" localhost "$port" get 127.0.0.1 "$d/root.pem" > "c$port.out" 2> "c$port.err"; rc=$?
  kill "$fp" 2> /dev/null; wait "$fp" 2> /dev/null
  got="$(tr -d '\n' < "c$port.err") (exit $rc)"
  case "$got" in "$2"*) v=PASS ;; *) v=FAIL; fail=1 ;; esac
  printf '%s | %-28s | expect %-60s | got %s\n' "$v" "fake ServerHello: $1" "$2" "$got"
}
fake tls12  "TLSERR TLS_ERR_PROTOCOL_VERSION"
fake hrr    "TLSERR TLS_ERR_HELLO_RETRY_UNSUPPORTED"
fake suite  "TLSERR TLS_ERR_CIPHER_SUITE"
fake sid    "TLSERR TLS_ERR_SESSION_ID_ECHO"
fake group  "TLSERR TLS_ERR_KEY_SHARE"
fake zero   "TLSERR TLS_ERR_ZERO_SHARED_SECRET"
fake dupext "TLSERR TLS_ERR_EXTENSION"
fake trail  "TLSERR TLS_ERR_DECODE"

# full handshakes from fakesrv.py (security review of omega-mv #813): CCS policy, record-count caps,
# CertificateVerify refusals, close_notify during the handshake. EXPECT OK needs exit 0 and the reply.
fsrv() {
  nextport; rows=$((rows + 1))
  python3 "$here/fakesrv.py" "$port" "$d" "$1" > "f$port.log" 2>&1 &
  fp=$!
  sleep 0.6
  timeout 60 "$client" localhost "$port" get 127.0.0.1 "$d/root.pem" > "c$port.out" 2> "c$port.err"; rc=$?
  kill "$fp" 2> /dev/null; wait "$fp" 2> /dev/null
  if [ "$rc" = 0 ] && grep -q '^HTTP/1.0 200 ok' "c$port.out"; then got=OK; else got="$(tr -d '\n' < "c$port.err") (exit $rc)"; fi
  case "$got" in "$2"*) v=PASS ;; *) v=FAIL; fail=1 ;; esac
  printf '%s | %-28s | expect %-60s | got %s\n' "$v" "fakesrv: $1" "$2" "$got"
}
fsrv ok          OK
fsrv empty50     OK
fsrv ccs2        "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv ccsflood    "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv ccspre      "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv ccslate     "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv encccs      "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv frag        "TLSERR TLS_ERR_MESSAGE_TOO_BIG detail 0 (exit 32)"
fsrv cvbad       "TLSERR TLS_ERR_CERTIFICATE_VERIFY"
fsrv cvkey       "TLSERR TLS_ERR_CERTIFICATE_VERIFY"
fsrv empty1500   "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv tickets1500 "TLSERR TLS_ERR_UNEXPECTED_MESSAGE detail 0 (exit 17)"
fsrv closehs     "TLSERR TLS_ERR_ALERT_RECEIVED detail 0 (exit 31)"
fsrv closepre    "TLSERR TLS_ERR_ALERT_RECEIVED detail 0 (exit 31)"

# record tamper: a proxy flips one bit in the first server application-data record after the
# handshake (the 5th server record); the client must refuse it with bad_record_mac
nextport; rows=$((rows + 1)); lp=$port; nextport; bp=$port; port=$lp
openssl s_server -accept "127.0.0.1:$bp" -naccept 1 -cert good.pem -key good.key -cert_chain int.pem -www -tls1_3 > "s$bp.log" 2>&1 &
sp=$!
python3 "$here/tamper.py" "$port" "$bp" > "p$port.log" 2>&1 &
pp=$!
sleep 0.6
timeout 30 "$client" localhost "$port" get 127.0.0.1 "$d/root.pem" > "c$port.out" 2> "c$port.err"; rc=$?
kill "$sp" "$pp" 2> /dev/null; wait "$sp" "$pp" 2> /dev/null
got="$(tr -d '\n' < "c$port.err") (exit $rc)"
case "$got" in "TLSERR TLS_ERR_BAD_RECORD_MAC"*) v=PASS ;; *) v=FAIL; fail=1 ;; esac
printf '%s | %-28s | expect %-60s | got %s\n' "$v" "tampered record (proxy)" "TLSERR TLS_ERR_BAD_RECORD_MAC" "$got"

if [ $fail = 0 ]; then echo "INTEROP PASS rows=$rows"; else echo "INTEROP FAIL rows=$rows"; fi
exit $fail
