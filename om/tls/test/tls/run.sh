#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test suite for om/tls/tls.om (the TLS 1.3 handshake, record layer, DNS and TCP).
# usage: sh om/tls/test/tls/run.sh OMC
#   1. RFC 8448 section 3 known-answer test (kat.om): ECDHE, every key-schedule secret, handshake
#      and application keys/IVs, server Finished (accept and tamper), client Finished, and record
#      seal/open byte for byte against the RFC's records.
#   2. Mutants of tls.om that the KAT must catch (MISMATCH, exit 1).
#   3. DNS answer parser unit cases (dnsunit.om, vectors/dns.txt from gen_dns.py) and their mutants
#      (detected by a MISMATCH or a runtime trap, exit >= 100).
#   4. Local interop and refusal table (interop.sh): openssl s_server with good and bad
#      certificates, protocol refusals, crafted ServerHellos, full fake-server handshakes
#      (fakesrv.py: CCS policy, record-count caps, CertificateVerify, close_notify during the
#      handshake) and a record-tampering proxy. Every failed row also checks that tls_close keeps
#      the connection failed (client.om prints CLOSE_NOT_FAILED otherwise).
#   5. Mutants of tls.om that the interop table must catch.
#   6. With TLS_NET=1 only (needs the Internet): real handshakes and Python-ssl parity of fetched
#      bytes for api.github.com and raw.githubusercontent.com (read-only GETs), and the SMTP relay
#      mail.gandi.net on 465 (implicit TLS) and 587 (STARTTLS), EHLO and QUIT only.
# Exit 0 only when every executed step passes; the network step prints NOT_RUN without TLS_NET=1.
set -u
omc=${1:?usage: run.sh OMC}
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
root=$(cd "$tls/../.." && pwd)
out="$root/build/tls/tls"
rm -rf "$out"; mkdir -p "$out"
fail=0; mut=0; mutok=0
echo "omc $(sha256sum "$omc" | cut -c1-64)"
for u in sha2 hkdf x25519 chacha aesgcm ec rsa x509 tls; do echo "$u.om $(sha256sum "$tls/$u.om" | cut -c1-64)"; done

# build NAME TLSFILE PROGRAM: compose with a given tls.om (the real one or a mutant)
build() {
  mkdir -p "$out/$1"
  for u in sha2 hkdf x25519 chacha aesgcm ec rsa x509; do cp "$tls/$u.om" "$out/$1/"; done
  cp "$2" "$out/$1/tls.om"; cp "$tls/build.sh" "$out/$1/"
  sh "$out/$1/build.sh" "$omc" "$3" "$out/$1/bin" 2> "$out/$1.err" || { echo "FAIL build $1: $(cat "$out/$1.err")"; return 1; }
}
kat() { "$1" < "$here/vectors/rfc8448-1rtt.txt" > "$out/$2.log" 2>&1; }

build kat "$tls/tls.om" "$here/kat.om" || exit 1
if kat "$out/kat/bin" kat; then echo "PASS rfc8448: $(tail -n 1 "$out/kat.log")"; else echo "FAIL rfc8448:"; cat "$out/kat.log"; fail=1; fi
kcount=$(sed -n 's/^tls kat: \([0-9]*\) checks.*/\1/p' "$out/kat.log")

# expectation mutant: the first verify_data byte of the expected client Finished changed
sed 's/^cfin 14000020a8/cfin 14000020a9/' "$here/vectors/rfc8448-1rtt.txt" > "$out/exp.txt"
mutant() { # LABEL RC LOG: detected = exit 1 with a MISMATCH line
  mut=$((mut + 1))
  if [ "$2" = 1 ] && grep -q '^MISMATCH ' "$3"; then mutok=$((mutok + 1)); echo "PASS $1 detected: $(grep -c '^MISMATCH ' "$3") mismatches"
  else echo "FAIL $1 not detected (exit $2)"; fail=1; fi
}
rc=0; "$out/kat/bin" < "$out/exp.txt" > "$out/exp.log" 2>&1 || rc=$?
mutant "expectation mutant" "$rc" "$out/exp.log"

# library mutants for the KAT: a label byte, the nonce sequence xor, the HKDF label length
kmut() { # LABEL SED-EXPR
  sed "$2" "$tls/tls.om" > "$out/m.om"
  if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL $1: the mutation did not apply"; fail=1; return; fi
  rm -rf "$out/km"; build km "$out/m.om" "$here/kat.om" || { fail=1; return; }
  rc=0; kat "$out/km/bin" km || rc=$?
  mutant "$1" "$rc" "$out/km.log"
}
kmut "mutant label c hs traffic" 's/^text TLLCHS "c hs traffic"/text TLLCHS "c hs traffie"/'
kmut "mutant nonce ignores seq" 's/^b = and shr sq s 255/b = and shr sq s 0/'
kmut "mutant label length"       's/^put p 34 uchar add t 6/put p 34 uchar add t 5/'
kmut "mutant finished key"       's/^r = tls_drv c tls_sel TLTMP so 32 TLIFIN part e 0 0/r = tls_drv c tls_sel TLTMP so 32 TLIDER part e 0 0/'

