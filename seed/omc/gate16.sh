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
length_local|7|const N uint 4\nfn main to int\na = new uint N 0\nput a 3 7\nret int get a 3\nend
length_global|4|const N uint 4\nglobal g uchar N\nfn main to int\nret int len g\nend
length_param|4|const N uint 4\nfn f a view uint N to int\nret int len a\nend\nfn main to int\nx = new uint N 0\nret f x\nend
float_bits|154|const H f64 0.1\nfn main to int\nret int rem bitcast ulong H ulong 256\nend
float32_bits|205|const H f32 0.1\nfn main to int\nret int rem bitcast uint H uint 256\nend
float_arith|0|const H f64 0.1\nfn main to int\nx = add H f64 0.2\nif eq x f64 0.3\nret 1\nend\nret 0\nend
struct_length|32|const N uint 4\nstruct S a uchar N end\nfn main to int\np = new S 0\nret int sizeof S\nend
RUN
[ "$n" = 15 ] || bad "value table ran $n of 15 cases"

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
ir_refused|9|ir\nconst N uint 5\nfn f to uint\nret N\nend
extra_word|5|const N uint 5 6\nfn main to int\nret 0\nend
long_name|13|const abcdefghi uint 5\nfn main to int\nret 0\nend
reserved|16|const while uint 5\nfn main to int\nret 0\nend
missing_value|4|const N uint\nfn main to int\nret 0\nend
inside_fn|16|fn main to int\nconst N uint 5\nret 0\nend
use_before|3|fn f to int\nret int N\nend\nconst N int 9\nfn main to int\nret f\nend
len_zero|8|const N uint 0\nfn main to int\na = new uint N 0\nret 0\nend
len_big|23|const N uint 16777217\nfn main to int\na = new uint N 0\nret 0\nend
len_varname|16|fn main to int\nn = uint 4\na = new uint n 0\nret 0\nend
len_after|16|fn f to int\na = new uint N 0\nret 0\nend\nconst N uint 4\nfn main to int\nret f\nend
float_as_length|16|const H f64 0.1\nfn main to int\na = new uchar H 0\nret 0\nend
float_int_literal|16|const H f64 5\nfn main to int\nret 0\nend
struct_length_big|14|const N uint 65\nstruct S a uchar N end\nfn main to int\nret 0\nend
REF
[ "$n" = 17 ] || bad "refusal table ran $n of 17 cases"

say "PASS gate16 15 constant programs and 17 refusals"
exit 0
