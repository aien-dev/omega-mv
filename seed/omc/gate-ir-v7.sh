#!/bin/sh
# OSC IR v7 producer gate (spec/one-layer-format.md section 10, OMEGESE 11.2): omc in `ir` mode.
# Expected bytes below are derived field by field from spec 3.1/3.3/3.5/10.1/10.2/10.3, not copied from
# omc output; the float and little-endian encodings were computed with Wolfram (see the PR body).
# The independent referee is seed/wasm/v7/oracle.js (override with V7_ORACLE); node is required.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/irv7
ORACLE=${V7_ORACLE:-seed/wasm/v7/oracle.js}
mkdir -p "$B"
fail() { printf 'FAIL %s\n' "$*"; echo RC=1; exit 1; }
[ -f "$ORACLE" ] || fail "oracle $ORACLE missing (set V7_ORACLE)"
command -v node > /dev/null || fail 'node missing'
seed/rung4/om4.bin < seed/omc/omc.om > "$B/stage1"
chmod +x "$B/stage1"
"$B/stage1" < seed/omc/omc.om > "$B/stage2"
chmod +x "$B/stage2"
cmp "$B/stage1" "$B/stage2" || fail 'stage1/stage2 differ'
echo "PASS fixpoint $(sha256sum "$B/stage2")"

# Units: name, source, expected hex (comments after #), oracle script, expected oracle output.
unit() { printf '%s\n' "$2" > "$B/$1.om"; printf '%s' "$3" | sed 's/#.*//' | tr -d ' \n' > "$B/$1.hex"; printf '%s\n' "$4" > "$B/$1.run"; printf '%s\n' "$5" > "$B/$1.want"; echo "$1" >> "$B/units"; }
: > "$B/units"
M='4f53433149520007 00'   # magic "OSC1IR\0", version 7, nstructs 0
unit glob 'ir
global g uint 4
fn f to uint
put g 1 7
ret get g 1
end' "$M
0100  01 67 04 00 ffff 04000000 00000000   # 1 region: g, u32, class 0, module owner, count 4, no data
0100  01 66 00 04                          # 1 function f, 0 params, returns u32
0300  05 04 04                             # regs: r0 u64 index, r1 u32 value, r2 u32 load
0100  00000000 05000000  05000000          # 1 block: first 0, count 5; 5 insns
01 0000 0100000000000000                   # CONST r0 = 1
01 0100 0700000000000000                   # CONST r1 = 7
1f 0000 0000 0100                          # RSTORE g, r0, r1
1e 0200 0000 0000                          # RLOAD r2, g, r0
0f 0200                                    # RET r2
0000 0000                                  # requires, ensures empty" 'call f
region g' 'f = 7
region g 00000000070000000000000000000000'
unit text 'ir
text t "hi\n"
fn f to ulong
ret len t
end' "$M
0100  01 74 02 02 ffff 03000000 03000000 68690a   # t: u8 text, count 3, data 'h' 'i' LF
0100  01 66 00 05  0100 05
0100  00000000 02000000  02000000
01 0000 0300000000000000                   # len t: CONST r0 = 3 (u64)
0f 0000
0000 0000" 'call f
region t' 'f = 3
region t 68690a'
unit tget 'ir
text t "hi"
fn f to uchar
ret get t 1
end' "$M
0100  01 74 02 02 ffff 02000000 02000000 6869
0100  01 66 00 02  0200 05 02
0100  00000000 03000000  03000000
01 0000 0100000000000000
1e 0100 0000 0000                          # RLOAD r1, t, r0
0f 0100
0000 0000" 'call f' 'f = 105'
unit local 'ir
fn f to int
a = new int 3 int 5
put a 2 int 9
ret get a 2
end' "$M
0100  01 61 08 01 0000 03000000 00000000   # a: i32, class 1 local, owner function 0, count 3
0100  01 66 00 08  0400 08 05 08 08
0100  00000000 07000000  07000000
01 0000 0500000000000000                   # CONST r0 = 5 (i32 init); constants lead the entry block (v1 rule)
01 0100 0200000000000000
01 0200 0900000000000000
21 0000 0000                               # RINIT a, r0
1f 0000 0100 0200                          # RSTORE a, r1, r2
1e 0300 0000 0100                          # RLOAD r3, a, r1
0f 0300
0000 0000" 'call f' 'f = 9'
unit fadd 'ir
fn f x f32 to f32
ret add x 1.5
end' "$M
0000                                       # no regions; v7 because of f32 (tag 13)
0100  01 66 01 0d  0300 0d 0d 0d
0100  00000000 03000000  03000000
01 0100 0000c03f00000000                   # CONST r1 = f32 1.5 (0x3fc00000), upper half zero
03 01 0200 0000 0100                       # BIN add r2 = r0 + r1
0f 0200
0000 0000" 'call f 0x3fc00000' 'f = 0x40400000'
unit fmul 'ir
fn f to f64
ret mul 2.0f64 0.5f64
end' "$M
0000
0100  01 66 00 0e  0300 0e 0e 0e
0100  00000000 04000000  04000000
01 0000 0000000000000040                   # f64 2.0 = 0x4000000000000000
01 0100 000000000000e03f                   # f64 0.5 = 0x3fe0000000000000
03 03 0200 0000 0100                       # BIN mul
0f 0200
0000 0000" 'call f' 'f = 0x3ff0000000000000'
unit bitc 'ir
fn f to uint
ret bitcast uint 1.0f32
end' "$M
0000
0100  01 66 00 04  0200 0d 04
0100  00000000 03000000  03000000
01 0000 0000803f00000000                   # f32 1.0 = 0x3f800000
20 0100 0000                               # BITCAST r1 (u32), r0 (f32)
0f 0100
0000 0000" 'call f' 'f = 1065353216'
unit v1 'ir
fn f to uint
ret 3
end' '4f53433149520001 0100  01 66 00 04  0100 04  0100 00000000 02000000  02000000
01 0000 0300000000000000  0f 0000  0000 0000   # old-only unit stays version 1' 'call f' 'f = 3'

