#!/bin/sh
# gate15.sh -- refusal-code conformance. One minimal program per compiler error code 3..23 (1 and 2
# are I/O errors on the compiler's own input/output, not language inputs). Every case must refuse
# with exactly its code; a valid control must compile. This pins the table documented at
# docs/one_layer/OMEGESE.md (section 9) and in the omega-om skill.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
S=seed/out/omc
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
mkdir -p build/seed/r15
T=$(mktemp -d build/seed/r15/codes.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

n=0
while IFS='|' read -r name want src; do
    [ -n "$name" ] || continue
    n=$((n + 1))
    printf '%b\n' "$src" > "$T/p.om"
    set +e
    "$S" < "$T/p.om" > "$T/p.bin" 2> "$T/p.err"
    rc=$?
    set -e
    [ "$rc" = "$want" ] || bad "$name: exit $rc, wanted $want ($(cat "$T/p.err"))"
    grep -q "error $want\$" "$T/p.err" || bad "$name: stderr lacks 'error $want': $(cat "$T/p.err")"
done <<'CASES'
e3_unknown_name|3|fn f to int\nret q\nend
e4_line_ends|4|struct S a uint\nfn main to int\nret 0\nend
e5_extra_words|5|fn f to int\nret 0 extra\nend
e6_bad_number|6|fn f to int\nx = 12a\nret 0\nend
e7_type_mismatch|7|fn f to int\nx = add int 1 uint 2\nret 0\nend
e8_out_of_range|8|fn f to int\nx = int 2147483648\nret 0\nend
e9_break_outside|9|fn f to int\nbreak\nret 0\nend
e10_duplicate_param|10|fn f x int x int to int\nret 0\nend
e11_ambiguous|11|fn f to int\nx = 5\nret 0\nend
e13_long_variable|13|fn f to int\nabcdefghi = int 1\nret 0\nend
e15_missing_end|15|fn f to int\nret 0
e16_syntax|16|wat
e17_after_path|17|fn f to int\nret 0\nret 1\nend
e18_missing_ret|18|fn f to int\nend
e19_global_passed|19|global g uchar 4\nfn f a edit uchar 4 to void\nend\nfn main to int\nf g\nret 0\nend
e20_readonly|20|text t "abc"\nfn main to int\nput t 0 1\nret 0\nend
e21_const_index|21|fn main to int\na = new uint 4 0\nput a 4 1\nret 0\nend
e22_text_literal|22|text t "a\\q"\nfn main to int\nret 0\nend
e23_size_capacity|23|fn main to int\na = new uchar 16777217 0\nret 0\nend
CASES
[ "$n" = 19 ] || bad "table ran $n of 19 cases"

# 12: the 17th live name exhausts the 16-register pool.
{
    printf 'fn f to int\n'
    i=1
    while [ "$i" -le 17 ]; do printf 'a%s = int %s\n' "$i" "$i"; i=$((i + 1)); done
    printf 'ret add a1 add a2 add a3 add a4 add a5 add a6 add a7 add a8 add a9 add a10 add a11 add a12 add a13 add a14 add a15 add a16 a17\n'
    printf 'end\n'
} > "$T/reg.om"
set +e
"$S" < "$T/reg.om" > /dev/null 2> "$T/reg.err"
rc=$?
set -e
[ "$rc" = 12 ] || bad "e12_register_pool: exit $rc, wanted 12 ($(cat "$T/reg.err"))"

# 14: the 1025th global exceeds the name table.
{
    i=1
    while [ "$i" -le 1025 ]; do printf 'global g%s uchar 1\n' "$i"; i=$((i + 1)); done
    printf 'fn main to int\nret 0\nend\n'
} > "$T/cap.om"
set +e
"$S" < "$T/cap.om" > /dev/null 2> "$T/cap.err"
rc=$?
set -e
[ "$rc" = 14 ] || bad "e14_capacity: exit $rc, wanted 14 ($(cat "$T/cap.err"))"

# Control: a valid program must compile.
printf 'fn main to int\nret 0\nend\n' > "$T/ok.om"
set +e
"$S" < "$T/ok.om" > /dev/null 2> "$T/ok.err"
rc=$?
set -e
[ "$rc" = 0 ] || bad "valid control: exit $rc ($(cat "$T/ok.err"))"

say "PASS gate15 21 refusal cases (codes 3..23; 1 and 2 are compiler I/O) plus a valid control"
exit 0
