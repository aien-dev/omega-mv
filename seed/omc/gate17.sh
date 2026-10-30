#!/bin/sh
# gate17.sh -- differential fuzz against the pinned reference compiler. The reference is built from the
# in-tree pinned source (seed/omc/pinned.om, the R5 compiler) with the seed-owned om4, and its digest is
# asserted. 300 deterministic structured programs (old subset: scalars, arrays, if/while/for, arithmetic)
# are compiled by both compilers; each must be accepted and produce byte-identical machine code.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
O=seed/out/omc
[ -x "$O" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$O" >&2; exit 1; }
P=seed/rung4/om4.bin
[ -x "$P" ] || { printf 'FAIL: %s missing\n' "$P" >&2; exit 1; }
mkdir -p build/seed/r17
T=$(mktemp -d build/seed/r17/diff.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

"$P" < seed/omc/pinned.om > "$T/pinned"
chmod +x "$T/pinned"
sha=$(sha256sum "$T/pinned" | cut -d' ' -f1)
[ "$sha" = e5d60db892c8f21cc8c9128af26d8bded0e96d7f92946f5a549397c43a872649 ] || \
    bad "pinned compiler digest $sha, wanted e5d60db8..."
say "PASS pinned reference compiler e5d60db8..."

awk -v dir="$T" 'BEGIN{
    M = 65536;
    for (p = 0; p < 300; p++) {
        seed = 777 + p * 13;
        f = sprintf("%s/p%03d.om", dir, p);
        print "fn main to int" > f;
        print "a = new ulong 4 0" > f;
        print "x = ulong 1" > f;
        print "y = ulong 2" > f;
        depth = 0;
        for (l = 0; l < 24; l++) {
            seed = (seed * 25173 + 13849) % M;
            r = seed / M;
            if (r < 0.60 || depth == 0) {
                seed = (seed * 25173 + 13849) % M;
                k = seed % 8;
                if (k == 0) print "x = add x 1" > f;
                else if (k == 1) print "y = mul y 2" > f;
                else if (k == 2) print "x = add x y" > f;
                else if (k == 3) print "put a 0 x" > f;
                else if (k == 4) print "x = get a 1" > f;
                else if (k == 5) print "x = sub x 1" > f;
                else if (k == 6) print "y = xor y 1" > f;
                else print "x = rem x 7" > f;
            } else if (r < 0.80 && depth < 4) {
                print "if lt x 100" > f; depth++;
            } else if (r < 0.90 && depth < 4) {
                print "while lt x 100 bound 1000" > f; depth++;
            } else if (r < 0.95 && depth < 4) {
                print "for i ulong 4" > f; depth++;
            } else if (depth > 0) {
                print "end" > f; depth--;
            }
        }
        while (depth > 0) { print "end" > f; depth--; }
        print "ret int x" > f;
        print "end" > f;
        close(f);
    }
}'

n=0
for f in "$T"/p*.om; do
    n=$((n + 1))
    set +e
    "$O" < "$f" > "$T/a.bin" 2> "$T/a.err"
    ra=$?
    "$T/pinned" < "$f" > "$T/p.bin" 2> "$T/p.err"
    rp=$?
    set -e
    [ "$ra" = 0 ] || bad "$f: current compiler exit $ra ($(cat "$T/a.err"))"
    [ "$rp" = 0 ] || bad "$f: pinned compiler exit $rp ($(cat "$T/p.err"))"
    [ -s "$T/a.err" ] && bad "$f: current compiler wrote a diagnostic"
    [ -s "$T/p.err" ] && bad "$f: pinned compiler wrote a diagnostic"
    cmp -s "$T/a.bin" "$T/p.bin" || bad "$f: machine code differs from the pinned compiler"
done
[ "$n" = 300 ] || bad "ran $n of 300 programs"
say "PASS gate17 300 programs byte-identical to the pinned compiler"
exit 0