# DNS answer parser (security review of omega-mv #813, finding 8)
build dns "$tls/tls.om" "$here/dnsunit.om" || exit 1
if "$out/dns/bin" < "$here/vectors/dns.txt" > "$out/dns.log" 2>&1; then echo "PASS dns: $(tail -n 1 "$out/dns.log")"
else echo "FAIL dns:"; cat "$out/dns.log"; fail=1; fi
dcount=$(sed -n 's/^dns unit: \([0-9]*\) checks.*/\1/p' "$out/dns.log")
dmut() { # LABEL SED-EXPR: detected = exit 1 with a MISMATCH line, or a runtime trap (exit >= 100)
  sed "$2" "$tls/tls.om" > "$out/m.om"
  if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL $1: the mutation did not apply"; fail=1; return; fi
  rm -rf "$out/dm"; build dm "$out/m.om" "$here/dnsunit.om" || { fail=1; return; }
  mut=$((mut + 1))
  rc=0; "$out/dm/bin" < "$here/vectors/dns.txt" > "$out/dm.log" 2>&1 || rc=$?
  if [ "$rc" = 1 ] && grep -q '^MISMATCH ' "$out/dm.log"; then mutok=$((mutok + 1)); echo "PASS $1 detected: $(grep -c '^MISMATCH ' "$out/dm.log") mismatches"
  elif [ "$rc" -ge 100 ]; then mutok=$((mutok + 1)); echo "PASS $1 detected: trap (exit $rc)"
  else echo "FAIL $1 not detected (exit $rc)"; fail=1; fi
}
dmut "mutant dns flags only QR"    's/^if ne and fl 64015 32768$/if ne and fl 32768 32768/'
dmut "mutant dns owner ignored"    's/^if eq dns_neq r o cur 0$/if lt dns_neq r o cur 2/'
dmut "mutant dns label cap gone"   's/^if gt k 127$/if gt k 9999/'

# interop with the real tls.om
build client "$tls/tls.om" "$here/client.om" || exit 1
if sh "$here/interop.sh" "$out/client/bin" > "$out/interop.log" 2>&1; then
  echo "PASS interop: $(tail -n 1 "$out/interop.log")"
else
  echo "FAIL interop:"; grep -v '^PASS' "$out/interop.log"; fail=1
fi
irows=$(sed -n 's/^INTEROP .* rows=\([0-9]*\)/\1/p' "$out/interop.log")
cp "$out/interop.log" "$out/interop-table.txt"

# interop mutants: each must turn at least one row of the table into FAIL
imut() { # LABEL SED-EXPR
  sed "$2" "$tls/tls.om" > "$out/m.om"
  if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL $1: the mutation did not apply"; fail=1; return; fi
  rm -rf "$out/im"; build im "$out/m.om" "$here/client.om" || { fail=1; return; }
  mut=$((mut + 1))
  if sh "$here/interop.sh" "$out/im/bin" > "$out/im.log" 2>&1; then echo "FAIL $1 not detected"; fail=1
  else mutok=$((mutok + 1)); echo "PASS $1 detected: $(grep -c '^FAIL' "$out/im.log") rows fail"; fi
}
imut "mutant chain result ignored" '/^r = x509_chain_verify/{n;s/^if ne r 0/if eq r 99/}'
imut "mutant CertificateVerify ignored" 's/^ret TLECV$/ret 0/'
imut "mutant CCS count not checked"  's/^if ne get c TLCCS 1$/if eq get c TLCCS 9/'
imut "mutant close revives failed"   's/^if ne get c TLPH 3$/if ne get c TLPH 99/'
imut "mutant read record cap gone"   's/^if gt k 1001$/if gt k 999998/'
imut "mutant handshake record cap gone" 's/^if gt k 4000$/if gt k 9999/'
imut "mutant handshake close_notify unmapped" '/^if eq e TLCLOSE$/{n;s/^ret TLEALRT$/ret e/}'
imut "mutant version not checked"  's/^ret TLEVER$/ret 0/'
imut "mutant record MAC ignored"   's/^ret TLEMAC$/ret 0/'
imut "mutant session id not checked" 's/^ret TLESID$/ret 0/'

if [ "${TLS_NET:-0}" = 1 ]; then
  if python3 "$here/parity.py" "$out/client/bin" api.github.com /licenses/mit /licenses/apache-2.0 /repos/torvalds/linux/license > "$out/net.log" 2>&1 \
    && python3 "$here/parity.py" "$out/client/bin" raw.githubusercontent.com /torvalds/linux/v6.0/README /torvalds/linux/v6.0/MAINTAINERS >> "$out/net.log" 2>&1 \
    && python3 "$here/parity.py" "$out/client/bin" --smtp mail.gandi.net 465 smtp >> "$out/net.log" 2>&1 \
    && python3 "$here/parity.py" "$out/client/bin" --smtp mail.gandi.net 587 tsmtp >> "$out/net.log" 2>&1; then
    net="PASS"
  else net="FAIL"; fail=1; fi
  cat "$out/net.log"
else
  net="NOT_RUN"; echo "NOT_RUN network parity (set TLS_NET=1)"
fi

if [ $fail = 0 ]; then
  echo "TLS PASS vectors=$kcount (rfc8448) dns=$dcount interop=$irows net=$net mutants=$mutok/$mut"; exit 0
fi
echo "TLS FAIL vectors=${kcount:-0} dns=${dcount:-0} interop=${irows:-0} net=$net mutants=$mutok/$mut"; exit 1
