#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Build the native Omega contract checker with the fixed seed compiler.
# The seed reads .om on stdin and writes an AArch64 ELF on stdout.
set -eu

cd "$(dirname "$0")/../.."

SEED=${OMEGA_SEED:-/home/drakestapleton/workspace/omega-bridge/build/bootstrap/omc}
SEED_SHA256=191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9

if [ ! -x "$SEED" ]; then
    echo "contract-check: seed compiler not executable: $SEED" >&2
    exit 1
fi

got=$(sha256sum "$SEED" | awk '{print $1}')
if [ "$got" != "$SEED_SHA256" ]; then
    echo "contract-check: seed fingerprint mismatch" >&2
    echo "  expected $SEED_SHA256" >&2
    echo "  actual   $got" >&2
    exit 1
fi

mkdir -p build
build_one() {
SRC=$1
OUT=$2
tmp="$OUT.tmp.$$"
trap 'rm -f "$tmp"' EXIT HUP INT TERM

"$SEED" < "$SRC" > "$tmp"
if [ ! -s "$tmp" ]; then
    echo "contract-check: seed produced no output" >&2
    exit 1
fi

# Refuse anything that is not an AArch64 ELF. The seed always writes one; this
# check keeps a wrong or replaced seed from being published as a target.
if ! file "$tmp" | grep -Eq 'ELF 64-bit.*(ARM aarch64|AArch64)'; then
    echo "contract-check: output is not an AArch64 ELF" >&2
    file "$tmp" >&2 || true
    exit 1
fi

chmod +x "$tmp"
mv "$tmp" "$OUT"
trap - EXIT HUP INT TERM
printf '%s: seed=%s\n' "$(basename "$OUT")" "$SEED_SHA256"
printf '%s: output=%s\n' "$(basename "$OUT")" "$(sha256sum "$OUT" | awk '{print $1}')"
}
build_one tools/max-interop/contract_check.om build/contract-check
