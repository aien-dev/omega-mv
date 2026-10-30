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
#      handshake, HelloRetryRequest, NewSessionTicket, CertificateEntry extensions, zero-length
#      tls_read, htlssend of seed/net/https.om via bigsend.om) and a record-tampering proxy. Every
#      failed row also checks that tls_close keeps the connection failed (client.om prints
#      CLOSE_NOT_FAILED otherwise).
#   5. Mutants of tls.om (and of https.om htlssend) that the interop table must catch.
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
for u in sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 rev fips tls; do echo "$u.om $(sha256sum "$tls/$u.om" | cut -c1-64)"; done

# build NAME TLSFILE PROGRAM: compose with a given tls.om (the real one or a mutant); a non-empty
# $ecdhsrc replaces ecdh.om (an ecdh.om mutant under the unchanged tls.om)
ecdhsrc=
build() {
  mkdir -p "$out/$1"
  for u in sha2 hkdf drbg x25519 chacha aesgcm ec ecdh rsa x509 rev fips; do cp "$tls/$u.om" "$out/$1/"; done
  [ -z "$ecdhsrc" ] || cp "$ecdhsrc" "$out/$1/ecdh.om"
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
dmut "mutant dns_a4 name length unchecked" 's/^if gt len hn 253$/if gt len hn 9999/'

# DIT: tls_hwdit must agree with the kernel's cpuinfo flag (1 on a GB10, 0 on hosted runners); a
# mutant looking for an auxv key that no kernel emits (60) must report -1 on every host
if grep -qw dit /proc/cpuinfo; then dexp=1; else dexp=0; fi
build dit "$tls/tls.om" "$here/dit.om" || exit 1
dgot=$("$out/dit/bin" | sed -n 's/^hwdit //p')
if [ "$dgot" = "$dexp" ]; then echo "PASS dit: hwdit=$dgot cpuinfo=$dexp"; else echo "FAIL dit: hwdit=$dgot cpuinfo=$dexp"; fail=1; fi
sed 's/^if eq k 16$/if eq k 60/' "$tls/tls.om" > "$out/m.om"
if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL mutant auxv key: the mutation did not apply"; fail=1
else
  rm -rf "$out/dm"; build dm "$out/m.om" "$here/dit.om" || fail=1
  mut=$((mut + 1)); mgot=$("$out/dm/bin" | sed -n 's/^hwdit //p')
  if [ "$mgot" = "-1" ]; then mutok=$((mutok + 1)); echo "PASS mutant auxv key detected: hwdit=$mgot"; else echo "FAIL mutant auxv key not detected: hwdit=$mgot"; fail=1; fi
fi

# interop with the real tls.om
build client "$tls/tls.om" "$here/client.om" || exit 1
# bigsend.om runs on http.om + https.om (seed/net) after the om/tls units
cat "$root/seed/net/http.om" "$root/seed/net/https.om" "$here/bigsend.om" > "$out/bigsend.om"
build big "$tls/tls.om" "$out/bigsend.om" || exit 1
if sh "$here/interop.sh" "$out/client/bin" "$out/big/bin" > "$out/interop.log" 2>&1; then
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
imut "mutant chain result ignored" '/^r = x509_chain_verify/,/^ret TLECERT$/s/^if ne r 0$/if eq r 99/'
imut "mutant CertificateVerify ignored" 's/^ret TLECV$/ret 0/'
imut "mutant CCS count not checked"  's/^if ne get c TLCCS 1$/if eq get c TLCCS 9/'
imut "mutant close revives failed"   's/^if ne get c TLPH 3$/if ne get c TLPH 99/'
imut "mutant read record cap gone"   's/^if gt k 1001$/if gt k 999998/'
imut "mutant handshake record cap gone" 's/^if gt k 4000$/if gt k 9999/'
imut "mutant handshake close_notify unmapped" '/^if eq e TLCLOSE$/{n;s/^ret TLEALRT$/ret e/}'
imut "mutant version not checked"  's/^ret TLEVER$/ret 0/'
imut "mutant record MAC ignored"   's/^ret TLEMAC$/ret 0/'
imut "mutant session id not checked" 's/^ret TLESID$/ret 0/'
# revocation (flag 8, rev.om; rev.om's own mutants are in test/rev/run.sh): the rev rows catch these
imut "mutant revocation result ignored"  's/^r = tls_rev cb q st b$/r = ulong 0/'
imut "mutant status_request not offered" 's/^tls_pn c q 5 2$/tls_pn c q 65280 2/'
# MODEL-REVIEWED fixes: record limit on read and write, proactive KeyUpdate, handshake budget.
# "KeyUpdate not sent" is caught by the client's own seal limit (SEQUENCE_LIMIT); with both gone
# the kuw:8 server sees too many records under one key and answers with a fatal alert.
imut "mutant read record limit gone"   '/^fn tls_open /,/^end$/s/^if ge tls_gn c TLRS 8 tls_reclim c$/if ge tls_gn c TLRS 8 99999999/'
imut "mutant write KeyUpdate not sent" 's/^if ge tls_gn c TLWS 8 sub tls_reclim c 1$/if ge tls_gn c TLWS 8 99999999/'
imut "mutant write limit and KeyUpdate gone" 's/^if ge tls_gn c TLWS 8 sub tls_reclim c 1$/if ge tls_gn c TLWS 8 99999999/; s/^if ge tls_gn c TLWS 8 tls_reclim c$/if ge tls_gn c TLWS 8 99999999/'
imut "mutant handshake budget not armed" 's/^tls_pn c TLDL add ulong monons b 8$/tls_pn c TLDL 0 8/'
# HelloRetryRequest (RFC 8446 4.1.4)
imut "mutant HRR cookie not echoed"       's/^cl = tls_gn c TLCKL 2$/cl = ulong 0/'
imut "mutant ClientHello2 random changed" 's/^tls_mov c add b 6 TLRND 32$/put c add b 6 0/'
imut "mutant ClientHello2 session id changed" 's/^tls_mov c add b 39 TLSID 32$/put c add b 39 0/'
imut "mutant HRR group already shared"   '/^if ne and ulong get c TLGS g 0$/{n;s/^ret TLEKEYS$/ret 0/}'
imut "mutant HRR group never offered"    '/^if or eq g 0 eq and ulong get c TLGO g 0$/{n;s/^ret TLEKEYS$/ret 0/}'
imut "mutant second HRR accepted"        '/^fn tls_sh2 /,/^fn /{/^if eq e TLEHRR$/{n;s/^ret TLEUNEX$/ret 0/}}'
imut "mutant HRR suite not compared"     '/^if ne get c TLSU get c TLHSU$/{n;s/^ret TLECIPH$/ret 0/}'
imut "mutant HRR without change"         '/^fn tls_hrrext /,/^fn /s/^ret TLEHRR$/ret 0/'
imut "mutant HRR unknown extension"      's/^if or eq b 0 ne and seen b 0$/if ne and seen b 0/'
imut "mutant HRR recognised extension"   's/^if and eq b 0 ne tls_kext t 0$/if eq t 99999/'
imut "mutant HRR supported_versions optional" '/^fn tls_hrrext /,/^fn /s/^ret TLEVER$/ret 0/'
imut "mutant HRR cookie size unbounded"  's/^if gt n 12000$/if gt n 99999/'
# NewSessionTicket (RFC 8446 4.6.1), CertificateEntry extension, zero-length tls_read
imut "mutant ticket lifetime unchecked"  '/^if gt tls_gn c add TLHB 4 4 604800$/{n;s/^ret TLETKT$/ret 0/}'
imut "mutant empty ticket accepted"      '/^if eq tl 0$/{n;s/^ret TLEDEC$/ret 0/}'
imut "mutant ticket trailing bytes"      '/^if ne add p el z$/{n;s/^ret TLEDEC$/ret 0/}'
imut "mutant ticket extension overrun"   '/^fn tls_nst /,/^fn /{/^if ne p z$/{n;s/^ret TLEDEC$/ret 0/}}'
imut "mutant zero-length read is close"  '/^if eq len o 0$/{n;s/^ret neg long TLEARG$/ret 0/}'
imut "mutant entry extension unmapped"   's/^if eq r X5EEXT$/if eq r 9999/'
# htlssend (seed/net/https.om): without the range check a send past the end of io traps
sed '/^fn htlssend /,/^fn /s/^ret HEINVAL$/ret 0/' "$root/seed/net/https.om" > "$out/mhttps.om"
if cmp -s "$out/mhttps.om" "$root/seed/net/https.om"; then echo "FAIL mutant htlssend range: the mutation did not apply"; fail=1
else
  cat "$root/seed/net/http.om" "$out/mhttps.om" "$here/bigsend.om" > "$out/mbig.om"
  rm -rf "$out/mb"; build mb "$tls/tls.om" "$out/mbig.om" || exit 1
  mut=$((mut + 1))
  if sh "$here/interop.sh" "$out/client/bin" "$out/mb/bin" > "$out/mb.log" 2>&1; then echo "FAIL mutant htlssend range not detected"; fail=1
  else mutok=$((mutok + 1)); echo "PASS mutant htlssend range detected: $(grep -c '^FAIL' "$out/mb.log") rows fail"; fi
fi
# approved-only mode (tls_init flag 4): the approved rows of the table must catch each of these
imut "mutant approved accepts ChaCha"       's/^if and eq get c TLSU 1 eq tls_apm c 1$/if and eq get c TLSU 1 eq tls_apm c 9/'
imut "mutant approved offers ChaCha"        '/^fn tls_suites /,/^end$/s/^tls_pn c add p 2 4865 2$/tls_pn c add p 2 4867 2/'
imut "mutant approved offers x25519 group"  '/^fn tls_apgs /,/^end$/s/^tls_pn c add q 6 23 2$/tls_pn c add q 6 29 2/'
imut "mutant approved invalid share accepted" '/^fn tls_ecdhp /,/^fn tls_ikm /s/^ret TLEKEYS$/ret 0/'
# ecdh.om mutants under the real tls.om: the peer-point verdict ignored (SP 800-56A Rev. 3
# 5.6.2.3.4 validation skipped) and Z taken from the y coordinate. A wrong ladder step is not
# here: it changes d*G and d*Q by the same scalar map, so both sides still agree on Z (measured:
# interop 57/57 PASS); the ecdh unit's CAVP vectors catch it (test/ecdh mutant-step).
emut() { # LABEL SED-EXPR
  sed "$2" "$tls/ecdh.om" > "$out/me.om"
  if cmp -s "$out/me.om" "$tls/ecdh.om"; then echo "FAIL $1: the mutation did not apply"; fail=1; return; fi
  rm -rf "$out/im"; ecdhsrc="$out/me.om"; build im "$tls/tls.om" "$here/client.om" || { ecdhsrc=; fail=1; return; }
  ecdhsrc=; mut=$((mut + 1))
  if sh "$here/interop.sh" "$out/im/bin" > "$out/im.log" 2>&1; then echo "FAIL $1 not detected"; fail=1
  else mutok=$((mutok + 1)); echo "PASS $1 detected: $(grep -c '^FAIL' "$out/im.log") rows fail"; fi
}
emut "mutant ecdh peer validation skipped" '/^fn ecdh_shared /,/^fn ecdh_pct /{/^rc = ec_pub w p$/{n;s/^if ne rc 0$/if eq rc 99/}}'
emut "mutant ecdh Z from y"               '/^fn ecdh_shared /,/^fn ecdh_pct /s/^ecd_out w EDR0 z 0$/ecd_out w add EDR0 12 z 0/'

# secret residue (MODEL-REVIEWED, item 4b): after the handshake no handshake secret may remain in
# the client's memory; after tls_close no secret at all. The interop PKI must exist (built above).
# A second run in approved-only mode (fakesrv.py ap-hold:4: secp256r1, AES-128-GCM) checks the
# P-256 shared secret c[TLECZ] and the AES key schedule; residue=PASS needs both runs.
res="NOT_RUN"
rc=0; python3 "$here/residue.py" "$out/client/bin" "$root/build/tls/interop" > "$out/residue.log" 2>&1 || rc=$?
rc2=0; python3 "$here/residue.py" "$out/client/bin" "$root/build/tls/interop" --approved > "$out/residue-ap.log" 2>&1 || rc2=$?
if [ "$rc" = 0 ] && [ "$rc2" = 0 ]; then res="PASS"; echo "PASS residue: $(tail -n 1 "$out/residue.log")"; echo "PASS residue approved: $(tail -n 1 "$out/residue-ap.log")"
elif [ "$rc" = 3 ] || [ "$rc2" = 3 ]; then echo "NOT_RUN residue: $(tail -n 1 "$out/residue.log") / $(tail -n 1 "$out/residue-ap.log")"
else res="FAIL"; echo "FAIL residue:"; cat "$out/residue.log" "$out/residue-ap.log"; fail=1; fi
rmut() { # LABEL SED-EXPR [--approved]: the residue check must fail on the mutant
  [ "$res" = PASS ] || return
  sed "$2" "$tls/tls.om" > "$out/m.om"
  if cmp -s "$out/m.om" "$tls/tls.om"; then echo "FAIL $1: the mutation did not apply"; fail=1; return; fi
  rm -rf "$out/rm"; build rm "$out/m.om" "$here/client.om" || { fail=1; return; }
  mut=$((mut + 1))
  rc=0; python3 "$here/residue.py" "$out/rm/bin" "$root/build/tls/interop" ${3:-} > "$out/rm.log" 2>&1 || rc=$?
  if [ "$rc" = 1 ]; then mutok=$((mutok + 1)); echo "PASS $1 detected: $(grep -c '^  after' "$out/rm.log") residues"
  else echo "FAIL $1 not detected (exit $rc)"; fail=1; fi
}
rmut "mutant close keeps the traffic secrets" '/^fn tls_wipe /,/^end$/s/^tls_zero c TLCAP 64$/tls_zero c TLCAP 0/'
rmut "mutant handshake secrets kept after Finished" 's/^tls_zero c TLHS 128$/tls_zero c TLHS 0/'
rmut "mutant shared secret kept" 's/^tls_zero c TLSS 32$/tls_zero c TLSS 0/'
rmut "mutant approved P-256 shared secret kept" 's/^tls_zero c TLECZ 48$/tls_zero c TLECZ 0/' --approved

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
  echo "TLS PASS vectors=$kcount (rfc8448) dns=$dcount dit=$dgot interop=$irows residue=$res net=$net mutants=$mutok/$mut"; exit 0
fi
echo "TLS FAIL vectors=${kcount:-0} dns=${dcount:-0} dit=${dgot:-?} interop=${irows:-0} residue=$res net=$net mutants=$mutok/$mut"; exit 1
