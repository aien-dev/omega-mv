#!/bin/sh
# Qualification for seed/query (omq, the hole oracle over the pinned omc).
# The tool is seeded Omega; this coordinator only compiles, runs and compares.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN (wrong compiler).
set -u
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OMC=${OMC:-$ROOT/seed/out/omc}
PIN=$(sed -n 's/^OMC_PIN=//p' om/units.sh)
[ -x "$OMC" ] || { echo "NOT_RUN query: no omc at $OMC"; exit 3; }
[ "$(sha256sum < "$OMC" | cut -c1-64)" = "$PIN" ] || { echo "NOT_RUN query: omc is not the pinned compiler $PIN"; exit 3; }
B=$ROOT/build/seed/query
rm -rf "$B"; mkdir -p "$B"
C=seed/query/cases
Q=$B/omq
fails=0; checks=0
ok() { checks=$((checks + 1)); printf 'ok   %s\n' "$*"; }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); printf 'FAIL %s\n' "$*"; }

"$OMC" < seed/query/omq.om > "$Q" 2> "$Q.err" && chmod +x "$Q" || { cat "$Q.err"; echo "QUERY FAIL build"; exit 1; }
echo "omc $PIN"
echo "omq sha256 $(sha256sum < "$Q" | cut -c1-64)"

# 1. Goldens: every case answer is byte-identical to the reviewed expected answer.
for f in $C/*.om; do
    n=$(basename "$f" .om)
    "$Q" "$OMC" < "$f" > "$B/$n.json"
    rc=$?
    want=0
    case $n in twoholes|outside) want=1 ;; esac
    if [ $rc = $want ] && cmp -s "$B/$n.json" "$C/$n.json"; then ok "golden $n rc=$rc"
    else bad "golden $n rc=$rc expected=$want"; diff "$C/$n.json" "$B/$n.json" | head -5; fi
done

# 2. Negative controls: properties that a wrong oracle would break.
has() { grep -q -- "$2" "$B/$1.json"; }
if has stmt_effects '"name":"z"'; then bad "closed-block name z offered in scope"; else ok "closed-block name z absent from scope"; fi
if has stmt_effects '"break"'; then bad "break offered outside a loop"; else ok "break absent outside a loop"; fi
if has stmt_loop '"break"'; then ok "break offered inside a loop"; else bad "break missing inside a loop"; fi
if has value_om2 '"next":\[[^]]*"a"[,\]]'; then bad "uint[] name a offered for a ulong hole"; else ok "wrong-type array a excluded at a ulong hole"; fi
if has stmt_loop '"requires <bool>"'; then bad "contract word offered in an OM-1 unit"; else ok "no contract forms in an OM-1 unit"; fi
if has stmt_om2 '"invariant <bool>"'; then ok "invariant offered in an OM-2 loop"; else bad "invariant missing in an OM-2 loop"; fi
if has stmt_effects '"effects":\["write:io"\]'; then ok "effect write:io reported"; else bad "effect write:io missing"; fi
if has else_ret '"check":{"code":0,"status":"INCOMPLETE"}'; then ok "hole in an else after a returning then: base witness compiles"; else bad "else_ret base witness rejected"; fi
if has arr_call '"fill k ulong 1"' && ! has arr_call '"fill [sb] '; then ok "array argument: the length-matching array k, not s or b"; else bad "array argument choice"; fi
if has arr_call '"name":"v","type":"ulong"'; then ok "binding without = (v ulong 5) in scope"; else bad "binding without = missing from scope"; fi
sed 's/^??$/fill k ulong 1/' $C/arr_call.om | "$OMC" > /dev/null 2>&1 && ok "offered call fill k ulong 1 compiles" || bad "offered call fill k ulong 1 rejected"
sed 's/^??$/fill s ulong 1/' $C/arr_call.om | "$OMC" > /dev/null 2>&1 && bad "control: fill s (uint 8 for uint 64) accepted" || ok "control: fill s (uint 8 for uint 64) rejected by omc"
# Outcome: three values, never a plain VALID. A hole with a compiling base is INCOMPLETE, an
# error before the hole is INVALID, a whole accepted program is VALID-UNPROVEN.
if has err3 '"status":"INVALID"' && has stmt_loop '"status":"INCOMPLETE"'; then ok "status INVALID / INCOMPLETE"; else bad "status INVALID / INCOMPLETE"; fi
printf 'fn main to int\nret int 0\nend\n' | "$Q" "$OMC" > "$B/whole.json"
grep -q '"status":"VALID-UNPROVEN"' "$B/whole.json" && ok "status VALID-UNPROVEN for an accepted whole program" || bad "status for an accepted whole program"
if grep -h -o '"status":"VALID"' "$B"/*.json | grep -q .; then bad "plain VALID emitted"; else ok "no answer claims plain VALID"; fi

# 3. Every next form at the loop hole really compiles: substitute a concrete instance
#    for each listed shape and recompile. This checks omq's claims with omc directly.
inst() { # inst FORM -> concrete statement
    case "$1" in
    'ret <ulong>') echo 'ret ulong 1' ;;
    'n = <ulong>'|'s = <ulong>'|'i = <ulong>') echo "${1% = *} = ulong 1" ;;
    'NEW = <value>') echo '_n = ulong 1' ;;
    'if <bool>') printf 'if true\nend' ;;
    'while <bool>') printf 'while false\nend' ;;
    'for NAME T COUNT') printf 'for _k ulong 1\nend' ;;
    'trap <1..255>') echo 'trap 1' ;;
    'exit <int>') echo 'exit 0' ;;
    'end') printf 'end\nfor _k ulong 1' ;;
    'put cnt <ulong> <ulong>') echo 'put cnt 0 ulong 1' ;;
    *) echo "$1" ;;
    esac
}
forms=$(sed 's/.*"next":\[\([^]]*\)\].*/\1/' "$B/stmt_loop.json" | tr ',' '\n' | sed 's/^"//; s/"$//')
nf=0; nbad=0
IFS='
'
for form in $forms; do
    nf=$((nf + 1))
    s=$(inst "$form")
    awk -v s="$s" '$0 == "??" { print s; next } { print }' $C/stmt_loop.om > "$B/form.om"
    "$OMC" < "$B/form.om" > /dev/null 2>&1 || { nbad=$((nbad + 1)); echo "  form does not compile: $form"; }
