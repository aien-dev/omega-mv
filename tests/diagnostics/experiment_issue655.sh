#!/bin/bash
# tests/diagnostics/experiment_issue655.sh
# Empirical experiment for issue #655: incremental compiler feedback for .om generation.
# Tests 10 held-out .om programs with known-good versions and deliberately broken drafts.
# Verifies:
#   1. Known-good programs yield zero error records (negative control: output is solely "omega-diag v1", exit code 0).
#   2. Deliberately broken programs yield exact diagnostics locating the first error span and matching the compiler refusal code.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
OMC="${OMC:-$ROOT/seed/out/omc}"
[ -x "$OMC" ] || { echo "FAIL: $OMC not found or not executable" >&2; exit 1; }

PASS_COUNT=0
TOTAL=39

echo "=== Issue #655 Milestone A Diagnostic Precision Experiment ==="
printf "%-14s | %-12s | %-8s | %-9s | %-14s | %s\n" "Task" "Good Result" "Bad Exit" "Span" "Span Text" "Diagnostic"
echo "---------------------------------------------------------------------------------------------------------"

run_case() {
    local name="$1"
    local good="$2"
    local bad="$3"
    local exp_text="$4"
    local exp_code="$5"

    # 1. Good program (negative control)
    local out_good
    local rc_good=0
    out_good=$(printf "check\n%s" "$good" | "$OMC" 2>&1) || rc_good=$?
    if [ "$rc_good" -ne 0 ] || [ "$out_good" != "omega-diag v1" ]; then
        echo "FAIL negative control on $name: rc=$rc_good out=$out_good" >&2
        return 1
    fi

    # 2. Broken draft
    local out_bad
    local rc_bad=0
    out_bad=$(printf "check\n%s" "$bad" | "$OMC" 2>&1) || rc_bad=$?
    if [ "$rc_bad" -ne "$exp_code" ]; then
        echo "FAIL bad exit code on $name: expected $exp_code, got $rc_bad" >&2
        return 1
    fi

    # Extract diagnostic record (line 2 of output)
    local diag_line
    diag_line=$(echo "$out_bad" | grep -E '^om[12] ' || true)
    if [ -z "$diag_line" ]; then
        echo "FAIL missing om record on $name: $out_bad" >&2
        return 1
    fi

    # Extract span
    local span
    span=$(echo "$diag_line" | grep -o 'span=[0-9]*-[0-9]*' | cut -d= -f2)
    local a b
    a=$(echo "$span" | cut -d- -f1)
    b=$(echo "$span" | cut -d- -f2)

    # Extract span text using python
    local span_text
    span_text=$(python3 -c "import sys; src = sys.stdin.read(); a, b = int(sys.argv[1]), int(sys.argv[2]); print(repr(src[a:b]))" "$a" "$b" <<< "$bad")

    local exp_repr
    exp_repr=$(python3 -c "import sys; print(repr(sys.argv[1]))" "$exp_text")

    if [ "$span_text" != "$exp_repr" ]; then
        echo "FAIL span text mismatch on $name: expected $exp_repr, got $span_text" >&2
        return 1
    fi
    local category
    case "$exp_code" in
        3|10|13) category=NAME ;;
        4|5|6|9|15|16|22) category=SYNTAX ;;
        7|11|19) category=TYPE ;;
        8|21) category=RANGE ;;
        12|14|23|24) category=CAPACITY ;;
        17|18) category=FLOW ;;
        27|28|29|30) category=CONTRACT ;;
        *) echo "FAIL unmapped diagnostic code $exp_code" >&2; return 1 ;;
    esac
    if [[ "$diag_line" != "om1 $category "* ]]; then
        echo "FAIL category on $name: expected $category, got $diag_line" >&2
        return 1
    fi
    # Finality and repairability are independent; these edited drafts are not capacity failures.
    if [[ "$diag_line" != *" REPAIRABLE code=$exp_code "* ]] || [[ "$diag_line" == *"  "* ]]; then
        echo "FAIL repairability or spacing on $name: $diag_line" >&2
        return 1
    fi

    printf "%-14s | %-12s | %-8s | %-9s | %-14s | %s\n" "$name" "PASS (0 err)" "$rc_bad" "$span" "$span_text" "$diag_line"
    PASS_COUNT=$((PASS_COUNT + 1))
}
run_next_case() {
    local name="$1"
    local input="$2"
    local expected="$3"

    local out
    local rc=0
    out=$(printf "next\n%s" "$input" | "$OMC" 2>&1) || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL next exit code on $name: expected 0, got $rc" >&2
        echo "$out" >&2
        return 1
    fi

    if [ "$out" != "$expected" ]; then
        echo "FAIL next output mismatch on $name:" >&2
        diff -u <(echo "$expected") <(echo "$out") >&2 || true
        return 1
    fi

    printf "%-14s | %-12s | %-8s | %-9s | %-14s | %s\n" "$name" "PASS (query)" "0" "-" "-" "prefix query matches expected"
    PASS_COUNT=$((PASS_COUNT + 1))
}

