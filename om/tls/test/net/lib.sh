# SPDX-License-Identifier: Apache-2.0
# Shared by hostile.sh and badssl.sh (sourced; needs $omc, $tls and $out set by the caller).
# build NAME TLSFILE: compose test/tls/client.om after every unit (the given tls.om) into
# $out/NAME/bin, exactly as test/tls/run.sh does; accepted only with rc 0 and empty stderr.
build() {
  mkdir -p "$out/$1"
  for u in sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 fips; do cp "$tls/$u.om" "$out/$1/"; done
  cp "$2" "$out/$1/tls.om"; cp "$tls/build.sh" "$out/$1/"
  sh "$out/$1/build.sh" "$omc" "$tls/test/tls/client.om" "$out/$1/bin" 2> "$out/$1.err"
  rc=$?
  if [ "$rc" != 0 ] || [ -s "$out/$1.err" ]; then echo "FAIL build $1 (rc $rc): $(cat "$out/$1.err")"; return 1; fi
}
q() { "$@" > /dev/null 2>&1 || { echo "SETUP FAIL: $*"; exit 2; }; }
# mkpki DIR: root -> intermediate -> leaves for localhost (EC P-256 "good", RSA-2048 "rsa"), and
# an unrelated EC key "other.key". Throwaway, made with the openssl CLI (test tooling).
mkpki() {
  rm -rf "$1"; mkdir -p "$1"
  ( cd "$1" || exit 2
    printf '[ca]\ndefault_ca = d\n[d]\ndatabase = index.txt\nnew_certs_dir = .\nserial = serial\ndefault_md = sha256\npolicy = p\ncopy_extensions = copy\nunique_subject = no\n[p]\ncommonName = supplied\n[v3ca]\nbasicConstraints = critical,CA:true\nkeyUsage = critical,keyCertSign,cRLSign\n[v3leaf]\nbasicConstraints = critical,CA:false\nkeyUsage = critical,digitalSignature\nextendedKeyUsage = serverAuth\n' > ca.cnf
    : > index.txt; echo 1000 > serial
    for k in root int good other; do q openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out $k.key; done
    q openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rsa.key
    q openssl req -x509 -new -key root.key -subj /CN=omega-net-test-root -days 30 -sha256 \
      -addext basicConstraints=critical,CA:true -addext keyUsage=critical,keyCertSign,cRLSign -out root.pem
    q openssl req -new -key int.key -subj /CN=omega-net-test-int -out int.csr
    q openssl ca -batch -config ca.cnf -cert root.pem -keyfile root.key -extensions v3ca -days 30 -in int.csr -out int.pem -notext
    for n in good rsa; do
      q openssl req -new -key $n.key -subj /CN=$n -addext subjectAltName=DNS:localhost -out $n.csr
      q openssl ca -batch -config ca.cnf -cert int.pem -keyfile int.key -extensions v3leaf -days 10 -in $n.csr -out $n.pem -notext
    done ) || exit 2
}
