#!/usr/bin/env bash
# Native build only. Run via quiet-build. Never falls back to a C-born compiler.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
source tools/retirement/pins.sh
B=$ROOT/build/retirement
E=$ROOT/evidence/seed-retirement
OMC=$B/seed-src/seed/out/omc
mkdir -p "$B" "$E"
printf '%s  %s\n' "$SEED_SHA" "$OMC" | sha256sum -c - > "$E/native-build.log"
t0=$(date +%s%N)
"$OMC" < seed/retirement/trn1/trn1.om > "$B/trn1.new" 2> "$B/compile.err"
test -s "$B/trn1.new"
readelf -h "$B/trn1.new" | grep -q 'Machine:.*AArch64'
readelf -l -d "$B/trn1.new" > "$E/native-elf.log"
if grep -Eq 'INTERP|NEEDED' "$E/native-elf.log"; then echo 'unexpected native dependency' >&2; exit 1; fi
chmod +x "$B/trn1.new"
mv "$B/trn1.new" "$B/trn1"
printf 'compile_rc=0 bytes=%s compile_ns=%s\n' "$(wc -c < "$B/trn1")" "$(( $(date +%s%N) - t0 ))" >> "$E/native-build.log"
sha256sum seed/retirement/trn1/trn1.om "$B/trn1" "$OMC" >> "$E/native-build.log"
printf 'BUILD PASS\n'
