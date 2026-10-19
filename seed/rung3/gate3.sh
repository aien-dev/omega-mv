#!/bin/sh
# gate3.sh -- the checks of rung 3 (seed/README.md). Run from the repo root:  sh seed/rung3/gate3.sh
# Needs build/omegese/omegese and build/omega_emit/omega_emit (the C path, built by make through
# quiet-build) and cc for the reference runner. Nothing here writes a byte of seed/ except om3.bin,
# which only asm2 makes. Results: evidence/SEED-0/rung3/.
set -eu
R=seed/rung3; B=build/seed/r3; E=evidence/SEED-0/rung3
T=tests/one_layer/exp445/dev/tasks/cases; O=tests/one_layer/omegese/dev
mkdir -p "$B" "$E"
fail=0
say() { printf '%s\n' "$*" | tee -a "$E/gate3.log"; }
bad() { say "FAIL: $*"; fail=1; }
: > "$E/gate3.log"
say "gate3 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"

# 1. asm2 builds om3; a second build gives the same bytes; the file-size field matches the file
cat seed/rung2/table.s2 $R/table3.s2 $R/om3.s | seed/rung2/asm2.bin > $R/om3.bin
cat seed/rung2/table.s2 $R/table3.s2 $R/om3.s | seed/rung2/asm2.bin > $B/om3.again
chmod +x $R/om3.bin
cmp -s $R/om3.bin $B/om3.again && say "PASS om3.bin built by asm2 twice, same bytes" || bad "two builds differ"
size=$(stat -c %s $R/om3.bin)
fsz=$(od -An -tu4 -j96 -N4 $R/om3.bin | tr -d ' ')
[ "$size" = "$fsz" ] && say "PASS p_filesz $fsz = file size $size" || bad "p_filesz $fsz, file $size"
sha256sum $R/om3.bin | tee "$E/om3.sha256" >> "$E/gate3.log"

# 2. every instruction of om3 decodes (objdump, reference) to its name, registers, numbers, label
if sh seed/check/audit2.sh $R/om3.s $R/om3.bin > $R/om3.audit; then :; fi
tail -1 $R/om3.audit | tee -a "$E/gate3.log" | grep -q 'mismatch=0$' && say "PASS audit (objdump reference run)" || bad "audit"
#    controls: a wrong name, register, label and number in the text must each be caught
for c in 's/^  add.x x12 x12 x0$/  sub.x x12 x12 x0/' 's/^  mov.x x21 x20$/  mov.x x21 x19/' \
         's/^  b RD$/  b RDDONE/' 's/^  ubfm.x x1 x1 54 53$/  ubfm.x x1 x1 54 52/' \
         's/^  str.x x12 sp 0$/  str.x x12 sp 1/'; do
  sed "$c" $R/om3.s > $B/bad.s
  if cmp -s $B/bad.s $R/om3.s; then bad "control edit did nothing: $c"
  elif sh seed/check/audit2.sh $B/bad.s $R/om3.bin > $B/bad.audit; then bad "control not caught: $c"
  else say "PASS control caught ($c): $(tail -1 $B/bad.audit)"; fi
done

