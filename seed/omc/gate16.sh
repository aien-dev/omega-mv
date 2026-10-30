#!/bin/sh
# gate16.sh -- named integer constants. `const NAME TYPE INTEGER` is a top-level typed value, visible
# to later functions only (define before use). The first table runs programs and compares exit status;
# the second requires a compile refusal with the exact code (including function mode, where a global,
# a text or a host word stays error 9); the third compiles function-mode units (no `fn main`) with
# constants and requires words byte-identical to the same unit written with literals.
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
const_bound|2|const B ulong 3\nfn main to int\nx = ulong 0\nwhile lt x ulong 2 bound B\nx = add x 1\nend\nret int x\nend
trap_const|102|const T uint 2\nfn main to int\ntrap T\nend
for_const|4|const K ulong 4\nfn main to int\nx = ulong 0\nfor i ulong K\nx = add x 1\nend\nret int x\nend
const_order|12|const kk ulong 4\nfn a fs edit long kk to void\nput fs 0 1\nend\nconst k2 ulong 3\nfn b fs edit long kk gs edit long k2 to long\nput fs 3 7\nput gs 2 5\nret add get fs 3 get gs 2\nend\nfn main to int\nx = new long kk 0\ny = new long k2 0\na x\nret int b x y\nend
RUN
[ "$n" = 19 ] || bad "value table ran $n of 19 cases"

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
ir_float_const|9|ir\nconst H f64 0.1\nfn f to f64\nret H\nend
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
float_bound|16|const B f64 0.5\nfn main to int\nwhile eq int 1 int 1 bound B\nend\nret 0\nend
trap_range|8|const T uint 0\nfn main to int\ntrap T\nend
fm_global|9|const N ulong 5\nglobal g ulong 4\nfn f a ulong to ulong\nret add a N\nend
fm_text|9|const N ulong 5\ntext t "hi"\nfn f a ulong to ulong\nret add a N\nend
fm_global_only|9|global g ulong 4\nfn f a ulong to ulong\nret a\nend
fm_text_only|9|text t "hi"\nfn f a ulong to ulong\nret a\nend
fm_host_word|9|const N ulong 5\nfn f a ulong to long\nx = getpid\nret x\nend
fm_trap_range|8|const T uint 15\nfn f a ulong to ulong\ntrap T\nend
fm_float_length|16|const H f64 0.1\nfn f a view uchar H to ulong\nret 0\nend
fm_assign|7|const N ulong 5\nfn f a ulong to ulong\nN = ulong 6\nret a\nend
REF
[ "$n" = 27 ] || bad "refusal table ran $n of 27 cases"

# Function mode (no `fn main`): an integer or float constant folds to its literal, so the bare function
# words must be byte-identical to the same unit written with literals. Each line: name|const unit|literal unit.
n=0
while IFS='|' read -r name csrc lsrc; do
    [ -n "$name" ] || continue
    n=$((n + 1))
    printf '%b\n' "$csrc" > "$T/c.om"
    printf '%b\n' "$lsrc" > "$T/l.om"
    set +e
    "$S" < "$T/c.om" > "$T/c.bin" 2> "$T/c.err"
    rc=$?
    "$S" < "$T/l.om" > "$T/l.bin" 2> "$T/l.err"
    lrc=$?
    set -e
    [ "$rc" = 0 ] || bad "$name: const unit compile $rc: $(cat "$T/c.err")"
    [ "$lrc" = 0 ] || bad "$name: literal unit compile $lrc: $(cat "$T/l.err")"
    [ -s "$T/c.bin" ] || bad "$name: no function words"
    cmp -s "$T/c.bin" "$T/l.bin" || bad "$name: const words differ from literal words"
done <<'FM'
fm_value|const AB ulong 5\nfn top a ulong to ulong\nret add a AB\nend|fn top a ulong to ulong\nret add a 5\nend
fm_negative|const M long -3\nfn top a long to long\nret mul a M\nend|fn top a long to long\nret mul a -3\nend
fm_param_len|const N ulong 4\nfn f a view ulong N to ulong\nret get a sub N 1\nend|fn f a view ulong 4 to ulong\nret get a sub ulong 4 1\nend
fm_new_len|const N uint 4\nfn f k uint to uint\nb = new uint N k\nret get b 3\nend|fn f k uint to uint\nb = new uint 4 k\nret get b 3\nend
fm_bound|const B ulong 3\nfn f a ulong to ulong\nx = ulong 0\nwhile lt x a bound B\nx = add x 1\nend\nret x\nend|fn f a ulong to ulong\nx = ulong 0\nwhile lt x a bound 3\nx = add x 1\nend\nret x\nend
fm_trap|const T uint 5\nfn f a ulong to ulong\nif eq a 1\ntrap T\nend\nret a\nend|fn f a ulong to ulong\nif eq a 1\ntrap 5\nend\nret a\nend
fm_for|const K ulong 4\nfn f a ulong to ulong\nx = a\nfor i ulong K\nx = add x i\nend\nret x\nend|fn f a ulong to ulong\nx = a\nfor i ulong 4\nx = add x i\nend\nret x\nend
fm_two_fns|const N ulong 7\nfn g a ulong to ulong\nret add a N\nend\nfn f a ulong to ulong\nret g mul a N\nend|fn g a ulong to ulong\nret add a 7\nend\nfn f a ulong to ulong\nret g mul a 7\nend
fm_float|const H f64 0.1\nfn f a f64 to f64\nret add a H\nend|fn f a f64 to f64\nret add a f64 0.1\nend
FM
[ "$n" = 9 ] || bad "function-mode table ran $n of 9 cases"

say "PASS gate16 19 constant programs, 27 refusals and 9 function-mode units"
exit 0