run_next_neg_control() {
    local name="$1"
    local input="$2"
    local expected="$3"
    local corrupted="$4"

    local out
    local rc=0
    out=$(printf "next\n%s" "$input" | "$OMC" 2>&1) || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL next exit code on $name: expected 0, got $rc" >&2
        return 1
    fi

    if [ "$out" != "$expected" ]; then
        echo "FAIL next output should match expected on $name" >&2
        return 1
    fi

    if [ "$out" = "$corrupted" ]; then
        echo "FAIL negative control on $name: corrupted expectation was not detected" >&2
        return 1
    fi

    printf "%-14s | %-12s | %-8s | %-9s | %-14s | %s\n" "$name" "PASS (negctrl)" "0" "-" "-" "detected altered expected line"
    PASS_COUNT=$((PASS_COUNT + 1))
}

# 1. clamp_sub
run_case "clamp_sub" \
"fn clamp_sub a uint b uint to uint
if lt a b
ret 0
end
ret sub a b
end
" \
"fn clamp_sub a uint b uint to uint
if lt a b
ret true
end
ret sub a b
end
" \
"true" 7

# 2. narrow_sum
run_case "narrow_sum" \
"fn narrow_sum a short b short to byte
ret byte add a b
end
" \
"fn narrow_sum a short b short to byte
ret byte add a
end
" \
$'\n' 4

# 3. power_mod
run_case "power_mod" \
"fn power_mod b uint e uint m uint to uint
r = uint 1
base = rem b m
while gt e 0
if eq rem e 2 1
r = rem mul r base m
end
base = rem mul base base m
e = div e 2
end
ret r
end
" \
"fn power_mod b uint e uint m uint to uint
r = 1
base = rem b m
while gt e 0
if eq rem e 2 1
r = rem mul r base m
end
base = rem mul base base m
e = div e 2
end
ret r
end
" \
"1" 11

# 4. tri_kind
run_case "tri_kind" \
"fn tri_kind a int b int c int to int
if eq a b
ret 1
end
ret 0
end
" \
"fn tri_kind a int b int c int to int
if eq a
ret 1
end
ret 0
end
" \
$'\n' 4

# 5. t_conv
run_case "t_conv" \
"fn main to int
x = ulong 3000000000
ret int x
end
" \
"fn main to int
x = ulong 3000000000
ret int
end
" \
$'\n' 4

# 6. sort
run_case "sort" \
"fn sorted a view ulong 10 to ulong
bad = ulong 0
for i ulong 9
if gt get a i get a add i 1
bad = add bad 1
end
end
ret bad
end
" \
"fn sorted a view ulong 10 to ulong
bad = ulong 0 1
for i ulong 9
if gt get a i get a add i 1
bad = add bad 1
end
end
ret bad
end
" \
"1" 5

# 7. bytecopy
run_case "bytecopy" \
"fn copy_one a uchar to uchar
ret a
end
" \
"fn copy_one a uchar to uchar
ret 1000
end
" \
"1000" 8

# 8. echo
run_case "echo" \
"fn echo_val v long to long
ret v
end
" \
"fn echo_val v long to long
break
ret v
end
" \
"break" 9

# 9. sum_squares
run_case "sum_squares" \
"fn sum_sq a int b int to int
x = mul a a
y = mul b b
ret add x y
end
" \
"fn sum_sq a int b int to int
x = mul a unknown_var
y = mul b b
ret add x y
end
" \
"unknown_var" 13

# 10. t_bounds
run_case "t_bounds" \
"fn check_bound x int to int
if lt x 0
trap 1
end
ret x
end
" \
"fn check_bound x int to int
if lt x 0
trap 999
end
ret x
end
" \
"999" 8
# 11. next_while_loop (loop constructs break/continue, local variable, signature)
run_next_case "next_while" \
"fn main to int
a = ulong 3
while lt a 9
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note break continue
names:
a ulong local
functions:
fn main to int"

# 12. next_params (bool param vs int local, non-loop constructs, return void bool)
run_next_case "next_params" \
"fn compute c bool to void
val = int 42
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note
names:
c bool param
val int local
functions:
fn compute to void bool"

# 13. next_sig_multi (multiple typed params and locals, multi-arg signature)
run_next_case "next_sig_multi" \
"fn add_scaled x uint y uint to uint
s = uint 2
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note
names:
x uint param
y uint param
s uint local
functions:
fn add_scaled to uint uint uint"

