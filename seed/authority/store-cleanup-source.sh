#!/bin/sh
# Source-only composition; does not compile or run Omega code.
# Optional store source permits the unchanged pre-fix negative control.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source=${1:-"$here/store.om"}
tmp=$(mktemp "${TMPDIR:-/tmp}/store-cleanup-source.XXXXXX")
trap 'rm -f "$tmp"' 0
trap 'exit 1' HUP INT TERM
# Portable awk token matching, not GNU sed word boundaries. Fail closed if any
# write-side effect has drifted from the reviewed shape. Global buffer arguments
# are omitted because ordinary Omega functions cannot receive global arrays.
# Buffer contents are not tested; scalar arguments/control flow stay unchanged.
# Buffer all output until both source transformation and harness read succeed.
awk '
function refuse() {
    print "store cleanup composer: unmatched effect at line " NR > "/dev/stderr"
    exit 1
}
$1 == "note" { print; next }
{
    changed = 0
    for (i = 1; i <= NF; i++) {
        if ($i == "fopen") {
            if ($(i+1) != "spath") refuse()
            $i = "cfopen"; $(i+1) = ""; changed = 1
        } else if ($i == "write") {
            if ($(i+1) != "int" || $(i+2) != "fd" || $(i+3) != "sb") refuse()
            $i = "cwrite"; $(i+3) = ""; changed = 1
        } else if ($i == "fsync" || $i == "fclose") {
            if ($(i+1) != "int" || $(i+2) != "fd") refuse()
            $i = "c" $i; changed = 1
        } else if ($i == "renames") {
            if ($(i+1) != "spkt") refuse()
            $i = "crenames"; $(i+1) = ""; changed = 1
        }
    }
    if (!changed) { print; next }
    line = ""
    for (i = 1; i <= NF; i++) {
        if ($i != "") line = line (line == "" ? "" : " ") $i
    }
    print line
}' "$source" > "$tmp"
cat "$here/store-cleanup-test.om" >> "$tmp"
cat "$tmp"
