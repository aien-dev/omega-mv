#!/bin/sh
# Build the test-only C2 receipt issuer/verifier against the existing receipt code.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build
cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Isrc/checker2 \
   -o build/receipt-tool tests/max-interop/receipt_tool.c \
   src/checker2/c2_receipt.c src/sha256.c \
   src/searchtrace/sig/ed25519.c src/searchtrace/sig/sha512.c src/searchtrace/sig/ct.c
file build/receipt-tool