# 14. next_toplevel (toplevel scope, global and const declarations)
run_next_case "next_toplevel" \
"global g_buf uchar 16
const MAX_LEN ulong 1024
" \
"omega-query v1
scope: toplevel
constructs: fn const global text struct note
names:
g_buf uchar global
MAX_LEN ulong const
functions:"

# 15. next_for_loop (for loop allows break and continue, loop index is local)
run_next_case "next_for_loop" \
"fn loop_test to int
sum = int 0
for i ulong 10
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note break continue
names:
sum int local
i ulong local
functions:
fn loop_test to int"

# 16. next_om2_func (om2 profile, completed preceding function and active function)
run_next_case "next_om2_func" \
"om2
fn helper x ulong to ulong
ret x
end
fn caller a ulong to ulong
b = ulong 1
while lt b 10
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note break continue
names:
a ulong param
b ulong local
functions:
fn helper to ulong ulong
fn caller to ulong ulong"

# 17. next_om2_view (om2 view parameter kind)
run_next_case "next_om2_view" \
"om2
fn process buf view uchar 32 to void
n = ulong 0
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note
names:
buf uchar view
n ulong local
functions:
fn process to void uchar"

# 18. next_neg_control (negative control: fails if expected output has wrong kind)
run_next_neg_control "next_neg_ctrl" \
"fn main to int
a = ulong 3
while lt a 9
" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note break continue
names:
a ulong local
functions:
fn main to int" \
"omega-query v1
scope: function
constructs: ret if while for trap exit put end note break continue
names:
a ulong param
functions:
fn main to int"

# qty must be preserved in names and function signatures, not reported as view.
run_next_case "next_qty" \
$'fn grow n qty to qty\nv = qty 1\n' \
$'omega-query v1\nscope: function\nconstructs: ret if while for trap exit put end note\nnames:\nn qty param\nv qty local\nfunctions:\nfn grow to qty qty'

run_check_record() {
    local name="$1" input="$2" code="$3" expected="$4" out rc=0
    out=$(printf "check\n%s" "$input" | "$OMC" 2>/dev/null) || rc=$?
    if [ "$rc" -ne "$code" ] || [ "$out" != "$expected" ]; then
        echo "FAIL check record on $name: rc=$rc out=$out" >&2
        return 1
    fi
    printf '%s | PASS (exact check record)\n' "$name"
    PASS_COUNT=$((PASS_COUNT + 1))
}

run_check_record "check_final" $'fn f to int\nret true\nend\n' 7 \
$'omega-diag v1\nom1 TYPE span=16-20 FINAL REPAIRABLE code=7 line=2 expect=value found=true'
run_check_record "check_unknown" $'fn f to int\nret missing\nend\n' 3 \
$'omega-diag v1\nom1 NAME span=16-23 UNKNOWN REPAIRABLE code=3 line=2 expect=name found=missing'
run_check_record "check_incomplete" $'fn f to int\n' 15 \
$'omega-diag v1\nom1 SYNTAX span=12-12 INCOMPLETE REPAIRABLE code=15 line=2 expect=end found=eof'

# Register exhaustion is still UNREPAIRABLE, regardless of finality.
capacity=$'fn f to int\n'
for i in {1..17}; do capacity+="v$i = int 0"$'\n'; done
capacity+=$'ret 0\nend\n'
run_check_record "check_capacity" "$capacity" 12 \
$'omega-diag v1\nom1 CAPACITY span=201-204 FINAL UNREPAIRABLE code=12 line=18 expect=word found=int'

# DIAG-1: exact records from the category audit, with corrected wire categories.
run_check_record "check_range" $'fn f to uchar\nret 1000\nend\n' 8 \
$'omega-diag v1\nom1 RANGE span=18-22 FINAL REPAIRABLE code=8 line=2 expect=number found=1000'
run_check_record "check_flow" $'fn f to int\nend\n' 18 \
$'omega-diag v1\nom1 FLOW span=15-16 FINAL REPAIRABLE code=18 line=3 expect=newline found=0a'
run_check_record "check_size" $'global g uchar 16777217\nfn main to int\nret 0\nend\n' 23 \
$'omega-diag v1\nom1 CAPACITY span=15-23 FINAL UNREPAIRABLE code=23 line=1 expect=type found=16777217'
run_check_record "check_dead" $'fn f to int\nret 0\nret 1\nend\n' 17 \
$'omega-diag v1\nom1 FLOW span=18-21 FINAL REPAIRABLE code=17 line=3 expect=word found=ret'

