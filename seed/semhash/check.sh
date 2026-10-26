#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# seed/semhash/check.sh: build seed/semhash/semhash.om with the pinned omc and check it.
#   1 keyword parity   tool's table == genkw.awk lines 16-18 + omc.om fn kw2 (decoded here)
#   2 oracle           omc(canon(x)) == omc(x) byte for byte, for every unit omc accepts
#   3 invariance       alpha-renaming, notes/blanks/spaces, reordering: every fn SAME
#   4 controls         meaning changes report CHANGED where expected (cases/*.exp)
#   5 mutant           a copy with one keyword dropped must fail check 1 and check 2
#   6 rename           a fn-name-only rename reports RENAMED with an equal fn hash
# Prints SEMHASH PASS cases=N controls=M (exit 0), SEMHASH FAIL ... (exit 1), or
# SEMHASH NOT_RUN ... (exit 3) when the pinned compiler is missing or differs.
# Run from anywhere; paths are relative to the repository root.
set -u
cd "$(dirname "$0")/../.." || exit 3
OMC=${OMC:-seed/out/omc}
PIN=${SEMHASH_OMC_PIN:-2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f}
W=${SEMHASH_OUT:-build/semhash-check}
C=seed/semhash/cases

notrun() { printf 'SEMHASH NOT_RUN %s\n' "$1"; exit 3; }
[ -x "$OMC" ] || notrun "no compiler at $OMC"
GOT=$(sha256sum "$OMC" | cut -d ' ' -f 1)
[ "$GOT" = "$PIN" ] || notrun "compiler $OMC sha256 $GOT differs from pin $PIN"
printf 'omc %s sha256 %s\n' "$OMC" "$GOT"

rm -rf "$W"
mkdir -p "$W/or" "$W/mut" || exit 3
FAILS=0
CASES=0
CONTROLS=0
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

# build: sha256-om2 (exports made plain fns) followed by the tool; semhash.om has no om2 line
build() { # $1 = tool source, $2 = output binary
    { sed 's/^export fn /fn /' om/sha256/sha256-om2.om; cat "$1"; } > "$2.full.om" || return 1
    "$OMC" < "$2.full.om" > "$2" 2> "$2.err" || return 1
    chmod +x "$2"
}
build seed/semhash/semhash.om "$W/semhash" || { printf 'SEMHASH FAIL build: %s\n' "$(cat "$W/semhash.err")"; exit 1; }
SH=$W/semhash
printf 'built %s sha256 %s\n' "$SH" "$(sha256sum "$SH" | cut -d ' ' -f 1)"

# ---- 1 keyword parity
# expected list: genkw.awk lines 16 and 18 (word:code pairs) plus omc.om fn kw2 (packed word -> code)
awk 'NR==16||NR==18{ s=$0; sub(/^[^"]*"/,"",s); sub(/".*$/,"",s); n=split(s,a," ");
     for(i=1;i<=n;i++){ split(a[i],p,":"); print p[1], p[2] } }' seed/omc/genkw.awk > "$W/kw.exp"
unpack() { # little-endian packed word -> bytes
    v=$1
    while [ "$v" -gt 0 ]; do
        printf "\\$(printf '%03o' $((v % 256)))"
        v=$((v / 256))
    done
}
awk '/^fn kw2 /{on=1; next} !on{next} /^if eq w /{w=$4; inif=1; next}
     inif&&/^ret /{print w, $2; next} /^end$/{if(inif){inif=0; next} exit}' seed/omc/omc.om > "$W/kw2.raw"
[ -s "$W/kw2.raw" ] || fail "keyword parity: no fn kw2 table decoded from seed/omc/omc.om"
while read -r w c; do printf '%s %s\n' "$(unpack "$w")" "$c"; done < "$W/kw2.raw" >> "$W/kw.exp"
kwcheck() { # $1 = tool binary, $2 = tag; returns 1 on mismatch
    "$1" keywords > "$W/$2.kw.got" 2>&1 || return 1
    sort -u "$W/kw.exp" > "$W/$2.kw.e"
    sort -u "$W/$2.kw.got" > "$W/$2.kw.g"
    cmp -s "$W/$2.kw.e" "$W/$2.kw.g"
}
if kwcheck "$SH" tool; then
    printf 'keywords: %s words match genkw.awk + omc.om kw2\n' "$(wc -l < "$W/tool.kw.e" | tr -d ' ')"
