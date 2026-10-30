#!/bin/sh
# gate15.sh -- refusal-code conformance and the error-code registry (issue #797).
# Every emitted or io code in seed/omc/error-codes.reg has at least one minimal case here: codes 1..25
# and 27..30 (26 has no emitting site), including the IR-mode contract refusal (#791). Each case must
# exit with exactly its code and print exactly `omc: line L: error C` on stderr; a valid control must
# compile. Each refusal appends `NAME CODE` to a ran-log. seed/omc/codereg.om, compiled by the same
# omc, then checks the registry against omc.om, irwasm.om, the docs/one_layer/OMEGESE.md tables
# (sections 9.14 and 11.1) and the ran-log. Built-in negative controls (a changed registry meaning, a
# changed OMEGESE row, a dropped ran-log case) must each make codereg fail.
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
: > "$T/ran.log"
n=0

# expect NAME CODE LINE: the last compile ($T/p.err, exit $rc) refused exactly so.
expect() {
    [ "$rc" = "$2" ] || bad "$1: exit $rc, wanted $2 ($(cat "$T/p.err"))"
    [ "$(cat "$T/p.err")" = "omc: line $3: error $2" ] ||
        bad "$1: stderr '$(cat "$T/p.err")', wanted 'omc: line $3: error $2'"
    printf '%s %s\n' "$1" "$2" >> "$T/ran.log"
    n=$((n + 1))
}
# run FILE [OUT]: compile FILE, stdout to OUT (default $T/p.bin).
run() {
    set +e
    "$S" < "$1" > "${2:-$T/p.bin}" 2> "$T/p.err"
    rc=$?
    set -e
}

while IFS='|' read -r name want line src; do
    [ -n "$name" ] || continue
    printf '%b\n' "$src" > "$T/p.om"
    run "$T/p.om"
    expect "$name" "$want" "$line"
done <<'CASES'
e3_unknown_name|3|2|fn f to int\nret q\nend
e4_line_ends|4|2|struct S a uint\nfn main to int\nret 0\nend
e5_extra_words|5|2|fn f to int\nret 0 extra\nend
e6_bad_number|6|2|fn f to int\nx = 12a\nret 0\nend
e7_type_mismatch|7|2|fn f to int\nx = add int 1 uint 2\nret 0\nend
e8_out_of_range|8|2|fn f to int\nx = int 2147483648\nret 0\nend
e9_break_outside|9|2|fn f to int\nbreak\nret 0\nend
e10_duplicate_param|10|1|fn f x int x int to int\nret 0\nend
e11_ambiguous|11|2|fn f to int\nx = 5\nret 0\nend
e13_long_variable|13|2|fn f to int\nabcdefghi = int 1\nret 0\nend
e15_missing_end|15|3|fn f to int\nret 0
e16_syntax|16|1|wat
e17_after_path|17|3|fn f to int\nret 0\nret 1\nend
e18_missing_ret|18|3|fn f to int\nend
e19_global_passed|19|5|global g uchar 4\nfn f a edit uchar 4 to void\nend\nfn main to int\nf g\nret 0\nend
e20_readonly|20|3|text t "abc"\nfn main to int\nput t 0 1\nret 0\nend
e21_const_index|21|3|fn main to int\na = new uint 4 0\nput a 4 1\nret 0\nend
e22_text_literal|22|1|text t "a\\q"\nfn main to int\nret 0\nend
e23_size_capacity|23|2|fn main to int\na = new uchar 16777217 0\nret 0\nend
e25_target|25|1|target arm64li\nfn main to int\nret 0\nend
e27_fresh_get|27|3|om2\nfn f s edit uchar 8 fresh to ulong\nret ulong get s 0\nend\nfn main to int\nret 0\nend
e28_seal_outside|28|4|om2\nabstract h uint\nfn main to int\nx = seal h uint 1\nret 0\nend
e28_holds_dup32|28|3|om2\nabstract h uint\nholds h dup32\nfn main to int\nret 0\nend
e29_layout_past|29|3|om2\nlayout z 8\nfield a ulong at 1 little\nend\nfn main to int\nret 0\nend
e30_contract_after_ret|30|4|om2\nfn f x uint to uint\nret x\nrequires lt x 10\nend\nfn main to int\nret 0\nend
e30_ir_requires|30|3|ir\nfn f x uint to uint\nrequires lt x 10\nret x\nend
e30_ir_ensures|30|3|ir\nfn f x uint to uint\nensures lt x 10\nret x\nend
e30_ir_invariant|30|3|ir\nfn f x uint to uint\ninvariant lt x 10\nret x\nend
CASES

