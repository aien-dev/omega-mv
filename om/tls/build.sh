#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Compose the om/tls client into one OM-2 unit with a program and compile it.
# usage: sh om/tls/build.sh OMC PROGRAM.om OUT
#        sh om/tls/build.sh -p OMC BIN     (only the integrity step, for a program composed elsewhere)
# The unit is: the line `om2`, then sha2, hkdf, drbg, x25519, sha3, mlkem, chacha, aesgcm, ec, ecdh, rsa, x509, rev, fips, tls (this
# order: each unit calls only units before it), then PROGRAM.om (which has `fn main`). The composed
# source is kept beside OUT as OUT.unit.om. Exit status is omc's; OUT is executable only on success.
# Integrity step: the same units with a three-line main calling fips_itool (fips.om) are compiled to
# a tool that writes HMAC-SHA-256 of the binary into its fips.om FKIH slot; fips_check verifies it at
# first use (failure code 16). A binary that skips this step fails closed. Tool exit: 0 done,
# 2 usage, 3 I/O error, 4 no marker, two markers or too large.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
units() { for u in sha2 hkdf drbg x25519 sha3 mlkem chacha aesgcm ec ecdh rsa x509 rev fips tls; do cat "$here/$u.om"; done; }
# integ OMC BIN: compile the integrity tool beside BIN, run it on BIN, remove it
integ() {
  { printf 'om2\n'; units; printf 'fn main to int\nret fips_itool\nend\n'; } > "$2.itool.om"
  if "$1" < "$2.itool.om" > "$2.itool" 2> "$2.itool.err"; then :; else
    rc=$?; cat "$2.itool.err" >&2; rm -f "$2.itool" "$2.itool.om" "$2.itool.err"; return "$rc"
  fi
  chmod +x "$2.itool"
  rc=0; "$2.itool" "$2" || rc=$?
  rm -f "$2.itool" "$2.itool.om" "$2.itool.err"
  [ "$rc" -eq 0 ] || echo "build.sh: integrity step failed on $2 (tool exit $rc)" >&2
  return "$rc"
}
if [ "${1:-}" = -p ]; then
  omc=${2:?usage: build.sh -p OMC BIN}; bin=${3:?usage: build.sh -p OMC BIN}
  integ "$omc" "$bin"; exit
fi
omc=${1:?usage: build.sh OMC PROGRAM.om OUT}; prog=${2:?}; out=${3:?}
{ printf 'om2\n'; units; cat "$prog"; } > "$out.unit.om"
rm -f "$out"
if "$omc" < "$out.unit.om" > "$out.tmp" 2> "$out.err"; then
  rm -f "$out.err"
else
  rc=$?; rm -f "$out.tmp"; cat "$out.err" >&2; exit "$rc"
fi
if integ "$omc" "$out.tmp"; then
  mv "$out.tmp" "$out"; chmod +x "$out"
else
  rc=$?; rm -f "$out.tmp"; exit "$rc"
fi