else
    fail "keyword parity: $(diff "$W/tool.kw.e" "$W/tool.kw.g" | tr '\n' ' ')"
fi

# ---- 2 oracle
ORACLE_OK=0
ORACLE_SKIP=0
ORACLE_FMT=0
ORACLE_PRES=0
SEM_REFUSED=0
oracle() { # $1 = unit, $2 = tool binary; prints a reason on failure, returns 1
    if ! "$OMC" < "$1" > "$W/or/a" 2> "$W/or/ae"; then
        # refused by omc: canon must not turn it into an accepted unit
        if "$2" canon < "$1" > "$W/or/c.om" 2> /dev/null; then
            if "$OMC" < "$W/or/c.om" > /dev/null 2>&1; then
                printf 'refused unit became accepted after canon'; return 1
            fi
            ORACLE_PRES=$((ORACLE_PRES + 1))
        fi
        ORACLE_SKIP=$((ORACLE_SKIP + 1)); return 0
    fi
    if [ "$(grep -v '^[[:space:]]*note\([[:space:]()]\|$\)' "$1" | grep -v '^[[:space:]]*$' | sed -n 1p)" = 'om2 fmt' ]; then
        ORACLE_FMT=$((ORACLE_FMT + 1)); return 0   # fmt output is the formatted source text, not code
    fi
    "$2" canon < "$1" > "$W/or/c.om" 2> "$W/or/ce"
    rc=$?
    if [ "$rc" = 3 ]; then
        SEM_REFUSED=$((SEM_REFUSED + 1)); printf 'SEMREFUSED %s %s\n' "$1" "$(cat "$W/or/ce")" >> "$W/semrefused"
        return 0
    fi
    [ "$rc" = 0 ] || { printf 'canon exit %s' "$rc"; return 1; }
    "$OMC" < "$W/or/c.om" > "$W/or/b" 2> "$W/or/be" || { printf 'canon output refused: %s' "$(cat "$W/or/be")"; return 1; }
    cmp -s "$W/or/a" "$W/or/b" || { printf 'compile(canon(x)) != compile(x)'; return 1; }
    ORACLE_OK=$((ORACLE_OK + 1))
}
J=seed/json
{ printf 'om2\n'; cat "$J/json.om" "$J/jsonw.om" "$J/jsonf.om"; printf 'fn main to int\nret 0\nend\n'; } > "$W/json-lib.om"
{ printf 'om2\n'; cat "$J/json.om" "$J/jsonw.om" "$J/jsonf.om" "$J/tests/jsdrv.om"; } > "$W/json-drv.om"
{ printf 'om2\n'; cat "$J/json.om" "$J/jsonw.om"; printf 'fn main to int\nret 0\nend\n'; } > "$W/json-nof.om"
find seed/rung4/tests seed/omc/contracts seed/omc/tests seed/omc/om2 seed/host/tests -name '*.om' -type f | LC_ALL=C sort > "$W/units"
for u in "$W/json-lib.om" "$W/json-drv.om" "$W/json-nof.om" om/blake3/blake3.om om/sha256/sha256-om2.om \
         seed/qualification/om2/leak/leakscan.om "$SH.full.om" $C/*.om; do
    printf '%s\n' "$u" >> "$W/units"
done
: > "$W/semrefused"
while read -r u; do
    # a redirected function call runs in this shell, so the counters stay visible
    oracle "$u" "$SH" > "$W/or/why" || fail "oracle $u: $(cat "$W/or/why")"
done < "$W/units"
NUNITS=$(wc -l < "$W/units" | tr -d ' ')
printf 'oracle: units=%s identical=%s omc-refused=%s (canon kept %s of them refused) fmt-skipped=%s semhash-refused=%s\n' \
    "$NUNITS" "$ORACLE_OK" "$ORACLE_SKIP" "$ORACLE_PRES" "$ORACLE_FMT" "$SEM_REFUSED"
cat "$W/semrefused"
[ "$ORACLE_OK" -gt 0 ] || fail "oracle compared no units"
CASES=$((CASES + ORACLE_OK))

# ---- 3, 4, 6 fixtures: diff base against each case; stdout and exit code must match
for x in $C/*.om; do
    n=$(basename "$x" .om)
    [ "$n" = base ] && continue
    "$SH" diff $C/base.om "$x" > "$W/$n.got" 2>&1
    rc=$?
    want=1
    case $n in inv_*) want=0 ;; esac
    if [ "$rc" != "$want" ]; then fail "case $n: exit $rc, want $want"; continue; fi
    if ! cmp -s "$C/$n.exp" "$W/$n.got"; then fail "case $n: $(diff "$C/$n.exp" "$W/$n.got" | tr '\n' ' ')"; continue; fi
    printf 'case %s: %s\n' "$n" "$(tail -n 1 "$W/$n.got")"
    case $n in neg_*) CONTROLS=$((CONTROLS + 1)) ;; *) CASES=$((CASES + 1)) ;; esac
done
unit() { "$SH" < "$1" | sed -n 's/^UNIT \([0-9a-f]*\) .*/\1/p'; }
UB=$(unit $C/base.om)
for n in inv_alpha inv_notes; do
    [ "$(unit $C/$n.om)" = "$UB" ] || fail "unit hash of $n differs from base"
