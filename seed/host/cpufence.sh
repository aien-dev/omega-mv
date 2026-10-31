#!/bin/sh
# cpufence.sh -- host CPU ordering words (#939): cpufence (keyword 199) and cpurelax (keyword 200).
# 1. seed/host/tests/cpufence.om runs (exit 42) natively on AArch64 and, with the x86 selector, under
#    qemu-x86_64.
# 2. GNU objdump (reference only, never on the seed path) reads each binary back. Against the same program
#    with the two words deleted, the AArch64 binary gains exactly one DSB SY (d5033f9f) per cpufence and one
#    YIELD (d503203f) per cpurelax; the x86-64 binary gains one MFENCE (0f ae f0) and one PAUSE (f3 90).
# 3. Misuse is refused at compile time.
# 4. Negative control: a compiler built from omc.om with both cpufence encodings swapped for NOPs
#    (AArch64 d503201f, x86 90 90 90) still runs the program to exit 42; check 2 must catch it.
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
OMC=${OMC:-"$root/seed/out/omc"}
command -v qemu-x86_64 >/dev/null || { echo 'NOT_RUN cpufence: requires qemu-x86_64'; exit 3; }
command -v x86_64-linux-gnu-objdump >/dev/null || { echo 'NOT_RUN cpufence: requires x86_64-linux-gnu-objdump'; exit 3; }
mkdir -p build/seed/host
T=$(mktemp -d "$root/build/seed/host/cpufence.XXXXXX")
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL cpufence: %s\n' "$*" >&2; exit 1; }
SRC=seed/host/tests/cpufence.om
nf=$(grep -c -x cpufence "$SRC")
nr=$(grep -c -x cpurelax "$SRC")
grep -v -x -e cpufence -e cpurelax "$SRC" > "$T/base.om"
printf 'target x86linux\n' | cat - "$SRC" > "$T/x86.om"
printf 'target x86linux\n' | cat - "$T/base.om" > "$T/x86base.om"

a64n() { objdump -D -z -b binary -m aarch64 "$1" | awk -F'\t' -v w="$2" -v m="$3" -v o="$4" \
    '$2 ~ "^" w && $3 == m && $4 == o {n++} END {print n + 0}'; }
x86n() { x86_64-linux-gnu-objdump -D -z -b binary -m i386:x86-64 "$1" | awk -F'\t' -v w="$2" -v m="$3" \
    '{b=$2; sub(/ +$/, "", b); a=$3; sub(/ +$/, "", a)} b == w && a == m {n++} END {print n + 0}'; }
run() { # compiler source out arch -> exit status
    "$1" < "$2" > "$3"
    chmod +x "$3"
    set +e
    if [ "$4" = x86 ]; then qemu-x86_64 "$3"; else "$3"; fi
    rc=$?
    set -e
    echo "$rc"
}
# measure compiler tag: prints "dsb yield mfence pause" deltas over the word-free program
measure() {
    for f in prog base; do
        src=$SRC; [ $f = base ] && src=$T/base.om
        rc=$(run "$1" "$src" "$T/$2-$f.a64" a64)
        [ "$rc" = 42 ] || bad "$2 $f AArch64 exit $rc, wanted 42"
        xs=$T/x86.om; [ $f = base ] && xs=$T/x86base.om
        rc=$(run "$1" "$xs" "$T/$2-$f.x86" x86)
        [ "$rc" = 42 ] || bad "$2 $f x86-64 exit $rc, wanted 42"
    done
    d=$(( $(a64n "$T/$2-prog.a64" d5033f9f dsb sy) - $(a64n "$T/$2-base.a64" d5033f9f dsb sy) ))
    y=$(( $(a64n "$T/$2-prog.a64" d503203f yield '') - $(a64n "$T/$2-base.a64" d503203f yield '') ))
    m=$(( $(x86n "$T/$2-prog.x86" '0f ae f0' mfence) - $(x86n "$T/$2-base.x86" '0f ae f0' mfence) ))
    p=$(( $(x86n "$T/$2-prog.x86" 'f3 90' pause) - $(x86n "$T/$2-base.x86" 'f3 90' pause) ))
    echo "$d $y $m $p"
}
want="$nf $nr $nf $nr"
got=$(measure "$OMC" real)
[ "$got" = "$want" ] || bad "instruction deltas (dsb yield mfence pause) $got, wanted $want"
printf 'PASS cpufence runs exit 42 on AArch64 and x86-64 (qemu); objdump deltas dsb=%s yield=%s mfence=%s pause=%s\n' $got

refuse() { # name want-exit source-line...
    name=$1; wantc=$2; shift 2
    printf '%s\n' "$@" > "$T/$name.om"
    set +e
    "$OMC" < "$T/$name.om" > "$T/$name.out" 2> "$T/$name.err"
    c=$?
    set -e
    [ "$c" = "$wantc" ] && [ ! -s "$T/$name.out" ] || bad "refusal $name exit $c, wanted $wantc"
}
refuse operand 5 'fn main to int' 'cpufence 1' 'ret 0' 'end'
refuse as-value 16 'fn main to int' 'x = cpufence' 'ret 0' 'end'
refuse as-name 5 'fn main to int' 'cpurelax = int 1' 'ret 0' 'end'
refuse top-level 16 'cpufence' 'fn main to int' 'ret 0' 'end'
refuse dead 17 'fn main to int' 'ret 0' 'cpufence' 'end'
printf '%s\n' 'fn f x ulong to ulong' 'cpufence' 'cpurelax' 'ret x' 'end' > "$T/bare.om"
"$OMC" < "$T/bare.om" > "$T/bare.bin"
[ "$(a64n "$T/bare.bin" d5033f9f dsb sy) $(a64n "$T/bare.bin" d503203f yield '')" = "1 1" ] ||
    bad "function-mode unit lacks exactly one dsb sy and one yield"
printf 'PASS cpufence/cpurelax in a function-mode unit: one dsb sy, one yield\n'
printf 'PASS cpufence refusals: operand, value use, name, top level, dead code\n'

awk '/^if eq c 199$/{f=1} f && /^emit 3573759903$/{$0="emit 3573751839"} f && /^xb 15 174 240$/{$0="xb 144 144 144"}
     f && /^ret 0$/{f=0} {print}' seed/omc/omc.om > "$T/mutant.om"
cmp -s seed/omc/omc.om "$T/mutant.om" && bad "mutant site not found"
"$OMC" < "$T/mutant.om" > "$T/omc-mutant"
chmod +x "$T/omc-mutant"
mgot=$(measure "$T/omc-mutant" mutant)
[ "$mgot" = "0 $nr 0 $nr" ] || bad "nop mutant gave deltas $mgot, wanted 0 $nr 0 $nr"
printf 'PASS negative control: nop cpufence mutant still exits 42 but objdump deltas %s are caught (wanted %s)\n' "$mgot" "$want"
