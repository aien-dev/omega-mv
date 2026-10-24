#!/bin/sh
# gate14.sh -- numeric conformance. Expected values were computed exactly in Wolfram Language through
# the Wolfram MCP server (agenttools.wolfram.com, Wolfram 2026.09.15); the table and the exact
# expressions are in evidence/SEED-0/num-conformance/wolfram.txt. The gate is independent of the
# compiler: it only compares exit statuses.
#
# Value rows return the low byte of the exact result (main's value mod 256). Trap rows must stop with
# 100 + trap code: 1 signed overflow, 2 divide/remainder by zero, 3 bounds, 4 loop bound,
# 5 conversion, 7 shift. A compile refusal or a timeout is a failure.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
S=seed/out/omc
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
mkdir -p build/seed/r14
T=$(mktemp -d build/seed/r14/num.XXXXXX)
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
    [ "$rc" = 0 ] || bad "$name: compile exit $rc: $(cat "$T/p.err")"
    chmod +x "$T/p.bin"
    set +e
    timeout 5 "$T/p.bin" > /dev/null 2>&1
    got=$?
    set -e
    [ "$got" != 124 ] || bad "$name: timeout"
    [ "$got" = "$want" ] || bad "$name: exit $got, wanted $want"
done <<'CASES'
u8_add|0|fn main to int\nx = add uchar 255 1\nret int x\nend
u8_sub|255|fn main to int\nx = sub uchar 0 1\nret int x\nend
u16_add|1|fn main to int\nx = add ushort 65535 2\nret int x\nend
u32_add|0|fn main to int\nx = add uint 4294967295 1\nret int x\nend
u64_add|0|fn main to int\nx = add ulong 18446744073709551615 1\nret int x\nend
u8_shl7|128|fn main to int\nx = shl uchar 1 uchar 7\nret int x\nend
u64_shl63_hi|128|fn main to int\nx = shl ulong 1 ulong 63\ny = shr x ulong 56\nret int y\nend
i32_shr_neg8|252|fn main to int\nx = shr int -8 int 1\nret int x\nend
i32_div_neg7_2|253|fn main to int\nx = div int -7 int 2\nret int x\nend
i32_rem_neg7_2|255|fn main to int\nx = rem int -7 int 2\nret int x\nend
i32_div_7_neg2|253|fn main to int\nx = div int 7 int -2\nret int x\nend
i32_rem_7_neg2|1|fn main to int\nx = rem int 7 int -2\nret int x\nend
i32_min_rem_neg1|0|fn main to int\nx = rem int -2147483648 int -1\nret int x\nend
u64_max_div10|153|fn main to int\nx = div ulong 18446744073709551615 ulong 10\ny = rem x ulong 256\nret int y\nend
u64_max_rem10|5|fn main to int\nx = rem ulong 18446744073709551615 ulong 10\nret int x\nend
i32_min|0|fn main to int\nx = int -2147483648\nret int x\nend
i32_max|255|fn main to int\nx = int 2147483647\nret int x\nend
conv_u8_255|255|fn main to int\nx = uchar 255\nret int x\nend
conv_byte_neg128|128|fn main to int\nx = byte -128\nret int x\nend
byte_add_ovf|101|fn main to int\nx = add byte 127 1\nret int x\nend
byte_sub_ovf|101|fn main to int\nx = sub byte -128 1\nret int x\nend
byte_neg_ovf|101|fn main to int\nx = neg byte -128\nret int x\nend
short_add_ovf|101|fn main to int\nx = add short 32767 1\nret int x\nend
int_add_ovf|101|fn main to int\nx = add int 2147483647 1\nret int x\nend
long_add_ovf|101|fn main to int\nx = add long 9223372036854775807 1\nret int x\nend
int_min_div|101|fn main to int\nx = div int -2147483648 int -1\nret int x\nend
int_shl31|101|fn main to int\nx = shl int 1 int 31\nret int x\nend
conv_300|105|fn main to int\nx = uchar int 300\nret int x\nend
conv_m129|105|fn main to int\nx = byte int -129\nret int x\nend
conv_2p31|105|fn main to int\nx = int long 2147483648\nret int x\nend
shift8|107|fn main to int\nx = shl uchar 1 uchar 8\nret int x\nend
div0|102|fn main to int\nx = div int 1 int 0\nret int x\nend
rem0|102|fn main to int\nx = rem int 1 int 0\nret int x\nend
index|103|fn main to int\na = new uint 4 0\ni = ulong 4\nx = get a i\nret int x\nend
bound|104|fn main to int\na = new uint 1 0\nwhile eq int 1 int 1 bound 3\nput a 0 1\nend\nret 0\nend
CASES

[ "$n" = 35 ] || bad "ran $n of 35 cases"
say "PASS gate14 35 numeric cases (19 values, 16 traps; Wolfram-derived expectations)"
exit 0
