#!/bin/sh
# Compare the original native parent with the extended native compiler. No foreign compiler.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/seed/host
stage=$(mktemp -d build/seed/host/regression.XXXXXX)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
# Fixed baseline selected at ec07d25d5506d33567adbd4d9b518177b7933bad.
# Adding/removing a case or changing its expected status requires explicit review;
# content filters must never silently remove the original comparison obligation.
manifest=seed/host/original-fixtures.tsv
bad() { printf 'FAIL original fixtures: %s\n' "$*" >&2; exit 1; }
git ls-files --error-unmatch -- "$manifest" > /dev/null || bad "untracked manifest"
awk -F '\t' '
    NF != 2 || $1 !~ /^[A-Za-z0-9_./-]+$/ || $2 !~ /^(0|5|7|9)$/ { exit 1 }
    seen[$1]++ { exit 1 }
    END { if (NR != 62) exit 1 }
' "$manifest" || bad "invalid, duplicate or incomplete baseline (expected 62 cases)"
count=0
while IFS="$(printf '\t')" read -r source expected; do
    [ -f "$source" ] || bad "missing baseline: $source"
    git ls-files --error-unmatch -- "$source" > /dev/null || bad "untracked baseline: $source"
    old=0
    new=0
    seed/rung4/om4.bin < "$source" > "$stage/old" 2> "$stage/old.err" || old=$?
    seed/out/omc < "$source" > "$stage/new" 2> "$stage/new.err" || new=$?
    test "$old" = "$expected" || bad "$source: parent status $old, expected $expected"
    test "$old" = "$new" || { echo "Changed compiler status: $source" >&2; exit 1; }
    cmp "$stage/old" "$stage/new"
    sed 's/^om4:/compiler:/' "$stage/old.err" > "$stage/old.normal"
    sed 's/^omc:/compiler:/' "$stage/new.err" > "$stage/new.normal"
    cmp "$stage/old.normal" "$stage/new.normal"
    count=$((count + 1))
done < "$manifest"
test "$count" -eq 62 || bad "incomplete read: $count of 62 cases"
printf 'PASS %s original fixtures retain identical machine code, refusal status and diagnostics\n' "$count"
