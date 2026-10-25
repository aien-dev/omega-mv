#!/bin/sh
# Parity of the Omega SHA-256 (src/sha256.c adapter + src/omega_om_units.inc) against the C it
# replaced: src/sha256.c at a647b6a9 (sha256 a8fb88611e68ea5a876ee1658e1665994a03bfbed215983618d769f0e7205473).
# 2000 inputs (fixed boundary lengths, 0..299 bytes, up to 2 MiB) hashed one-shot and in random
# update splits, plus the FIPS 180-2 vectors (empty, abc, 448-bit, one million 'a').
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
ORACLE=a647b6a9821fffcf52ba9ce813f75afa4b84be62
git -C "$ROOT" show "$ORACLE:src/sha256.c" > "$T/c.c"
[ "$(sha256sum < "$T/c.c" | cut -c1-64)" = a8fb88611e68ea5a876ee1658e1665994a03bfbed215983618d769f0e7205473 ]
sed 's/sha256_\(init\|update\|final\|hash\|transform\)/orc_sha256_\1/g; s/^static const uint32_t K\b/static const uint32_t ORC_K/; s/\bK\[/ORC_K[/g' "$T/c.c" > "$T/oracle.c"
cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -I"$ROOT/src" -c "$ROOT/src/sha256.c" -o "$T/new.o"
cc -std=gnu11 -O2 -I"$ROOT/src" -c "$T/oracle.c" -o "$T/oracle.o"
cc -O2 -I"$ROOT/src" "$(dirname "$0")/par.c" "$T/new.o" "$T/oracle.o" -o "$T/par" -pthread
"$T/par" > "$T/out"
cat "$T/out"
printf '%s\n' e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 \
  ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad \
  248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 \
  cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 "cases=2000 diff=0" | cmp -s - "$T/out" \
  && echo "SHA256-PARITY PASS" || { echo "SHA256-PARITY FAIL"; exit 1; }
