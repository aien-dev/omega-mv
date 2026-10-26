#!/bin/sh
# gate19.sh -- om2 query, the white-box hole interface (OMEGESE.md 9.13). The cases in seed/omc/tests/query
# and their *.omq.json goldens are copied from the black-box oracle seed/query/omq.om (aien-dev/omega-mv
# #838, feat/omc-query at df23dc7b1). For every statement hole, om2 query must print one JSON line whose
# hole, scope, fns and obligations equal omq's (omq's check, next and compiles come from witness compiles
# and are not part of the white-box answer). The two error objects must carry omq's error text. A
# deliberately wrong answer (a name dropped from scope) must fail the comparison (negative control).
# Without the lead line, ?? stays the refusal it was before om2 query (same code and line), and om2 query
# on a program without a hole writes the same program as the program alone.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
O=${OMC:-seed/out/omc}
[ -x "$O" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$O" >&2; exit 1; }
D=seed/omc/tests/query
mkdir -p build/seed/r19
T=$(mktemp -d build/seed/r19/q.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
n=0

# field FILE KEY: the value of top-level key KEY in the one-line JSON object in FILE ("" when absent)
field() {
    awk -v key="$2" '{
        s = $0; L = length(s); d = 0; q = 0; want = "\"" key "\":"; out = ""
        for (i = 1; i <= L; i++) {
            c = substr(s, i, 1)
            if (q) { if (c == "\\") { i++; continue } if (c == "\"") q = 0; continue }
            if (c == "\"") {
                if (d == 1 && substr(s, i, length(want)) == want) {
                    st = i + length(want); e = st; dd = 0; qq = 0
                    for (; e <= L; e++) {
                        x = substr(s, e, 1)
                        if (qq) { if (x == "\\") { e++; continue } if (x == "\"") qq = 0; continue }
                        if (x == "\"") { qq = 1; continue }
                        if (x == "{" || x == "[") dd++
                        if (x == "}" || x == "]") { if (dd == 0) break; dd-- }
                        if (x == "," && dd == 0) break
                    }
                    out = substr(s, st, e - st); break
                }
                q = 1; continue
            }
            if (c == "{" || c == "[") d++
            if (c == "}" || c == "]") d--
        }
        print out
    }' "$1"
}

# same WHITE OMQ: hole, scope, fns and obligations of the two answers are equal and present
same() {
    for k in hole scope fns obligations; do
        a=$(field "$1" "$k"); b=$(field "$2" "$k")
        [ -n "$a" ] && [ "$a" = "$b" ] || { printf '%s differs: %s | %s\n' "$k" "$a" "$b"; return 1; }
    done
    return 0
}

for c in arr_call else_ret stmt_effects stmt_loop stmt_om2; do
    { printf 'om2 query\n'; cat "$D/$c.om"; } > "$T/$c.in"
    set +e; "$O" < "$T/$c.in" > "$T/$c.out" 2> "$T/$c.err"; rc=$?; set -e
    [ "$rc" = 0 ] || bad "$c: om2 query exit $rc, wanted 0"
    [ -s "$T/$c.err" ] && bad "$c: om2 query wrote stderr"
    [ "$(wc -l < "$T/$c.out")" = 1 ] || bad "$c: om2 query output is not one line"
    same "$T/$c.out" "$D/$c.omq.json" > "$T/$c.why" || bad "$c: $(cat "$T/$c.why")"
    n=$((n + 1))
done
printf 'PASS gate19 %s statement holes: hole, scope, fns and obligations equal omq\n' "$n"

# negative control: drop the first scope name from a correct answer; the comparison must fail
sed 's/"scope":\[{[^}]*},/"scope":[/' "$T/stmt_loop.out" > "$T/mutant.out"
cmp -s "$T/mutant.out" "$T/stmt_loop.out" && bad "negative control: the mutant equals the answer"
if same "$T/mutant.out" "$D/stmt_loop.omq.json" > "$T/mutant.why"; then
    bad "negative control: a dropped scope name passed"
fi
printf 'PASS gate19 negative control: a dropped scope name fails (%s)\n' "$(cut -c1-40 "$T/mutant.why")"

for c in outside twoholes; do
    { printf 'om2 query\n'; cat "$D/$c.om"; } > "$T/$c.in"
    set +e; "$O" < "$T/$c.in" > "$T/$c.out" 2> /dev/null; rc=$?; set -e
    [ "$rc" = 1 ] || bad "$c: om2 query exit $rc, wanted 1"
    a=$(field "$T/$c.out" error); b=$(field "$D/$c.omq.json" error)
    [ -n "$a" ] && [ "$a" = "$b" ] || bad "$c: error $a, omq $b"
done
printf 'PASS gate19 error objects: hole outside a function, more than one hole\n'

# off: ?? without the lead line is refused exactly as before om2 query (base omc 2c824805 at 0b77e6ea)
while read -r c want code; do
    set +e; "$O" < "$D/$c.om" > /dev/null 2> "$T/off.err"; rc=$?; set -e
    got=$(tr ' ' _ < "$T/off.err")
    [ "$rc" = "$code" ] && [ "$got" = "$want" ] || bad "$c off: exit $rc '$got', wanted $code '$want'"
done <<'ROWS'
arr_call omc:_line_10:_error_4 4
else_ret omc:_line_6:_error_4 4
stmt_effects omc:_line_9:_error_4 4
stmt_loop omc:_line_6:_error_4 4
stmt_om2 omc:_line_11:_error_4 4
outside omc:_line_2:_error_16 16
twoholes omc:_line_3:_error_4 4
ROWS
printf 'PASS gate19 off: ?? without om2 query keeps the refusal code and line (7 rows)\n'

# no hole: om2 query writes the same program as the rest alone
sed '/^ *??$/d' "$D/stmt_loop.om" > "$T/nohole.om"
{ printf 'om2 query\n'; cat "$T/nohole.om"; } > "$T/nohole.in"
"$O" < "$T/nohole.om" > "$T/nohole.a"
"$O" < "$T/nohole.in" > "$T/nohole.b"
[ -s "$T/nohole.a" ] && cmp -s "$T/nohole.a" "$T/nohole.b" || bad "no hole: om2 query output differs from the program alone"
printf 'PASS gate19 no hole: om2 query output equals the program alone\n'

# lead placement: om2 query only at byte 0, never inside an om2 fmt or om2 query rest (error 16, line 2)
for lead in 'note x\nom2 query\n' 'om2 query\nom2 query\n' 'om2 query\nom2 fmt\n' 'om2 fmt\nom2 query\n'; do
    { printf "$lead"; cat "$D/stmt_loop.om"; } > "$T/lead.in"
    set +e; "$O" < "$T/lead.in" > "$T/lead.out" 2> "$T/lead.err"; rc=$?; set -e
    [ "$rc" = 16 ] && [ "$(cat "$T/lead.err")" = 'omc: line 2: error 16' ] && [ ! -s "$T/lead.out" ] ||
        bad "lead $lead: exit $rc, $(cat "$T/lead.err")"
done
printf 'PASS gate19 lead: om2 query after a note, after om2 query or om2 fmt, or before om2 fmt is error 16 (4 rows)\n'
exit 0
