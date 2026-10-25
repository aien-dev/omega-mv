#!/bin/sh
# Regenerate src/omega_om_units.inc from om/sha256/sha256.om, or with --check confirm the committed
# file is exactly what the pinned tools produce (exit 1 on any difference, 3 NOT_RUN without tools).
#   OMEGA_OMC     seed-born omc, sha256 pinned below
#   OMEGA_BRIDGE  an aien-dev/omega-bridge checkout; src/native.om sha256 pinned below
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
NATIVE_PIN=e79733f88680a0310b3b23fba271fc2e394abc8755bc512f1ffd69e097e4f80f
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/src/omega_om_units.inc
[ -n "${OMEGA_OMC:-}" ] && [ -n "${OMEGA_BRIDGE:-}" ] || { echo "NOT_RUN: set OMEGA_OMC and OMEGA_BRIDGE"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "NOT_RUN: omc is not the pinned compiler"; exit 3; }
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "NOT_RUN: native.om is not the pinned generator"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
"$OMEGA_OMC" < "$ROOT/om/sha256/sha256.om" > "$T/omsha.bin"
{ cat "$ROOT/om/sha256/sha256.om"; printf '\0'; printf '%08d' "$(wc -c < "$T/omsha.bin")"; cat "$T/omsha.bin"; } \
  | "$T/native" > "$T/units.inc"
if [ "${1:-}" = --check ]; then
  if cmp -s "$T/units.inc" "$OUT"; then echo "PASS src/omega_om_units.inc regenerates byte for byte"; exit 0; fi
  echo "FAIL src/omega_om_units.inc differs from the pinned tools' output"; exit 1
fi
cp "$T/units.inc" "$OUT"; echo "wrote $OUT"
