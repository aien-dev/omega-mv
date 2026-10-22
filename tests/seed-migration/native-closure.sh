#!/bin/sh
# Host orchestration only; ELF parsing runs in the seed-built Omega executable.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
CC=seed/out/omc
EXPECTED=01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6
ACTUAL=$(sha256sum "$CC")
[ "${ACTUAL%% *}" = "$EXPECTED" ] || { printf '%s\n' 'REFUSED: compiler identity' >&2; exit 2; }
# A private build directory prevents overlapping checks from sharing output files.
mkdir -p "$ROOT/build/manual-migration"
D=$(mktemp -d "$ROOT/build/manual-migration/closure.XXXXXXXX")
trap 'rm -f "$D/check" "$D/literal"; rmdir "$D"' EXIT HUP INT TERM
"$CC" < seed/native/elfcheck.om > "$D/check"
[ -s "$D/check" ] || exit 2
chmod 700 "$D/check"
"$D/check" < "$D/check"
"$D/check" < "$CC"
"$CC" < seed/native/literal.om > "$D/literal"
[ -s "$D/literal" ] || exit 2
"$D/check" < "$D/literal"
printf '%s\n' 'PASS native ELF check: checker, compiler, literal' 'Scope: ELF shape/no dynamic segments; static source ancestry checked separately.'
