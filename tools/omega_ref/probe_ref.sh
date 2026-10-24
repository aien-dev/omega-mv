#!/bin/sh
# probe_ref.sh -- spec probes beyond gate4: small programs, each with the answer OMEGESE.md (sections 3
# and 9) gives, run through om4 (black box: build, then run the ELF) and through the interpreter.
# Run from the repo root after build.sh:  sh tools/omega_ref/probe_ref.sh
# want: the answer the spec gives: an exit status, <status>:<stdout as hex>, or R<n> (refused with error n);
# or G<k>:<answer> where the spec is silent and SPEC_GAPS.md G<k> proposes that answer. The interpreter must
# give exactly that answer on every case. om4 must equal it too, except the cases listed in KNOWN below
# (om4 findings F<k> and gaps where om4 differs from the proposal), each explained in SPEC_GAPS.md.
# SEED=<dir> points it at another seed tree (om4.bin; default seed); EVID=<dir> is the results dir (default
# evidence/OMEGA-REF): probe_ref.log. A disagreement with the spec is a finding.
set -eu
SEED=${SEED:-seed}; R=$SEED/rung4; B=build/omega_ref/probe; E=${EVID:-evidence/OMEGA-REF}; I=build/omega_ref/omega_ref
mkdir -p "$B" "$E"
LOG=$E/probe_ref.log
: > "$LOG"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
fail=0; n=0; silent=0; known=0
KNOWN=" lead_zero minus_zero rw_ulong regs17 "
say "probe_ref $(date -u +%FT%TZ) at $(git rev-parse HEAD), om4.bin $(sha256sum $R/om4.bin | cut -c1-16)"
outcome() { # tool src -> "R<n>" or "<exit>:<sha of stdout>"
  if [ $1 = om4 ]; then
    set +e; $R/om4.bin < $2 > $B/p.elf 2> /dev/null; rc=$?; set -e
    [ $rc = 0 ] || { echo "R$rc"; return; }
    chmod +x $B/p.elf
    set +e; timeout 60 ./$B/p.elf < $B/stdin > $B/p.out 2> /dev/null; rc=$?; set -e
  else
    set +e; $I check $2 2> /dev/null; rc=$?; set -e
    [ $rc = 0 ] || { echo "R$rc"; return; }
    set +e; timeout 60 $I run $2 < $B/stdin > $B/p.out 2> /dev/null; rc=$?; set -e
  fi
  if [ -s $B/p.out ]; then echo "$rc:$(od -An -tx1 $B/p.out | tr -d " \\n" | head -c 80)"; else echo "$rc"; fi
}
p() { # name want program
  n=$((n + 1))
  printf '%b' "$3" > $B/$1.om
  a=$(outcome om4 $B/$1.om); b=$(outcome ref $B/$1.om)
  want=${2#G*:}
  case $2 in G*) silent=$((silent + 1)); tag=" (spec silent, ${2%%:*})";; *) tag="";; esac
  case "$KNOWN" in *" $1 "*) isknown=1;; *) isknown=0;; esac
  if [ "$b" != "$want" ]; then say "FAIL  $1: want $want$tag, interpreter $b (om4 $a)"; fail=1
  elif [ "$a" = "$want" ] && [ $isknown = 0 ]; then say "PASS  $1: $a$tag"
  elif [ "$a" = "$want" ]; then say "FAIL  $1: listed as known but om4 now agrees ($a): update KNOWN and SPEC_GAPS.md"; fail=1
  elif [ $isknown = 1 ]; then known=$((known + 1)); say "KNOWN $1: want $want$tag = interpreter; om4 $a (SPEC_GAPS.md)"
  else say "FAIL  $1: want $want$tag = interpreter, om4 $a (new finding)"; fail=1; fi
}
: > $B/stdin
M='fn main to int\n'
# 9.3 numbers: wrap for unsigned, trap 1 for signed, at every width
p add_u8_wrap 0       "${M}x = uchar 255\ny = add x 1\nret int y\nend\n"
p add_u16_wrap 1      "${M}x = ushort 65535\ny = mul x x\nret int y\nend\n"
p sub_u32_wrap 255    "${M}x = uint 0\ny = sub x 1\nret int and y 255\nend\n"
p neg_u64_wrap 255    "${M}x = ulong 1\ny = neg x\nret int and y 255\nend\n"
p shl_u8_wrap 0       "${M}x = uchar 128\ny = shl x 1\nret int y\nend\n"
p not_u16 255         "${M}x = ushort 0\ny = not x\nret int and y 255\nend\n"
p add_i8_ovf 101      "${M}x = byte 127\ny = add x 1\nret 0\nend\n"
p sub_i16_ovf 101     "${M}x = short -32768\ny = sub x 1\nret 0\nend\n"
p mul_i32_ovf 101     "${M}x = int 65536\ny = mul x x\nret 0\nend\n"
p mul_i64_ovf 101     "${M}x = long 4294967296\ny = mul x x\nret 0\nend\n"
p neg_i8_min 101      "${M}x = byte -128\ny = neg x\nret 0\nend\n"
p div_i16_min 101     "${M}x = short -32768\ny = div x -1\nret 0\nend\n"
p rem_i64_min 0       "${M}x = long -9223372036854775808\ny = rem x -1\nret int y\nend\n"
p div_trunc 253       "${M}x = int -7\ny = div x 2\nret y\nend\n"
p rem_sign 255        "${M}x = int -7\ny = rem x 2\nret y\nend\n"
p rem_sign2 1         "${M}x = int 7\ny = rem x -2\nret y\nend\n"
p div0_u 102          "${M}x = uchar 0\ny = div uchar 5 x\nret 0\nend\n"
p rem0_i 102          "${M}x = byte 0\ny = rem byte -5 x\nret 0\nend\n"
p shl_i_ovf 101       "${M}x = int 1073741824\ny = shl x 1\nret 0\nend\n"
p shl_i_neg_ok 252    "${M}x = int -1\ny = shl x 2\nret y\nend\n"
p shr_i_sign 255      "${M}x = int -8\ny = shr x 3\nret y\nend\n"
p shr_u 31            "${M}x = uchar 248\ny = shr x 3\nret int y\nend\n"
p shift_w 107         "${M}x = uint 1\ns = uchar 32\ny = shl x s\nret 0\nend\n"
p shift_w8 107        "${M}x = uchar 1\ns = ulong 8\ny = shr x s\nret 0\nend\n"
p shift_neg 107       "${M}x = long 1\ns = int -1\ny = shl x s\nret 0\nend\n"
p shift_bare_l 16     "${M}s = uchar 4\ny = shl 1 s\nret int y\nend\n"
p shift_bare_l_w 107  "${M}s = uchar 8\ny = shl 1 s\nret int y\nend\n"
p conv_ok 128         "${M}x = int 128\nret int uchar x\nend\n"
p conv_hi 105         "${M}x = int 256\ny = uchar x\nret 0\nend\n"
p conv_neg 105        "${M}x = int -1\ny = ulong x\nret 0\nend\n"
p conv_typed 105      "${M}y = uchar int 300\nret 0\nend\n"
p conv_sign 105       "${M}x = ulong 9223372036854775808\ny = long x\nret 0\nend\n"
p conv_bool G2:R9       "${M}b = true\nret int b\nend\n"
p typed_range R8      "${M}y = uchar 256\nret 0\nend\n"
p typed_neg_u R8      "${M}y = uint -1\nret 0\nend\n"
p cmp_signed 1        "${M}x = long -1\ny = long 1\nif lt x y\nret 1\nend\nret 0\nend\n"
p cmp_unsigned 0      "${M}x = ulong 18446744073709551615\ny = ulong 1\nif lt x y\nret 1\nend\nret 0\nend\n"
p mixed R7            "${M}x = int 1\ny = long 1\nz = add x y\nret 0\nend\n"
p two_bare R11        "${M}x = int 1\nz = add x mul 2 3\nret 0\nend\n"
p bare_new G3:R11       "${M}x = 5\nret x\nend\n"
p bare_unary 251      "${M}ret neg 5\nend\n"
p bool_arith R7       "${M}b = true\nc = add b b\nret 0\nend\n"
p true_false 1        "${M}b = and true not false\nif b\nret 1\nend\nret 0\nend\n"
# 9.1 exit status: modulo 256
p ret_mod 44          'fn main to ulong\nret 300\nend\n'
p ret_neg 255         "${M}ret -1\nend\n"
p ret_void 0          'fn main to void\nend\n'
p exit_mod 0          "${M}exit 256\nend\n"
p exit_neg 254        "${M}exit -2\nend\n"
p exit_in_fn 9        'fn f to void\nexit 9\nend\nfn main to int\nf\nret 1\nend\n'
p main_bool G4:1        'fn main to bool\nret true\nend\n'
# 9.2 calls: arguments left to right, each fully before the next (writes show the order)
p arg_order 0:31320a 'text a "1"\ntext b "2"\ntext n "\\n"\nfn wa to long\nr = write 1 a 0 1\nret r\nend\nfn wb to long\nr = write 1 b 0 1\nret r\nend\nfn two x long y long to long\nret add x y\nend\nfn main to int\nr = two wa wb\nw = write 1 n 0 1\nret 0\nend\n'
p and_both 0:3231 'text a "1"\ntext b "2"\nfn ta to bool\nr = write 1 a 0 1\nret true\nend\nfn fb to bool\nr = write 1 b 0 1\nret false\nend\nfn main to int\nif and fb ta\nret 1\nend\nret 0\nend\n'
p deep_rec 0          'fn r n ulong to ulong\nif eq n 0\nret 0\nend\nret add r sub n 1 1\nend\nfn main to int\nx = r 100000\nret int sub x 100000\nend\n'
p fn_name_63 0        "fn $(printf 'f%.0s' $(seq 63)) to int\nret 0\nend\nfn main to int\nret $(printf 'f%.0s' $(seq 63))\nend\n"
p fn_name_64 R13      "fn $(printf 'f%.0s' $(seq 64)) to int\nret 0\nend\nfn main to int\nret 0\nend\n"
p name_8 0            "${M}abcdefgh = int 0\nret abcdefgh\nend\n"
p ret_both 3          'fn f a int to int\nif eq a 0\nret 1\nelse\nret 3\nend\nend\nfn main to int\nret f 5\nend\n'
p after_ifelse G9:R17      'fn f a int to int\nif eq a 0\nret 1\nelse\nret 3\nend\nx = a\nret x\nend\nfn main to int\nret f 5\nend\n'
p after_break G1:R9      "${M}for i int 3\nbreak\nx = i\nend\nret 0\nend\n"
p void_ret R17         'global g uchar 1\nfn s to void\nput g 0 7\nret\nput g 0 8\nend\nfn main to int\ns\nret int get g 0\nend\n'
# section 3 control: loops, bounds, break, for counts
p while_default 104   "${M}i = ulong 0\nwhile true\ni = add i 1\nend\nret 0\nend\n"
p while_exact 10      "${M}i = int 0\nwhile lt i 10 bound 10\ni = add i 1\nend\nret i\nend\n"
p for_value_cap 104   "${M}n = ulong 1000001\ns = ulong 0\nfor i ulong add n 0\ns = add s 1\nend\nret 0\nend\n"
p for_value_ok 64     "${M}n = ulong 1000000\ns = ulong 0\nfor i ulong add n 0\ns = add s 1\nend\nret int and s 255\nend\n"
p for_lit_cap G10:0       "${M}s = ulong 0\nfor i ulong 1000001\ns = add s 1\nend\nret 0\nend\n"
p for_name_reread 3   "${M}n = int 10\nc = int 0\nfor i int n\nn = int 3\nc = add c 1\nend\nret c\nend\n"
p for_value_once 10   "${M}n = int 10\nc = int 0\nfor i int add n 0\nn = int 3\nc = add c 1\nend\nret c\nend\n"
p for_neg_count 0     "${M}n = int -5\nc = int 0\nfor i int n\nc = add c 1\nend\nret c\nend\n"
p break_inner G1:R9       "${M}c = int 0\nfor i int 3\nfor j int 5\nif eq j 3\nbreak\nend\nc = add c 1\nend\nend\nret c\nend\n"
p scope_reuse 2       "${M}if true\nx = int 1\nend\nx = int 2\nret x\nend\n"
p scope_dead R3       "${M}if true\nx = int 1\nend\nret x\nend\n"
p bound_0 R8          "${M}while true bound 0\nend\nret 0\nend\n"
p trap_15 G1:R9         "${M}trap 15\nend\n"
p trap_14 G1:R9         "${M}trap 14\nend\n"
# 9.4 arrays and pools
p pool_zero 0         'global g ulong 1000\nfn main to int\ns = ulong 0\nfor i ulong 1000\ns = add s get g i\nend\nret int s\nend\n'
p get_neg 103         'global g uchar 4\nfn main to int\ni = int -1\nret int get g i\nend\n'
p put_order R21        'global g uchar 4\nfn main to int\nz = uchar 0\nput g 9 div uchar 1 z\nret 0\nend\n'
p put_idx_first G11:102     'global g uchar 4\nfn main to int\nz = uchar 0\ni = ulong 9\nput g i div uchar 1 z\nret 0\nend\n'
p new_init 28         "${M}x = new int 5 7\nput x 4 0\ns = int 0\nfor i ulong 5\ns = add s get x i\nend\nret s\nend\n"
p new_fresh 14        'fn f a int to int\nx = new int 3 a\nput x 0 add get x 0 get x 1\nret get x 0\nend\nfn main to int\ny = f 3\nret f 7\nend\n'
p view_len 5          'fn l a view uchar 5 to ulong\nret len a\nend\nfn main to int\nx = new uchar 5 0\nr = l x\nret int r\nend\n'
p edit_through 9      'fn s a edit int 2 to void\nput a 1 9\nend\nfn t a edit int 2 to void\ns a\nend\nfn main to int\nx = new int 2 0\nt x\nret get x 1\nend\n'
p view_edit_pfx 9     'fn s a edit int 2 to void\nput a 1 9\nend\nfn main to int\nx = new int 2 0\ns edit x\nret get x 1\nend\n'
p view_pfx_to_edit R20  'fn s a edit int 2 to void\nput a 1 9\nend\nfn main to int\nx = new int 2 0\ns view x\nret get x 1\nend\n'
p text_pass G6:0         'text t "ab"\nfn s a view uchar 2 to uchar\nret get a 0\nend\nfn main to int\nr = s t\nret 0\nend\n'
p free_noop R9        "${M}x = new int 2 7\nfree x\nret get x 0\nend\n"
p local_4mib 0        "${M}x = new ulong 524288 0\nret 0\nend\n"
p local_4mib_1 R23    "${M}x = new ulong 524288 0\ny = new uchar 1 0\nret 0\nend\n"
p pool_max 0          'global g uchar 16777216\nfn main to int\nret 0\nend\n'
p pool_1gib R23        "$(for k in 1 2 3 4 5 6 7 8 9; do printf 'global g%s ulong 16777216\\n' $k; done)fn main to int\nret 0\nend\n"
p idx_lit_neg G7:R8     'global g uchar 4\nfn main to int\nret int get g -1\nend\n'
p len_text 3          'text t "a\\tb"\nfn main to int\nret int len t\nend\n'
p text_bytes 0:6109620a5c22 'text t "a\\tb\\n\\\\\\""\nfn main to int\nw = write 1 t 0 len t\nret 0\nend\n'
p text_empty G12:0        'text t ""\nfn main to int\nret 0\nend\n'
p text_space 0:61202062 'text t "a  b"\nfn main to int\nw = write 1 t 0 len t\nret 0\nend\n'
p text_fnmode R9      'text t "ab"\nfn f a uint to uint\nret a\nend\n'
p stack_local 106     'fn r n ulong to ulong\nx = new uchar 65536 0\nret add r add n 1 ulong get x 0\nend\nfn main to int\nx = r 0\nret 0\nend\n'
# 9.5 read and write
p write_bad 103       'global g uchar 4\nfn main to int\nw = write 1 g 3 2\nret 0\nend\n'
p write_wrap 103      'global g uchar 4\nfn main to int\nw = write 1 g 18446744073709551615 2\nret 0\nend\n'
p write_n0 0          'global g uchar 4\nfn main to int\nw = write 1 g 4 0\nret int w\nend\n'
p write_badfd 247     'global g uchar 4\nfn main to int\nw = write 99 g 0 1\nret int w\nend\n'
p read_eof 0          'global g uchar 4\nfn main to int\nr = read 0 g 0 4\nret int r\nend\n'
p rw_ulong G8:R7          'global g ulong 4\nfn main to int\nw = write 1 g 0 1\nret 0\nend\n'
# words, numbers and spacing (section 3 and 6.1)
p brackets 8          "${M}x = int 3\nret ( add x 5 )\nend\n"
p tab_sep G13:3           "${M}x = int 3\nret\tx\nend\n"
p lead_zero G5:R6        "${M}ret 07\nend\n"
p minus_zero G5:R6       "${M}ret -0\nend\n"
p eq_less 4           "${M}x int 4\nret x\nend\n"
p ir_types G14:4          "${M}x = i32 4\nret x\nend\n"
p crlf G15:3              "${M}ret 3\r\nend\r\n"
p note_inside 0       "${M}note anything at all ( ) 12x\nret 0\nend\n"
p nest_1024 G16:R14         "fn main to int\nx = long 1\ny = $(printf 'neg %.0s' $(seq 1024))x\nret 0\nend\n"
p shadow_param R10    'fn f a int to int\nfor a int 3\nend\nret a\nend\nfn main to int\nret f 1\nend\n'
p loop_name_assign G17:1  "${M}c = int 0\nfor i int 5\ni = int 4\nc = add c 1\nend\nret c\nend\n"
p break_while G1:R9    "${M}i = int 0\nwhile lt i 5\nbreak\nend\nret 0\nend\n"
p trap_1 G1:R9         "${M}trap 1\nend\n"
p to_bool G2:R9        "${M}x = uint 1\ny = bool x\nret 0\nend\n"
p u63 1               "${M}x = ulong 9223372036854775807\nret int shr x 62\nend\n"
p idx_2_32 R21        'global g uchar 4\nfn main to int\nret int get g 4294967296\nend\n'
p text_edit R20       'text t "ab"\nfn s a edit uchar 2 to void\nend\nfn main to int\ns t\nret 0\nend\n'
p fn_after_main 0     'fn main to int\nret 0\nend\nfn g to int\nret 0\nend\n'
p global_after 0      'fn main to int\nret 0\nend\nglobal g uchar 4\nfn g2 to int\nret int get g 0\nend\n'
p reach18 R18         "${M}x = int 1\nif eq x 1\nret 2\nend\nend\n"
p write_local 0:00000000 "${M}x = new uchar 4 0\nw = write 1 x 0 4\nret 0\nend\n"
p regs17 G18:0        "${M}x = int 0\ny = int 0\nz = int 0\na1 = int 0\na2 = int 0\na3 = int 0\na4 = int 0\na5 = int 0\na6 = int 0\na7 = int 0\na8 = int 0\na9 = int 0\nb1 = int 0\nb2 = int 0\nb3 = int 0\nb4 = int 0\nb5 = int 0\nret add x add y add z add a1 add a2 add a3 add a4 add a5 add a6 add a7 add a8 add a9 add b1 add b2 add b3 add b4 b5\nend\n"
# 9.2 names pass (synced 2026-10-24 to the om4 of seed/omc-unified): a function written later is a call,
# cycles, the names of functions, at most 6 parameters, fn names up to 63 letters
L63=$(printf 'a%.0s' $(seq 63)); L64=${L63}a
p fwd_call 7          'fn main to int\nret later\nend\nfn later to int\nret 7\nend\n'
p mutual_rec 1        'fn main to int\nret even 8\nend\nfn even n int to int\nif eq n 0\nret 1\nend\nret odd sub n 1\nend\nfn odd n int to int\nif eq n 0\nret 0\nend\nret even sub n 1\nend\n'
p fm_cycle R9         'fn a to int\nret b\nend\nfn b to int\nret a\nend\n'
p fm_self R9          'fn r n ulong to ulong\nret r n\nend\n'
p fm_cycle_late G26:R3 'fn a to int\nret b\nend\nfn b to int\nret a\nend\nfn c to int\nret zz\nend\n'
p later_assign R7     'fn main to int\nlater = 2\nret 0\nend\nfn later to int\nret 0\nend\n'
p void_assign G31:R5  'fn main to int\nlater = 2\nret 0\nend\nfn later to void\nend\n'
p fn_in_fn G27:R15    'fn main to int\nret later\n\nfn later to int\nret 7\nend\n'
p seven_params R9     'fn f a int b int c int d int e int f1 int g int to int\nret a\nend\nfn main to int\nret f 1 2 3 4 5 6 7\nend\n'
p six_params 1        'fn f a int b int c int d int e int f1 int to int\nret a\nend\nfn main to int\nret f 1 2 3 4 5 6\nend\n'
p fn_name_63_fwd 7       "fn main to int\nret $L63\nend\nfn $L63 to int\nret 7\nend\n"
p fn_name_64_fwd R13     "fn main to int\nret $L64\nend\nfn $L64 to int\nret 7\nend\n"
p dup_fn R10          'fn main to int\nret 0\nend\nfn main to int\nret 0\nend\n'
p fn_global_clash R10 'global g uchar 4\nfn g to int\nret 0\nend\nfn main to int\nret 0\nend\n'
# bool numbers (s.6, s.7) and the order of refusals within a line (G25, G28, G29)
p if_one 3            "${M}if 1\nret 3\nend\nret 0\nend\n"
p bool_255 G28:R8     "${M}while 255\nret 0\nend\nret 0\nend\n"
p cond_type_first G29:R7 "${M}r = int 3\nif r 1\nret 0\nend\nret r\nend\n"
p num_line_after_ret G25:R16 "${M}ret 0\n0\nend\n"
p bound_word G25:R16  "${M}while true bound bound 10\nret 0\nend\nret 0\nend\n"
say "probes: $n written, $n run ($silent where the spec is silent; $known known om4 differences)"
[ $fail = 0 ] && say "PROBE_REF PASS" || say "PROBE_REF FAIL"
exit $fail
