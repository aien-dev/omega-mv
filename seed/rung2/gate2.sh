#!/bin/sh
# gate2.sh <out dir> -- rung 2 gates (seed/README.md). Shell only; the C encoder/decoder and objdump are
# reference checks and never make a byte of seed/.
#  build: hex1 (rung 1 gate first) turns asm2.hex1 into asm2.bin = committed
#  self:  asm2 assembling table.s2 + asm2.s (asm2 in its own language) gives asm2.bin
#  audit: every word of asm2 against our decoder and objdump, 0 mismatches
#  encoder: every form of src/aarch64_encoder.h, 5450 operand sets with numbers in the distance fields
#         plus the label path (b, b.cond, cbz, cbnz, adr at short and at the largest distances asm2 can
#         hold): asm2's word = the C encoder's word, every case; disagreements listed (the manual decides)
#  refusals: out-of-range operands refused by asm2 (error 8) where the C encoder refuses them too
#  controls: each asm2 error code on a broken input, with its line number
set -u
D=$(cd "$(dirname "$0")/.." && pwd); R=$(cd "$D/.." && pwd); O=$1; mkdir -p "$O"
fail=0; ok() { echo "PASS $*"; }; bad() { echo "FAIL $*"; fail=1; }
sh "$D/rung1/gate1.sh" "$O/rung1" > "$O/gate1.log" 2>&1 && ok "rung 1 gate (and rung 0 inside it)" || bad "rung 1 gate"
H1=$O/rung1/hex1.bin; CHK=$R/build/seed/a64check; A=$O/asm2.bin; T=$D/rung2/table.s2
"$H1" < "$D/rung2/asm2.hex1" > "$A"; rc=$?; chmod +x "$A"
[ $rc = 0 ] && cmp -s "$A" "$D/rung2/asm2.bin" && ok "build: hex1 makes asm2.bin = committed ($(wc -c < "$A") bytes)" || bad "build rc=$rc"
sh "$D/rung2/mks.sh" > "$O/asm2.s"; cmp -s "$O/asm2.s" "$D/rung2/asm2.s" || bad "asm2.s differs from committed"
cat "$T" "$O/asm2.s" | "$A" > "$O/asm2.self"; rc=$?
[ $rc = 0 ] && cmp -s "$O/asm2.self" "$A" && ok "self: asm2 assembles itself from table.s2 + asm2.s byte for byte" || bad "self rc=$rc"
sh "$D/check/audit.sh" "$D/rung2/asm2.hex1" "$A" "$CHK" > "$O/asm2.audit"; rc=$?
[ $rc = 0 ] && ok "audit $(tail -1 "$O/asm2.audit")" || bad "audit $(tail -1 "$O/asm2.audit")"
# encoder comparison, numbers in the distance fields
sh "$D/rung2/enccases.sh" > "$O/cases"
cut -d'|' -f1 "$O/cases" | while read -r f args; do "$CHK" encode $f $args 2>&1; done > "$O/c.words"
{ cat "$T"; cut -d'|' -f2 "$O/cases"; } | "$A" > "$O/a.bin"; rc=$?
od -An -v -tx4 -w4 "$O/a.bin" | tr -d ' ' > "$O/a.words"
paste -d'|' "$O/cases" "$O/c.words" "$O/a.words" | awk -F'|' 'BEGIN { print "C encoder call\tasm2 line\tC word\tasm2 word\tverdict" }
  { v = ($3==$4 && $3 ~ /^[0-9a-f]{8}$/) ? "same" : "DIFFERENT"; print $1 "\t" $2 "\t" $3 "\t" $4 "\t" v }' > "$O/encoder-compare.tsv"
