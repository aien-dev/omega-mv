#!/bin/sh
# gate19.sh -- OM-2 implied index checks and loop-constant hoisting (omc.om: nm_bk, ixbound, hzopen,
# hzrem). The compiler under test is seed/out/omc, already built and proved self-identical by
# seed/omega-from-seed.sh. Cases: an index range implied by a constant-count `for` drops its check (p1)
# and every index whose range is not implied keeps its check and traps (n1..n5, exit 103); hoisted
# constants are reloaded after a call whose loop hoists into the same registers (h1); with 8 hoistable
# constants a program-mode argcnt still reads argv through x27 (a8: hoisting stops at x26, hzcap 7); a
# for inside an untaken if leaves no hoist active after the if (b1). Five mutants of omc.om, each built
# by seed/out/omc, must be caught by a case: m1 accepts `sub C i` for any C (n5), m2 ignores an
# assignment to the counter (n3), m3 drops the reload after a BL (h1), m4 drops the block close's
# restore of hz_n (b1), m5 restores the old cap of 8, which hoists into x27 (a8). m0 never sets the
# implied range, so p1 must grow under it: the elimination is real, not only harmless.
# Function mode: the exported unit f1 must write all of x20..x26 and none of x19, x27, x28, x29; no
# program here writes x19 or x29 (objdump decodes the destinations). Differential fuzz: 64 random
# programs of nested loops, ifs and calls (tests/hoist/fuzz.awk) exit the same under seed/out/omc as
# under mz, the same compiler with hoisting and implied ranges both disabled.
# RC=0 only when every case passed and every mutant was caught.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r19
T=seed/omc/tests/hoist
S=seed/out/omc
SRC=seed/omc/omc.om
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# runc compiler case: prints the run exit of case compiled by compiler
runc() {
    set +e
    "$1" < "$T/$2.om" > "$B/$3-$2.bin" 2> "$B/$3-$2.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] || bad "$2 under $3: compiler exit $crc ($(cat "$B/$3-$2.err"))"
    chmod +x "$B/$3-$2.bin"
    set +e
    "$B/$3-$2.bin" > /dev/null 2>&1
    rrc=$?
    set -e
    echo "$rrc"
}

n=0
for c in p1:120 h1:168 n1:103 n2:103 n3:103 n4:103 n5:103 a8:1 b1:1; do
    name=${c%%:*} want=${c#*:}
    got=$(runc "$S" "$name" omc)
    [ "$got" = "$want" ] || bad "$name: ran $got, wanted $want"
    n=$((n+1))
    say "PASS $name exit $got"
done

# mutant name anchor replacement: replace (or with an empty replacement, delete) the one line equal to
# anchor, build it with seed/out/omc, and refuse a mutant whose anchor is missing or not unique.
mutant() {
    k=$(grep -c -x -F -- "$2" "$SRC" || true)
    [ "$k" = 1 ] || bad "mutant $1: anchor '$2' found $k times in $SRC"
    awk -v a="$2" -v r="$3" '$0 == a { if (r != "") print r; next } { print }' "$SRC" > "$B/$1.om"
    set +e
    "$S" < "$B/$1.om" > "$B/$1.omc" 2> "$B/$1.err"
    mrc=$?
    set -e
    [ "$mrc" = 0 ] || bad "mutant $1 did not build: exit $mrc ($(cat "$B/$1.err"))"
    chmod +x "$B/$1.omc"
}
caught() { # mutant case wanted
    got=$(runc "$B/$1.omc" "$2" "$1")
    [ "$got" != "$3" ] || bad "mutant $1 not caught: $2 still exits $3"
    say "PASS mutant $1 caught by $2 (exit $got, wanted $3)"
}
mutant m1 'if lt c sub k 1' 'if eq c 987654321'
caught m1 n5 103
mutant m2 'if eq fwscan w 0' 'if eq w w'
caught m2 n3 103
mutant m3 'hzrem' ''
caught m3 h1 168
mutant m4 'put hz_n 0 get bhz i' ''
caught m4 b1 1
mutant m5 'if lt n hzcap' 'if lt n 8'
caught m5 a8 1

mutant m0 'if eq fwscan w 0' 'if ne w w'
runc "$B/m0.omc" p1 m0 > /dev/null
a=$(wc -c < "$B/omc-p1.bin") z=$(wc -c < "$B/m0-p1.bin")
[ "$a" -lt "$z" ] || bad "p1 is $a bytes with the implied range and $z without: no check was dropped"
say "PASS p1 drops checks: $a bytes, $z bytes with no implied range"

# dests bin: the destination registers of every instruction objdump decodes in bin (stores, compares,
# branches and undecodable words have none; a load pair has two)
command -v objdump > /dev/null || bad "objdump (binutils) is required for the register scan"
dests() {
    objdump -D -b binary -m aarch64 "$1" | awk -F'\t' 'NF >= 4 {
        m = $3; sub(/ +$/, "", m)
        if (m ~ /^(st[a-z0-9]*|cmp|cmn|tst|fcmp|fcmpe|ccmp|ccmn|b|b\..*|bl|br|blr|cbz|cbnz|tbz|tbnz|ret|svc|nop|udf|prfm|\.inst)$/) next
        n = split($4, a, ", "); print a[1]; if (m ~ /^ldp/ && n > 1) print a[2] }'
}
"$S" < "$T/f1.om" > "$B/f1.unit" 2> "$B/f1.err" || bad "f1: compiler exit ($(cat "$B/f1.err"))"
dests "$B/f1.unit" > "$B/f1.dests"
for x in 20 21 22 23 24 25 26; do grep -q -x "x$x" "$B/f1.dests" || bad "f1: x$x never written"; done
! grep -q -x -E '[xw](19|27|28|29)' "$B/f1.dests" || bad "f1: writes $(grep -x -E '[xw](19|27|28|29)' "$B/f1.dests" | sort -u | tr '\n' ' ')"
say "PASS f1 (function mode) writes x20..x26 and none of x19, x27, x28, x29"
mutant m6 'ret add j 20' 'ret add j 19'
"$B/m6.omc" < "$T/f1.om" > "$B/m6-f1.unit" 2> "$B/m6-f1.err" || bad "f1 under m6: compiler exit ($(cat "$B/m6-f1.err"))"
dests "$B/m6-f1.unit" | grep -q -x x19 || bad "mutant m6 not caught: hoisting from x19 writes no x19 the scan sees"
say "PASS mutant m6 (hoist from x19) caught by the f1 register scan"
for f in "$B"/omc-*.bin; do
    ! dests "$f" | grep -q -x -E '[xw](19|29)' || bad "$f writes x19 or x29"