# A conversion word converts an operation at its own type (as native): long division, then CAST to u32.
unit conv 'ir
fn f x long to uint
ret uint div x 2
end' '4f53433149520001 0100  01 66 01 04  0400 09 09 09 04
0100 00000000 04000000  04000000
01 0100 0200000000000000                   # CONST r1 = 2 (i64)
03 04 0200 0000 0100                       # BIN div (36 - 32) r2 = r0 / r1
06 0300 0200                               # CAST r3 (u32) = r2
0f 0300  0000 0000' 'call f 4294967298' 'f = 2147483649'

refusals() { # compiler: prints one line per refusal case, PASS or BAD
    while IFS='|' read -r name code; do
        set +e
        timeout 10 "$1" < "$B/r_$name.om" > "$B/r_$name.out" 2> /dev/null
        crc=$?
        set -e
        if [ "$crc" = "$code" ]; then echo "PASS $name refused $code"; else echo "BAD $name exit $crc wanted $code"; fi
    done < "$B/refusals"
}
: > "$B/refusals"
refuse() { printf '%s\n' "$3" > "$B/r_$1.om"; echo "$1|$2" >> "$B/refusals"; }
refuse text_put 20 'ir
text t "x"
fn f to void
put t 0 uchar 1
end'
refuse get_oob 21 'ir
global g uint 4
fn f to uint
ret get g 4
end'
refuse put_oob 21 'ir
fn f to int
a = new int 2 int 0
put a 2 int 1
ret 0
end'
refuse new_nested 9 'ir
fn f to int
if true
a = new int 3 int 5
end
ret 0
end'
refuse global_bool 9 'ir
global g bool 4
fn f to int
ret 0
end'
refuse new_bool 9 'ir
fn f to int
a = new bool 2 false
ret 0
end'
refuse glob_dup 10 'ir
global g uint 4
global g int 4
fn f to int
ret 0
end'
# a new local named like a module name is refused by ir_assign's existing rule (7), before ir_new
refuse local_shadow 7 'ir
global a uint 4
fn f to int
a = new int 3 int 5
ret 0
end'
refuse glob_after_local 10 'ir
fn f to int
a = new int 3 int 5
ret 0
end
global a uint 4'
refuse glob_long 13 'ir
global abcdefghi uint 4
fn f to int
ret 0
end'
refuse glob_count 23 'ir
global g uchar 16777217
fn f to int
ret 0
end'
{ printf 'ir\n'; for i in 1 2 3 4 5 6 7 8 9; do printf 'global g%s ulong 16777216\n' "$i"; done; printf 'fn f to int\nret 0\nend\n'; } > "$B/r_glob_pool.om"
echo 'glob_pool|23' >> "$B/refusals"
refuse local_pool 23 'ir
fn f to int
a = new ulong 524288 ulong 0
b = new uchar 1 uchar 0
ret 0
end'
{ printf 'ir\n'; i=0; while [ $i -lt 1025 ]; do printf 'global g%s uchar 1\n' $i; i=$((i+1)); done; printf 'fn f to int\nret 0\nend\n'; } > "$B/r_regions.om"
echo 'regions|14' >> "$B/refusals"
refuse glob_scalar 7 'ir
global g uint 4
fn f to uint
ret g
end'
refuse local_scalar 7 'ir
fn f to int
a = new int 3 int 5
a = int 1
ret 0
end'
refuse idx_float 7 'ir
global g uint 4
fn f to uint
ret get g 1.0f32
end'
refuse put_type 7 'ir
global g uint 4
fn f to void
put g 0 int 1
end'
refuse new_type 7 'ir
fn f to int
a = new int 3 uint 5
ret 0
end'
refuse bare_int_float 7 'ir
fn f x f32 to f32
ret add x 1
end'
refuse float_rem 7 'ir
fn f x f64 to f64
ret rem x x
end'
refuse float_not 7 'ir
fn f x f32 to f32
ret not x
end'
refuse float_bool 9 'ir
fn f x f32 to bool
ret bool x
end'
refuse v7_bool_cast 9 'ir
global g uint 1
fn f b bool to uint
ret uint b
end'
refuse bitc_width 7 'ir
fn f to ulong
ret bitcast ulong 1.0f32
end'
refuse bitc_bare 11 'ir
fn f to uint
ret bitcast uint 1
end'
refuse bitc_bool 7 'ir
fn f b bool to uchar
ret bitcast uchar b
end'
refuse fsqrt 3 'ir
fn f x f64 to f64
ret fsqrt x
end'
refuse no_array 3 'ir
fn f to uint
ret get zz 0
end'
refuse text_escape 22 'ir
text t "a\q"
fn f to int
ret 0
end'
refuse conv_bool 7 'ir
fn f x uint to uint
ret uint eq x 1
end'
refuse struct 9 'ir
struct S a uchar end
fn f to int
ret 0
end'

