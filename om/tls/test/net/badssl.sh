#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# badssl.com sweep for the om/tls client. Network: the badssl.com hosts only (plus 127.0.0.1).
# usage: sh om/tls/test/net/badssl.sh OMC
# Every host listed on https://badssl.com/ is tried live and, because badssl.com offers no TLS 1.3,
# also by replaying its real certificate chain from a local TLS 1.3 server (badssl_run.py explains
# the method); openssl verify on the same chain and trust store is the expected verdict.
# Writes om/tls/test/net/badssl.tsv. Chains and the dashboard are cached in $BADSSL_CACHE
# (default ../tlsverify-TlsSuites-ext/badssl next to the repository); delete it to refetch.
# Final line: BADSSL PASS|FAIL refuse_ok=.. accept_ok=.. explained=.. unexplained=.. na=.. hosts=..
set -u
omc=${1:?usage: badssl.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
root=$(cd "$tls/../.." && pwd)
out="$root/build/tls/net-badssl"
cache=${BADSSL_CACHE:-$root/../tlsverify-TlsSuites-ext/badssl}
. "$here/lib.sh"
rm -rf "$out"; mkdir -p "$out" "$cache/chains"
echo "omc $(sha256sum "$omc" | cut -c1-64)"
echo "tls.om $(sha256sum "$tls/tls.om" | cut -c1-64)"
echo "python $(python3 -c 'import sys,cryptography;print(sys.version.split()[0],"cryptography",cryptography.__version__)') $(openssl version)"
echo "store /etc/ssl/certs/ca-certificates.crt $(sha256sum /etc/ssl/certs/ca-certificates.crt | cut -c1-64)"
if [ ! -s "$cache/dashboard.html" ]; then
  curl -fsS --max-time 30 -o "$cache/dashboard.html" https://badssl.com/ || { echo "BADSSL FAIL refuse_ok=0 accept_ok=0 explained=0 unexplained=1 na=0 hosts=0"; exit 1; }
fi
echo "dashboard $(sha256sum "$cache/dashboard.html" | cut -c1-64) $(date -u -r "$cache/dashboard.html" +%Y-%m-%dT%H:%M:%SZ)"
build client "$tls/tls.om" || { echo "BADSSL FAIL refuse_ok=0 accept_ok=0 explained=0 unexplained=1 na=0 hosts=0"; exit 1; }
mkpki "$out/pki"
cl="$out/client/bin"

# the negotiation rows (cipher suite, key exchange, protocol) rest on what the client offers
p=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 "$here/hostile.py" "$p" "$out/pki" ch_policy > "$out/ch.log" 2>&1 &
sp=$!
sleep 0.5
timeout 20 "$cl" localhost "$p" get 127.0.0.1 "$out/pki/root.pem" > /dev/null 2>&1
wait $sp
pol=$(grep '^SRV ch_policy' "$out/ch.log")
echo "$pol"
case "$pol" in *" bad=none") polok=1 ;; *) echo "FAIL ClientHello policy"; polok=0 ;; esac

python3 "$here/badssl_run.py" "$cl" "$out/pki" "$cache/dashboard.html" "$cache/chains" "$here/badssl.tsv" > "$out/run.log" 2>&1
rc=$?
grep -v '^BADSSL ' "$out/run.log"
final=$(grep '^BADSSL ' "$out/run.log")
[ -n "$final" ] || final="BADSSL FAIL refuse_ok=0 accept_ok=0 explained=0 unexplained=1 na=0 hosts=0"
if [ $rc = 0 ] && [ $polok = 1 ]; then echo "$final"; exit 0; fi
echo "$final" | sed 's/^BADSSL PASS/BADSSL FAIL/'; exit 1