# 3. the reference runner (seed/check/runner.c, links the repo C path: osc_ir decode, osc_cg, osc_rt)
cc -O1 -Isrc -Isrc/compiler -o $B/runner seed/check/runner.c \
  $(ls src/compiler/*.c | grep -v oscc_main.c) src/sha256.c
[ -x build/omegese/omegese ] && [ -x build/omega_emit/omega_emit ] || { bad "C path tools not built"; exit 1; }

# 4. the 10 dev tasks: same results as the C path and as the expected file; word counts
tab="$E/words.tsv"
printf 'task\tentry\tinputs\tc_path_words\tseed_words\tseed_minus_c\n' > "$tab"
tot_c=0; tot_s=0
for d in dev01 dev02 dev03 dev04 dev05 dev06 dev07 dev08 dev09 dev10; do
  entry=$(awk '$1=="entry"{print $2}' $T/$d/requirement)
  build/omegese/omegese emit $O/$d.omegese $B/$d.json > /dev/null
  build/omega_emit/omega_emit emit $B/$d.json $B/$d.oscir > /dev/null
  $R/om3.bin < $O/$d.omegese > $B/$d.seed.bin
  $B/runner $B/$d.oscir "$entry" $T/$d/hidden/inputs > $B/$d.c.out
  $B/runner $B/$d.oscir "$entry" $T/$d/hidden/inputs $B/$d.seed.bin > $B/$d.seed.out
  cw=$(sed -n 's/^words //p' $B/$d.c.out); sw=$(sed -n 's/^words //p' $B/$d.seed.out)
  n=$(wc -l < $T/$d/hidden/inputs)
  tail -n +2 $B/$d.c.out > $B/$d.c.res; tail -n +2 $B/$d.seed.out > $B/$d.seed.res
  cut -f1 $B/$d.seed.res > $B/$d.seed.val
  if cmp -s $B/$d.c.res $B/$d.seed.res; then say "PASS $d: $n inputs, seed = C path (value and raw x0)"
  else bad "$d differs from the C path"; diff $B/$d.c.res $B/$d.seed.res | head -5 >> "$E/gate3.log"; fi
  cmp -s $B/$d.seed.val $T/$d/hidden/expected && say "PASS $d: seed = expected" || bad "$d differs from expected"
  grep -q NONCANONICAL $B/$d.seed.res && bad "$d: a result not in canonical form" || true
  #   every word the seed chain made is an instruction (objdump, reference)
  objdump -D -z -b binary -m aarch64 -M no-aliases $B/$d.seed.bin | grep -E '^ +[0-9a-f]+:' > $B/$d.seed.dis
  if grep -qE '	(udf|\.inst|undefined)' $B/$d.seed.dis; then bad "$d: a word that is not an instruction"; fi
  cp $B/$d.seed.bin $E/$d.seed.bin; cp $B/$d.seed.dis $E/$d.seed.dis
  cp $B/$d.seed.out $E/$d.seed.out; cp $B/$d.c.out $E/$d.c.out
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' $d "$entry" $n $cw $sw $((sw - cw)) >> "$tab"
  tot_c=$((tot_c + cw)); tot_s=$((tot_s + sw))
done
printf 'total\t\t\t%s\t%s\t%s\n' $tot_c $tot_s $((tot_s - tot_c)) >> "$tab"
cat "$tab" >> "$E/gate3.log"

# 5. om3 refuses what is outside the subset or wrong, each with its own code
t() { # expected-code text
  printf '%b' "$2" > $B/ctl.omegese
  set +e; $R/om3.bin < $B/ctl.omegese > $B/ctl.bin 2> $B/ctl.err; rc=$?; set -e
  [ "$rc" = "$1" ] && say "PASS control exit $rc: $(cat $B/ctl.err)" || bad "control wanted exit $1, got $rc: $2"
}
t 3  'fn f a uint to uint\nret add a q\nend\n'
t 4  'fn f a uint to uint\nret add a\nend\n'
t 5  'fn f a uint to uint\nret a a\nend\n'
t 6  'fn f a uint to uint\nret add a 12x\nend\n'
t 7  'fn f a uint b int to uint\nret add a b\nend\n'
t 8  'fn f a uchar to uchar\nret add a 256\nend\n'
t 9  'fn f a int b int to int\nret div a b\nend\n'
t 9  'fn f a uint to void\nend\n'
t 10 'fn f a uint a uint to uint\nret a\nend\n'
t 11 'fn f a uint to uint\nx = add 1 2\nret a\nend\n'
t 13 'fn f a uint to uint\nabcdefghi = a\nret a\nend\n'
t 15 'fn f a uint to uint\nret a\n'
t 9  'fn f a uint to uint\nret a\nend\nend\n'
t 16 'ret 5\n'
[ $fail = 0 ] && say "GATE3 PASS" || say "GATE3 FAIL"
exit $fail
