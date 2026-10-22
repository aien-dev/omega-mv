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
trap 'rm -f "$D/check" "$D/literal" "$D/equal" "$D/hash" "$D/copy" "$D/copied" "$D/stage1" "$D/stage2" "$D/stage3"; rmdir "$D"' EXIT HUP INT TERM
"$CC" < seed/native/elfcheck.om > "$D/check"
[ -s "$D/check" ] || exit 2
chmod 700 "$D/check"
"$D/check" < "$D/check"
"$D/check" < "$CC"
"$CC" < seed/native/literal.om > "$D/literal"
[ -s "$D/literal" ] || exit 2
"$D/check" < "$D/literal"
"$CC" < seed/native/byteequal.om > "$D/equal"
[ -s "$D/equal" ] || exit 2
"$D/check" < "$D/equal"
chmod 700 "$D/equal"
"$CC" < seed/omc/omc.om > "$D/stage1"
"$D/check" < "$D/stage1"
chmod 700 "$D/stage1"
"$D/stage1" < seed/omc/omc.om > "$D/stage2"
"$D/check" < "$D/stage2"
chmod 700 "$D/stage2"
"$D/stage2" < seed/omc/omc.om > "$D/stage3"
"$D/check" < "$D/stage3"
"$D/equal" 3< "$CC" 4< "$D/stage1"
"$D/equal" 3< "$D/stage1" 4< "$D/stage2"
"$D/equal" 3< "$D/stage2" 4< "$D/stage3"
"$CC" < seed/native/sha256.om > "$D/hash"
[ -s "$D/hash" ] || exit 2
"$D/check" < "$D/hash"
chmod 700 "$D/hash"
[ "$("$D/hash" < "$CC")" = "$EXPECTED" ] || { printf '%s\n' 'REFUSED: native hash mismatch' >&2; exit 2; }
"$CC" < seed/native/streamcopy.om > "$D/copy"
[ -s "$D/copy" ] || exit 2
"$D/check" < "$D/copy"
chmod 700 "$D/copy"
"$D/copy" < "$D/stage3" > "$D/copied"
"$D/equal" 3< "$D/stage3" 4< "$D/copied" || { printf '%s\n' 'REFUSED: copied compiler differs' >&2; exit 2; }
"$D/check" < "$D/copied"
for name in check literal equal hash copy copied stage1 stage2 stage3; do
  digest=$("$D/hash" < "$D/$name")
  printf 'OMEGA_SHA256 %s %s\n' "$digest" "$name"
done
for name in elfcheck literal byteequal sha256 streamcopy; do
  digest=$("$D/hash" < "seed/native/$name.om")
  printf 'OMEGA_SHA256 %s seed/native/%s.om\n' "$digest" "$name"
done
printf '%s\n' 'PASS native hashing: compiler identity cross-check and source/artifact fingerprints'
printf '%s\n' 'PASS native ELF check: checker, compiler, literal, comparator, hasher, copier, copied compiler, stages 1/2/3' 'PASS Omega binary comparison: pinned compiler = stage1 = stage2 = stage3' 'Scope: native self-host comparison; seed ancestry and static dependencies remain separate.'
