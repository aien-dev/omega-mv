#!/bin/sh
# gate20.sh -- OM-2 signed div / rem by a constant 2^k as shifts (omc.om: sdiv2k, sdiv2kt). The compiler
# under test is seed/out/omc, already built and proved self-identical by seed/omega-from-seed.sh.
# d1 hashes q and r of long, int, short and byte division by every 2^k the width allows, over 4096
# xorshift values plus each width's boundaries; its expected stdout (tests/sdiv/d1.out) was produced by
# omc 0d006207, which uses SDIV, so SDIV is the oracle. Under seed/out/omc d1 must print d1.out with no
# SDIV left in its code; m0 (the hook disabled) must print the same with SDIV back, so the shifts are
# what is being tested. u1: unsigned div / rem by 2^k with the top bit set stay unsigned (exit 7).
# Five mutants of omc.om, each built by seed/out/omc, must be caught: ma (LSR instead of ASR for the
# quotient), mb (rem mask one bit short), mc (bias from ASR #0 instead of #63), md (k 1 bias shift #0)
# by d1, and me (the hook taken for unsigned types) by u1.
# RC=0 only when every case passed and every mutant was caught.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r20
T=seed/omc/tests/sdiv
S=seed/out/omc
SRC=seed/omc/omc.om
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }
command -v objdump > /dev/null || bad "objdump (binutils) is required to count SDIV"

# build compiler case tag: compile case with compiler into $B/tag-case.bin
build() {
    set +e
    "$1" < "$T/$2.om" > "$B/$3-$2.bin" 2> "$B/$3-$2.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] || bad "$2 under $3: compiler exit $crc ($(cat "$B/$3-$2.err"))"
    chmod +x "$B/$3-$2.bin"
}
# runout compiler case tag: stdout of the run, then a line rc=<exit>
runout() {
    build "$1" "$2" "$3"
    set +e
    "$B/$3-$2.bin" 2> /dev/null
    rrc=$?
    set -e
    echo "rc=$rrc"
}
nsdiv() { objdump -D -b binary -m aarch64 "$1" | awk -F'\t' '$3 == "sdiv"' | wc -l; }

printf '%s\nrc=0\n' "$(cat "$T/d1.out")" > "$B/d1.want"
runout "$S" d1 omc > "$B/omc-d1.out"
cmp -s "$B/omc-d1.out" "$B/d1.want" || bad "d1: output differs from the SDIV oracle (see $B/omc-d1.out)"
k=$(nsdiv "$B/omc-d1.bin")
[ "$k" = 0 ] || bad "d1: $k SDIV left in the code"
say "PASS d1 matches the SDIV oracle with no SDIV"
u=$(runout "$S" u1 omc | tail -n 1)
[ "$u" = rc=7 ] || bad "u1: ran $u, wanted rc=7"
say "PASS u1 exit 7"

# mutant name anchor replacement: replace the one line equal to anchor, build it with seed/out/omc,
# and refuse a mutant whose anchor is missing or not unique.
mutant() {
    k=$(grep -c -x -F -- "$2" "$SRC" || true)
    [ "$k" = 1 ] || bad "mutant $1: anchor '$2' found $k times in $SRC"
    awk -v a="$2" -v r="$3" '$0 == a { print r; next } { print }' "$SRC" > "$B/$1.om"
    set +e
    "$S" < "$B/$1.om" > "$B/$1.omc" 2> "$B/$1.err"
    mrc=$?
    set -e
    [ "$mrc" = 0 ] || bad "mutant $1 did not build: exit $mrc ($(cat "$B/$1.err"))"
    chmod +x "$B/$1.omc"
}

mutant m0 'if ne sdiv2kt op ty ra bk bn dst 0' 'if eq bk 987654321'
runout "$B/m0.omc" d1 m0 > "$B/m0-d1.out"
cmp -s "$B/m0-d1.out" "$B/d1.want" || bad "m0: d1 with SDIV differs from d1.out"
k=$(nsdiv "$B/m0-d1.bin")
[ "$k" -gt 0 ] || bad "m0: no SDIV with the hook disabled, so d1 does not reach it"
say "PASS m0 (hook disabled) matches d1.out with $k SDIV"

n=0
for m in \
    'ma|emit or or or 2470509568 rd shl t 5 shl k 16|emit or or or 3544251392 rd shl t 5 shl k 16|d1' \
    'mb|emit or or w shl sub 64 k 16 shl sub 63 k 10|emit or or w shl sub 64 k 16 shl sub 62 k 10|d1' \
    'mc|emit or or 2474638336 t shl ra 5|emit or or 2470509568 t shl ra 5|d1' \
    'md|emit or or or 2336291840 t shl ra 5 shl ra 16|emit or or or 2336227328 t shl ra 5 shl ra 16|d1' \
    'me|if or or ne bk 3 lt ty 6 gt ty 9|if or or ne bk 3 lt ty 2 gt ty 9|u1'; do
    name=${m%%|*} r=${m#*|}
    a=${r%%|*} r=${r#*|}
    b=${r%%|*} c=${r#*|}
    mutant "$name" "$a" "$b"
    runout "$B/$name.omc" "$c" "$name" > "$B/$name-$c.out"
    if [ "$c" = d1 ]; then
        ! cmp -s "$B/$name-$c.out" "$B/d1.want" || bad "mutant $name not caught: d1 still matches"
    else
        [ "$(tail -n 1 "$B/$name-$c.out")" != rc=7 ] || bad "mutant $name not caught: u1 still exits 7"
    fi
    n=$((n+1))
    say "PASS mutant $name caught by $c ($(tail -n 1 "$B/$name-$c.out"))"
done

say "PASS gate20 d1, u1, m0 control, $n mutants caught"
exit 0