nd=$(grep -c 'DIFFERENT$' "$O/encoder-compare.tsv"); nc=$(($(wc -l < "$O/encoder-compare.tsv") - 1))
nf=$(cut -d' ' -f1 "$O/cases" | cut -d'|' -f1 | sort -u | wc -l)
[ $rc = 0 ] && [ "$nd" = 0 ] && ok "encoder: $nc cases over $nf forms, asm2 = C encoder on every one" || bad "encoder: $nd of $nc differ (rc=$rc)"
# label path: distance d in words for branches, in bytes for adr
fill() { awk -v n="$1" -v s="${2:-0}" 'BEGIN { while (n > 0 && s % 4) { print ".b 0"; n--; s++ } for (i = 0; i < int(n / 4); i++) print ".w 0"; for (i = 0; i < n % 4; i++) print ".b 0" }'; }  # n bytes from offset s
lcase() { # <C form + leading args> <asm2 insn + leading operands> <distance> <unit 4|1>
  d=$3; u=$4
  if [ "$u" = 4 ]; then
    if [ "$d" -le 0 ]; then { cat "$T"; echo ":T"; fill $((-d * 4)); echo "$2 T"; } > "$O/l.s"; pos=last
    else { cat "$T"; echo "$2 T"; fill $(((d - 1) * 4)); echo ":T"; } > "$O/l.s"; pos=first; fi
  else
    if [ "$d" -le 0 ]; then a=$((-d)); p=$(( (4 - a % 4) % 4 )); { cat "$T"; fill $p; echo ":T"; fill $a $p; echo "$2 T"; } > "$O/l.s"; pos=last
    else { cat "$T"; echo "$2 T"; fill $((d - 4)); echo ":T"; } > "$O/l.s"; pos=first; fi
  fi
  "$A" < "$O/l.s" > "$O/l.bin"; rc=$?
  if [ $pos = first ]; then got=$(head -c 4 "$O/l.bin" | od -An -tx4 | tr -d ' '); else got=$(tail -c 4 "$O/l.bin" | od -An -tx4 | tr -d ' '); fi
  want=$("$CHK" encode $1 $d 2>&1)
  if [ $rc = 0 ] && [ "$got" = "$want" ]; then lab_ok=$((lab_ok + 1)); else lab_bad=$((lab_bad + 1)); echo "  label case DIFFERENT: $1 d=$d asm2=$got rc=$rc C=$want"; fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$1 $d" "$2 <label at $d>" "$want" "$got" "$([ "$got" = "$want" ] && echo same || echo DIFFERENT)" >> "$O/encoder-compare.tsv"
}
lab_ok=0; lab_bad=0
for d in -5 -1 0 1 2 1000 -1000 1048575 -1048576; do lcase "b" "b" $d 4; done
for d in -3 0 1 7 262143 -262144; do lcase "b_cond 1" "b.ne" $d 4; lcase "b_cond 14" "b.al" $d 4; lcase "cbz 1 3" "cbz.x x3" $d 4; lcase "cbnz 0 30" "cbnz.w w30" $d 4; done
for d in 0 4 5 6 7 100 -1 -2 -3 -4 -5 1048575 -1048576; do lcase "adr 5" "adr x5" $d 1; done
[ $lab_bad = 0 ] && ok "encoder, label path: $lab_ok cases (b, b.cond, cbz, cbnz, adr; up to the largest distances) all = C encoder" || bad "label path: $lab_bad differ"
# refusals: asm2 error 8 where the value is outside the field; the C encoder refuses (aborts) too
rf() { printf '%s\n' "$2" | cat "$T" - | "$A" > /dev/null 2> "$O/r.err"; rc=$?
  "$CHK" encode $1 > /dev/null 2>&1; crc=$?
  [ $rc = 8 ] && [ $crc != 0 ] && ref_ok=$((ref_ok + 1)) || { ref_bad=$((ref_bad + 1)); echo "  refusal case: '$2' asm2 rc=$rc, C rc=$crc"; }; }
ref_ok=0; ref_bad=0
rf "b 33554432" "b.raw 33554432"; rf "b -33554433" "b.raw -33554433"; rf "b_cond 16 0" "b.c.raw 16 0"
rf "cbz 1 0 262144" "cbz.x.raw x0 262144"; rf "subs_imm 1 0 0 4096" "subs.x.i x0 x0 4096"
rf "ldr_uoff 1 0 1 32768" "ldr.x x0 x1 4096"; rf "ldrb_uoff 0 1 4096" "ldrb w0 x1 4096"; rf "ldr_post 0 1 256" "ldr.w.post w0 x1 256"
rf "str_x_post 0 1 -257" "str.x.post x0 x1 -257"; rf "movz 0 0 1 32" "movz.w w0 1 2"; rf "movk 1 0 1 64" "movk.x x0 1 4"
[ $ref_bad = 0 ] && ok "refusals: $ref_ok out-of-range operands refused by asm2 (error 8) and by the C encoder" || bad "refusals: $ref_bad wrong"
printf 'b T\n.w 0\n' > "$O/far.s"; awk 'BEGIN { for (i = 0; i < 262142; i++) print ".w 0"; print ":T" }' >> "$O/far.s"
cat "$T" "$O/far.s" | sed 's/^b T$/b.ne T/' | "$A" > /dev/null 2>&1; rc=$?; [ $rc = 8 ] && ok "refusal: b.ne to a label 262144 words away (one past imm19) refused (error 8)" || bad "far b.ne rc=$rc"
# error controls
ec() { printf "$2" | cat "$T" - | "$A" > /dev/null 2> "$O/e.err"; rc=$?; want="asm2: line $(($(wc -l < "$T") + $3)): error $1"
  [ $rc = "$1" ] && [ "$(cat "$O/e.err")" = "$want" ] && ok "control: $4 -> \"$want\"" || bad "$4: rc=$rc msg='$(cat "$O/e.err")' want '$want'"; }
ec 3 'ret\nfoo x0\n' 2 "unknown instruction name"
ec 4 'add.x x0 x1\n' 1 "too few operands"
ec 5 'ret x0\n' 1 "extra words"
ec 6 'movz.x x0 12z 0\n' 1 "bad number"
ec 7 'add.x x0 x1 x31\n' 1 "bad register (x31 is spelled xzr or sp)"
ec 9 'b nowhere\n' 1 "unknown label"
ec 10 ':a\n:a\n' 2 "label named twice"
ec 11 '= ret 0xd65f03c0\n' 1 "table name defined twice"
ec 12 '= odd 0x0 q5\n' 1 "bad table field"
ec 13 'abcdefghijklmnopq\n' 1 "word over 16 characters"
ec 14 '.b 1\nret\n' 2 "instruction not 4-byte aligned"
ec 15 '.q 1\n' 1 "unknown directive"
[ $fail = 0 ] && echo "GATE2 PASS" || echo "GATE2 FAIL"
exit $fail
