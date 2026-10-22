#!/bin/bash
# evidence_moves.sh -- check that every evidence file a change deletes was recorded as moved to the
# evidence shelf (L396, ADR 0039). Reviewed evidence is never rewritten; the one allowed deletion is a
# move to aien-dev/omega-evidence, pinned by evidence.lock.
#
# A move is recorded in evidence/SHELF/moved-*.tsv (added by the change, so additions-only still holds):
# one line per file, "<path><TAB><blob id it had on the base branch>". This script needs only the two
# commits, no shelf checkout, so the hosted check can run it; the Spark gate also runs
# tools/modules/pin.sh moved, which proves the shelf commit holds those exact bytes.
#
# Usage: tools/modules/evidence_moves.sh <base> <head>
# Prints one line per deletion that is not recorded with the right bytes, then a summary.
# Exit: 0 every deletion recorded (or none); 1 at least one is not.
set -euo pipefail

base=${1:?usage: evidence_moves.sh <base> <head>}
head=${2:?usage: evidence_moves.sh <base> <head>}
mb=$(git merge-base "$base" "$head")

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git diff --no-renames --name-only --diff-filter=D "$mb" "$head" -- evidence/ > "$tmp/deleted"
git ls-tree -r "$mb" -- evidence/ | awk -F'\t' '{ split($1, a, " "); print $2 "\t" a[3] }' > "$tmp/base"
git ls-tree -r --name-only "$head" -- evidence/SHELF/ | grep -E '/moved-[^/]*\.tsv$' |
    while read -r f; do git cat-file blob "$head:$f"; done > "$tmp/moved"

awk -F'\t' -v movedf="$tmp/moved" -v basef="$tmp/base" '
BEGIN {
    while ((getline l < movedf) > 0) { split(l, a, "\t"); if (a[1] != "" && a[1] !~ /^#/) moved[a[1]] = a[2] }
    while ((getline l < basef) > 0) { split(l, a, "\t"); blob[a[1]] = a[2] }
}
{
    n++
    if (!($0 in moved)) { print "D\t" $0 "\t(not recorded in evidence/SHELF/moved-*.tsv)"; bad++ }
    else if (moved[$0] != blob[$0]) { print "D\t" $0 "\t(recorded with other bytes than the base branch had)"; bad++ }
}
END {
    printf "evidence_moves: %d file(s) deleted from evidence/, %d recorded as moved to the shelf\n", n, n - bad
    exit (bad > 0)
}' "$tmp/deleted"