# Cover the remaining readily constructed category mappings, independently of spans.
run_category_case() {
    local name="$1" code="$2" category="$3" input="$4" out rc=0
    out=$(printf 'check\n%s' "$input" | "$OMC" 2>/dev/null) || rc=$?
    if [ "$rc" -ne "$code" ] ||
       [[ "$out" != $'omega-diag v1\nom1 '"$category "* ]] ||
       [[ "$out" != *" code=$code "* ]] ||
       [ "$(printf '%s\n' "$out" | wc -l)" -ne 2 ]; then
        echo "FAIL category on $name: rc=$rc out=$out" >&2
        return 1
    fi
    PASS_COUNT=$((PASS_COUNT + 1))
}
run_category_case "category_number" 6 SYNTAX $'fn f to int\nx = 12a\nret 0\nend\n'
run_category_case "category_duplicate" 10 NAME $'fn f x int x int to int\nret 0\nend\n'
run_category_case "category_syntax" 16 SYNTAX $'wat\n'
run_category_case "category_alias" 19 TYPE $'global g uchar 4\nfn f a edit uchar 4 to void\nend\nfn main to int\nf g\nret 0\nend\n'
run_category_case "category_index" 21 RANGE $'fn main to int\na = new uint 4 0\nput a 4 1\nret 0\nend\n'
run_category_case "category_text" 22 SYNTAX $'text t "a\\q"\nfn main to int\nret 0\nend\n'
table_capacity=''
for ((i=1; i<=8193; i++)); do table_capacity+="global g$i uchar 1"$'\n'; done
table_capacity+=$'fn main to int\nret 0\nend\n'
run_category_case "category_table" 14 CAPACITY "$table_capacity"

# DIAG-2: compare every byte and record, including the final newline. The small
# control uses the same legal function names without crossing the old msg pool.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
for count in 10 1200; do
    printf 'next\n' > "$scratch/query.om"
    printf 'omega-query v1\nscope: toplevel\nconstructs: fn const global text struct note\nnames:\nfunctions:\n' > "$scratch/query.want"
    for ((i=0; i<count; i++)); do
        name="function_${i}_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        printf 'fn %s to int\nret 0\nend\n' "$name" >> "$scratch/query.om"
        printf 'fn %s to int\n' "$name" >> "$scratch/query.want"
    done
    rc=0
    "$OMC" < "$scratch/query.om" > "$scratch/query.out" 2> "$scratch/query.err" || rc=$?
    if [ "$rc" -ne 0 ] || [ -s "$scratch/query.err" ] ||
       ! cmp -s "$scratch/query.want" "$scratch/query.out" ||
       [ "$(grep -c '^fn ' "$scratch/query.out")" -ne "$count" ]; then
        echo "FAIL next complete records count=$count rc=$rc" >&2
        diff -u "$scratch/query.want" "$scratch/query.out" >&2 || true
        exit 1
    fi
    PASS_COUNT=$((PASS_COUNT + 1))
done

# DIAG-3: each failed-output probe has an identical successful-output control.
# Ordinary compilation is the independent pre-existing error-2 control.
[ -c /dev/full ] || { echo "FAIL: /dev/full unavailable" >&2; exit 1; }
for mode in check next normal; do
    {
        [ "$mode" = normal ] || printf '%s\n' "$mode"
        printf 'fn main to int\nret 0\nend\n'
    } > "$scratch/io.om"
    rc=0
    "$OMC" < "$scratch/io.om" > "$scratch/io.out" 2> "$scratch/io.err" || rc=$?
    if [ "$rc" -ne 0 ] || [ ! -s "$scratch/io.out" ] || [ -s "$scratch/io.err" ]; then
        echo "FAIL $mode successful-output control: rc=$rc" >&2
        exit 1
    fi
    if [ "$mode" = check ]; then
        printf 'omega-diag v1\n' > "$scratch/io.want"
        cmp -s "$scratch/io.want" "$scratch/io.out" || { echo "FAIL check output control" >&2; exit 1; }
    elif [ "$mode" = next ]; then
        printf 'omega-query v1\nscope: toplevel\nconstructs: fn const global text struct note\nnames:\nfunctions:\nfn main to int\n' > "$scratch/io.want"
        cmp -s "$scratch/io.want" "$scratch/io.out" || { echo "FAIL next output control" >&2; exit 1; }
    fi
    rc=0
    "$OMC" < "$scratch/io.om" > /dev/full 2> "$scratch/io.err" || rc=$?
    if [ "$rc" -ne 2 ]; then
        echo "FAIL $mode /dev/full: expected compiler I/O error 2, got $rc" >&2
        exit 1
    fi
    PASS_COUNT=$((PASS_COUNT + 1))
done

[ "$PASS_COUNT" -eq "$TOTAL" ] || {
    echo "FAIL case count: ran $PASS_COUNT, expected $TOTAL" >&2
    exit 1
}

echo "---------------------------------------------------------------------------------------------------------"
echo "ALL $PASS_COUNT/$TOTAL EXPERIMENT CHECKS PASSED (negative control + exact first error span matching)"