suite() { # compiler: golden bytes, oracle check and run; prints PASS/BAD lines
    while read -r u; do
        set +e
        timeout 10 "$1" < "$B/$u.om" > "$B/$u.ir" 2> /dev/null
        crc=$?
        set -e
        if [ "$crc" != 0 ]; then echo "BAD $u omc exit $crc"; continue; fi
        got=$(od -An -v -tx1 "$B/$u.ir" | tr -d ' \n')
        if [ "$got" = "$(cat "$B/$u.hex")" ]; then echo "PASS $u bytes"; else echo "BAD $u bytes"; fi
        want=$(cut -c16 "$B/$u.hex")
        chk=$(node "$ORACLE" check "$B/$u.ir" || true)
        if [ "$chk" = "ACCEPT v$want" ]; then echo "PASS $u oracle $chk"; else echo "BAD $u oracle $chk"; fi
        node "$ORACLE" run "$B/$u.ir" "$B/$u.run" > "$B/$u.got" 2>&1 || true
        if cmp -s "$B/$u.got" "$B/$u.want"; then echo "PASS $u run"; else echo "BAD $u run $(tr '\n' ' ' < "$B/$u.got")"; fi
    done < "$B/units"
}

suite "$B/stage2" > "$B/suite.log"
refusals "$B/stage2" >> "$B/suite.log"
cat "$B/suite.log"
nbad=$(grep -c '^BAD' "$B/suite.log" || true)
npass=$(grep -c '^PASS' "$B/suite.log" || true)
[ "$nbad" = 0 ] || fail "$nbad checks failed"
nu=$(wc -l < "$B/units"); nr=$(wc -l < "$B/refusals")

# Mutants of the producer, each compiled by om4: the suite above must catch every one.
mutant() { # name pattern replacement
    [ "$(grep -c "$2" seed/omc/omc.om)" = 1 ] || fail "mutant $1 pattern not unique"
    sed "s/$2/$3/" seed/omc/omc.om > "$B/m_$1.om"
    seed/rung4/om4.bin < "$B/m_$1.om" > "$B/m_$1" || fail "mutant $1 did not compile"
    chmod +x "$B/m_$1"
    { suite "$B/m_$1"; refusals "$B/m_$1"; } > "$B/m_$1.log"
    k=$(grep -c '^BAD' "$B/m_$1.log" || true)
    [ "$k" != 0 ] || fail "mutant $1 survived"
    echo "CAUGHT $1: $k failing checks; first: $(grep -m1 '^BAD' "$B/m_$1.log")"
}
mutant no_rinit '^ir_op 33 g v 0 0$' 'note mutant'
mutant owner '^ow = sub get irf_n 0 1$' 'ow = get irf_n 0'
mutant wire_tag '^emitb uchar ir_wt ulong get ir_rty i$' 'emitb get ir_rty i'
mutant version '^emitb 7$' 'emitb 1'
mutant bound '^if ge get ir_cm i get rg_n g$' 'if gt get ir_cm i get rg_n g'
mutant convert '^put ir_cx 0 1$' 'put ir_cx 0 0'
# The oracle on its own must refuse the RINIT-less and wrong-owner units (not only the byte goldens).
for m in no_rinit owner; do
    "$B/m_$m" < "$B/local.om" > "$B/m_$m.ir"
    r=$(node "$ORACLE" check "$B/m_$m.ir" || true)
    case "$r" in REFUSE*) echo "PASS oracle refuses $m unit: $r" ;; *) fail "oracle accepted $m unit: $r" ;; esac
done
echo "IR_V7_PRODUCER $nu units ($((nu * 3)) byte/oracle/run checks) + $nr refusals; $npass PASS; 6 mutants caught"
echo RC=0
