#!/bin/sh
# Experimental scalar IR checkpoint; not the completed R6 gate.
# Requires C-built omegese/omega_emit/checker2 as comparison oracles only.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/ir
mkdir -p "$B"
fail() { printf 'FAIL %s\n' "$*"; exit 1; }
seed/rung4/om4.bin < seed/omc/omc.om > "$B/stage1"
chmod +x "$B/stage1"
"$B/stage1" < seed/omc/omc.om > "$B/stage2"
chmod +x "$B/stage2"
"$B/stage2" < seed/omc/omc.om > "$B/stage3"
cmp "$B/stage1" "$B/stage2"
cmp "$B/stage2" "$B/stage3"
echo "PASS fixpoint $(sha256sum "$B/stage3")"
n=0
for f in tests/one_layer/omegese/dev/*.omegese; do
 id=$(basename "$f" .omegese)
 build/omegese/omegese emit "$f" "$B/$id.json"
 build/omega_emit/omega_emit emit "$B/$id.json" "$B/$id.oracle"
 { printf 'ir\n'; cat "$f"; } > "$B/$id.input"
 timeout 5 "$B/stage2" < "$B/$id.input" > "$B/$id.got"
 cmp "$B/$id.oracle" "$B/$id.got" || fail "$id byte mismatch"
 build/checker2/checker2 check "$B/$id.got" > "$B/$id.check"
 grep -q '^checker2=ACCEPT ' "$B/$id.check" || fail "$id checker refusal"
 echo "PASS $id identical; checker2 ACCEPT"
 n=$((n+1))
done
[ "$n" = 10 ] || fail "ran $n expected 10"
# Exercise the result comparison: change the arithmetic operation while preserving a valid program.
sed 's/ret mul a b/ret add a b/' tests/one_layer/omegese/dev/dev05.omegese > "$B/mut.om"
{ printf 'ir\n'; cat "$B/mut.om"; } > "$B/mut.input"
timeout 5 "$B/stage2" < "$B/mut.input" > "$B/mut.ir"
if cmp -s "$B/mut.ir" "$B/dev05.oracle"; then fail 'mutant did not change IR'; fi
echo 'PASS mul-to-add mutant differs'
# IR mode must refuse what it cannot lower, never skip a declaration silently.
ir_refuse() { # name expected-error source
    printf '%s\n' "$3" > "$B/$1.om"
    set +e
    timeout 5 "$B/stage2" < "$B/$1.om" > "$B/$1.out" 2> "$B/$1.err"
    crc=$?
    set -e
    [ "$crc" = "$2" ] || fail "$1: exit $crc, wanted $2"
    echo "PASS $1 refused $2"
}
ir_refuse struct_decl 9 'ir
struct S a uchar end
fn main to int
ret 0
end'
ir_refuse global_decl 9 'ir
global g uchar 4
fn main to int
ret 0
end'
ir_refuse text_decl 9 'ir
text t "x"
fn main to int
ret 0
end'
ir_refuse float_use 3 'ir
fn main to int
x = f64 1.0
ret 0
end'
ir_refuse continue_use 3 'ir
fn main to int
continue
end'
# Integer constants fold to the IR of their literal; float constants are still refused.
printf 'ir\nconst N uint 5\nfn f to uint\nret N\nend\n' > "$B/c1.om"
printf 'ir\nfn f to uint\nret uint 5\nend\n' > "$B/c2.om"
timeout 5 "$B/stage2" < "$B/c1.om" > "$B/c1.ir"
timeout 5 "$B/stage2" < "$B/c2.om" > "$B/c2.ir"
cmp -s "$B/c1.ir" "$B/c2.ir" || fail 'integer constant IR differs from its literal'
echo 'PASS integer constant folds to the literal IR'
# Constants in trap, bound and for positions fold to the same IR as literals.
ir_fold() { # name const-source literal-source
    printf '%b\n' "$2" > "$B/$1c.om"
    printf '%b\n' "$3" > "$B/$1l.om"
    timeout 5 "$B/stage2" < "$B/$1c.om" > "$B/$1c.ir"
    timeout 5 "$B/stage2" < "$B/$1l.om" > "$B/$1l.ir"
    cmp -s "$B/$1c.ir" "$B/$1l.ir" || fail "$1 constant IR differs from its literal"
    echo "PASS $1 constant folds to the literal IR"
}
ir_fold trap_const 'ir\nconst T uint 2\nfn f to uint\ntrap T\nend' 'ir\nfn f to uint\ntrap 2\nend'
ir_fold bound_const 'ir\nconst B ulong 3\nfn f to uint\nx = uint 0\nwhile lt x uint 2 bound B\nx = add x uint 1\nend\nret x\nend' 'ir\nfn f to uint\nx = uint 0\nwhile lt x uint 2 bound 3\nx = add x uint 1\nend\nret x\nend'
ir_fold for_const 'ir\nconst K ulong 4\nfn f to uint\nx = uint 0\nfor i ulong K\nx = add x uint 1\nend\nret x\nend' 'ir\nfn f to uint\nx = uint 0\nfor i ulong 4\nx = add x uint 1\nend\nret x\nend'
ir_refuse float_const 9 'ir
const H f64 0.1
fn f to f64
ret H
end'
ir_refuse call_self 3 'ir
fn f to uint
ret f
end'
ir_refuse call_forward 3 'ir
fn f to uint
ret g
end
fn g to uint
ret uint 1
end'
ir_refuse call_nonvoid_stmt 7 'ir
fn inc a uint to uint
ret add a uint 1
end
fn f to uint
inc uint 4
ret uint 2
end'
ir_refuse call_void_value 7 'ir
fn sink a uint to void
ret
end
fn f to uint
x = sink uint 1
ret uint 2
end'
ir_refuse call_arg_type 7 'ir
fn inc a uchar to uint
ret uint 1
end
fn f to uint
ret inc uint 4
end'
ir_refuse call_arg_local 7 'ir
fn inc a uchar to uint
ret uint 1
end
fn f to uint
x = uint 4
ret inc x
end'
ir_refuse call_result_type 7 'ir
fn inc a uint to uint
ret add a uint 1
end
fn f to uchar
ret inc uint 4
end'
ir_refuse break_outside 9 'ir
fn f to uint
break
end'
ir_refuse after_break 17 'ir
fn f to uint
while lt uint 1 uint 2 bound 3
break
ret uint 0
end
ret uint 0
end'
ir_refuse bin_type_local 7 'ir
fn f to uint
s = uint 1
i = ulong 2
x = add s i
ret uint 0
end'
ir_refuse bin_bool 7 'ir
fn f to uint
x = add bool true true
ret uint 0
end'
ir_refuse bin_shift_bool 7 'ir
fn f to ulong
ret shl ulong 1 true
end'
ir_refuse bin_result_type 7 'ir
fn f to uchar
ret add uint 1 uint 2
end'
ir_refuse cmp_assign_type 7 'ir
fn f to uint
x = uint 1
x = eq x x
ret x
end'
ir_refuse bool_result 7 'ir
fn f to uint
ret true
end'
ir_refuse neg_bool 7 'ir
fn f to uint
ret neg true
end'
ir_refuse cond_type 7 'ir
fn f to uint
x = uint 1
if x
ret 1
end
ret 0
end'
ir_refuse ret_type_local 7 'ir
fn f to uchar
x = uint 1
ret x
end'
ir_refuse neg_bool_local 7 'ir
fn f to uint
x = neg true
ret uint 0
end'

# Multi-function units with calls: byte-identical to the C oracle and checker2 ACCEPT.
ir_multi() { # name source
    printf '%b\n' "$2" > "$B/$1.omegese"
    build/omegese/omegese emit "$B/$1.omegese" "$B/$1.json"
    build/omega_emit/omega_emit emit "$B/$1.json" "$B/$1.oracle"
    { printf 'ir\n'; cat "$B/$1.omegese"; } > "$B/$1.input"
    timeout 5 "$B/stage2" < "$B/$1.input" > "$B/$1.got"
    cmp -s "$B/$1.oracle" "$B/$1.got" || fail "$1 multi-function IR differs"
    build/checker2/checker2 check "$B/$1.got" > "$B/$1.check"
    grep -q '^checker2=ACCEPT ' "$B/$1.check" || fail "$1 checker refusal"
    echo "PASS $1 IR identical; checker2 ACCEPT"
}
ir_multi call_value 'fn inc a uint to uint\nret add a uint 1\nend\nfn f to uint\nret inc uint 4\nend'
ir_multi call_void 'fn sink a uint to void\nret\nend\nfn f to uint\nsink uint 1\nret uint 2\nend'
ir_multi call_zero 'fn z to uint\nret uint 1\nend\nfn f to uint\nret z\nend'
ir_multi call_two 'fn add2 a uint b uint to uint\nret add a b\nend\nfn f to uint\nret add2 uint 3 uint 4\nend'
ir_multi call_long 'fn increment_one a uint to uint\nret add a uint 1\nend\nfn f to uint\nret increment_one uint 4\nend'
ir_multi call_nested 'fn inc a uint to uint\nret add a uint 1\nend\nfn f to uint\nret inc inc uint 4\nend'
ir_multi call_expr 'fn inc a uint to uint\nret add a uint 1\nend\nfn f to uint\nret add inc uint 1 inc uint 2\nend'
ir_multi call_loop 'fn add1 a uint to uint\nret add a uint 1\nend\nfn f to uint\nx = uint 0\nwhile lt x uint 3 bound 8\nx = add1 x\nend\nret x\nend'
ir_multi call_six 'fn sum6 a uint b uint c uint d uint e uint f uint to uint\nret add add add add add a b c d e f\nend\nfn f to uint\nret sum6 uint 1 uint 2 uint 3 uint 4 uint 5 uint 6\nend'
ir_multi break_while 'fn f to uint\nx = uint 0\nwhile lt x uint 10 bound 100\nif eq x uint 3\nbreak\nend\nx = add x uint 1\nend\nret x\nend'
ir_multi break_for 'fn f to uint\ns = uint 0\nfor i ulong 10\nif eq i ulong 3\nbreak\nend\ns = add s uint i\nend\nret s\nend'
ir_multi break_nested 'fn f to uint\nx = uint 0\nwhile lt x uint 5 bound 20\ny = uint 0\nwhile lt y uint 5 bound 20\nif eq y uint 2\nbreak\nend\ny = add y uint 1\nend\nx = add x y\nend\nret x\nend'
ir_multi shift_mixed 'fn f to ulong\nret shl ulong 1 uint 3\nend'
ir_multi cmp_assign 'fn f to bool\nx = lt uint 1 uint 2\nx = eq uint 1 uint 1\nret x\nend'
ir_multi cast_ret 'fn f to uchar\nret uchar uint 1\nend'
# Random two-function programs with a call: differential against the oracle.
rm -f "$B"/fz*
awk -v dir="$B" 'BEGIN{
    M = 65536;
    split("add sub mul xor and or", O, " ");
    split("eq ne lt le gt ge", C, " ");
    for (p = 0; p < 30; p++) {
        s = 1000 + p * 37;
        s = (s * 25173 + 13849) % M; o1 = O[1 + (s % 6)];
        s = (s * 25173 + 13849) % M; o2 = O[1 + (s % 6)];
        s = (s * 25173 + 13849) % M; o3 = O[1 + (s % 6)];
        s = (s * 25173 + 13849) % M; cm = C[1 + (s % 6)];
        s = (s * 25173 + 13849) % M; n1 = 1 + (s % 9);
        s = (s * 25173 + 13849) % M; n2 = 1 + (s % 9);
        s = (s * 25173 + 13849) % M; n3 = 1 + (s % 9);
        s = (s * 25173 + 13849) % M; n4 = 1 + (s % 9);
        s = (s * 25173 + 13849) % M; o4 = O[1 + (s % 6)];
        s = (s * 25173 + 13849) % M; n5 = 1 + (s % 9);
        f = sprintf("%s/fz%02d.omegese", dir, p);
        print "fn h a ulong b ulong to ulong" > f;
        print "x = " o1 " a b" > f;
        print "y = " o2 " x " n1 > f;
        print "if " cm " y " n2 > f;
        print "y = " o3 " y " n3 > f;
        print "end" > f;
        print "ret y" > f;
        print "end" > f;
        print "fn f to ulong" > f;
        print "p = ulong " n4 > f;
        print "q = ulong " n4 > f;
        print "r = h p q" > f;
        print "z = " o4 " r " n5 > f;
        print "ret z" > f;
        print "end" > f;
        close(f);
    }
}'
fzn=0
for f in "$B"/fz*.omegese; do
    fzn=$((fzn + 1))
    b=${f%.omegese}
    build/omegese/omegese emit "$f" "$b.json" > /dev/null
    build/omega_emit/omega_emit emit "$b.json" "$b.oracle" > /dev/null
    { printf 'ir\n'; cat "$f"; } > "$b.input"
    timeout 5 "$B/stage2" < "$b.input" > "$b.got"
    cmp -s "$b.oracle" "$b.got" || fail "fuzz $b differs from the oracle"
    build/checker2/checker2 check "$b.got" > "$b.check"
    grep -q '^checker2=ACCEPT ' "$b.check" || fail "fuzz $b checker refusal"
done
[ "$fzn" = 30 ] || fail "fuzz ran $fzn of 30 programs"
echo "PASS 30 random two-function call units identical; checker2 ACCEPT"
echo 'SCALAR_IR_CHECKPOINT 10/10 + 26 refusals + 9 multi-call units + 3 break units + 1 shift unit + 1 bool unit + 1 cast unit + 30 fuzz units; structs IR spec-blocked'
echo 'RC=0'
