#!/bin/sh
# Parity of the Omega SHA-256 (om/sha256/sha256-om2.om, generated into src/omega_om_units.c and called
# through src/sha256.h -> src/omega_api_decl.h) against the C it replaced: src/sha256.c at a647b6a9 (sha256
# a8fb88611e68ea5a876ee1658e1665994a03bfbed215983618d769f0e7205473), retrieved from git history with
# its own header and compiled only here, as an oracle outside the delivered closure.
# 2000 inputs (fixed boundary lengths, 0..299 bytes, up to 2 MiB) hashed one-shot and in random
# update splits, every length 0..9000 one-shot and split, the FIPS 180-2 vectors (empty, abc, 448-bit,
# one million 'a'), and the section 7 / erratum E3 receipts (receipts.c).
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
ORACLE=a647b6a9821fffcf52ba9ce813f75afa4b84be62
git -C "$ROOT" show "$ORACLE:src/sha256.c" > "$T/c.c"
[ "$(sha256sum < "$T/c.c" | cut -c1-64)" = a8fb88611e68ea5a876ee1658e1665994a03bfbed215983618d769f0e7205473 ]
git -C "$ROOT" show "$ORACLE:src/sha256.h" > "$T/sha256.h"
sed 's/sha256_\(init\|update\|final\|hash\|transform\)/orc_sha256_\1/g; s/^static const uint32_t K\b/static const uint32_t ORC_K/; s/\bK\[/ORC_K[/g' "$T/c.c" > "$T/oracle.c"
CF="-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2"
cc $CF -c "$ROOT/src/omega_om_units.c" -o "$T/new.o"
cc -std=gnu11 -O2 -c "$T/oracle.c" -o "$T/oracle.o"   # finds the historical header beside it
for p in par every receipts; do cc $CF -I"$ROOT/src" "$(dirname "$0")/$p.c" "$T/new.o" "$T/oracle.o" -o "$T/$p" -pthread; done
# every length 0..9000: one shot and an update split
"$T/every" | tee "$T/every.out"
[ "$(cat "$T/every.out")" = "lengths 0..9000: diff=0" ] || { echo "SHA256-PARITY FAIL"; exit 1; }
"$T/receipts" receipts | tee "$T/receipts.out" || { echo "SHA256-PARITY FAIL (receipts)"; exit 1; }
"$T/par" > "$T/out"
cat "$T/out"
printf '%s\n' e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 \
  ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad \
  248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 \
  cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 "cases=2000 diff=0" | cmp -s - "$T/out" \
  && echo "SHA256-PARITY PASS" || { echo "SHA256-PARITY FAIL"; exit 1; }
