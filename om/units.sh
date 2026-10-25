#!/bin/sh
# Regenerate src/omega_om_units.inc from every Omega unit linked into the C build (UNITS below), or
# with --check confirm the committed
# file is exactly what the pinned tools produce (exit 1 on any difference, 3 NOT_RUN without tools).
#   OMEGA_OMC     seed-born omc, sha256 pinned below
#   OMEGA_BRIDGE  an aien-dev/omega-bridge checkout; src/native.om sha256 pinned below
set -eu
OMC_PIN=3134244727189883d317e6813b93d2f55b24f2ca5e236daf6d39ef48412f049f
NATIVE_PIN=2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# One binding file for all units: each generated binding defines the shared ABI runtime (ombr_enter),
# so a second file in another translation unit would collide at link time.
UNITS="om/sha256/sha256.om om/blake3/b3unit.om"
OUT=$ROOT/src/omega_om_units.inc
[ -n "${OMEGA_OMC:-}" ] && [ -n "${OMEGA_BRIDGE:-}" ] || { echo "NOT_RUN: set OMEGA_OMC and OMEGA_BRIDGE"; exit 3; }
[ "$(sha256sum < "$OMEGA_OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "NOT_RUN: omc is not the pinned compiler"; exit 3; }
[ "$(sha256sum < "$OMEGA_BRIDGE/src/native.om" | cut -c1-64)" = "$NATIVE_PIN" ] || { echo "NOT_RUN: native.om is not the pinned generator"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"$OMEGA_OMC" < "$OMEGA_BRIDGE/src/native.om" > "$T/native"; chmod +x "$T/native"
for u in $UNITS; do
  "$OMEGA_OMC" < "$ROOT/$u" > "$T/u.bin"
  cat "$ROOT/$u"; printf '\0'; printf '%08d' "$(wc -c < "$T/u.bin")"; cat "$T/u.bin"
done | "$T/native" > "$T/units.inc"
if [ "${1:-}" = --check ]; then
  if cmp -s "$T/units.inc" "$OUT"; then echo "PASS src/omega_om_units.inc regenerates byte for byte"; exit 0; fi
  echo "FAIL src/omega_om_units.inc differs from the pinned tools' output"; exit 1
fi
cp "$T/units.inc" "$OUT"; echo "wrote $OUT"
