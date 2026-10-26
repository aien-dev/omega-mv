#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Compose the om/tls client into one OM-2 unit with a program and compile it.
# usage: sh om/tls/build.sh OMC PROGRAM.om OUT
# The unit is: the line `om2`, then sha2, hkdf, x25519, chacha, aesgcm, ec, rsa, x509, tls (this
# order: each unit calls only units before it), then PROGRAM.om (which has `fn main`). The composed
# source is kept beside OUT as OUT.unit.om. Exit status is omc's; OUT is executable only on success.
set -eu
omc=${1:?usage: build.sh OMC PROGRAM.om OUT}; prog=${2:?}; out=${3:?}
here=$(cd "$(dirname "$0")" && pwd)
{ printf 'om2\n'
  for u in sha2 hkdf x25519 chacha aesgcm ec rsa x509 tls; do cat "$here/$u.om"; done
  cat "$prog"; } > "$out.unit.om"
rm -f "$out"
if "$omc" < "$out.unit.om" > "$out.tmp" 2> "$out.err"; then
  mv "$out.tmp" "$out"; chmod +x "$out"; rm -f "$out.err"
else
  rc=$?; rm -f "$out.tmp"; cat "$out.err" >&2; exit "$rc"
fi