# Valid control, also the program for the output-error case.
printf 'fn main to int\nret 0\nend\n' > "$T/ok.om"
run "$T/ok.om"
[ "$rc" = 0 ] || bad "valid control: exit $rc ($(cat "$T/p.err"))"

# 1: stdin is a directory, so the first read fails before any line.
mkdir "$T/dir"
run "$T/dir"
expect e1_input_unreadable 1 0
# 2: stdout is a full device; the write happens after the last line.
if [ -w /dev/full ]; then
    run "$T/ok.om" /dev/full
    expect e2_output_unwritable 2 4
else
    bad "e2_output_unwritable: /dev/full is not writable here"
fi

# 12: the 17th live name exhausts the 16-register pool.
{
    printf 'fn f to int\n'
    i=1
    while [ "$i" -le 17 ]; do printf 'a%s = int %s\n' "$i" "$i"; i=$((i + 1)); done
    printf 'ret add a1 add a2 add a3 add a4 add a5 add a6 add a7 add a8 add a9 add a10 add a11 add a12 add a13 add a14 add a15 add a16 a17\n'
    printf 'end\n'
} > "$T/p.om"
run "$T/p.om"
expect e12_register_pool 12 18

# 14: the 8193rd global exceeds the name table.
{
    i=1
    while [ "$i" -le 8193 ]; do printf 'global g%s uchar 1\n' "$i"; i=$((i + 1)); done
    printf 'fn main to int\nret 0\nend\n'
} > "$T/p.om"
run "$T/p.om"
expect e14_capacity 14 8193

# 24: an `if` body of N one-word lines. N=262142 keeps the B.cond displacement in range (accept);
# N=262143 does not, and the refusal names the `end` that closes the `if` (line N+4).
br() {
    { printf 'fn main to int\nx = uint 0\nif eq x 1\n'; yes 'x = add x 1' | head -n "$1"; printf 'end\nret 0\nend\n'; } > "$T/p.om"
}
br 262142
run "$T/p.om"
[ "$rc" = 0 ] || bad "e24 accept side (262142 lines): exit $rc ($(cat "$T/p.err"))"
br 262143
run "$T/p.om"
expect e24_branch_range 24 262147

[ "$n" = 33 ] || bad "ran $n of 33 refusal cases"

# The registry drift check, compiled by the same omc.
run seed/omc/codereg.om "$T/codereg"
[ "$rc" = 0 ] || bad "codereg.om did not compile: $(cat "$T/p.err")"
chmod +x "$T/codereg"
REG=seed/omc/error-codes.reg
DOC=docs/one_layer/OMEGESE.md
SRC=seed/omc/omc.om
IRW=seed/wasm/irwasm.om
"$T/codereg" "$REG" "$SRC" "$DOC" "$IRW" "$T/ran.log" > "$T/cr.out" 2>&1 || bad "codereg: $(cat "$T/cr.out")"
say "$(cat "$T/cr.out")"

# Negative controls: each mutation must make codereg fail.
neg() { # name registry omegese ranlog
    if "$T/codereg" "$2" "$SRC" "$3" "$IRW" "$4" > "$T/neg.out" 2>&1; then
        bad "negative control $1: codereg accepted it"
    fi
    say "PASS negative control $1: $(head -n 1 "$T/neg.out")"
}
sed 's/^omc|7|emitted|e7_type_mismatch|type mismatch$/omc|7|emitted|e7_type_mismatch|type mismatch or worse/' "$REG" > "$T/reg.mut"
cmp -s "$REG" "$T/reg.mut" && bad "negative control registry-meaning: sed changed nothing"
neg registry-meaning "$T/reg.mut" "$DOC" "$T/ran.log"
sed 's/^| omc 7 | type mismatch |$/| omc 7 | type mismatch or worse |/' "$DOC" > "$T/doc.mut"
cmp -s "$DOC" "$T/doc.mut" && bad "negative control omegese-row: sed changed nothing"
neg omegese-row "$REG" "$T/doc.mut" "$T/ran.log"
grep -v '^e30_ir_requires 30$' "$T/ran.log" > "$T/ran.mut" || true
cmp -s "$T/ran.log" "$T/ran.mut" && bad "negative control dropped-case: grep removed nothing"
neg dropped-case "$REG" "$DOC" "$T/ran.mut"

say "PASS gate15 $n refusal cases (codes 1..25, 27..30; 24 with its accept side) plus a valid control, registry drift check and 3 negative controls"
exit 0