done
[ "$(unit $C/inv_reorder.om)" != "$UB" ] || fail "unit hash of inv_reorder equals base (order is part of the unit)"
fh() { "$SH" < "$1" | awk -v f="$2" '$1=="FN" && $2==f {print $3}'; }
H1=$(fh $C/base.om other)
H2=$(fh $C/ren_fn.om other2)
if [ -n "$H1" ] && [ "$H1" = "$H2" ]; then printf 'rename: other -> other2 fn hash equal %s\n' "$H1"
else fail "rename: fn hash other=$H1 other2=$H2"; fi

# ---- refusals and usage
printf 'fn main to int\r\nret 0\nend\n' > "$W/cr.om"
"$SH" < "$W/cr.om" > /dev/null 2>&1; rc=$?
[ "$rc" = 3 ] || fail "CR input: exit $rc, want 3"
printf 'fn main to int\n$v0 = 1\nret 0\nend\n' > "$W/rsv.om"
"$SH" < "$W/rsv.om" > /dev/null 2>&1; rc=$?
[ "$rc" = 3 ] || fail "reserved-byte input: exit $rc, want 3"
"$SH" nosuchmode < /dev/null > /dev/null 2>&1; rc=$?
[ "$rc" = 2 ] || fail "unknown mode: exit $rc, want 2"
CONTROLS=$((CONTROLS + 3))

# ---- 5 mutant: drop one keyword from the table; check 1 and check 2 must both fail for it
sed 's/\\nwhile 69\\n/\\n/' seed/semhash/semhash.om > "$W/mut/semhash.om"
if cmp -s seed/semhash/semhash.om "$W/mut/semhash.om"; then
    fail "mutant: sed did not change the keyword table"
elif ! build "$W/mut/semhash.om" "$W/mut/semhash"; then
    fail "mutant does not build: $(cat "$W/mut/semhash.err")"
else
    k1=alive
    kwcheck "$W/mut/semhash" mut || k1=killed
    k2=alive
    while read -r u; do
        oracle "$u" "$W/mut/semhash" > /dev/null || { k2=killed; break; }
    done < "$W/units"
    printf 'mutant (while dropped): keyword-parity %s, oracle %s\n' "$k1" "$k2"
    if [ "$k1" = killed ] && [ "$k2" = killed ]; then printf 'mutant killed\n'; CONTROLS=$((CONTROLS + 1))
    else fail "mutant survived (keyword-parity $k1, oracle $k2)"; fi
fi

if [ "$FAILS" = 0 ]; then
    printf 'SEMHASH PASS cases=%s controls=%s\n' "$CASES" "$CONTROLS"
    exit 0
fi
printf 'SEMHASH FAIL failures=%s cases=%s controls=%s\n' "$FAILS" "$CASES" "$CONTROLS"
exit 1