done
say "PASS no case writes x19 or x29"

awk -v a='if lt n hzcap' -v b='if eq fwscan w 0' \
    '$0 == a { print "if eq n 987654321"; next } $0 == b { print "if ne w w"; next } { print }' "$SRC" > "$B/mz.om"
[ "$(grep -c -x -e 'if eq n 987654321' -e 'if ne w w' "$B/mz.om")" = 2 ] || bad "mz: anchors not found"
"$S" < "$B/mz.om" > "$B/mz.omc" 2> "$B/mz.err" || bad "mz did not build ($(cat "$B/mz.err"))"
chmod +x "$B/mz.omc"
mkdir -p "$B/fz"
smaller=0
for s in $(seq 1 64); do
    awk -v seed="$s" -f "$T/fuzz.awk" < /dev/null > "$B/fz/p$s.om"
    for c in omc mz; do
        cc=$S; [ "$c" = mz ] && cc=$B/mz.omc
        "$cc" < "$B/fz/p$s.om" > "$B/fz/p$s.$c" 2> "$B/fz/p$s.$c.err" || bad "fuzz seed $s under $c: compiler exit ($(cat "$B/fz/p$s.$c.err"))"
        chmod +x "$B/fz/p$s.$c"
        set +e
        "$B/fz/p$s.$c" > "$B/fz/p$s.$c.out" 2>&1
        echo $? >> "$B/fz/p$s.$c.out"
        set -e
    done
    cmp -s "$B/fz/p$s.omc.out" "$B/fz/p$s.mz.out" || bad "fuzz seed $s: $(tail -n 1 "$B/fz/p$s.omc.out") under omc, $(tail -n 1 "$B/fz/p$s.mz.out") under mz"
    ! dests "$B/fz/p$s.omc" | grep -q -x -E '[xw](19|29)' || bad "fuzz seed $s writes x19 or x29"
    [ "$(wc -c < "$B/fz/p$s.omc")" -ge "$(wc -c < "$B/fz/p$s.mz")" ] || smaller=$((smaller+1))
done
[ "$smaller" -gt 0 ] || bad "fuzz: no program changed size, so hoisting and implied ranges were never exercised"
say "PASS fuzz 64 programs exit the same with and without hoisting ($smaller smaller with it)"

say "PASS gate19 $n cases, 6 mutants caught, f1, register scan, fuzz 64"
exit 0
