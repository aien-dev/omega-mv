#!/bin/sh
# gate18.sh -- keyword generator parity. genkw.awk is a text generator (not on the seed path), but it
# must reproduce the committed keyword chain: every literal pack/id pair, plus the two computed entries
# readlnk (133) and sockopt (134). This catches list drift and awk precision regressions (packs above
# 2^53 are printed with decimal string arithmetic, not doubles).
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
mkdir -p build/seed/r18
T=$(mktemp -d build/seed/r18/kw.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

awk -f seed/omc/genkw.awk > "$T/gen"

# The committed chain, scoped to fn kw: stop at its final `ret 0` fallback.
awk '
    /^fn kw /{inkw=1; next}
    inkw && /^if eq w /{p=$4; next}
    inkw && /^ret /{
        if ($2 == 0) { inkw=0; exit }
        if ($2 != 133 && $2 != 134) print $2, p
    }
' seed/omc/omc.om | sort -n > "$T/chain.pairs"

awk '
    /^if eq w /{p=$4}
    /^ret /{ if ($2 != 133 && $2 != 134) print $2, p }
' "$T/gen" | sort -n > "$T/gen.pairs"

cmp -s "$T/chain.pairs" "$T/gen.pairs" || {
    diff -u "$T/chain.pairs" "$T/gen.pairs" | sed -n '1,20p' >&2
    bad "generated keyword packs differ from the committed chain"
}
n=$(wc -l < "$T/gen.pairs")
[ "$n" = 157 ] || bad "compared $n literal pairs, wanted 157"

grep -q '^ret 133$' seed/omc/omc.om || bad "committed chain lacks readlnk (133)"
grep -q '^ret 134$' seed/omc/omc.om || bad "committed chain lacks sockopt (134)"
grep -q 'readlnk:133' seed/omc/genkw.awk || bad "generator lacks readlnk:133"
grep -q 'sockopt:134' seed/omc/genkw.awk || bad "generator lacks sockopt:134"

printf 'PASS gate18 157 literal packs and both computed entries match the committed chain\n'
exit 0
