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
ir_refuse float_const 9 'ir
const H f64 0.1
fn f to f64
ret H
end'

echo 'SCALAR_IR_CHECKPOINT 10/10 + 7 refusals; structs/multifunction IR still open'
echo 'RC=0'