done
unset IFS
if [ $nf -gt 0 ] && [ $nbad = 0 ]; then ok "all $nf next forms at the loop hole compile"; else bad "$nbad of $nf next forms do not compile"; fi
# and a form omq rejected really does not compile: else at a for-loop hole.
awk '$0 == "??" { print "else"; next } { print }' $C/stmt_loop.om > "$B/form.om"
if "$OMC" < "$B/form.om" > /dev/null 2>&1; then bad "else compiles but omq omitted it"; else ok "omitted form else is refused by omc"; fi

# 4. Repairs: each fix, applied by hand, yields the reported rc.
sed 's/abcdefghijk/abcdefg0/g' $C/err13.om > "$B/fix.om"; "$OMC" < "$B/fix.om" > /dev/null 2>&1
r=$?; if [ $r = 0 ] && has err13 '"rc_after_fix":0'; then ok "err13 rename repair compiles"; else bad "err13 repair rc=$r"; fi

# 5. A compiler that is not omc is refused, never answered.
for fake in /bin/false /bin/cat /nonexistent; do
    "$Q" "$fake" < $C/stmt_loop.om > "$B/fake.json" 2> /dev/null
    r=$?
    if [ $r = 2 ] && [ ! -s "$B/fake.json" ]; then ok "fake compiler $fake refused rc=2"; else bad "fake compiler $fake rc=$r"; fi
done

# 6. Prefix mode (absorbed omviable): length-prefixed requests, one verdict line each.
req() { # req TEXT -> 4-byte little-endian length + bytes
    n=$(printf '%s' "$1" | wc -c)
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $((n & 255)) $((n >> 8 & 255)) $((n >> 16 & 255)) $((n >> 24 & 255)))"
    printf '%s' "$1"
}
H='fn main to int
'
{
    req "${H}x = add"
    req "${H}x = add int 1 2
ret x
end
"
    req "${H}x = add int 1 2
ret y"
    req "${H}x = add int 1 2
ret y "
    req "${H}abcdefghij = 1"
} | "$Q" "$OMC" prefix > "$B/prefix.txt"
printf '1 4\n0 0\n1 3\n2 3\n2 13\n' > "$B/prefix.want"
if cmp -s "$B/prefix.txt" "$B/prefix.want"; then ok "prefix verdicts 5/5"; else bad "prefix verdicts"; diff "$B/prefix.want" "$B/prefix.txt"; fi

# 7. The goldens can fail: a corrupted expected answer must not compare equal.
sed 's/"regs_free":11/"regs_free":12/' $C/stmt_loop.json > "$B/corrupt.json"
if cmp -s "$B/stmt_loop.json" "$B/corrupt.json"; then bad "corrupted golden still matches"; else ok "corrupted golden is detected"; fi

# 8. Mutant: an omq whose statement witnesses accept any compiler verdict must be caught.
awk '/^fn trys / { m = 1 } m && $0 == "ret eq run 0" { print "ret ne run 255"; m = 0; next } { print }' \
    seed/query/omq.om > "$B/mutant.om"
"$OMC" < "$B/mutant.om" > "$B/mutant" 2> /dev/null && chmod +x "$B/mutant"
"$B/mutant" "$OMC" < $C/stmt_effects.om > "$B/mutant.json"
if cmp -s "$B/mutant.json" $C/stmt_effects.json; then bad "mutant omq (accept every witness) not caught"
elif grep -q '"break"' "$B/mutant.json"; then ok "mutant omq caught: offers break outside a loop"
else ok "mutant omq caught: golden differs"; fi

# 9. Host-word closure: the effect table covers every omc host keyword (code >= 100).
sed -n 16,18p seed/omc/genkw.awk | grep -oE '[a-z0-9]+:[0-9]+' \
    | awk -F: '$2 >= 100 && $1 != "const" { print $1 }' | sort > "$B/kw.txt"
grep '^text hostw' seed/query/omq.om | grep -oE '[a-z0-9]+:[a-z]+' | cut -d: -f1 \
    | grep -v -x -e read -e write -e exit | sort > "$B/hw.txt"
if cmp -s "$B/kw.txt" "$B/hw.txt"; then ok "effect table == $(wc -l < "$B/kw.txt") omc host keywords"; else bad "effect table differs from omc host keywords"; diff "$B/kw.txt" "$B/hw.txt"; fi

if [ $fails = 0 ]; then echo "SEED QUERY PASS $checks checks"; exit 0; fi
echo "SEED QUERY FAIL $fails of $checks checks"; exit 1
