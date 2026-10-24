#!/bin/sh
# gate16.sh -- named integer constants. `const NAME TYPE INTEGER` is a top-level typed value, visible
# to later functions only (define before use). The first table runs programs and compares exit status;
# the second requires a compile refusal with the exact code.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
S=seed/out/omc
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
mkdir -p build/seed/r16
T=$(mktemp -d build/seed/r16/const.XXXXXX)
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
    [ "$rc" = 0 ] || bad "$name: compile $rc: $(cat "$T/p.err")"
    chmod +x "$T/p.bin"
    set +e
    "$T/p.bin" > /dev/null 2>&1
    got=$?
    set -e
    [ "$got" = "$want" ] || bad "$name: exit $got, wanted $want"
done <<'RUN'
basic|5|const N uint 5\nfn main to int\nret int N\nend
negative|253|const M long -3\nfn main to int\nret int M\nend
expression|6|const N uint 4\nfn main to int\nx = add N uint 2\nret int x\nend
call_argument|8|const K int 7\nfn f a int to int\nret add a K\nend\nfn main to int\nret f int 1\nend
bool_flag|3|const B bool 1\nfn main to int\nif B\nret 3\nend\nret 4\nend
ulong_max|255|const U ulong 18446744073709551615\nfn main to int\nret int rem U ulong 256\nend
long_min|0|const L long -9223372036854775808\nfn main to int\nret int rem L long 256\nend
between_fns|9|fn f to int\nret 1\nend\nconst N int 9\nfn main to int\nret N\nend
RUN
[ "$n" = 8 ] || bad "value table ran $n of 8 cases"

n=0
while IFS='|' read -r name want src; do
    [ -n "$name" ] || continue
    n=$((n + 1))
    printf '%b\n' "$src" > "$T/p.om"
    set +e
    "$S" < "$T/p.om" > /dev/null 2> "$T/p.err"
    rc=$?
    set -e
    [ "$rc" = "$want" ] || bad "$name: exit $rc, wanted $want ($(cat "$T/p.err"))"
    grep -q "error $want\$" "$T/p.err" || bad "$name: stderr lacks error $want"
done <<'REF'
assign|7|const N uint 5\nfn main to int\nN = uint 6\nret 0\nend
duplicate|10|const N uint 5\nconst N uint 6\nfn main to int\nret 0\nend
out_of_range|8|const N uchar 300\nfn main to int\nret 0\nend
float_refused|9|const N f64 1.5\nfn main to int\nret 0\nend
ir_refused|9|ir\nconst N uint 5\nfn f to uint\nret N\nend
extra_word|5|const N uint 5 6\nfn main to int\nret 0\nend
long_name|13|const abcdefghi uint 5\nfn main to int\nret 0\nend
reserved|16|const while uint 5\nfn main to int\nret 0\nend
missing_value|4|const N uint\nfn main to int\nret 0\nend
inside_fn|16|fn main to int\nconst N uint 5\nret 0\nend
use_before|3|fn f to int\nret int N\nend\nconst N int 9\nfn main to int\nret f\nend
REF
[ "$n" = 11 ] || bad "refusal table ran $n of 11 cases"

say "PASS gate16 8 constant programs and 11 refusals"
exit 0
