#!/bin/bash
# tests/diagnostics/experiment_issue655.sh
# Empirical experiment for issue #655: incremental compiler feedback for .om generation.
# Tests 10 held-out .om programs with known-good versions and deliberately broken drafts.
# Verifies:
#   1. Known-good programs yield zero error records (negative control: output is solely "omega-diag v1", exit code 0).
#   2. Deliberately broken programs yield exact diagnostics locating the first error span and matching the compiler refusal code.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
OMC="$ROOT/seed/out/omc"
[ -x "$OMC" ] || { echo "FAIL: $OMC not found or not executable" >&2; exit 1; }

PASS_COUNT=0
TOTAL=18

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

echo "---------------------------------------------------------------------------------------------------------"
echo "ALL $PASS_COUNT/$TOTAL EXPERIMENT CHECKS PASSED (negative control + exact first error span matching)"
