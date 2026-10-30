#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Test tooling: build the OpenSSL 3.5.4 command-line tool (the X25519MLKEM768 interop oracle for
# interop.sh, OPENSSL35) into PREFIX, from the release tarball verified against its pinned sha256.
# usage: sh om/tls/test/tls/openssl35.sh PREFIX    -> PREFIX/bin/openssl (static, no shared libs)
# Reuses an existing PREFIX/bin/openssl that reports 3.5.4 and lists ML-KEM-768 (the CI cache).
set -eu
prefix=${1:?usage: openssl35.sh PREFIX}
ver=3.5.4
sha=967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99
url=https://github.com/openssl/openssl/releases/download/openssl-$ver/openssl-$ver.tar.gz
ok() { "$prefix/bin/openssl" version 2> /dev/null | grep -q "^OpenSSL $ver " \
  && "$prefix/bin/openssl" list -kem-algorithms 2> /dev/null | grep -q ML-KEM-768; }
if ok; then echo "openssl35: cached $("$prefix/bin/openssl" version)"; exit 0; fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL -o "$work/openssl.tar.gz" "$url"
echo "$sha  $work/openssl.tar.gz" | sha256sum -c -
tar -xzf "$work/openssl.tar.gz" -C "$work"
cd "$work/openssl-$ver"
./Configure --prefix="$prefix" --openssldir="$prefix/ssl" no-shared no-docs no-tests > "$work/configure.log"
make -j"$(nproc)" > "$work/make.log" 2>&1 || { tail -n 40 "$work/make.log"; exit 1; }
make install_sw > /dev/null
ok || { echo "openssl35: built binary does not report $ver with ML-KEM-768"; exit 1; }
echo "openssl35: built $("$prefix/bin/openssl" version)"
