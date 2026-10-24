#!/bin/sh
# Compare the original native parent with the extended native compiler. No foreign compiler.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/seed/host
stage=$(mktemp -d build/seed/host/regression.XXXXXX)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
# Struct, break, trap, f32 and f64 are omc-only features (OMEGESE 9.7 to 9.9): om4 refuses them by
# design, and gate6, gate7 and gate8 own those semantics. The strict byte-equality corpus leaves out
# those fixture directories and keeps everything else under the full comparison.
: > "$stage/inputs"
git ls-files seed/rung4/tests seed/omc/tests tests/one_layer/omegese tools/exp445/arms/examples \
    | grep -vE '^seed/omc/tests/(struct|loop|float)/' \
    | sed -n '/\.om$/p; /\.omegese$/p' > "$stage/all"
while IFS= read -r f; do
    [ -f "$f" ] || continue
    grep -qE '^[[:space:]]*(break|trap|struct)([[:space:]]|$)' "$f" && continue
    printf '%s\n' "$f" >> "$stage/inputs"
done < "$stage/all"
count=0
while IFS= read -r source; do
    old=0
    new=0
    seed/rung4/om4.bin < "$source" > "$stage/old" 2> "$stage/old.err" || old=$?
    seed/out/omc < "$source" > "$stage/new" 2> "$stage/new.err" || new=$?
    test "$old" = "$new" || { echo "Changed compiler status: $source" >&2; exit 1; }
    cmp "$stage/old" "$stage/new"
    sed 's/^om4:/compiler:/' "$stage/old.err" > "$stage/old.normal"
    sed 's/^omc:/compiler:/' "$stage/new.err" > "$stage/new.normal"
    cmp "$stage/old.normal" "$stage/new.normal"
    count=$((count + 1))
done < "$stage/inputs"
printf 'PASS %s original fixtures retain identical machine code, refusal status and diagnostics\n' "$count"
