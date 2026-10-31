#!/bin/sh
# gate-recursion.sh -- bounded recursion in seeded Omegese (issue #645). The compiler under test is
# seed/out/omc (OMC overrides it). seed/omc/tests/recursion/nest.om is a mutually recursive descent
# evaluator (primary -> expr -> term -> primary) with an explicit depth bound (DMAX 16) and a work
# bound (FUEL 64). Every row of cases.tsv must give its exact exit status and stdout: depth 16 is
# accepted and depth 17 is refused with status 40 before any frame for it exists, 64 primaries are
# accepted and the 65th is refused with status 42. Then each source mutant (one line, whose site
# occurs exactly once) must compile and be detected by a wrong status or stdout on some row whose
# status is below 100. A mutant that does not compile, or is noticed only by a trap or a signal, is
# INVALID and fails the gate: a crash is never a killed mutant. RC=0 only when everything passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/recursion
T=seed/omc/tests/recursion
S=${OMC:-seed/out/omc}
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# one input file and one expected-stdout file per row
awk -F '\t' -v d="$B" '
/^#/ || NF == 0 { next }
NF != 4 { print "bad row " NR > "/dev/stderr"; exit 1 }
{
    x = $2; s = x
    if (x ~ /^nest:/ || x ~ /^open:/) {
        split(x, f, ":"); s = ""
        for (i = 0; i < f[2]; i++) s = s "("
        s = s f[3]
        if (f[1] == "nest") for (i = 0; i < f[2]; i++) s = s ")"
    } else if (x ~ /^sum:/) {
        split(x, f, ":"); s = "1"
        for (i = 1; i < f[2]; i++) s = s "+1"
    }
    printf "%s\n", s > (d "/" $1 ".in"); close(d "/" $1 ".in")
    printf "%s\n", $4 > (d "/" $1 ".want"); close(d "/" $1 ".want")
    print $1, $3 > (d "/rows")
}' "$T/cases.tsv" || bad "cases.tsv is malformed"

# build SRC BIN: rc 0 and empty stderr, else returns 1
build() {
    set +e
    "$S" < "$1" > "$2" 2> "$2.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] && [ ! -s "$2.err" ] || return 1
    chmod +x "$2"
}

# check BIN: prints one line per row, "ok", "wrong" (status < 100) or "trap" (status >= 100)
check() {
    while read -r name want; do
        set +e
        "$1" < "$B/$name.in" > "$B/$name.out"
        rc=$?
        set -e
        if [ "$rc" = "$want" ] && cmp -s "$B/$name.out" "$B/$name.want"; then st=ok
        elif [ "$rc" -ge 100 ]; then st=trap
        else st=wrong; fi
        printf '%s %s %s %s\n' "$name" "$st" "$rc" "$want"
    done < "$B/rows"
}

say "omc    sha256 $(sha256sum "$S" | cut -d' ' -f1)"
say "source sha256 $(sha256sum "$T/nest.om" | cut -d' ' -f1)"
build "$T/nest.om" "$B/nest" || bad "nest.om: compiler exit $crc ($(cat "$B/nest.err"))"
say "binary sha256 $(sha256sum "$B/nest" | cut -d' ' -f1)"
check "$B/nest" > "$B/base.res"
n=$(wc -l < "$B/rows")
while read -r name st rc want; do
    [ "$st" = ok ] || bad "$name: exit $rc (want $want), stdout $(cat "$B/$name.out")"
    say "PASS $name exit $rc: $(cat "$B/$name.out")"
done < "$B/base.res"

# mutant NAME OLD-LINE NEW-LINE
m=0
mutant() {
    m=$((m + 1))
    c=$(grep -cxF -- "$2" "$T/nest.om" || true)
    [ "$c" = 1 ] || bad "mutant $1: site found $c times: $2"
    awk -v o="$2" -v n="$3" '$0 == o { $0 = n } { print }' "$T/nest.om" > "$B/mut-$1.om"
    cmp -s "$T/nest.om" "$B/mut-$1.om" && bad "mutant $1: source unchanged"
    build "$B/mut-$1.om" "$B/mut-$1" || bad "mutant $1: INVALID, compiler exit $crc ($(cat "$B/mut-$1.err"))"
    check "$B/mut-$1" > "$B/mut-$1.res"
    k=$(awk '$2 == "wrong" { print $1 " exit " $3 " want " $4; exit }' "$B/mut-$1.res")
    [ -n "$k" ] || bad "mutant $1: not detected ($(awk '$2 != "ok"' "$B/mut-$1.res" | tr '\n' ' '))"
    say "PASS mutant $1 detected: $k"
}
mutant depth-limit-17   'const DMAX ulong 16'          'const DMAX ulong 17'
mutant depth-off-by-one 'if gt nd DMAX'                'if ge nd DMAX'
mutant depth-no-count   'nd = add depth 1'             'nd = depth'
mutant fuel-limit-65    'const FUEL ulong 64'          'const FUEL ulong 65'
mutant fuel-no-decrement 'put St 3 sub get St 3 1'     'put St 3 get St 3'
mutant eval-mul-to-add  'v = mul v r'                  'v = add v r'
mutant digit-order     'v = add mul v 10 sub c 48'     'v = add mul 10 sub c 48 v'
mutant return-frame     'ret inner'                    'ret nd'
say "RECURSION PASS cases=$n mutants=$m/$m"
