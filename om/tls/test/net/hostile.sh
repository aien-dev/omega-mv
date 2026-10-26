#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Malicious-server suite for the om/tls client (Omega has no TLS server: every case is a scripted
# hostile TLS 1.3 server, hostile.py, against a build of test/tls/client.om).
# usage: sh om/tls/test/net/hostile.sh OMC [CASE...]
#   1. ClientHello policy: what the client offers (versions, suites, groups, signature schemes).
#   2. Every case of hostile.tsv (or the named ones): RFC 8446 section, expected abort and alert,
#      observed client error and the alert the client sent (decrypted by the server).
#   3. Mutants of tls.om that the table must catch (full run only).
# Needs python3 with `cryptography` and the openssl CLI; listens on 127.0.0.1 only.
# Final line: HOSTILE PASS|FAIL cases=N pass=P fail=F (mutants are reported above it and make it FAIL).
set -u
omc=${1:?usage: hostile.sh OMC [CASE...]}; shift
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
root=$(cd "$tls/../.." && pwd)
out="$root/build/tls/net-hostile"
. "$here/lib.sh"
rm -rf "$out"; mkdir -p "$out"
echo "omc $(sha256sum "$omc" | cut -c1-64)"
echo "tls.om $(sha256sum "$tls/tls.om" | cut -c1-64)"
echo "python $(python3 -c 'import sys,cryptography;print(sys.version.split()[0],"cryptography",cryptography.__version__)') $(openssl version)"
build client "$tls/tls.om" || { echo "HOSTILE FAIL cases=0 pass=0 fail=1"; exit 1; }
mkpki "$out/pki"
cl="$out/client/bin"

# 1. ClientHello policy
p=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 "$here/hostile.py" "$p" "$out/pki" ch_policy > "$out/ch.log" 2>&1 &
sp=$!
sleep 0.5
timeout 20 "$cl" localhost "$p" get 127.0.0.1 "$out/pki/root.pem" > /dev/null 2>&1
wait $sp
pol=$(grep '^SRV ch_policy' "$out/ch.log")
echo "$pol"
polfail=0
case "$pol" in *" bad=none") ;; *) echo "FAIL ClientHello policy"; polfail=1 ;; esac

# 2. the case table
python3 "$here/hostile_run.py" "$cl" "$out/pki" "$here/hostile.tsv" "$@" > "$out/table.log" 2>&1
trc=$?
grep -v '^HOSTILE ' "$out/table.log"
final=$(grep '^HOSTILE ' "$out/table.log")
[ -n "$final" ] || { cat "$out/table.log"; final="HOSTILE FAIL cases=0 pass=0 fail=1"; trc=1; }

# 3. mutants (full run only): each mutant of tls.om must turn at least one case that passes on the
#    real tls.om into FAIL (the named cases all pass on the unmodified file)
mfail=0
if [ $# = 0 ]; then
  mut() { # LABEL CASES SED-EXPR
    sed "$3" "$tls/tls.om" > "$out/m.om"
    if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL mutant $1: the mutation did not apply"; mfail=1; return; fi
    build mut "$out/m.om" || { mfail=1; return; }
    if python3 "$here/hostile_run.py" "$out/mut/bin" "$out/pki" "$here/hostile.tsv" $2 > "$out/mut.log" 2>&1; then
      echo "FAIL mutant $1: not detected ($2)"; mfail=1
    else echo "PASS mutant $1: detected ($(grep -c '^FAIL' "$out/mut.log") of $(echo $2 | wc -w) cases fail)"; fi
  }
  mut "EE unknown extension accepted"   "ee_unsol ee_mfl"        's/^if or eq bit 0 ne and seen bit 0$/if ne and seen bit 0/'
  mut "SH extension type not checked"   "sh_ext_unsol sh_ext_ksalias" 's/^if or ne t 51 ne and seen 2 0$/if ne and seen 2 0/'
  mut "post-handshake non-KeyUpdate accepted" "post_cr post_unknown_hs" '/^if ne t 24$/{n;s/^ret TLEUNEX$/ret 0/;}'
fi

if [ $trc = 0 ] && [ $polfail = 0 ] && [ $mfail = 0 ]; then echo "$final"; exit 0; fi
echo "$final" | sed 's/^HOSTILE PASS/HOSTILE FAIL/'; exit 1
